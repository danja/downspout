#include "DistrhoPlugin.hpp"

#include "plank_core.hpp"
#include "plank_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::plank;

// One text state holding the whole patch. Without it the host has nothing to
// save and every parameter reverts on reopen.
constexpr std::uint32_t kStateParameters = 0;
constexpr std::uint32_t kStateCount = 1;
constexpr const char* kStateKeyParameters = "parameters";

ParameterEnumerationValue kScaleEnumValues[] = {
    {0.0f, kScaleNames[0]},   {1.0f, kScaleNames[1]},   {2.0f, kScaleNames[2]},
    {3.0f, kScaleNames[3]},   {4.0f, kScaleNames[4]},   {5.0f, kScaleNames[5]},
    {6.0f, kScaleNames[6]},   {7.0f, kScaleNames[7]},   {8.0f, kScaleNames[8]},
    {9.0f, kScaleNames[9]},   {10.0f, kScaleNames[10]}, {11.0f, kScaleNames[11]},
    {12.0f, kScaleNames[12]}, {13.0f, kScaleNames[13]}, {14.0f, kScaleNames[14]},
    {15.0f, kScaleNames[15]}, {16.0f, kScaleNames[16]}, {17.0f, kScaleNames[17]},
    {18.0f, kScaleNames[18]}, {19.0f, kScaleNames[19]}, {20.0f, kScaleNames[20]},
    {21.0f, kScaleNames[21]}, {22.0f, kScaleNames[22]}, {23.0f, kScaleNames[23]},
};

ParameterEnumerationValue kSpreadEnumValues[] = {
    {0.0f, kSpreadNames[0]}, {1.0f, kSpreadNames[1]},
    {2.0f, kSpreadNames[2]}, {3.0f, kSpreadNames[3]},
    {4.0f, kSpreadNames[4]},
};

ParameterEnumerationValue kEngineEnumValues[] = {
    {0.0f, kEngineNames[0]}, {1.0f, kEngineNames[1]}, {2.0f, kEngineNames[2]},
    {3.0f, kEngineNames[3]}, {4.0f, kEngineNames[4]}, {5.0f, kEngineNames[5]},
    {6.0f, kEngineNames[6]},
};

ParameterEnumerationValue kExciterEnumValues[] = {
    {0.0f, kExciterNames[0]}, {1.0f, kExciterNames[1]},
};

ParameterEnumerationValue kLfoEnumValues[] = {
    {0.0f, kLfoShapeNames[0]},  {1.0f, kLfoShapeNames[1]},  {2.0f, kLfoShapeNames[2]},
    {3.0f, kLfoShapeNames[3]},  {4.0f, kLfoShapeNames[4]},  {5.0f, kLfoShapeNames[5]},
    {6.0f, kLfoShapeNames[6]},  {7.0f, kLfoShapeNames[7]},  {8.0f, kLfoShapeNames[8]},
    {9.0f, kLfoShapeNames[9]},  {10.0f, kLfoShapeNames[10]},
};

ParameterEnumerationValue kModEnumValues[] = {
    {0.0f, kModTargetNames[0]}, {1.0f, kModTargetNames[1]}, {2.0f, kModTargetNames[2]},
    {3.0f, kModTargetNames[3]}, {4.0f, kModTargetNames[4]}, {5.0f, kModTargetNames[5]},
};

void applyEnumValues(Parameter& parameter, ParameterEnumerationValue* values, const std::size_t count)
{
    parameter.hints |= kParameterIsInteger;
    parameter.ranges.min = 0.0f;
    parameter.ranges.max = static_cast<float>(count - 1u);
    parameter.enumValues.count = static_cast<uint8_t>(count);
    parameter.enumValues.restrictedMode = true;
    parameter.enumValues.values = values;
    parameter.enumValues.deleteLater = false;
}

MidiMessage toCoreMidiMessage(const MidiEvent& event)
{
    MidiMessage message {};
    message.frame = event.frame;
    message.size = static_cast<std::uint16_t>(std::min<std::size_t>(event.size, message.data.size()));
    const std::uint8_t* const bytes = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < message.size; ++i)
        message.data[i] = bytes[i];
    return message;
}

MidiEvent toDpfMidiEvent(const MidiMessage& event)
{
    MidiEvent midi {};
    midi.frame = event.frame;
    midi.size = event.size;
    if (event.size > MidiEvent::kDataSize)
    {
        midi.dataExt = event.data.data();
    }
    else
    {
        for (std::size_t i = 0; i < event.size; ++i)
            midi.data[i] = event.data[i];
        midi.dataExt = nullptr;
    }
    return midi;
}

