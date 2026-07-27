#include "DistrhoPlugin.hpp"

#include "lifeform_params.hpp"
#include "lifeform_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

using downspout::lifeform::ClockMode;
using downspout::lifeform::EmitMode;
using downspout::lifeform::MidiMessage;
using downspout::lifeform::OutputMode;
using downspout::lifeform::ProcessResult;
using downspout::lifeform::Processor;
using downspout::lifeform::ScaleId;
using downspout::lifeform::TransportSnapshot;
using downspout::lifeform::kCellCount;
using downspout::lifeform::kClockModeNames;
using downspout::lifeform::kEmitModeNames;
using downspout::lifeform::kOutputModeNames;
using downspout::lifeform::kParamBaseChannel;
using downspout::lifeform::kParamCellStart;
using downspout::lifeform::kParamClear;
using downspout::lifeform::kParamClockMode;
using downspout::lifeform::kParamDensity;
using downspout::lifeform::kParamEmitMode;
using downspout::lifeform::kParamGate;
using downspout::lifeform::kParamLedFeedback;
using downspout::lifeform::kParamMutation;
using downspout::lifeform::kParamOutputMode;
using downspout::lifeform::kParamPanic;
using downspout::lifeform::kParamPassInput;
using downspout::lifeform::kParamRandomize;
using downspout::lifeform::kParamRootNote;
using downspout::lifeform::kParamRunning;
using downspout::lifeform::kParamScale;
using downspout::lifeform::kParamSeed;
using downspout::lifeform::kParamStatusActive;
using downspout::lifeform::kParamStatusCellStart;
using downspout::lifeform::kParamStatusGeneration;
using downspout::lifeform::kParamStep;
using downspout::lifeform::kParamVelocity;
using downspout::lifeform::kParameterCount;
using downspout::lifeform::kScaleNames;

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
    {2.0f, kOutputModeNames[2]},
};

ParameterEnumerationValue kEmitEnumValues[] = {
    {0.0f, kEmitModeNames[0]},
    {1.0f, kEmitModeNames[1]},
};

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
                            (timePos.bbt.ticksPerBeat > 0.0
                                 ? timePos.bbt.tick / timePos.bbt.ticksPerBeat
                                 : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.bpm = timePos.bbt.beatsPerMinute;
    }
    return transport;
}

} // namespace

class LifeformPlugin : public Plugin
{
public:
    LifeformPlugin()
        : Plugin(kParameterCount, 0, 0)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override
    {
        return "Lifeform";
    }

    const char* getDescription() const override
    {
        return "Conway Game of Life MIDI generator for Launchpad performance.";
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
        return d_cconst('L', 'i', 'f', 'e');
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
            parameter.name = String("Cell ") + String(static_cast<int>(index + 1));
            parameter.symbol = String("cell_") + String(static_cast<int>(index + 1));
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
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kScaleEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kScaleEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamGate:
            parameter.name = "Gate";
            parameter.symbol = "gate";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.52f;
            break;
        case kParamVelocity:
            parameter.name = "Velocity";
            parameter.symbol = "velocity";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.72f;
            break;
        case kParamMutation:
            parameter.name = "Mutation";
            parameter.symbol = "mutation";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamDensity:
            parameter.name = "Density";
            parameter.symbol = "density";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.34f;
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
            parameter.symbol = "output";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(OutputMode::count) - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kOutputEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kOutputEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamEmitMode:
            parameter.name = "Emit";
            parameter.symbol = "emit";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(EmitMode::count) - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kEmitEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kEmitEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamBaseChannel:
            parameter.name = "Base Channel";
            parameter.symbol = "base_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 4.0f;
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
        case kParamRunning:
            parameter.name = "Running";
            parameter.symbol = "running";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamRandomize:
            parameter.name = "Randomize";
            parameter.symbol = "randomize";
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
        case kParamSeed:
            parameter.name = "Seed";
            parameter.symbol = "seed";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 7.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStep:
            parameter.name = "Step";
            parameter.symbol = "step";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamPanic:
            parameter.name = "Panic";
            parameter.symbol = "panic";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusActive:
            parameter.name = "Active Cells";
            parameter.symbol = "status_active";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 64.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusGeneration:
            parameter.name = "Generation";
            parameter.symbol = "status_generation";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1023.0f;
            parameter.ranges.def = 0.0f;
            break;
        default:
            if (index >= kParamStatusCellStart && index < kParamStatusCellStart + kCellCount)
            {
                const uint32_t cell = index - kParamStatusCellStart;
                parameter.name = String("Cell Status ") + String(static_cast<int>(cell + 1));
                parameter.symbol = String("status_cell_") + String(static_cast<int>(cell + 1));
                parameter.hints = kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger;
                parameter.ranges.min = 0.0f;
                parameter.ranges.max = 1.0f;
                parameter.ranges.def = 0.0f;
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LifeformPlugin)
};

Plugin* createPlugin()
{
    return new LifeformPlugin();
}

END_NAMESPACE_DISTRHO
