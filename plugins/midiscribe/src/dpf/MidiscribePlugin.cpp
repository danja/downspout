// MidiscribePlugin.cpp — DPF wrapper for the Midiscribe MIDI capture plugin.
//
// Porting notes:
//   - audioInputs=0, audioOutputs=0, midiInputs=1, midiOutputs=1
//   - DISTRHO_PLUGIN_IS_RT_SAFE=0 because writeFile() does blocking file I/O.
//     The write trigger should be pressed while stopped or between phrases; the
//     plugin does not write every block, only on the 0→1 trigger edge.
//   - Transport: absolute beat = (bar-1)*beatsPerBar + (beat-1) + tick fraction.
//     Falls back to frame/sampleRate conversion if BBT is unavailable.
//   - The exportPath state key carries a plain filesystem path string.

#include "DistrhoPlugin.hpp"
#include "DistrhoPluginInfo.h"

#include "midiscribe_core.hpp"
#include "midiscribe_params.hpp"

#include <algorithm>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Convert a DPF TimePosition to an absolute beat count (0-based) and BPM.
struct TransportInfo {
    double absoluteBeat = 0.0;
    double bpm          = downspout::midiscribe::kDefaultBpm;
    bool   valid        = false;
};

TransportInfo extractTransport(const TimePosition& tp, double sampleRate)
{
    TransportInfo info;
    info.valid = tp.bbt.valid;

    if (tp.bbt.valid) {
        const double barBeats = static_cast<double>(tp.bbt.bar - 1) * tp.bbt.beatsPerBar;
        const double beatFrac = (tp.bbt.ticksPerBeat > 0.0)
            ? (tp.bbt.tick / tp.bbt.ticksPerBeat)
            : 0.0;
        info.absoluteBeat = barBeats + (tp.bbt.beat - 1) + beatFrac;
        info.bpm          = tp.bbt.beatsPerMinute > 0.0
            ? tp.bbt.beatsPerMinute
            : downspout::midiscribe::kDefaultBpm;
    } else {
        // Fall back to frame-based estimate with default BPM.
        if (sampleRate > 0.0) {
            const double seconds = static_cast<double>(tp.frame) / sampleRate;
            info.absoluteBeat = seconds * (downspout::midiscribe::kDefaultBpm / 60.0);
        }
    }

    return info;
}

}  // anonymous namespace

// Enum indices for DPF state slots.
enum StateIndex : uint32_t {
    kStateExportPath = 0,
    kStateControls   = 1,
    kStateCount
};

