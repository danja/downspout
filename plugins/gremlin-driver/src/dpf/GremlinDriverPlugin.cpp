#include "DistrhoPlugin.hpp"

#include "gremlin_driver_dpf_shared.hpp"
#include "gremlin_driver_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

using downspout::gremlin_driver::MidiMessage;
using downspout::gremlin_driver::ProcessResult;
using downspout::gremlin_driver::Processor;
using downspout::gremlin_driver::TransportSnapshot;
using downspout::gremlin_driver::kActionNames;
using downspout::gremlin_driver::kClockModeNames;
using downspout::gremlin_driver::kGlobalParamSpecs;
using downspout::gremlin_driver::kGremlinDriverParameterCount;
using downspout::gremlin_driver::kLaneCount;
using downspout::gremlin_driver::kLaneParamSpecs;
using downspout::gremlin_driver::kLaneStatusNames;
using downspout::gremlin_driver::kParamBpm;
using downspout::gremlin_driver::kParamClockMode;
using downspout::gremlin_driver::kParamLaneStart;
using downspout::gremlin_driver::kParamRandomize;
using downspout::gremlin_driver::kParamStatusBpm;
using downspout::gremlin_driver::kParamStatusLaneStart;
using downspout::gremlin_driver::kParamStatusTransport;
using downspout::gremlin_driver::kParamStatusTriggerStart;
using downspout::gremlin_driver::kParamTriggerStart;
using downspout::gremlin_driver::kShapeNames;
using downspout::gremlin_driver::kTargetNames;
using downspout::gremlin_driver::kTriggerCount;
using downspout::gremlin_driver::kTriggerParamSpecs;
using downspout::gremlin_driver::kTriggerStatusNames;

ParameterEnumerationValue kClockModeEnumValues[] = {
    {0.0f, "Manual"},
    {1.0f, "Host"},
};

ParameterEnumerationValue kTargetEnumValues[] = {
    {0.0f, "Off"},
    {1.0f, "Macro 1"},
    {2.0f, "Macro 2"},
    {3.0f, "Macro 3"},
    {4.0f, "Macro 4"},
    {5.0f, "Macro 5"},
    {6.0f, "Macro 6"},
    {7.0f, "Macro 7"},
    {8.0f, "Macro 8"},
    {9.0f, "Master"},
};

ParameterEnumerationValue kShapeEnumValues[] = {
    {0.0f, "Sine"},
    {1.0f, "Triangle"},
    {2.0f, "Ramp"},
    {3.0f, "SampleHold"},
    {4.0f, "RandomWalk"},
    {5.0f, "Logistic"},
};

ParameterEnumerationValue kActionEnumValues[] = {
    {0.0f, "Off"},
    {1.0f, "Reseed"},
    {2.0f, "Burst"},
    {3.0f, "Rand Source"},
    {4.0f, "Rand Delay"},
    {5.0f, "Rand All"},
    {6.0f, "Scene Down"},
    {7.0f, "Scene Up"},
    {8.0f, "Panic"},
    {9.0f, "Mode Down"},
    {10.0f, "Mode Up"},
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
    MidiEvent midiEvent {};
    midiEvent.frame = event.frame;
    midiEvent.size = event.size;
    for (std::size_t i = 0; i < event.size && i < MidiEvent::kDataSize; ++i)
        midiEvent.data[i] = event.data[i];
    midiEvent.dataExt = nullptr;
    return midiEvent;
}

TransportSnapshot toCoreTransport(const TimePosition& timePos, const float fallbackBpm, const int clockMode)
{
    TransportSnapshot transport {};
    transport.transportDetected = timePos.bbt.valid;
    transport.transportRunning = timePos.playing && timePos.bbt.valid;
    transport.playing = timePos.playing;
    transport.bpm = fallbackBpm;

    if (timePos.bbt.valid && timePos.bbt.beatsPerMinute > 1.0)
        transport.bpm = static_cast<float>(timePos.bbt.beatsPerMinute);

    if (clockMode == 0)
        transport.bpm = fallbackBpm;

    return transport;
}

}  // namespace

class GremlinDriverPlugin : public Plugin
{
public:
    GremlinDriverPlugin()
        : Plugin(kGremlinDriverParameterCount, 0, 0)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override
    {
        return "gremlin_driver";
    }