TransportSnapshot toTransport(const TimePosition& timePos)
{
    TransportSnapshot transport {};
    transport.valid = timePos.bbt.valid;
    transport.playing = timePos.playing;
    if (timePos.bbt.valid)
    {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1) +
                            (timePos.bbt.ticksPerBeat > 0.0 ? timePos.bbt.tick / timePos.bbt.ticksPerBeat : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.bpm = timePos.bbt.beatsPerMinute;
    }
    return transport;
}

}  // namespace

class PlankPlugin : public Plugin {
public:
    PlankPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Plank"; }

    const char* getDescription() const override
    {
        return "Eight-voice synthesizer played from a Launchpad grid, with a Plinky-derived wavetable voice and Plonk-style resonator engines.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', 'l', 'n', 'k'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (!input && index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= kParameterCount)
            return;

        // Grid cells carry no host-facing name; they exist so the UI and host
        // automation can pluck a string without a MIDI port.
        if (index >= kCellParameterStart && index < kCellParameterStart + kCellCount)
        {
            const uint32_t cell = index - static_cast<uint32_t>(kCellParameterStart);
            parameter.name = String("String ") + String(static_cast<int>(cell % kGridWidth + 1u)) +
                             String(" Row ") + String(static_cast<int>(cell / kGridWidth + 1u));
            parameter.symbol = String("cell_") + String(static_cast<int>(cell + 1u));
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        const ParamSpec& spec = getParameterSpec(index);
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (spec.integer && spec.maximum <= 1.0f && !spec.output)
            parameter.hints |= kParameterIsBoolean;
        if (index == static_cast<std::uint32_t>(ParamId::panic))
            parameter.hints |= kParameterIsTrigger;

        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        switch (static_cast<ParamId>(index))
        {
        case ParamId::scale:
            applyEnumValues(parameter, kScaleEnumValues, std::size(kScaleEnumValues));
            parameter.ranges.def = spec.defaultValue;
            break;
        case ParamId::spread:
            applyEnumValues(parameter, kSpreadEnumValues, std::size(kSpreadEnumValues));
            parameter.ranges.def = spec.defaultValue;
            break;
        case ParamId::engine:
            applyEnumValues(parameter, kEngineEnumValues, std::size(kEngineEnumValues));
            parameter.ranges.def = spec.defaultValue;
            break;
        case ParamId::exciter:
            applyEnumValues(parameter, kExciterEnumValues, std::size(kExciterEnumValues));
            parameter.ranges.def = spec.defaultValue;
            break;
        case ParamId::lfoAShape:
        case ParamId::lfoBShape:
            applyEnumValues(parameter, kLfoEnumValues, std::size(kLfoEnumValues));
            parameter.ranges.def = spec.defaultValue;
            break;
        case ParamId::lfoATarget:
        case ParamId::lfoBTarget:
            applyEnumValues(parameter, kModEnumValues, std::size(kModEnumValues));
            parameter.ranges.def = spec.defaultValue;
            break;
        case ParamId::root:
            // The root spans five octaves; a plain integer slider that long is
            // unusable in a host, so it steps by a semitone per detent.
            parameter.ranges.def = spec.defaultValue;
            break;
        default:
            break;
        }
    }

    float getParameterValue(const uint32_t index) const override { return processor_.getParameter(index); }

    void setParameterValue(const uint32_t index, const float value) override
    {
        processor_.setParameter(index, value);
    }

    void initState(const uint32_t index, State& state) override
    {
        if (index != kStateParameters)
            return;
        state.key = kStateKeyParameters;
        state.label = "Parameters";
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(processor_.serializeParameters().c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;

        // A malformed state is ignored rather than half-applied; the core
        // refuses unknown symbols and version mismatches outright.
        static_cast<void>(
            processor_.deserializeParameters(value != nullptr ? std::string_view(value) : std::string_view()));
    }

    void activate() override { processor_.activate(); }

    void sampleRateChanged(const double newSampleRate) override { processor_.setSampleRate(newSampleRate); }

    void run(const float**, float** outputs, const uint32_t frames, const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        std::array<MidiMessage, 256> coreEvents {};
        const uint32_t eventCount = std::min<uint32_t>(midiEventCount, static_cast<uint32_t>(coreEvents.size()));
        for (uint32_t i = 0; i < eventCount; ++i)
            coreEvents[i] = toCoreMidiMessage(midiEvents[i]);

        result_ = ProcessResult {};
        processor_.process(outputs[0], outputs[1], frames, toTransport(getTimePosition()),
                           coreEvents.data(), eventCount, result_);

        std::stable_sort(result_.events.begin(), result_.events.begin() + result_.eventCount,
                         [](const MidiMessage& a, const MidiMessage& b) { return a.frame < b.frame; });

        for (uint32_t i = 0; i < result_.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result_.events[i]));
    }

private:
    Processor processor_;
    ProcessResult result_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlankPlugin)
};

Plugin* createPlugin() { return new PlankPlugin(); }

END_NAMESPACE_DISTRHO
