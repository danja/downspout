#include "DistrhoPlugin.hpp"

#include "bassgen_engine.hpp"
#include "bassgen_pattern.hpp"
#include "bassgen_serialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamRootNote = 0,
    kParamScale,
    kParamGenre,
    kParamStyleMode,
    kParamChannel,
    kParamLengthBeats,
    kParamSubdivision,
    kParamDensity,
    kParamRegister,
    kParamHold,
    kParamAccent,
    kParamSeed,
    kParamVary,
    kParamActionNew,
    kParamActionNotes,
    kParamActionRhythm,
    kParamFollowDodge,
    kParamListenChannel,
    kParamListenNote,
    kParamColor,
    kParamInputMatchMode,
    kParamInputSensitivity,
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

using CoreControls = downspout::bassgen::Controls;
using CoreEngineState = downspout::bassgen::EngineState;
using CoreInputMidiEvent = downspout::bassgen::InputMidiEvent;
using CoreTransport = downspout::bassgen::TransportSnapshot;
using CoreMidiEvent = downspout::bassgen::ScheduledMidiEvent;
using CoreMidiEventType = downspout::bassgen::MidiEventType;

ParameterEnumerationValue kInputMatchEnumValues[] = {
    {0.0f, "Exact"},
    {1.0f, "Channel"},
    {2.0f, "Any"},
};

CoreTransport toCoreTransport(const TimePosition& timePos) {
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

MidiEvent toDpfMidiEvent(const CoreMidiEvent& event) {
    MidiEvent midiEvent {};
    midiEvent.frame = event.frame;
    midiEvent.size = 3;
    midiEvent.data[0] = static_cast<uint8_t>((event.type == CoreMidiEventType::noteOn ? 0x90 : 0x80) | (event.channel & 0x0f));
    midiEvent.data[1] = event.data1;
    midiEvent.data[2] = event.data2;
    midiEvent.dataExt = nullptr;
    return midiEvent;
}

CoreInputMidiEvent toCoreMidiEvent(const MidiEvent& event)
{
    CoreInputMidiEvent midi {};
    midi.frame = event.frame;
    midi.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, downspout::bassgen::kMaxMidiMessageData));

    const uint8_t* const source = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < midi.size; ++i) {
        midi.data[i] = source[i];
    }

    return midi;
}

}  // namespace

class BassgenPlugin : public Plugin
{
public:
    BassgenPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        controls_ = downspout::bassgen::clampControls(controls_);
        engine_.controls = controls_;
        engine_.previousControls = controls_;
    }

