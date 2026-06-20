#include "DistrhoPlugin.hpp"

#include "xoxolo_engine.hpp"
#include "xoxolo_params.hpp"
#include "xoxolo_serialization.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum StateIndex : uint32_t {
    kStatePattern = 0,
    kStateCount
};

using downspout::xoxolo::BlockResult;
using downspout::xoxolo::Controls;
using downspout::xoxolo::EngineState;
using downspout::xoxolo::MidiEventType;
using downspout::xoxolo::NotePresetId;
using downspout::xoxolo::ResolutionId;
using downspout::xoxolo::ScheduledMidiEvent;
using downspout::xoxolo::TransportSnapshot;

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

TransportSnapshot toCoreTransport(const TimePosition& timePos)
{
    TransportSnapshot transport {};
    transport.valid = timePos.bbt.valid;
    transport.playing = timePos.playing;

    if (timePos.bbt.valid) {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1) +
                            (timePos.bbt.ticksPerBeat > 0.0
                                 ? timePos.bbt.tick / timePos.bbt.ticksPerBeat
                                 : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.beatType = timePos.bbt.beatType;
        transport.bpm = timePos.bbt.beatsPerMinute;
        transport.meter = downspout::meterFromTimeSignature(timePos.bbt.beatsPerBar, timePos.bbt.beatType);
    }

    return transport;
}

MidiEvent toDpfMidiEvent(const ScheduledMidiEvent& event)
{
    MidiEvent midi {};
    midi.frame = event.frame;
    midi.size = 3;
    midi.data[0] = static_cast<std::uint8_t>((event.type == MidiEventType::noteOn ? 0x90u : 0x80u) |
                                             (event.channel & 0x0fu));
    midi.data[1] = event.data1;
    midi.data[2] = event.data2;
    midi.dataExt = nullptr;
    return midi;
}

}  // namespace

class XoxoloPlugin : public Plugin
{
public:
    XoxoloPlugin()
        : Plugin(downspout::xoxolo::kParameterCount, 0, kStateCount)
    {
        engine_.pattern = downspout::xoxolo::makeDefaultPattern();
        controls_ = downspout::xoxolo::clampControls(controls_);
        downspout::xoxolo::activate(engine_, controls_);
    }

protected:
    const char* getLabel() const override
    {
        return "Xoxolo";
    }

    const char* getDescription() const override
    {
        return "Simple transport-synced MIDI drum pattern editor.";
    }

    const char* getMaker() const override
    {
        return "danja";
    }

    const char* getHomePage() const override
    {
        return "https://danja.github.io/downspout/";
    }

    const char* getLicense() const override
    {
        return "MIT";
    }

