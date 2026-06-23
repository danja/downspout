#include "DistrhoPlugin.hpp"

#include "counterpointer_engine.hpp"
#include "counterpointer_serialization.hpp"

#include <algorithm>
#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamKey = 0,
    kParamScale,
    kParamCycleBars,
    kParamGranularity,
    kParamFollow,
    kParamCounter,
    kParamShortRandom,
    kParamLongRandom,
    kParamDensity,
    kParamRhythmFollow,
    kParamSyncopation,
    kParamConsonance,
    kParamEmbellish,
    kParamRegularity,
    kParamRegister,
    kParamSpan,
    kParamGate,
    kParamVelocityFollow,
    kParamPassInput,
    kParamOutputChannel,
    kParamFreeze,
    kParamActionLearn,
    kParamStatusReady,
    kParamStatusInput,
    kParamStatusOutput,
    kParamColor,
    kParamResponseMode,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStateControls = 0,
    kStatePhrase,
    kStateVariation,
    kStateCount
};

constexpr const char* kStateKeyControls = "controls";
constexpr const char* kStateKeyPhrase = "phrase";
constexpr const char* kStateKeyVariation = "variation";

using CoreControls = downspout::counterpointer::Controls;
using CoreEngineState = downspout::counterpointer::EngineState;
using CoreTransport = downspout::counterpointer::TransportSnapshot;
using CoreMidiEvent = downspout::counterpointer::InputMidiEvent;

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
    midi.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, downspout::counterpointer::kMaxMidiMessageData));

    const uint8_t* const source = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < midi.size; ++i)
        midi.data[i] = source[i];

    return midi;
}

MidiEvent toDpfMidiEvent(const downspout::counterpointer::ScheduledMidiEvent& event)
{
    MidiEvent midi {};
    midi.frame = event.frame;
    midi.size = event.size;
    midi.dataExt = nullptr;
    for (std::size_t i = 0; i < event.size && i < MidiEvent::kDataSize; ++i)
        midi.data[i] = event.data[i];
    return midi;
}

void initPercentParameter(Parameter& parameter, const char* name, const char* symbol, const float def)
{
    parameter.name = name;
    parameter.symbol = symbol;
    parameter.ranges.min = 0.0f;
    parameter.ranges.max = 1.0f;
    parameter.ranges.def = def;
}

ParameterEnumerationValue kResponseModeEnumValues[] = {
    {0.0f, "Counterpoint"},
    {1.0f, "Bass Descend"},
};

}  // namespace

class CounterpointerPlugin : public Plugin
{
public:
    CounterpointerPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        controls_ = downspout::counterpointer::clampControls(controls_);
        engine_.controls = controls_;
        engine_.previousControls = controls_;
    }

