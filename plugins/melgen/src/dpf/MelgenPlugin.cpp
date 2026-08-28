#include "DistrhoPlugin.hpp"

#include "melgen_engine.hpp"
#include "melgen_pattern.hpp"
#include "melgen_serialization.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamRootNote = 0,
    kParamScale,
    kParamChannel,
    kParamLengthBeats,
    kParamPhraseLength,
    kParamSubdivision,
    kParamPeriod,
    kParamContour,
    kParamAnswer,
    kParamDensity,
    kParamRegister,
    kParamHold,
    kParamAccent,
    kParamStructure,
    kParamRange,
    kParamLeap,
    kParamRest,
    kParamCadence,
    kParamSeed,
    kParamVary,
    kParamActionNew,
    kParamActionNotes,
    kParamActionRhythm,
    kParamFollow,
    kParamColor,
    kParamConductorChannel,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStateControls = 0,
    kStatePattern,
    kStateVariation,
    kStateCount
};

constexpr const char* kStateKeyControls = "controls";
constexpr const char* kStateKeyPattern = "pattern";
constexpr const char* kStateKeyVariation = "variation";

using CoreControls = downspout::melgen::Controls;
using CoreEngineState = downspout::melgen::EngineState;
using CoreTransport = downspout::melgen::TransportSnapshot;
using CoreInputMidiEvent = downspout::melgen::InputMidiEvent;
using CoreScheduledMidiEvent = downspout::melgen::ScheduledMidiEvent;
using CoreMidiEventType = downspout::melgen::MidiEventType;

CoreTransport toCoreTransport(const TimePosition& timePos)
{
    CoreTransport transport;
    transport.playing = timePos.playing;
    transport.valid = timePos.bbt.valid;

    if (timePos.bbt.valid) {
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

CoreInputMidiEvent toCoreMidiEvent(const MidiEvent& event)
{
    CoreInputMidiEvent midi {};
    midi.frame = event.frame;
    midi.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, downspout::melgen::kMaxMidiMessageData));

    const uint8_t* const source = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < midi.size; ++i) {
        midi.data[i] = source[i];
    }

    return midi;
}

MidiEvent toDpfMidiEvent(const CoreScheduledMidiEvent& event)
{
    MidiEvent midiEvent {};
    midiEvent.frame = event.frame;
    midiEvent.size = 3;
    midiEvent.data[0] = static_cast<uint8_t>((event.type == CoreMidiEventType::noteOn ? 0x90 : 0x80) | (event.channel & 0x0f));
    midiEvent.data[1] = event.data1;
    midiEvent.data[2] = event.data2;
    midiEvent.dataExt = nullptr;
    return midiEvent;
}

void initFloat(Parameter& parameter, const char* name, const char* symbol, float min, float max, float def)
{
    parameter.name = name;
    parameter.symbol = symbol;
    parameter.hints = kParameterIsAutomatable;
    parameter.ranges.min = min;
    parameter.ranges.max = max;
    parameter.ranges.def = def;
}

void initInteger(Parameter& parameter, const char* name, const char* symbol, float min, float max, float def)
{
    initFloat(parameter, name, symbol, min, max, def);
    parameter.hints |= kParameterIsInteger;
}

}  // namespace

class MelgenPlugin : public Plugin
{
public:
    MelgenPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        controls_ = downspout::melgen::clampControls(controls_);
        engine_.controls = controls_;
        engine_.previousControls = controls_;
    }

