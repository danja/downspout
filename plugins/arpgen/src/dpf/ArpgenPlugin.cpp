#include "DistrhoPlugin.hpp"

#include "arpgen_core.hpp"
#include "arpgen_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::arpgen;

ParameterEnumerationValue kModeValues[] = {{0.0f, "Chord Capture"}, {1.0f, "Scale Input"}};
ParameterEnumerationValue kOrderValues[] = {
    {0.0f, "Up"}, {1.0f, "Down"}, {2.0f, "Up / Down"}, {3.0f, "Down / Up"}
};
ParameterEnumerationValue kRateValues[] = {
    {0.0f, "1/4"}, {1.0f, "1/8"}, {2.0f, "1/8T"},
    {3.0f, "1/16"}, {4.0f, "1/16T"}, {5.0f, "1/32"}
};
ParameterEnumerationValue kSliceValues[] = {
    {0.0f, "Quarter Bar"}, {1.0f, "Half Bar"}, {2.0f, "Whole Bar"}
};
ParameterEnumerationValue kKeyValues[] = {
    {0.0f, "C"}, {1.0f, "C#"}, {2.0f, "D"}, {3.0f, "D#"},
    {4.0f, "E"}, {5.0f, "F"}, {6.0f, "F#"}, {7.0f, "G"},
    {8.0f, "G#"}, {9.0f, "A"}, {10.0f, "A#"}, {11.0f, "B"}
};
ParameterEnumerationValue kScaleValues[] = {
    {0.0f, "Major"}, {1.0f, "Natural Minor"}, {2.0f, "Harmonic Minor"},
    {3.0f, "Dorian"}, {4.0f, "Mixolydian"}, {5.0f, "Pentatonic Major"},
    {6.0f, "Pentatonic Minor"}, {7.0f, "Blues"}, {8.0f, "Lydian"},
    {9.0f, "Phrygian Dominant"}
};
ParameterEnumerationValue kShapeValues[] = {
    {0.0f, "Scale Run"}, {1.0f, "Triad"}, {2.0f, "Seventh"}
};

void setEnumeration(Parameter& parameter, ParameterEnumerationValue* values, const std::uint8_t count)
{
    parameter.enumValues.count = count;
    parameter.enumValues.restrictedMode = true;
    parameter.enumValues.values = values;
    parameter.enumValues.deleteLater = false;
}

TransportSnapshot toCoreTransport(const TimePosition& position)
{
    TransportSnapshot result;
    result.valid = position.bbt.valid;
    result.playing = position.playing;
    if (position.bbt.valid) {
        result.bar = static_cast<double>(position.bbt.bar - 1);
        result.barBeat = static_cast<double>(position.bbt.beat - 1) +
            (position.bbt.ticksPerBeat > 0.0 ? position.bbt.tick / position.bbt.ticksPerBeat : 0.0);
        result.beatsPerBar = position.bbt.beatsPerBar;
        result.beatType = position.bbt.beatType;
        result.bpm = position.bbt.beatsPerMinute;
    }
    return result;
}

InputMidiEvent toCoreMidi(const MidiEvent& source)
{
    InputMidiEvent result;
    result.frame = source.frame;
    result.size = static_cast<std::uint8_t>(std::min<std::size_t>(source.size, kMaxMidiData));
    const std::uint8_t* data = source.size > MidiEvent::kDataSize ? source.dataExt : source.data;
    for (std::uint8_t i = 0; i < result.size; ++i)
        result.data[i] = data[i];
    return result;
}

MidiEvent toDpfMidi(const ScheduledMidiEvent& source)
{
    MidiEvent result {};
    result.frame = source.frame;
    result.size = source.size;
    for (std::uint8_t i = 0; i < source.size && i < MidiEvent::kDataSize; ++i)
        result.data[i] = source.data[i];
    return result;
}

}  // namespace

