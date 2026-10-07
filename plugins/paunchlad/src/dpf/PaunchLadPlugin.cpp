#include "DistrhoPlugin.hpp"

#include "paunchlad_params.hpp"
#include "paunchlad_processor.hpp"

#include <algorithm>
#include <string_view>
#include <cstring>
#include <array>

START_NAMESPACE_DISTRHO

namespace {

using downspout::paunchlad::MidiMessage;
using downspout::paunchlad::ProcessResult;
using downspout::paunchlad::Processor;
using downspout::paunchlad::kCellCount;
using downspout::paunchlad::kControlParamSpecs;
using downspout::paunchlad::kParamDry;
using downspout::paunchlad::kParamLedFeedback;
using downspout::paunchlad::kParamPadMap;
using downspout::paunchlad::kParamPanic;
using downspout::paunchlad::kParamStatusActivity;
using downspout::paunchlad::kParamStatusCellStart;
using downspout::paunchlad::kParamStatusMode;
using downspout::paunchlad::kParameterCount;

MidiMessage toCoreMidiMessage(const MidiEvent& event)
{
    MidiMessage message {};
    message.frame = event.frame;
    message.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, message.data.size()));
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

} // namespace

constexpr std::uint32_t kStateParameters = 0;
constexpr std::uint32_t kStateCount = 1;
constexpr const char* kStateKeyParameters = "parameters";

class PaunchLadPlugin : public Plugin
{
public:
    PaunchLadPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override
    {
        return "PaunchLad";
    }

    const char* getDescription() const override
    {
        return "Launchpad-controlled dub effect with echo throws, spring splashes, dropouts, chops, and sirens.";
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
        return d_cconst('P', 'n', 'L', 'd');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

        if (index < kCellCount)
        {
            parameter.name = String("Pad ") + String(static_cast<int>(index + 1));
            parameter.symbol = String("pad_") + String(static_cast<int>(index + 1));
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index >= kParamDry && index <= kParamLedFeedback)
        {
            const std::size_t control = index - kParamDry;
            const auto& spec = kControlParamSpecs[control];
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            if (spec.integer)
                parameter.hints |= kParameterIsInteger;
            if (spec.boolean)
                parameter.hints |= kParameterIsBoolean;
            parameter.ranges.min = spec.minimum;
            parameter.ranges.max = spec.maximum;
            parameter.ranges.def = spec.defaultValue;
            return;
        }

        if (index == kParamPadMap)
        {
            parameter.name = "Pad Map";
            parameter.symbol = "pad_map";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 3.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index == kParamPanic)
        {
            parameter.name = "Panic";
            parameter.symbol = "panic";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index == kParamStatusActivity)
        {
            parameter.name = "Activity";
            parameter.symbol = "status_activity";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index == kParamStatusMode)
        {
            parameter.name = "Mode Row";
            parameter.symbol = "status_mode";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 7.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index >= kParamStatusCellStart && index < kParamStatusCellStart + kCellCount)
        {
            const uint32_t cell = index - kParamStatusCellStart;
            parameter.name = String("Pad Status ") + String(static_cast<int>(cell + 1));
            parameter.symbol = String("status_pad_") + String(static_cast<int>(cell + 1));
            parameter.hints = kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return processor_.getParameter(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        processor_.setParameter(index, value);
    }

    // One text state holding the settings. Without it the host has nothing to save
    // and everything reverts on reopen.
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

        // A malformed state is ignored rather than half-applied.
        static_cast<void>(
            processor_.deserializeParameters(value != nullptr ? std::string_view(value) : std::string_view()));
    }

    void activate() override
    {
        processor_.activate();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        processor_.setSampleRate(newSampleRate);
    }

    void run(const float** inputs,
             float** outputs,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        std::array<MidiMessage, 256> coreEvents {};
        const uint32_t eventCount = std::min<uint32_t>(midiEventCount, coreEvents.size());
        for (uint32_t i = 0; i < eventCount; ++i)
            coreEvents[i] = toCoreMidiMessage(midiEvents[i]);

        ProcessResult result = processor_.processBlock(inputs[0],
                                                       inputs[1],
                                                       outputs[0],
                                                       outputs[1],
                                                       frames,
                                                       coreEvents.data(),
                                                       eventCount);
        std::stable_sort(result.events.begin(),
                         result.events.begin() + result.eventCount,
                         [](const MidiMessage& a, const MidiMessage& b) { return a.frame < b.frame; });

        for (uint32_t i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result.events[i]));
    }

private:
    Processor processor_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PaunchLadPlugin)
};

Plugin* createPlugin()
{
    return new PaunchLadPlugin();
}

END_NAMESPACE_DISTRHO