    const char* getDescription() const override
    {
        return "Tempo-aware modulation and action sequencer for Gremlin-compatible MIDI control.";
    }

    const char* getMaker() const override
    {
        return "danja";
    }

    const char* getHomePage() const override
    {
        return "https://danja.github.io/flues/";
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
        return d_cconst('G', 'r', 'D', 'v');
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

        if (index == kParamClockMode)
        {
            const auto& spec = kGlobalParamSpecs[0];
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = spec.minimum;
            parameter.ranges.max = spec.maximum;
            parameter.ranges.def = spec.defaultValue;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kClockModeEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kClockModeEnumValues;
            parameter.enumValues.deleteLater = false;
            return;
        }

        if (index == kParamBpm)
        {
            const auto& spec = kGlobalParamSpecs[1];
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            parameter.ranges.min = spec.minimum;
            parameter.ranges.max = spec.maximum;
            parameter.ranges.def = spec.defaultValue;
            return;
        }

        if (index >= kParamLaneStart && index < kParamTriggerStart)
        {
            const auto& spec = kLaneParamSpecs[index - kParamLaneStart];
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            parameter.ranges.min = spec.minimum;
            parameter.ranges.max = spec.maximum;
            parameter.ranges.def = spec.defaultValue;
            if (spec.integer)
                parameter.hints |= kParameterIsInteger;

            const std::size_t offset = (index - kParamLaneStart) % 5;
            if (offset == 0)
            {
                parameter.enumValues.count = static_cast<uint8_t>(std::size(kTargetEnumValues));
                parameter.enumValues.restrictedMode = true;
                parameter.enumValues.values = kTargetEnumValues;
                parameter.enumValues.deleteLater = false;
            }
            else if (offset == 1)
            {
                parameter.enumValues.count = static_cast<uint8_t>(std::size(kShapeEnumValues));
                parameter.enumValues.restrictedMode = true;
                parameter.enumValues.values = kShapeEnumValues;
                parameter.enumValues.deleteLater = false;
            }
            return;
        }

        if (index >= kParamTriggerStart && index < kParamRandomize)
        {
            const auto& spec = kTriggerParamSpecs[index - kParamTriggerStart];
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            parameter.ranges.min = spec.minimum;
            parameter.ranges.max = spec.maximum;
            parameter.ranges.def = spec.defaultValue;
            if (spec.integer)
                parameter.hints |= kParameterIsInteger;

            if ((index - kParamTriggerStart) % 3 == 0)
            {
                parameter.enumValues.count = static_cast<uint8_t>(std::size(kActionEnumValues));
                parameter.enumValues.restrictedMode = true;
                parameter.enumValues.values = kActionEnumValues;
                parameter.enumValues.deleteLater = false;
            }
            return;
        }

        if (index == kParamRandomize)
        {
            parameter.name = "Randomise";
            parameter.symbol = "randomize";
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index >= kParamStatusLaneStart && index < kParamStatusTriggerStart)
        {
            const std::size_t laneIndex = index - kParamStatusLaneStart;
            parameter.name = kLaneStatusNames[laneIndex];
            parameter.symbol = laneIndex == 0 ? "status_lane1" :
                               laneIndex == 1 ? "status_lane2" :
                               laneIndex == 2 ? "status_lane3" : "status_lane4";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index >= kParamStatusTriggerStart && index < kParamStatusTransport)
        {
            const std::size_t triggerIndex = index - kParamStatusTriggerStart;
            parameter.name = kTriggerStatusNames[triggerIndex];
            parameter.symbol = triggerIndex == 0 ? "status_trigger1" : "status_trigger2";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index == kParamStatusTransport)
        {
            parameter.name = "Transport Status";
            parameter.symbol = "status_transport";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        parameter.name = "Effective BPM";
        parameter.symbol = "status_bpm";
        parameter.hints = kParameterIsOutput;
        parameter.ranges.min = 40.0f;
        parameter.ranges.max = 220.0f;
        parameter.ranges.def = 120.0f;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == kParamClockMode)
            return static_cast<float>(processor_.getClockMode());

        if (index == kParamBpm)
            return processor_.getBpm();

        if (index >= kParamLaneStart && index < kParamTriggerStart)
        {
            const std::size_t laneIndex = (index - kParamLaneStart) / 5;
            const std::size_t offset = (index - kParamLaneStart) % 5;
            const auto& lane = processor_.getLane(laneIndex);
            switch (offset)
            {
            case 0: return static_cast<float>(lane.target);
            case 1: return static_cast<float>(lane.shape);
            case 2: return lane.rate;
            case 3: return lane.depth;
            default: return lane.center;
            }
        }

        if (index >= kParamTriggerStart && index < kParamRandomize)
        {
            const std::size_t triggerIndex = (index - kParamTriggerStart) / 3;
            const std::size_t offset = (index - kParamTriggerStart) % 3;
            const auto& trigger = processor_.getTrigger(triggerIndex);
            switch (offset)
            {
            case 0: return static_cast<float>(trigger.action);
            case 1: return trigger.rate;
            default: return trigger.chance;
            }
        }

        if (index == kParamRandomize)
            return 0.0f;

        const auto& status = processor_.getStatus();
        if (index >= kParamStatusLaneStart && index < kParamStatusTriggerStart)
            return status.laneValues[index - kParamStatusLaneStart];
        if (index >= kParamStatusTriggerStart && index < kParamStatusTransport)
            return status.triggerFlashes[index - kParamStatusTriggerStart];
        if (index == kParamStatusTransport)
            return status.transportIndicator;
        return status.effectiveBpm;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index == kParamClockMode)
        {
            processor_.setClockMode(static_cast<int>(std::lround(value)));
            return;
        }

