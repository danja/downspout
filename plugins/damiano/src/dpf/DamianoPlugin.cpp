#include "DistrhoPlugin.hpp"

#include "damiano_core.hpp"

#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Parameter indices are stable — never reorder these, only append.
// Mode is kept at index 0 (non-automatable) for project save/restore compatibility.
// Real-time mode switching uses the "mode" state key (bypasses IParameterChanges/automation).
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamDrive,
    kParamTone,
    kParamFoldCount,
    kParamMix,
    kParamOutputGain,
    kParamCCDrive,
    kParamCCChannel,
    kParamStereo,
    kParamDriveR,
    kParamModeR,
    kParamToneR,
    kParamFoldCountR,
    kParamCCDriveR,
    kParamCCShape,
    kParamCCShapeR,
    kParameterCount
};

using CoreParameters  = downspout::damiano::Parameters;
using CoreEngineState = downspout::damiano::EngineState;
using CoreAudioBlock  = downspout::damiano::AudioBlock;
using CoreLive        = downspout::damiano::LiveControl;

constexpr uint32_t kWrapperChannelCount = 2;

// One row per parameter: the state key doubles as the parameter symbol, and the
// member pointer lets get/set/save share a single path.
struct ParamInfo {
    const char*          key;
    const char*          name;
    float CoreParameters::* member;
    float                min, max, def;
    bool                 integer;
};

constexpr ParamInfo kParams[kParameterCount] = {
    { "mode",         "Mode",         &CoreParameters::mode,       0.0f,   5.0f,   1.0f, true  },
    { "drive",        "Drive",        &CoreParameters::drive,      1.0f,  10.0f,   2.0f, false },
    { "tone",         "Tone",         &CoreParameters::tone,       0.0f, 100.0f,  50.0f, false },
    { "fold_count",   "Fold Count",   &CoreParameters::foldCount,  1.0f,   8.0f,   2.0f, true  },
    { "mix",          "Mix",          &CoreParameters::mix,        0.0f, 100.0f, 100.0f, false },
    { "output_gain",  "Output Gain",  &CoreParameters::outputGain, -24.0f, 24.0f,  0.0f, false },
    { "cc_drive",     "CC Drive",     &CoreParameters::ccDrive,    0.0f, 127.0f,   0.0f, true  },
    { "cc_channel",   "CC Channel",   &CoreParameters::ccChannel,  1.0f,  16.0f,   1.0f, true  },
    { "stereo",       "Stereo Split", &CoreParameters::stereo,     0.0f,   1.0f,   0.0f, true  },
    { "drive_r",      "Drive R",      &CoreParameters::driveR,     1.0f,  10.0f,   2.0f, false },
    { "mode_r",       "Mode R",       &CoreParameters::modeR,      0.0f,   5.0f,   1.0f, true  },
    { "tone_r",       "Tone R",       &CoreParameters::toneR,      0.0f, 100.0f,  50.0f, false },
    { "fold_count_r", "Fold Count R", &CoreParameters::foldCountR, 1.0f,   8.0f,   2.0f, true  },
    { "cc_drive_r",   "CC Drive R",   &CoreParameters::ccDriveR,   0.0f, 127.0f,   0.0f, true  },
    { "cc_shape",     "CC Shape",     &CoreParameters::ccShape,    0.0f, 127.0f,   0.0f, true  },
    { "cc_shape_r",   "CC Shape R",   &CoreParameters::ccShapeR,   0.0f, 127.0f,   0.0f, true  },
};

}  // namespace

class DamianoPlugin : public Plugin
{
public:
    DamianoPlugin()
        : Plugin(kParameterCount, 0, kParameterCount)  // one state key per parameter
    {
        parameters_ = downspout::damiano::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override       { return "Damiano"; }
    const char* getDescription() const override { return "Stereo distortion with per-channel (binaural) settings and MIDI CC control from Drift."; }
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
        return d_cconst('D', 'a', 'm', 'i');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_")       + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        // No parameter is automatable: Reaper's Write/Latch mode records any
        // slider touch and replays it every process() block via IParameterChanges,
        // permanently overriding subsequent UI changes. Drive is controlled live
        // via MIDI CC (Drift); all other parameters are set-and-forget.
        parameter.hints = 0;
        if (index >= kParameterCount)
            return;
        const ParamInfo& info = kParams[index];
        parameter.name   = info.name;
        parameter.symbol = info.key;
        parameter.hints  = info.integer ? kParameterIsInteger : 0;
        parameter.ranges = {info.min, info.max, info.def};
    }