protected:
    const char* getLabel() const override { return "MelGen"; }
    const char* getDescription() const override { return "Transport-synced MIDI melody generator with phrase and period structure."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'l', 'G', 'n'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (!input && index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        switch (index) {
        case kParamRootNote: initInteger(parameter, "Root Note", "root_note", 0.0f, 127.0f, 60.0f); break;
        case kParamScale:
            initInteger(parameter, "Scale", "scale", 0.0f, static_cast<float>(static_cast<int>(downspout::melgen::ScaleId::count) - 1), static_cast<float>(static_cast<int>(downspout::melgen::ScaleId::major)));
            break;
        case kParamChannel: initInteger(parameter, "Channel", "channel", 1.0f, 16.0f, 1.0f); break;
        case kParamLengthBeats:
            initInteger(parameter, "Length Beats", "length_beats", static_cast<float>(downspout::melgen::kMinLengthBeats), static_cast<float>(downspout::melgen::kMaxLengthBeats), 16.0f);
            break;
        case kParamPhraseLength: initInteger(parameter, "Phrase Bars", "phrase_bars", 1.0f, 8.0f, 2.0f); break;
        case kParamSubdivision:
            initInteger(parameter, "Subdivision", "subdivision", 0.0f, static_cast<float>(static_cast<int>(downspout::melgen::SubdivisionId::count) - 1), 1.0f);
            break;
        case kParamPeriod:
            initInteger(parameter, "Period", "period", 0.0f, static_cast<float>(static_cast<int>(downspout::melgen::PeriodId::count) - 1), 4.0f);
            break;
        case kParamContour:
            initInteger(parameter, "Contour", "contour", 0.0f, static_cast<float>(static_cast<int>(downspout::melgen::ContourId::count) - 1), 4.0f);
            break;
        case kParamAnswer:
            initInteger(parameter, "Answer", "answer", 0.0f, static_cast<float>(static_cast<int>(downspout::melgen::AnswerId::count) - 1), 0.0f);
            break;
        case kParamDensity: initFloat(parameter, "Density", "density", 0.0f, 1.0f, 0.48f); break;
        case kParamRegister: initInteger(parameter, "Register", "register", 0.0f, 4.0f, 1.0f); break;
        case kParamHold: initFloat(parameter, "Hold", "hold", 0.0f, 1.0f, 0.42f); break;
        case kParamAccent: initFloat(parameter, "Accent", "accent", 0.0f, 1.0f, 0.45f); break;
        case kParamStructure: initFloat(parameter, "Structure", "structure", 0.0f, 1.0f, 0.62f); break;
        case kParamRange: initFloat(parameter, "Range", "range", 0.0f, 1.0f, 0.45f); break;
        case kParamLeap: initFloat(parameter, "Leap", "leap", 0.0f, 1.0f, 0.28f); break;
        case kParamRest: initFloat(parameter, "Rest", "rest", 0.0f, 1.0f, 0.24f); break;
        case kParamCadence: initFloat(parameter, "Cadence", "cadence", 0.0f, 1.0f, 0.55f); break;
        case kParamSeed: initInteger(parameter, "Seed", "seed", 1.0f, 65535.0f, 1.0f); break;
        case kParamVary: initFloat(parameter, "Vary", "vary", 0.0f, 100.0f, 0.0f); break;
        case kParamFollow: initFloat(parameter, "Follow", "follow", 0.0f, 1.0f, 0.0f); break;
        case kParamColor: initFloat(parameter, "Color", "color", 0.0f, 1.0f, 0.5f); break;
        case kParamConductorChannel: initInteger(parameter, "Conductor Ch", "conductor_ch", 0.0f, 16.0f, 0.0f); break;
        case kParamActionNew:
        case kParamActionNotes:
        case kParamActionRhythm:
            parameter.name = index == kParamActionNew ? "New" : (index == kParamActionNotes ? "Notes" : "Rhythm");
            parameter.symbol = index == kParamActionNew ? "new" : (index == kParamActionNotes ? "notes" : "rhythm");
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
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
        if (index == kStateControls) {
            state.key = kStateKeyControls;
            state.label = "Controls";
        } else if (index == kStatePattern) {
            state.key = kStateKeyPattern;
            state.label = "Pattern";
        } else if (index == kStateVariation) {
            state.key = kStateKeyVariation;
            state.label = "Variation";
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamRootNote: return static_cast<float>(controls_.rootNote);
        case kParamScale: return static_cast<float>(static_cast<int>(controls_.scale));
        case kParamChannel: return static_cast<float>(controls_.channel);
        case kParamLengthBeats: return static_cast<float>(controls_.lengthBeats);
        case kParamPhraseLength: return static_cast<float>(controls_.phraseLengthBars);
        case kParamSubdivision: return static_cast<float>(static_cast<int>(controls_.subdivision));
        case kParamPeriod: return static_cast<float>(static_cast<int>(controls_.period));
        case kParamContour: return static_cast<float>(static_cast<int>(controls_.contour));
        case kParamAnswer: return static_cast<float>(static_cast<int>(controls_.answer));
        case kParamDensity: return controls_.density;
        case kParamRegister: return static_cast<float>(controls_.reg);
        case kParamHold: return controls_.hold;
        case kParamAccent: return controls_.accent;
        case kParamStructure: return controls_.structure;
        case kParamRange: return controls_.range;
        case kParamLeap: return controls_.leap;
        case kParamRest: return controls_.rest;
        case kParamCadence: return controls_.cadence;
        case kParamSeed: return static_cast<float>(controls_.seed);
        case kParamVary: return controls_.vary * 100.0f;
        case kParamFollow: return controls_.follow;
        case kParamColor: return controls_.color;
        case kParamConductorChannel: return static_cast<float>(conductorChannel_);
        case kParamActionNew:
        case kParamActionNotes:
        case kParamActionRhythm:
            return 0.0f;
        default:
            return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case kParamRootNote: controls_.rootNote = static_cast<int>(value); break;
        case kParamScale: controls_.scale = static_cast<downspout::melgen::ScaleId>(static_cast<int>(value)); break;
        case kParamChannel: controls_.channel = static_cast<int>(value); break;
        case kParamLengthBeats: controls_.lengthBeats = static_cast<int>(value); break;
        case kParamPhraseLength: controls_.phraseLengthBars = static_cast<int>(value); break;
        case kParamSubdivision: controls_.subdivision = static_cast<downspout::melgen::SubdivisionId>(static_cast<int>(value)); break;
        case kParamPeriod: controls_.period = static_cast<downspout::melgen::PeriodId>(static_cast<int>(value)); break;
        case kParamContour: controls_.contour = static_cast<downspout::melgen::ContourId>(static_cast<int>(value)); break;
        case kParamAnswer: controls_.answer = static_cast<downspout::melgen::AnswerId>(static_cast<int>(value)); break;
        case kParamDensity: controls_.density = value; break;
        case kParamRegister: controls_.reg = static_cast<int>(value); break;
        case kParamHold: controls_.hold = value; break;
        case kParamAccent: controls_.accent = value; break;
        case kParamStructure: controls_.structure = value; break;
        case kParamRange: controls_.range = value; break;
        case kParamLeap: controls_.leap = value; break;
        case kParamRest: controls_.rest = value; break;
        case kParamCadence: controls_.cadence = value; break;
        case kParamSeed: controls_.seed = static_cast<uint32_t>(value); break;
        case kParamVary: controls_.vary = value / 100.0f; break;
        case kParamFollow: controls_.follow = value; break;
        case kParamColor: controls_.color = value; break;
        case kParamConductorChannel: conductorChannel_ = static_cast<int>(value); break;
        case kParamActionNew: if (value > 0.5f) ++controls_.actionNew; break;
        case kParamActionNotes: if (value > 0.5f) ++controls_.actionNotes; break;
        case kParamActionRhythm: if (value > 0.5f) ++controls_.actionRhythm; break;
        }

        controls_ = downspout::melgen::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyControls) == 0) {
            return String(downspout::melgen::serializeControls(controls_).c_str());
        }
        if (std::strcmp(key, kStateKeyPattern) == 0) {
            return String(downspout::melgen::serializePatternState(engine_.pattern).c_str());
        }
        if (std::strcmp(key, kStateKeyVariation) == 0) {
            return String(downspout::melgen::serializeVariationState(engine_.variation).c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";
        if (std::strcmp(key, kStateKeyControls) == 0) {
            const auto controls = downspout::melgen::deserializeControls(text);
            if (controls.has_value()) {
                controls_ = *controls;
                engine_.controls = controls_;
                engine_.previousControls = controls_;
            }
            return;
        }
        if (std::strcmp(key, kStateKeyPattern) == 0) {
            const auto pattern = downspout::melgen::deserializePatternState(text);
            if (pattern.has_value()) {
                engine_.pattern = *pattern;
                engine_.patternValid = true;
            }
            return;
        }
        if (std::strcmp(key, kStateKeyVariation) == 0) {
            const auto variation = downspout::melgen::deserializeVariationState(text);
            if (variation.has_value()) {
                engine_.variation = *variation;
            }
        }
    }

    void activate() override { downspout::melgen::activate(engine_, controls_); }
    void deactivate() override { downspout::melgen::deactivate(engine_); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        const int condCh = conductorChannel_;
        if (condCh > 0) {
            for (uint32_t i = 0; i < midiEventCount; ++i) {
                const auto& ev = midiEvents[i];
                if (ev.size >= 3 && (ev.data[0] & 0xf0) == 0xb0 && (ev.data[0] & 0x0f) == condCh - 1) {
                    const auto cc  = ev.data[1];
                    const auto val = ev.data[2];
                    switch (cc) {
                    case 20:
                        controls_.contour = static_cast<downspout::melgen::ContourId>(std::clamp(val / 22, 0, 5));
                        break;
                    case 21:
                        controls_.scale = static_cast<downspout::melgen::ScaleId>(std::clamp(val * 23 / 128, 0, 22));
                        break;
                    case 22:
                        controls_.density = val / 127.0f;
                        break;
                    case 23:
                        controls_.vary = val / 127.0f;
                        break;
                    case 24:
                        if (val == 127) ++controls_.actionNew;
                        break;
                    default: break;
                    }
                }
            }
            controls_ = downspout::melgen::clampControls(controls_);
        }

        std::array<CoreInputMidiEvent, downspout::melgen::kMaxInputMidiEvents> inputEvents {};
        const bool followEnabled = controls_.follow > 0.001f;
        const uint32_t inputCount = followEnabled && midiEvents != nullptr
            ? std::min<uint32_t>(midiEventCount, static_cast<uint32_t>(inputEvents.size()))
            : 0u;
        for (uint32_t i = 0; i < inputCount; ++i) {
            inputEvents[i] = toCoreMidiEvent(midiEvents[i]);
        }

        const downspout::melgen::BlockResult result =
            downspout::melgen::processBlock(engine_,
                                            controls_,
                                            toCoreTransport(getTimePosition()),
                                            frames,
                                            getSampleRate(),
                                            inputEvents.data(),
                                            inputCount);

        for (int index = 0; index < result.eventCount; ++index) {
            writeMidiEvent(toDpfMidiEvent(result.events[index]));
        }
    }

private:
    CoreControls controls_ {};
    CoreEngineState engine_ {};
    int conductorChannel_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelgenPlugin)
};

Plugin* createPlugin()
{
    return new MelgenPlugin();
}

END_NAMESPACE_DISTRHO
