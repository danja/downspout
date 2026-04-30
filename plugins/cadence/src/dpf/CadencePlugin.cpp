#include "DistrhoPlugin.hpp"

#include "cadence_engine.hpp"
#include "cadence_serialization.hpp"

#include <algorithm>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamKey = 0,
    kParamScale,
    kParamCycleBars,
    kParamGranularity,
    kParamComplexity,
    kParamMovement,
    kParamChordSize,
    kParamNoteLength,
    kParamRegister,
    kParamSpread,
    kParamPassInput,
    kParamOutputChannel,
    kParamActionLearn,
    kParamStatusReady,
    kParamVary,
    kParamComp,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStateControls = 0,
    kStateProgression,
    kStateVariation,
    kStateCount
};

constexpr const char* kStateKeyControls = "controls";
constexpr const char* kStateKeyProgression = "progression";
constexpr const char* kStateKeyVariation = "variation";

using CoreControls = downspout::cadence::Controls;
using CoreEngineState = downspout::cadence::EngineState;
using CoreTransport = downspout::cadence::TransportSnapshot;
using CoreMidiEvent = downspout::cadence::InputMidiEvent;
using CoreProgressionState = downspout::cadence::ProgressionState;

CoreTransport toCoreTransport(const TimePosition& timePos)
{
    CoreTransport transport;
    transport.playing = timePos.playing;
    transport.valid = timePos.bbt.valid;

    if (timePos.bbt.valid)
    {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1)
            + (timePos.bbt.ticksPerBeat > 0.0 ? (timePos.bbt.tick / timePos.bbt.ticksPerBeat) : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.beatType = timePos.bbt.beatType;
        transport.bpm = timePos.bbt.beatsPerMinute;
        transport.meter = downspout::meterFromTimeSignature(timePos.bbt.beatsPerBar, timePos.bbt.beatType);
    }

    return transport;
}

CoreMidiEvent toCoreMidiEvent(const MidiEvent& event)
{
    CoreMidiEvent midi {};
    midi.frame = event.frame;
    midi.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, downspout::cadence::kMaxMidiMessageData));

    const uint8_t* const source = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < midi.size; ++i)
        midi.data[i] = source[i];

    return midi;
}

MidiEvent toDpfMidiEvent(const downspout::cadence::ScheduledMidiEvent& event)
{
    MidiEvent midi {};
    midi.frame = event.frame;
    midi.size = event.size;
    midi.dataExt = nullptr;
    for (std::size_t i = 0; i < event.size && i < MidiEvent::kDataSize; ++i)
        midi.data[i] = event.data[i];
    return midi;
}

}  // namespace

class CadencePlugin : public Plugin
{
public:
    CadencePlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        controls_ = downspout::cadence::clampControls(controls_);
        engine_.controls = controls_;
        engine_.previousControls = controls_;
        readyStatus_ = 0.0f;
    }