class ArpgenPlugin : public Plugin
{
public:
    ArpgenPlugin()
        : Plugin(kParameterCount, 0, 0)
    {
        controls_ = clampControls(controls_);
    }

protected:
    const char* getLabel() const override { return "Arpgen"; }
    const char* getDescription() const override
    {
        return "Transport-synced MIDI arpeggiator with captured-chord and scale-derived modes.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    std::uint32_t getVersion() const override { return d_version(0, 1, 0); }
    std::int64_t getUniqueId() const override { return d_cconst('A', 'r', 'p', 'g'); }

    void initParameter(const std::uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        switch (index) {
        case kParamMode:
            parameter.name = "Mode"; parameter.symbol = "mode";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 1.0f};
            setEnumeration(parameter, kModeValues, 2); break;
        case kParamOrder:
            parameter.name = "Order"; parameter.symbol = "order";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {2.0f, 0.0f, 3.0f};
            setEnumeration(parameter, kOrderValues, 4); break;
        case kParamRate:
            parameter.name = "Rate"; parameter.symbol = "rate";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {3.0f, 0.0f, 5.0f};
            setEnumeration(parameter, kRateValues, 6); break;
        case kParamCaptureSlice:
            parameter.name = "Capture Slice"; parameter.symbol = "capture_slice";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 2.0f};
            setEnumeration(parameter, kSliceValues, 3); break;
        case kParamKey:
            parameter.name = "Key"; parameter.symbol = "key";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 11.0f};
            setEnumeration(parameter, kKeyValues, 12); break;
        case kParamScale:
            parameter.name = "Scale"; parameter.symbol = "scale";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, static_cast<float>(SCALE_COUNT - 1)};
            setEnumeration(parameter, kScaleValues, SCALE_COUNT); break;
        case kParamScaleShape:
            parameter.name = "Scale Shape"; parameter.symbol = "scale_shape";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 2.0f};
            setEnumeration(parameter, kShapeValues, 3); break;
        case kParamOctaves:
            parameter.name = "Octaves"; parameter.symbol = "octaves";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {2.0f, 1.0f, 4.0f}; break;
        case kParamGate:
            parameter.name = "Gate"; parameter.symbol = "gate";
            parameter.ranges = {0.72f, 0.05f, 1.0f}; break;
        case kParamVelocityFollow:
            parameter.name = "Velocity Follow"; parameter.symbol = "velocity_follow";
            parameter.ranges = {0.8f, 0.0f, 1.0f}; break;
        case kParamPassInput:
            parameter.name = "Pass Input"; parameter.symbol = "pass_input";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 1.0f}; break;
        case kParamOutputChannel:
            parameter.name = "Output Channel"; parameter.symbol = "output_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 16.0f}; break;
        case kParamStatusMaterial:
            parameter.name = "Material Notes"; parameter.symbol = "status_material";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 128.0f}; break;
        case kParamStatusNote:
            parameter.name = "Active Note"; parameter.symbol = "status_note";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges = {-1.0f, -1.0f, 127.0f}; break;
        case kParamStatusInput:
            parameter.name = "MIDI Input Activity"; parameter.symbol = "status_input";
            parameter.hints = kParameterIsOutput; parameter.ranges = {0.0f, 0.0f, 1.0f}; break;
        case kParamStatusOutput:
            parameter.name = "MIDI Output Activity"; parameter.symbol = "status_output";
            parameter.hints = kParameterIsOutput; parameter.ranges = {0.0f, 0.0f, 1.0f}; break;
        }
    }

    float getParameterValue(const std::uint32_t index) const override
    {
        switch (index) {
        case kParamMode: return controls_.mode;
        case kParamOrder: return controls_.order;
        case kParamRate: return controls_.rate;
        case kParamCaptureSlice: return controls_.captureSlice;
        case kParamKey: return controls_.key;
        case kParamScale: return controls_.scale;
        case kParamScaleShape: return controls_.scaleShape;
        case kParamOctaves: return controls_.octaves;
        case kParamGate: return controls_.gate;
        case kParamVelocityFollow: return controls_.velocityFollow;
        case kParamPassInput: return controls_.passInput ? 1.0f : 0.0f;
        case kParamOutputChannel: return controls_.outputChannel;
        case kParamStatusMaterial: return materialStatus_;
        case kParamStatusNote: return noteStatus_;
        case kParamStatusInput: return inputStatus_;
        case kParamStatusOutput: return outputStatus_;
        default: return 0.0f;
        }
    }

    void setParameterValue(const std::uint32_t index, const float value) override
    {
        switch (index) {
        case kParamMode: controls_.mode = std::lround(value); break;
        case kParamOrder: controls_.order = std::lround(value); break;
        case kParamRate: controls_.rate = std::lround(value); break;
        case kParamCaptureSlice: controls_.captureSlice = std::lround(value); break;
        case kParamKey: controls_.key = std::lround(value); break;
        case kParamScale: controls_.scale = std::lround(value); break;
        case kParamScaleShape: controls_.scaleShape = std::lround(value); break;
        case kParamOctaves: controls_.octaves = std::lround(value); break;
        case kParamGate: controls_.gate = value; break;
        case kParamVelocityFollow: controls_.velocityFollow = value; break;
        case kParamPassInput: controls_.passInput = value >= 0.5f; break;
        case kParamOutputChannel: controls_.outputChannel = std::lround(value); break;
        default: return;
        }
        controls_ = clampControls(controls_);
    }

    void activate() override { downspout::arpgen::activate(engine_); }
    void deactivate() override { downspout::arpgen::deactivate(engine_); }

    void run(const float**, float**, const std::uint32_t frames,
             const MidiEvent* midiEvents, const std::uint32_t midiEventCount) override
    {
        std::array<InputMidiEvent, kMaxInputEvents> input {};
        const auto count = std::min<std::uint32_t>(midiEventCount, input.size());
        for (std::uint32_t i = 0; i < count; ++i)
            input[i] = toCoreMidi(midiEvents[i]);

        const BlockResult result = processBlock(engine_, controls_, toCoreTransport(getTimePosition()),
                                                frames, getSampleRate(), input.data(), count);
        const float decay = static_cast<float>(frames / std::max(1.0, getSampleRate() * 0.18));
        inputStatus_ = std::max(0.0f, inputStatus_ - decay);
        outputStatus_ = std::max(0.0f, outputStatus_ - decay);
        if (count > 0) inputStatus_ = 1.0f;
        if (result.eventCount > 0) outputStatus_ = 1.0f;
        materialStatus_ = static_cast<float>(result.materialCount);
        noteStatus_ = static_cast<float>(result.activeNote);
        for (int i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidi(result.events[static_cast<std::size_t>(i)]));
    }

private:
    Controls controls_ {};
    EngineState engine_ {};
    float materialStatus_ = 0.0f;
    float noteStatus_ = -1.0f;
    float inputStatus_ = 0.0f;
    float outputStatus_ = 0.0f;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpgenPlugin)
};

Plugin* createPlugin() { return new ArpgenPlugin(); }

END_NAMESPACE_DISTRHO