protected:
    const char* getLabel() const override
    {
        return "Counterpointer";
    }

    const char* getDescription() const override
    {
        return "Transport-synced MIDI counter-melody generator driven by the incoming MIDI pattern.";
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
        return d_cconst('C', 'p', 't', 'r');
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
            parameter.ranges.max = static_cast<float>(downspout::counterpointer::SCALE_COUNT - 1);
            parameter.ranges.def = static_cast<float>(downspout::counterpointer::SCALE_NAT_MINOR);
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
            parameter.ranges.def = static_cast<float>(downspout::counterpointer::GRANULARITY_BEAT);
            break;
        case kParamFollow: initPercentParameter(parameter, "Follow", "follow", 0.55f); break;
        case kParamCounter: initPercentParameter(parameter, "Counter", "counter", 0.55f); break;
        case kParamShortRandom: initPercentParameter(parameter, "Short Random", "short_random", 0.15f); break;
        case kParamLongRandom: initPercentParameter(parameter, "Long Random", "long_random", 0.0f); break;
        case kParamDensity: initPercentParameter(parameter, "Density", "density", 0.78f); break;
        case kParamRhythmFollow: initPercentParameter(parameter, "Rhythm Follow", "rhythm_follow", 0.65f); break;
        case kParamSyncopation: initPercentParameter(parameter, "Syncopation", "syncopation", 0.25f); break;
        case kParamConsonance: initPercentParameter(parameter, "Consonance", "consonance", 0.75f); break;
        case kParamColor: initPercentParameter(parameter, "Color", "color", 0.0f); break;
        case kParamResponseMode:
            parameter.name = "Response Mode";
            parameter.symbol = "response_mode";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(downspout::counterpointer::RESPONSE_COUNT - 1);
            parameter.ranges.def = static_cast<float>(downspout::counterpointer::RESPONSE_COUNTERPOINT);
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kResponseModeEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kResponseModeEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamEmbellish: initPercentParameter(parameter, "Embellish", "embellish", 0.25f); break;
        case kParamRegularity: initPercentParameter(parameter, "Regularity", "regularity", 0.65f); break;
        case kParamRegister:
            parameter.name = "Register";
            parameter.symbol = "register";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamSpan: initPercentParameter(parameter, "Span", "span", 0.55f); break;
        case kParamGate:
            parameter.name = "Gate";
            parameter.symbol = "gate";
            parameter.ranges.min = 0.10f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.72f;
            break;
        case kParamVelocityFollow: initPercentParameter(parameter, "Velocity Follow", "velocity_follow", 0.65f); break;
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
        case kParamFreeze:
            parameter.name = "Freeze";
            parameter.symbol = "freeze";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
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
        case kParamStatusInput:
            parameter.name = "MIDI Input Activity";
            parameter.symbol = "status_midi_input";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusOutput:
            parameter.name = "MIDI Output Activity";
            parameter.symbol = "status_midi_output";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
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
        case kStatePhrase:
            state.key = kStateKeyPhrase;
            state.label = "Phrase";
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
        case kParamFollow: return controls_.follow;
        case kParamCounter: return controls_.counter;
        case kParamShortRandom: return controls_.short_random;
        case kParamLongRandom: return controls_.long_random;
        case kParamDensity: return controls_.density;
        case kParamRhythmFollow: return controls_.rhythm_follow;
        case kParamSyncopation: return controls_.syncopation;
        case kParamConsonance: return controls_.consonance;
        case kParamColor: return controls_.color;
        case kParamResponseMode: return static_cast<float>(controls_.response_mode);
        case kParamEmbellish: return controls_.embellish;
        case kParamRegularity: return controls_.regularity;
        case kParamRegister: return static_cast<float>(controls_.reg);
        case kParamSpan: return controls_.span;
        case kParamGate: return controls_.gate;
        case kParamVelocityFollow: return controls_.velocity_follow;
        case kParamPassInput: return controls_.pass_input ? 1.0f : 0.0f;
        case kParamOutputChannel: return static_cast<float>(controls_.output_channel);
        case kParamFreeze: return controls_.freeze ? 1.0f : 0.0f;
        case kParamStatusReady: return readyStatus_;
        case kParamStatusInput: return midiInputStatus_;
        case kParamStatusOutput: return midiOutputStatus_;
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
        case kParamFollow: controls_.follow = value; break;
        case kParamCounter: controls_.counter = value; break;
        case kParamShortRandom: controls_.short_random = value; break;
        case kParamLongRandom: controls_.long_random = value; break;
        case kParamDensity: controls_.density = value; break;
        case kParamRhythmFollow: controls_.rhythm_follow = value; break;
        case kParamSyncopation: controls_.syncopation = value; break;
        case kParamConsonance: controls_.consonance = value; break;
        case kParamColor: controls_.color = value; break;
        case kParamResponseMode: controls_.response_mode = static_cast<int>(value); break;
        case kParamEmbellish: controls_.embellish = value; break;
        case kParamRegularity: controls_.regularity = value; break;
        case kParamRegister: controls_.reg = static_cast<int>(value); break;
        case kParamSpan: controls_.span = value; break;
        case kParamGate: controls_.gate = value; break;
        case kParamVelocityFollow: controls_.velocity_follow = value; break;
        case kParamPassInput: controls_.pass_input = value >= 0.5f; break;
        case kParamOutputChannel: controls_.output_channel = static_cast<int>(value); break;
        case kParamFreeze: controls_.freeze = value >= 0.5f; break;
        case kParamActionLearn: if (value > 0.5f) ++controls_.action_learn; break;
        case kParamStatusReady: break;
        case kParamStatusInput: break;
        case kParamStatusOutput: break;
        }

        controls_ = downspout::counterpointer::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyControls) == 0)
            return String(downspout::counterpointer::serializeControls(controls_).c_str());

        if (std::strcmp(key, kStateKeyPhrase) == 0)
            return String(downspout::counterpointer::serializePhraseState(engine_.playbackPhrase).c_str());

        if (std::strcmp(key, kStateKeyVariation) == 0)
            return String(downspout::counterpointer::serializeVariationState(engine_.variation).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";

        if (std::strcmp(key, kStateKeyControls) == 0)
        {
            const auto controls = downspout::counterpointer::deserializeControls(text);
            if (controls.has_value())
            {
                controls_ = *controls;
                engine_.controls = controls_;
                engine_.previousControls = controls_;
            }
            return;
        }

        if (std::strcmp(key, kStateKeyPhrase) == 0)
        {
            const auto phrase = downspout::counterpointer::deserializePhraseState(text);
            if (phrase.has_value())
            {
                engine_.basePhrase = *phrase;
                engine_.playbackPhrase = *phrase;
                engine_.capture.fill({});
                readyStatus_ = phrase->ready ? 1.0f : 0.0f;
            }
            return;
        }

        if (std::strcmp(key, kStateKeyVariation) == 0)
        {
            const auto variation = downspout::counterpointer::deserializeVariationState(text);
            if (variation.has_value())
                engine_.variation = *variation;
        }
    }

    void activate() override
    {
        downspout::counterpointer::activate(engine_);
    }

    void deactivate() override
    {
        downspout::counterpointer::deactivate(engine_);
    }

    void run(const float**, float**, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        std::array<CoreMidiEvent, 512> inputEvents {};
        const uint32_t eventCount = std::min<uint32_t>(midiEventCount, static_cast<uint32_t>(inputEvents.size()));
        for (uint32_t i = 0; i < eventCount; ++i)
            inputEvents[i] = toCoreMidiEvent(midiEvents[i]);

        const downspout::counterpointer::BlockResult result =
            downspout::counterpointer::processBlock(engine_,
                                                    controls_,
                                                    toCoreTransport(getTimePosition()),
                                                    frames,
                                                    getSampleRate(),
                                                    inputEvents.data(),
                                                    eventCount);

        const float decay = static_cast<float>(frames) / static_cast<float>(std::max(1.0, getSampleRate()) * 0.20);
        midiInputStatus_ = std::max(0.0f, midiInputStatus_ - decay);
        midiOutputStatus_ = std::max(0.0f, midiOutputStatus_ - decay);
        if (eventCount > 0)
            midiInputStatus_ = 1.0f;
        if (result.eventCount > 0)
            midiOutputStatus_ = 1.0f;
        readyStatus_ = result.ready ? 1.0f : 0.0f;

        for (int i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result.events[static_cast<std::size_t>(i)]));
    }

private:
    CoreControls controls_ {};
    CoreEngineState engine_ {};
    float readyStatus_ = 0.0f;
    float midiInputStatus_ = 0.0f;
    float midiOutputStatus_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CounterpointerPlugin)
};

Plugin* createPlugin()
{
    return new CounterpointerPlugin();
}

END_NAMESPACE_DISTRHO