class MidiscribePlugin : public Plugin
{
public:
    MidiscribePlugin()
        : Plugin(downspout::midiscribe::kParamCount, 0, kStateCount)
    {}

protected:
    // ----- Plugin metadata -----
    const char* getLabel() const override       { return "Midiscribe"; }
    const char* getDescription() const override {
        return "Non-destructive MIDI capture plugin that writes recorded events to a Standard MIDI File on demand.";
    }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('M', 'd', 'S', 'c');
    }

    // ----- Parameters -----
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        using namespace downspout::midiscribe;

        switch (index) {
        case kParamArmed:
            parameter.name    = "Armed";
            parameter.symbol  = "armed";
            parameter.hints   = kParameterIsAutomatable | kParameterIsBoolean;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;

        case kParamWrite:
            parameter.name    = "Write";
            parameter.symbol  = "write";
            parameter.hints   = kParameterIsAutomatable | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;

        case kParamCaptureBeats: {
            // Enum: values 8, 16, 32, 64 encoded as 0,1,2,3
            parameter.name    = "Capture Beats";
            parameter.symbol  = "capture_beats";
            parameter.hints   = kParameterIsAutomatable | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(kCaptureBeatCount - 1);
            parameter.ranges.def = static_cast<float>(kDefaultCaptureBeatIndex);

            static ParameterEnumerationValue enumVals[kCaptureBeatCount] = {
                {0.0f, "8"},
                {1.0f, "16"},
                {2.0f, "32"},
                {3.0f, "64"},
            };
            parameter.enumValues.count          = kCaptureBeatCount;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values         = enumVals;
            parameter.enumValues.deleteLater    = false;
            break;
        }

        case kParamBar:
            parameter.name    = "Bar";
            parameter.symbol  = "bar";
            parameter.hints   = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 9999.0f;
            parameter.ranges.def = 1.0f;
            break;

        case kParamBeat:
            parameter.name    = "Beat";
            parameter.symbol  = "beat";
            parameter.hints   = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 1.0f;
            break;

        case kParamBpm:
            parameter.name    = "BPM";
            parameter.symbol  = "bpm";
            parameter.hints   = kParameterIsOutput;
            parameter.ranges.min = 20.0f;
            parameter.ranges.max = 300.0f;
            parameter.ranges.def = static_cast<float>(kDefaultBpm);
            break;

        case kParamIsPlaying:
            parameter.name    = "Playing";
            parameter.symbol  = "playing";
            parameter.hints   = kParameterIsOutput | kParameterIsBoolean;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;

        default:
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        const auto& c = core_.controls();
        switch (index) {
        case downspout::midiscribe::kParamArmed:        return c.armed ? 1.0f : 0.0f;
        case downspout::midiscribe::kParamWrite:        return c.writeTrigger;
        case downspout::midiscribe::kParamCaptureBeats: return static_cast<float>(c.captureBeatIndex);
        case downspout::midiscribe::kParamBar:          return outBar_;
        case downspout::midiscribe::kParamBeat:         return outBeat_;
        case downspout::midiscribe::kParamBpm:          return outBpm_;
        case downspout::midiscribe::kParamIsPlaying:    return outIsPlaying_;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        auto c = core_.controls();
        switch (index) {
        case downspout::midiscribe::kParamArmed:
            c.armed = (value >= 0.5f);
            break;
        case downspout::midiscribe::kParamWrite:
            prevWriteTrigger_ = c.writeTrigger;
            c.writeTrigger    = value;
            break;
        case downspout::midiscribe::kParamCaptureBeats:
            c.captureBeatIndex = static_cast<int>(value + 0.5f);
            break;
        default:
            break;
        }
        core_.setControls(c);
    }

    // ----- State -----
    void initState(uint32_t index, State& state) override
    {
        state.defaultValue = "";

        switch (index) {
        case kStateExportPath:
            state.key          = downspout::midiscribe::kStateKeyExportPath;
            state.label        = "Export Path";
            state.hints        = kStateIsFilenamePath;
            state.defaultValue = downspout::midiscribe::kDefaultExportPath;
            break;
        case kStateControls:
            state.key   = "controls";
            state.label = "Controls";
            state.hints = kStateIsOnlyForDSP;
            break;
        default:
            break;
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, downspout::midiscribe::kStateKeyExportPath) == 0) {
            return String(core_.exportPath().c_str());
        }
        if (std::strcmp(key, "controls") == 0) {
            return String(downspout::midiscribe::serializeControls(core_.controls()).c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = (value != nullptr) ? value : "";
        if (std::strcmp(key, downspout::midiscribe::kStateKeyExportPath) == 0) {
            core_.setExportPath(text);
            return;
        }
        if (std::strcmp(key, "controls") == 0) {
            core_.setControls(downspout::midiscribe::deserializeControls(text));
        }
    }

    // ----- Audio / MIDI processing -----
    void run(const float** /*inputs*/,
             float**       /*outputs*/,
             uint32_t      frames,
             const MidiEvent* midiEvents,
             uint32_t         midiEventCount) override
    {
        // Snapshot transport.
        const TimePosition& tp = getTimePosition();
        const TransportInfo transport = extractTransport(tp, getSampleRate());

        // Update output parameter values — host polls getParameterValue() and notifies UI.
        outBar_  = (tp.bbt.valid && tp.bbt.bar  > 0) ? static_cast<float>(tp.bbt.bar)  : 1.0f;
        outBeat_ = (tp.bbt.valid && tp.bbt.beat > 0) ? static_cast<float>(tp.bbt.beat) : 1.0f;
        outBpm_  = static_cast<float>(transport.bpm);
        outIsPlaying_ = tp.playing ? 1.0f : 0.0f;

        // Convert incoming DPF MIDI events to CapturedEvent.
        // We use transport.absoluteBeat as the timestamp; for sub-block
        // resolution, a frame offset could be added — omitted here for clarity.
        // TODO: for higher accuracy, compute per-event beat offset from
        //       event.frame / sampleRate * bpm / 60.
        std::vector<downspout::midiscribe::CapturedEvent> inEvents;
        inEvents.reserve(midiEventCount);
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& ev = midiEvents[i];
            const uint8_t* src = (ev.size > MidiEvent::kDataSize) ? ev.dataExt : ev.data;
            if (ev.size >= 3 && src != nullptr) {
                downspout::midiscribe::CapturedEvent ce;
                ce.beatPosition = transport.absoluteBeat
                    + static_cast<double>(ev.frame) / (getSampleRate() > 0 ? getSampleRate() : 48000.0)
                      * (transport.bpm / 60.0);
                ce.status = src[0];
                ce.data1  = src[1];
                ce.data2  = src[2];
                inEvents.push_back(ce);
            }
        }

        std::vector<downspout::midiscribe::CapturedEvent> outEvents;
        const bool shouldWrite = core_.processBlock(
            inEvents, outEvents,
            transport.absoluteBeat,
            transport.bpm,
            prevWriteTrigger_);

        // Pass all events through unchanged (re-emit as DPF MIDI events).
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            writeMidiEvent(midiEvents[i]);
        }

        // Handle write trigger.
        if (shouldWrite) {
            core_.writeFile();
            // Reset the trigger parameter back to 0.
            auto c = core_.controls();
            prevWriteTrigger_ = c.writeTrigger;
            c.writeTrigger = 0.0f;
            core_.setControls(c);
        }

        (void)frames;
    }

private:
    downspout::midiscribe::MidiscribeCore core_ {};
    float prevWriteTrigger_ = 0.0f;

    // Cached output parameter values — updated in run(), read by getParameterValue()
    float outBar_       = 1.0f;
    float outBeat_      = 1.0f;
    float outBpm_       = static_cast<float>(downspout::midiscribe::kDefaultBpm);
    float outIsPlaying_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiscribePlugin)
};

Plugin* createPlugin()
{
    return new MidiscribePlugin();
}

END_NAMESPACE_DISTRHO