    void initState(uint32_t index, State& state) override
    {
        if (index >= kParameterCount)
            return;
        const ParamInfo& info = kParams[index];
        static char defaults[kParameterCount][16];
        std::snprintf(defaults[index], sizeof(defaults[index]), "%g", static_cast<double>(info.def));
        state.key          = info.key;
        state.label        = info.name;
        state.hints        = kStateIsOnlyForDSP;
        state.defaultValue = defaults[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < kParameterCount ? parameters_.*(kParams[index].member) : 0.0f;
    }

    void setParameterValue(uint32_t /*index*/, float /*value*/) override
    {
        // Intentional no-op. REAPER Write/Latch mode records touches and
        // replays them via IParameterChanges every process block, permanently
        // overriding live values. All slider state is driven through setState
        // so host automation cannot interfere.
    }

    // The inverse of setState(). Without it (and DISTRHO_PLUGIN_WANT_FULL_STATE)
    // the host saves the defaults from initState() into every project.
    String getState(const char* key) const override
    {
        for (const ParamInfo& info : kParams) {
            if (std::strcmp(key, info.key) == 0) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%.9g",
                              static_cast<double>(parameters_.*(info.member)));
                return String(buffer);
            }
        }
        return String();
    }

    // All parameters driven through state: bypasses IParameterChanges so
    // REAPER automation cannot override live UI changes.
    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        for (const ParamInfo& info : kParams) {
            if (std::strcmp(key, info.key) == 0) {
                parameters_.*(info.member) = static_cast<float>(std::atof(value));
                break;
            }
        }
        parameters_ = downspout::damiano::clampParameters(parameters_);
    }

    void activate() override
    {
        engineState_ = CoreEngineState{};
        live_        = CoreLive{};
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const int ccCh = static_cast<int>(parameters_.ccChannel);

        // A CC number of 0 disables that mapping and releases its override.
        const int ccDriveL = static_cast<int>(parameters_.ccDrive);
        const int ccDriveR = static_cast<int>(parameters_.ccDriveR);
        const int ccShapeL = static_cast<int>(parameters_.ccShape);
        const int ccShapeR = static_cast<int>(parameters_.ccShapeR);
        if (ccDriveL == 0) live_.driveL = -1.0f;
        if (ccDriveR == 0) live_.driveR = -1.0f;
        if (ccShapeL == 0) live_.modeL  = -1.0f;
        if (ccShapeR == 0) live_.modeR  = -1.0f;

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const auto& ev = midiEvents[i];
            if (ev.size < 3) continue;
            if ((ev.data[0] & 0xF0) != 0xB0) continue;
            if (static_cast<int>(ev.data[0] & 0x0F) + 1 != ccCh) continue;
            const int number = ev.data[1];
            const int value  = ev.data[2];
            if (number == ccDriveL && ccDriveL > 0) live_.driveL = downspout::damiano::driveFromCc(value);
            if (number == ccDriveR && ccDriveR > 0) live_.driveR = downspout::damiano::driveFromCc(value);
            if (number == ccShapeL && ccShapeL > 0) live_.modeL  = downspout::damiano::modeFromCc(value);
            if (number == ccShapeR && ccShapeR > 0) live_.modeR  = downspout::damiano::modeFromCc(value);
        }

        std::array<const float*, downspout::damiano::kMaxChannels> safeInputs {};
        std::array<float*,       downspout::damiano::kMaxChannels> safeOutputs {};

        for (uint32_t c = 0; c < kWrapperChannelCount; ++c) {
            safeInputs[c]  = inputs[c];
            safeOutputs[c] = outputs[c];
        }

        CoreAudioBlock audio;
        audio.inputs       = safeInputs;
        audio.outputs      = safeOutputs;
        audio.channelCount = kWrapperChannelCount;

        downspout::damiano::processBlock(engineState_, parameters_, frames,
                                         getSampleRate(), audio, live_);
    }

private:
    CoreParameters  parameters_ {};
    CoreEngineState engineState_ {};
    CoreLive        live_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamianoPlugin)
};

Plugin* createPlugin()
{
    return new DamianoPlugin();
}

END_NAMESPACE_DISTRHO