protected:
    const char* getLabel() const override
    {
        return "BassGen";
    }

    const char* getDescription() const override
    {
        return "Transport-synced MIDI bassline generator.";
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
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('B', 's', 'G', 'n');
    }

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
        parameter.hints = kParameterIsAutomatable;

        switch (index) {
        case kParamRootNote:
            parameter.name = "Root Note";
            parameter.symbol = "root_note";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = 36.0f;
            break;
        case kParamScale:
            parameter.name = "Scale";
            parameter.symbol = "scale";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(downspout::bassgen::ScaleId::count) - 1);
            parameter.ranges.def = 0.0f;
            break;
        case kParamGenre:
            parameter.name = "Genre";
            parameter.symbol = "genre";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(downspout::bassgen::GenreId::count) - 1);
            parameter.ranges.def = 0.0f;
            break;
        case kParamStyleMode:
            parameter.name = "Style";
            parameter.symbol = "style_mode";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(downspout::bassgen::StyleModeId::count) - 1);
            parameter.ranges.def = 0.0f;
            break;
        case kParamChannel:
            parameter.name = "Channel";
            parameter.symbol = "channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamLengthBeats:
            parameter.name = "Length Beats";
            parameter.symbol = "length_beats";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = static_cast<float>(downspout::bassgen::kMinLengthBeats);
            parameter.ranges.max = static_cast<float>(downspout::bassgen::kMaxLengthBeats);
            parameter.ranges.def = 16.0f;
            break;
        case kParamSubdivision:
            parameter.name = "Subdivision";
            parameter.symbol = "subdivision";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(downspout::bassgen::SubdivisionId::count) - 1);
            parameter.ranges.def = 1.0f;
            break;
        case kParamDensity:
            parameter.name = "Density";
            parameter.symbol = "density";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.45f;
            break;
        case kParamRegister:
            parameter.name = "Register";
            parameter.symbol = "register";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 3.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamHold:
            parameter.name = "Hold";
            parameter.symbol = "hold";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.35f;
            break;
        case kParamAccent:
            parameter.name = "Accent";
            parameter.symbol = "accent";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.45f;
            break;
        case kParamColor:
            parameter.name = "Color";
            parameter.symbol = "color";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.5f;
            break;
        case kParamSeed:
            parameter.name = "Seed";
            parameter.symbol = "seed";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 65535.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamVary:
            parameter.name = "Vary";
            parameter.symbol = "vary";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamFollowDodge:
            parameter.name = "Follow/Dodge";
            parameter.symbol = "follow_dodge";
            parameter.ranges.min = -100.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamListenChannel:
            parameter.name = "Listen Channel";
            parameter.symbol = "listen_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 10.0f;
            break;
        case kParamListenNote:
            parameter.name = "Listen Note";
            parameter.symbol = "listen_note";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = 36.0f;
            break;
        case kParamInputMatchMode:
            parameter.name = "Input Match";
            parameter.symbol = "input_match";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2.0f;
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kInputMatchEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kInputMatchEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamInputSensitivity:
            parameter.name = "Input Sensitivity";
            parameter.symbol = "input_sensitivity";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 100.0f;
            break;
        case kParamConductorChannel:
            parameter.name = "Conductor Ch";
            parameter.symbol = "conductor_ch";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionNew:
            parameter.name = "New";
            parameter.symbol = "new";
            parameter.hints = kParameterIsAutomatable|kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1048576.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionNotes:
            parameter.name = "Notes";
            parameter.symbol = "notes";
            parameter.hints = kParameterIsAutomatable|kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1048576.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionRhythm:
            parameter.name = "Rhythm";
            parameter.symbol = "rhythm";
            parameter.hints = kParameterIsAutomatable|kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1048576.0f;
            parameter.ranges.def = 0.0f;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";

        switch (index) {
        case kStateControls:
            state.key = kStateKeyControls;
            state.label = "Controls";
            break;
        case kStatePattern:
            state.key = kStateKeyPattern;
            state.label = "Pattern";
            break;
        case kStateVariation:
            state.key = kStateKeyVariation;
            state.label = "Variation";
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamRootNote: return static_cast<float>(controls_.rootNote);
        case kParamScale: return static_cast<float>(static_cast<int>(controls_.scale));
        case kParamGenre: return static_cast<float>(static_cast<int>(controls_.genre));
        case kParamStyleMode: return static_cast<float>(static_cast<int>(controls_.styleMode));
        case kParamChannel: return static_cast<float>(controls_.channel);
        case kParamLengthBeats: return static_cast<float>(controls_.lengthBeats);
        case kParamSubdivision: return static_cast<float>(static_cast<int>(controls_.subdivision));
        case kParamDensity: return controls_.density;
        case kParamRegister: return static_cast<float>(controls_.reg);
        case kParamHold: return controls_.hold;
        case kParamAccent: return controls_.accent;
        case kParamColor: return controls_.color;
        case kParamSeed: return static_cast<float>(controls_.seed);
        case kParamVary: return controls_.vary * 100.0f;
        case kParamFollowDodge: return controls_.followDodge * 100.0f;
        case kParamListenChannel: return static_cast<float>(controls_.listenChannel);
        case kParamListenNote: return static_cast<float>(controls_.listenNote);
        case kParamInputMatchMode: return static_cast<float>(static_cast<int>(controls_.inputMatchMode));
        case kParamInputSensitivity: return controls_.inputSensitivity * 100.0f;
        case kParamConductorChannel: return static_cast<float>(conductorChannel_);
        case kParamActionNew: return static_cast<float>(controls_.actionNew);
        case kParamActionNotes: return static_cast<float>(controls_.actionNotes);
        case kParamActionRhythm: return static_cast<float>(controls_.actionRhythm);
        default:
            return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case kParamRootNote: controls_.rootNote = static_cast<int>(value); break;
        case kParamScale: controls_.scale = static_cast<downspout::bassgen::ScaleId>(static_cast<int>(value)); break;
        case kParamGenre: controls_.genre = static_cast<downspout::bassgen::GenreId>(static_cast<int>(value)); break;
        case kParamStyleMode: controls_.styleMode = static_cast<downspout::bassgen::StyleModeId>(static_cast<int>(value)); break;
        case kParamChannel: controls_.channel = static_cast<int>(value); break;
        case kParamLengthBeats: controls_.lengthBeats = static_cast<int>(value); break;
        case kParamSubdivision: controls_.subdivision = static_cast<downspout::bassgen::SubdivisionId>(static_cast<int>(value)); break;
        case kParamDensity: controls_.density = value; break;
        case kParamRegister: controls_.reg = static_cast<int>(value); break;
        case kParamHold: controls_.hold = value; break;
        case kParamAccent: controls_.accent = value; break;
        case kParamColor: controls_.color = value; break;
        case kParamSeed: controls_.seed = static_cast<uint32_t>(value); break;
        case kParamVary: controls_.vary = value / 100.0f; break;
        case kParamFollowDodge: controls_.followDodge = value / 100.0f; break;
        case kParamListenChannel: controls_.listenChannel = static_cast<int>(value); break;
        case kParamListenNote: controls_.listenNote = static_cast<int>(value); break;
        case kParamInputMatchMode: controls_.inputMatchMode = static_cast<downspout::bassgen::InputMatchModeId>(static_cast<int>(value)); break;
        case kParamInputSensitivity: controls_.inputSensitivity = value / 100.0f; break;
        case kParamConductorChannel: conductorChannel_ = static_cast<int>(value); break;
        case kParamActionNew: controls_.actionNew = static_cast<int>(value); break;
        case kParamActionNotes: controls_.actionNotes = static_cast<int>(value); break;
        case kParamActionRhythm: controls_.actionRhythm = static_cast<int>(value); break;
        }

        controls_ = downspout::bassgen::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyControls) == 0) {
            return String(downspout::bassgen::serializeControls(controls_).c_str());
        }
        if (std::strcmp(key, kStateKeyPattern) == 0) {
            return String(downspout::bassgen::serializePatternState(engine_.pattern).c_str());
        }
        if (std::strcmp(key, kStateKeyVariation) == 0) {
            return String(downspout::bassgen::serializeVariationState(engine_.variation).c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";
        if (std::strcmp(key, kStateKeyControls) == 0) {
            const auto controls = downspout::bassgen::deserializeControls(text);
            if (controls.has_value()) {
                controls_ = *controls;
                engine_.controls = controls_;
                engine_.previousControls = controls_;
            }
            return;
        }
        if (std::strcmp(key, kStateKeyPattern) == 0) {
            const auto pattern = downspout::bassgen::deserializePatternState(text);
            if (pattern.has_value()) {
                engine_.pattern = *pattern;
                engine_.patternValid = true;
            }
            return;
        }
        if (std::strcmp(key, kStateKeyVariation) == 0) {
            const auto variation = downspout::bassgen::deserializeVariationState(text);
            if (variation.has_value()) {
                engine_.variation = *variation;
            }
        }
    }

    void activate() override
    {
        downspout::bassgen::activate(engine_, controls_);
    }

    void deactivate() override
    {
        downspout::bassgen::deactivate(engine_);
    }

    void run(const float**,
             float** outputs,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        if (conductorChannel_ > 0 && midiEvents != nullptr) {
            const int ch = conductorChannel_ - 1;
            for (uint32_t i = 0; i < midiEventCount; ++i) {
                const auto& ev = midiEvents[i];
                if (ev.size >= 3 && (ev.data[0] & 0xf0) == 0xb0 && (ev.data[0] & 0x0f) == ch) {
                    const uint8_t cc  = ev.data[1];
                    const uint8_t val = ev.data[2];
                    switch (cc) {
                    case 21: controls_.density = val / 127.0f; break;
                    case 22: controls_.accent  = val / 127.0f; break;
                    case 23: controls_.vary    = val / 127.0f; break;
                    case 24: if (val == 127) ++controls_.actionNew; break;
                    default: break;
                    }
                }
            }
            controls_ = downspout::bassgen::clampControls(controls_);
        }

        std::array<CoreInputMidiEvent, downspout::bassgen::kMaxInputMidiEvents> inputEvents {};
        const bool responseEnabled = std::fabs(controls_.followDodge) > 0.001f && controls_.inputSensitivity > 0.001f;
        const uint32_t inputCount = responseEnabled && midiEvents != nullptr
            ? std::min<uint32_t>(midiEventCount, static_cast<uint32_t>(inputEvents.size()))
            : 0u;
        for (uint32_t i = 0; i < inputCount; ++i) {
            inputEvents[i] = toCoreMidiEvent(midiEvents[i]);
        }

        const downspout::bassgen::BlockResult result =
            downspout::bassgen::processBlock(engine_,
                                             controls_,
                                             toCoreTransport(getTimePosition()),
                                             frames,
                                             getSampleRate(),
                                             inputEvents.data(),
                                             inputCount);

        for (int index = 0; index < result.eventCount; ++index) {
            const MidiEvent midiEvent = toDpfMidiEvent(result.events[index]);
            writeMidiEvent(midiEvent);
        }
    }

private:
    CoreControls controls_ {};
    CoreEngineState engine_ {};
    int conductorChannel_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassgenPlugin)
};

Plugin* createPlugin()
{
    return new BassgenPlugin();
}

END_NAMESPACE_DISTRHO