    uint32_t getVersion() const override
    {
        return d_version(0, 1, 0);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('X', 'o', 'X', 'o');
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

        switch (index) {
        case downspout::xoxolo::kParamSteps:
            parameter.name = "Steps";
            parameter.symbol = "steps";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = static_cast<float>(downspout::xoxolo::kMinSteps);
            parameter.ranges.max = static_cast<float>(downspout::xoxolo::kMaxSteps);
            parameter.ranges.def = static_cast<float>(downspout::xoxolo::kDefaultSteps);
            break;
        case downspout::xoxolo::kParamResolution:
            parameter.name = "Resolution";
            parameter.symbol = "resolution";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2.0f;
            parameter.ranges.def = 2.0f;
            break;
        case downspout::xoxolo::kParamChannel:
            parameter.name = "Channel";
            parameter.symbol = "channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 10.0f;
            break;
        case downspout::xoxolo::kParamNotePreset:
            parameter.name = "Note Preset";
            parameter.symbol = "note_preset";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(NotePresetId::count) - 1);
            parameter.ranges.def = static_cast<float>(static_cast<int>(NotePresetId::downspout));
            break;
        case downspout::xoxolo::kParamClear:
            parameter.name = "Clear";
            parameter.symbol = "clear";
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 4096.0f;
            parameter.ranges.def = 0.0f;
            break;
        case downspout::xoxolo::kParamPreviewLane:
            parameter.name = "Preview Lane";
            parameter.symbol = "preview_lane";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(downspout::xoxolo::kLaneCount - 1);
            parameter.ranges.def = 0.0f;
            break;
        case downspout::xoxolo::kParamPreview:
            parameter.name = "Preview";
            parameter.symbol = "preview";
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 16384.0f;
            parameter.ranges.def = 0.0f;
            break;
        case downspout::xoxolo::kParamCurrentStep:
            parameter.name = "Current Step";
            parameter.symbol = "current_step";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = -1.0f;
            parameter.ranges.max = static_cast<float>(downspout::xoxolo::kMaxSteps - 1);
            parameter.ranges.def = -1.0f;
            break;
        default:
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStatePattern) {
            static const std::string defaultPatternState =
                downspout::xoxolo::serializePatternState(downspout::xoxolo::makeDefaultPattern());
            state.key = downspout::xoxolo::kStateKeyPattern;
            state.defaultValue = defaultPatternState.c_str();
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case downspout::xoxolo::kParamSteps: return static_cast<float>(controls_.steps);
        case downspout::xoxolo::kParamResolution: return static_cast<float>(static_cast<int>(controls_.resolution));
        case downspout::xoxolo::kParamChannel: return static_cast<float>(controls_.channel);
        case downspout::xoxolo::kParamNotePreset: return static_cast<float>(static_cast<int>(controls_.notePreset));
        case downspout::xoxolo::kParamClear: return static_cast<float>(controls_.clearSerial);
        case downspout::xoxolo::kParamPreviewLane: return static_cast<float>(controls_.previewLane);
        case downspout::xoxolo::kParamPreview: return static_cast<float>(controls_.previewSerial);
        case downspout::xoxolo::kParamCurrentStep: return static_cast<float>(engine_.currentStep);
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case downspout::xoxolo::kParamSteps:
            controls_.steps = clampi(static_cast<int>(std::lround(value)),
                                     downspout::xoxolo::kMinSteps,
                                     downspout::xoxolo::kMaxSteps);
            break;
        case downspout::xoxolo::kParamResolution:
            controls_.resolution = static_cast<ResolutionId>(clampi(static_cast<int>(std::lround(value)), 0, 2));
            break;
        case downspout::xoxolo::kParamChannel:
            controls_.channel = clampi(static_cast<int>(std::lround(value)), 1, 16);
            break;
        case downspout::xoxolo::kParamNotePreset: {
            const auto preset = static_cast<NotePresetId>(
                clampi(static_cast<int>(std::lround(value)), 0, static_cast<int>(NotePresetId::count) - 1));
            controls_.notePreset = preset;
            if (engine_.pattern.notePreset != preset)
                downspout::xoxolo::applyNotePreset(engine_.pattern, preset);
            break;
        }
        case downspout::xoxolo::kParamClear:
            controls_.clearSerial = std::max(0, static_cast<int>(std::lround(value)));
            break;
        case downspout::xoxolo::kParamPreviewLane:
            controls_.previewLane = clampi(static_cast<int>(std::lround(value)), 0, downspout::xoxolo::kLaneCount - 1);
            break;
        case downspout::xoxolo::kParamPreview:
            controls_.previewSerial = std::max(0, static_cast<int>(std::lround(value)));
            break;
        default:
            break;
        }
        controls_ = downspout::xoxolo::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, downspout::xoxolo::kStateKeyPattern) == 0)
            return String(downspout::xoxolo::serializePatternState(engine_.pattern).c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, downspout::xoxolo::kStateKeyPattern) != 0)
            return;

        const auto pattern = downspout::xoxolo::deserializePatternState(value != nullptr ? value : "");
        if (!pattern.has_value())
            return;

        engine_.pattern = *pattern;
        controls_.steps = engine_.pattern.totalSteps;
        controls_.resolution = engine_.pattern.resolution;
        controls_.channel = engine_.pattern.channel;
        controls_.notePreset = engine_.pattern.notePreset;
        controls_ = downspout::xoxolo::clampControls(controls_);
    }

    void activate() override
    {
        downspout::xoxolo::activate(engine_, controls_);
    }

    void deactivate() override
    {
        downspout::xoxolo::deactivate(engine_);
    }

    void run(const float**, float**, uint32_t frames) override
    {
        const BlockResult result = downspout::xoxolo::processBlock(engine_,
                                                                   controls_,
                                                                   toCoreTransport(getTimePosition()),
                                                                   frames,
                                                                   getSampleRate());

        for (int index = 0; index < result.eventCount; ++index) {
            const MidiEvent midiEvent = toDpfMidiEvent(result.events[static_cast<std::size_t>(index)]);
            writeMidiEvent(midiEvent);
        }
    }

private:
    Controls controls_ {};
    EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XoxoloPlugin)
};

Plugin* createPlugin()
{
    return new XoxoloPlugin();
}

END_NAMESPACE_DISTRHO