        if (index == kParamBpm)
        {
            processor_.setBpm(value);
            return;
        }

        if (index >= kParamLaneStart && index < kParamTriggerStart)
        {
            const std::size_t laneIndex = (index - kParamLaneStart) / 5;
            const std::size_t offset = (index - kParamLaneStart) % 5;
            switch (offset)
            {
            case 0: processor_.setLaneTarget(laneIndex, static_cast<int>(std::lround(value))); break;
            case 1: processor_.setLaneShape(laneIndex, static_cast<int>(std::lround(value))); break;
            case 2: processor_.setLaneRate(laneIndex, value); break;
            case 3: processor_.setLaneDepth(laneIndex, value); break;
            case 4: processor_.setLaneCenter(laneIndex, value); break;
            default: break;
            }
            return;
        }

        if (index >= kParamTriggerStart && index < kParamRandomize)
        {
            const std::size_t triggerIndex = (index - kParamTriggerStart) / 3;
            const std::size_t offset = (index - kParamTriggerStart) % 3;
            switch (offset)
            {
            case 0: processor_.setTriggerAction(triggerIndex, static_cast<int>(std::lround(value))); break;
            case 1: processor_.setTriggerRate(triggerIndex, value); break;
            case 2: processor_.setTriggerChance(triggerIndex, value); break;
            default: break;
            }
            return;
        }

        if (index == kParamRandomize && value > 0.5f)
            processor_.triggerRandomize();
    }

    void activate() override
    {
        processor_.activate();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        processor_.init(newSampleRate);
    }

    void run(const float**,
             float**,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        std::array<MidiMessage, 256> coreEvents {};
        const std::uint32_t coreCount = std::min<std::uint32_t>(midiEventCount, coreEvents.size());
        for (std::uint32_t i = 0; i < coreCount; ++i)
            coreEvents[i] = toCoreMidiMessage(midiEvents[i]);

        ProcessResult result = processor_.processBlock(frames,
                                                       toCoreTransport(getTimePosition(), processor_.getBpm(), processor_.getClockMode()),
                                                       coreEvents.data(),
                                                       coreCount);

        std::stable_sort(result.events.begin(),
                         result.events.begin() + result.eventCount,
                         [](const MidiMessage& a, const MidiMessage& b) {
                             return a.frame < b.frame;
                         });

        for (std::uint32_t i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result.events[i]));
    }

private:
    Processor processor_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GremlinDriverPlugin)
};

Plugin* createPlugin()
{
    return new GremlinDriverPlugin();
}

END_NAMESPACE_DISTRHO
