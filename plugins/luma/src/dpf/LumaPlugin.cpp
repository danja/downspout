#include "DistrhoPlugin.hpp"

#include "luma_params.hpp"
#include "luma_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

using downspout::luma::ClockMode;
using downspout::luma::MidiMessage;
using downspout::luma::OutputMode;
using downspout::luma::ProcessResult;
using downspout::luma::Processor;
using downspout::luma::ScaleId;
using downspout::luma::TransportSnapshot;
using downspout::luma::kCellCount;
using downspout::luma::kClockModeNames;
using downspout::luma::kOutputModeNames;
using downspout::luma::kParamBaseChannel;
using downspout::luma::kParamCellStart;
using downspout::luma::kParamClear;
using downspout::luma::kParamClockMode;
using downspout::luma::kParamDensity;
using downspout::luma::kParamEnergy;
using downspout::luma::kParamGate;
using downspout::luma::kParamLedFeedback;
using downspout::luma::kParamOutputMode;
using downspout::luma::kParamPassInput;
using downspout::luma::kParamRandomize;
using downspout::luma::kParamRootNote;
using downspout::luma::kParamScale;
using downspout::luma::kParamStatusActive;
using downspout::luma::kParamStatusCellStart;
using downspout::luma::kParamStatusStep;
using downspout::luma::kParamSwing;
using downspout::luma::kParameterCount;
using downspout::luma::kScaleNames;

ParameterEnumerationValue kScaleEnumValues[] = {
    {0.0f, kScaleNames[0]},
    {1.0f, kScaleNames[1]},
    {2.0f, kScaleNames[2]},
    {3.0f, kScaleNames[3]},
    {4.0f, kScaleNames[4]},
    {5.0f, kScaleNames[5]},
    {6.0f, kScaleNames[6]},
    {7.0f, kScaleNames[7]},
    {8.0f, kScaleNames[8]},
    {9.0f, kScaleNames[9]},
    {10.0f, kScaleNames[10]},
    {11.0f, kScaleNames[11]},
    {12.0f, kScaleNames[12]},
};

ParameterEnumerationValue kClockEnumValues[] = {
    {0.0f, kClockModeNames[0]},
    {1.0f, kClockModeNames[1]},
    {2.0f, kClockModeNames[2]},
};

ParameterEnumerationValue kOutputEnumValues[] = {
    {0.0f, kOutputModeNames[0]},
    {1.0f, kOutputModeNames[1]},
};

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

TransportSnapshot toTransport(const TimePosition& timePos)
{
    TransportSnapshot transport {};
    transport.valid = timePos.bbt.valid;
    transport.playing = timePos.playing;
    if (timePos.bbt.valid)
    {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1) +
                            (timePos.bbt.ticksPerBeat > 0.0
                                 ? timePos.bbt.tick / timePos.bbt.ticksPerBeat
                                 : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.bpm = timePos.bbt.beatsPerMinute;
    }
    return transport;
}

} // namespace

class LumaPlugin : public Plugin
{
public:
    LumaPlugin()
        : Plugin(kParameterCount, 0, 0)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override
    {
        return "Luma";
    }

    const char* getDescription() const override
    {
        return "Launchpad-oriented MIDI performance generator with pad agents and LED feedback.";
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
        return d_cconst('L', 'u', 'm', 'a');
    }

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
        parameter.hints = kParameterIsAutomatable;

        if (index < kCellCount)
        {
            parameter.name = String("Pad ") + String(static_cast<int>(index + 1));
            parameter.symbol = String("pad_") + String(static_cast<int>(index + 1));
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        switch (index)
        {
        case kParamRootNote:
            parameter.name = "Root Note";
            parameter.symbol = "root_note";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = 48.0f;
            break;
        case kParamScale:
            parameter.name = "Scale";
            parameter.symbol = "scale";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(ScaleId::count) - 1);
            parameter.ranges.def = 1.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kScaleEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kScaleEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamDensity:
            parameter.name = "Density";
            parameter.symbol = "density";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.42f;
            break;
        case kParamEnergy:
            parameter.name = "Energy";
            parameter.symbol = "energy";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.48f;
            break;
        case kParamGate:
            parameter.name = "Gate";
            parameter.symbol = "gate";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.45f;
            break;
        case kParamSwing:
            parameter.name = "Swing";
            parameter.symbol = "swing";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamClockMode:
            parameter.name = "Clock";
            parameter.symbol = "clock";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(ClockMode::count) - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kClockEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kClockEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamOutputMode:
            parameter.name = "Output";
            parameter.symbol = "output_mode";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(OutputMode::count) - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kOutputEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kOutputEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamBaseChannel:
            parameter.name = "Base Channel";
            parameter.symbol = "base_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamLedFeedback:
            parameter.name = "LED Feedback";
            parameter.symbol = "led_feedback";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamPassInput:
            parameter.name = "Pass Input";
            parameter.symbol = "pass_input";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamRandomize:
            parameter.name = "Scatter";
            parameter.symbol = "scatter";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamClear:
            parameter.name = "Clear";
            parameter.symbol = "clear";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusActive:
            parameter.name = "Active Pads";
            parameter.symbol = "status_active";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 64.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusStep:
            parameter.name = "Current Step";
            parameter.symbol = "status_step";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 15.0f;
            parameter.ranges.def = 0.0f;
            break;
        default:
            if (index >= kParamStatusCellStart && index < kParamStatusCellStart + kCellCount)
            {
                const uint32_t cell = index - kParamStatusCellStart;
                parameter.name = String("Pad Status ") + String(static_cast<int>(cell + 1));
                parameter.symbol = String("status_pad_") + String(static_cast<int>(cell + 1));
                parameter.hints = kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger;
                parameter.ranges.min = 0.0f;
                parameter.ranges.max = 1.0f;
                parameter.ranges.def = 0.0f;
                return;
            }
            break;
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

    void activate() override
    {
        processor_.activate();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        processor_.init(newSampleRate);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        std::array<MidiMessage, 256> coreEvents {};
        const uint32_t eventCount = std::min<uint32_t>(midiEventCount, coreEvents.size());
        for (uint32_t i = 0; i < eventCount; ++i)
            coreEvents[i] = toCoreMidiMessage(midiEvents[i]);

        ProcessResult result = processor_.processBlock(frames, toTransport(getTimePosition()), coreEvents.data(), eventCount);
        std::stable_sort(result.events.begin(),
                         result.events.begin() + result.eventCount,
                         [](const MidiMessage& a, const MidiMessage& b) { return a.frame < b.frame; });

        for (uint32_t i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result.events[i]));
    }

private:
    Processor processor_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LumaPlugin)
};

Plugin* createPlugin()
{
    return new LumaPlugin();
}

END_NAMESPACE_DISTRHO