protected:
    const char* getLabel() const override
    {
        return "Cadence";
    }

    const char* getDescription() const override
    {
        return "Transport-synced MIDI harmonizer that learns a cycle and plays voiced chord progressions.";
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
        return d_cconst('C', 'd', 'n', 'c');
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

        switch (index)
        {
        case kParamKey:
            parameter.name = "Key";
            parameter.symbol = "key";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 11.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamScale:
            parameter.name = "Scale";
            parameter.symbol = "scale";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(downspout::cadence::SCALE_COUNT - 1);
            parameter.ranges.def = static_cast<float>(downspout::cadence::SCALE_NAT_MINOR);
            break;
        case kParamCycleBars:
            parameter.name = "Cycle Bars";
            parameter.symbol = "cycle_bars";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 8.0f;
            parameter.ranges.def = 2.0f;
            break;
        case kParamGranularity:
            parameter.name = "Granularity";
            parameter.symbol = "granularity";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2.0f;
            parameter.ranges.def = static_cast<float>(downspout::cadence::GRANULARITY_HALF_BAR);
            break;
        case kParamComplexity:
            parameter.name = "Complexity";
            parameter.symbol = "complexity";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.45f;
            break;
        case kParamMovement:
            parameter.name = "Movement";
            parameter.symbol = "movement";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.65f;
            break;
        case kParamChordSize:
            parameter.name = "Chord Size";
            parameter.symbol = "chord_size";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamNoteLength:
            parameter.name = "Note Length";
            parameter.symbol = "note_length";
            parameter.ranges.min = 0.10f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamRegister:
            parameter.name = "Register";
            parameter.symbol = "register";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamSpread:
            parameter.name = "Spread";
            parameter.symbol = "spread";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamPassInput:
            parameter.name = "Pass Input";
            parameter.symbol = "pass_input";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamOutputChannel:
            parameter.name = "Output Channel";
            parameter.symbol = "output_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionLearn:
            parameter.name = "Learn";
            parameter.symbol = "learn";
            parameter.hints = kParameterIsAutomatable|kParameterIsBoolean|kParameterIsInteger|kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusReady:
            parameter.name = "Ready";
            parameter.symbol = "ready";
            parameter.hints = kParameterIsAutomatable|kParameterIsOutput|kParameterIsBoolean|kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamVary:
            parameter.name = "Vary";
            parameter.symbol = "vary";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamComp:
            parameter.name = "Comp";
            parameter.symbol = "comp";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 0.0f;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";

        switch (index)
        {
        case kStateControls:
            state.key = kStateKeyControls;
            state.label = "Controls";
            break;
        case kStateProgression:
            state.key = kStateKeyProgression;
            state.label = "Progression";
            break;
        case kStateVariation:
            state.key = kStateKeyVariation;
            state.label = "Variation";
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamKey: return static_cast<float>(controls_.key);
        case kParamScale: return static_cast<float>(controls_.scale);
        case kParamCycleBars: return static_cast<float>(controls_.cycle_bars);
        case kParamGranularity: return static_cast<float>(controls_.granularity);
        case kParamComplexity: return controls_.complexity;
        case kParamMovement: return controls_.movement;
        case kParamChordSize: return static_cast<float>(controls_.chord_size);
        case kParamNoteLength: return controls_.note_length;
        case kParamRegister: return static_cast<float>(controls_.reg);
        case kParamSpread: return static_cast<float>(controls_.spread);
        case kParamPassInput: return controls_.pass_input ? 1.0f : 0.0f;
        case kParamOutputChannel: return static_cast<float>(controls_.output_channel);
        case kParamVary: return controls_.vary * 100.0f;
        case kParamComp: return controls_.comp * 100.0f;
        case kParamStatusReady: return readyStatus_;
        case kParamActionLearn:
        default:
            return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamKey: controls_.key = static_cast<int>(value); break;
        case kParamScale: controls_.scale = static_cast<int>(value); break;
        case kParamCycleBars: controls_.cycle_bars = static_cast<int>(value); break;
        case kParamGranularity: controls_.granularity = static_cast<int>(value); break;
        case kParamComplexity: controls_.complexity = value; break;
        case kParamMovement: controls_.movement = value; break;
        case kParamChordSize: controls_.chord_size = static_cast<int>(value); break;
        case kParamNoteLength: controls_.note_length = value; break;
        case kParamRegister: controls_.reg = static_cast<int>(value); break;
        case kParamSpread: controls_.spread = static_cast<int>(value); break;
        case kParamPassInput: controls_.pass_input = value >= 0.5f; break;
        case kParamOutputChannel: controls_.output_channel = static_cast<int>(value); break;
        case kParamActionLearn: if (value > 0.5f) ++controls_.action_learn; break;
        case kParamVary: controls_.vary = value / 100.0f; break;
        case kParamComp: controls_.comp = value / 100.0f; break;
        case kParamStatusReady: break;
        }

        controls_ = downspout::cadence::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyControls) == 0)
            return String(downspout::cadence::serializeControls(controls_).c_str());

        if (std::strcmp(key, kStateKeyProgression) == 0)
        {
            CoreProgressionState progression;
            progression.segmentCount = engine_.playbackSegmentCount;
            progression.ready = engine_.ready;
            for (int i = 0; i < progression.segmentCount; ++i)
                progression.slots[static_cast<std::size_t>(i)] = engine_.playback[static_cast<std::size_t>(i)];
            return String(downspout::cadence::serializeProgressionState(progression).c_str());
        }

        if (std::strcmp(key, kStateKeyVariation) == 0)
            return String(downspout::cadence::serializeVariationState(engine_.variation).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";

        if (std::strcmp(key, kStateKeyControls) == 0)
        {
            const auto controls = downspout::cadence::deserializeControls(text);
            if (controls.has_value())
            {
                controls_ = *controls;
                engine_.controls = controls_;
                engine_.previousControls = controls_;
            }
            return;
        }

        if (std::strcmp(key, kStateKeyProgression) == 0)
        {
            const auto progression = downspout::cadence::deserializeProgressionState(text);
            if (progression.has_value())
            {
                engine_.playback.fill({});
                engine_.baseProgression.fill({});
                engine_.playbackSegmentCount = progression->segmentCount;
                engine_.baseSegmentCount = progression->segmentCount;
                engine_.ready = progression->ready;
                for (int i = 0; i < progression->segmentCount; ++i)
                {
                    engine_.playback[static_cast<std::size_t>(i)] = progression->slots[static_cast<std::size_t>(i)];
                    engine_.baseProgression[static_cast<std::size_t>(i)] = progression->slots[static_cast<std::size_t>(i)];
                }
                engine_.learnedCapture.fill({});
                engine_.capture.fill({});
                engine_.learnedSegmentCount = 0;
                engine_.haveLearnedCapture = false;
                readyStatus_ = engine_.ready ? 1.0f : 0.0f;
            }
            return;
        }

        if (std::strcmp(key, kStateKeyVariation) == 0)
        {
            const auto variation = downspout::cadence::deserializeVariationState(text);
            if (variation.has_value())
                engine_.variation = *variation;
        }
    }

    void activate() override
    {
        downspout::cadence::activate(engine_);
    }

    void deactivate() override
    {
        downspout::cadence::deactivate(engine_);
    }

    void run(const float**, float**, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        std::array<CoreMidiEvent, 512> inputEvents {};
        const uint32_t eventCount = std::min<uint32_t>(midiEventCount, static_cast<uint32_t>(inputEvents.size()));
        for (uint32_t i = 0; i < eventCount; ++i)
            inputEvents[i] = toCoreMidiEvent(midiEvents[i]);

        const downspout::cadence::BlockResult result =
            downspout::cadence::processBlock(engine_,
                                             controls_,
                                             toCoreTransport(getTimePosition()),
                                             frames,
                                             getSampleRate(),
                                             inputEvents.data(),
                                             eventCount);

        readyStatus_ = result.ready ? 1.0f : 0.0f;

        for (int i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result.events[static_cast<std::size_t>(i)]));
    }

private:
    CoreControls controls_ {};
    CoreEngineState engine_ {};
    float readyStatus_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CadencePlugin)
};

Plugin* createPlugin()
{
    return new CadencePlugin();
}

END_NAMESPACE_DISTRHO
