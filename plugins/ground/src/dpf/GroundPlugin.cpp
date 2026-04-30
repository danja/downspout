#include "DistrhoPlugin.hpp"

#include "ground_engine.hpp"
#include "ground_params.hpp"
#include "ground_serialization.hpp"

#include <algorithm>
#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

using downspout::ground::BlockResult;
using downspout::ground::Controls;
using downspout::ground::EngineState;
using downspout::ground::MidiEventType;
using downspout::ground::PhraseRoleId;
using downspout::ground::ScaleId;
using downspout::ground::ScheduledMidiEvent;
using downspout::ground::StyleId;
using downspout::ground::TransportSnapshot;
using downspout::ground::kParameterCount;
using downspout::ground::kParamActionMutateCell;
using downspout::ground::kParamActionNewForm;
using downspout::ground::kParamActionNewPhrase;
using downspout::ground::kParamCadence;
using downspout::ground::kParamChannel;
using downspout::ground::kParamDensity;
using downspout::ground::kParamFormBars;
using downspout::ground::kParamMotion;
using downspout::ground::kParamPhraseBars;
using downspout::ground::kParamRegister;
using downspout::ground::kParamRegisterArc;
using downspout::ground::kParamRootNote;
using downspout::ground::kParamScale;
using downspout::ground::kParamSeed;
using downspout::ground::kParamSequence;
using downspout::ground::kParamStatusPhrase;
using downspout::ground::kParamStatusRole;
using downspout::ground::kParamStyle;
using downspout::ground::kParamTension;
using downspout::ground::kParamVary;
using downspout::ground::kStateCount;
using downspout::ground::kStateKeyControls;
using downspout::ground::kStateKeyForm;
using downspout::ground::kStateKeyVariation;

ParameterEnumerationValue kScaleEnumValues[] = {
    {0.0f, "Minor"},
    {1.0f, "Major"},
    {2.0f, "Dorian"},
    {3.0f, "Phrygian"},
    {4.0f, "Pent Minor"},
    {5.0f, "Blues"},
    {6.0f, "Mixolydian"},
    {7.0f, "Harm Minor"},
    {8.0f, "Pent Major"},
    {9.0f, "Locrian"},
    {10.0f, "Phryg Dom"},
};

ParameterEnumerationValue kStyleEnumValues[] = {
    {0.0f, "Grounded"},
    {1.0f, "Ostinato"},
    {2.0f, "March"},
    {3.0f, "Pulse"},
    {4.0f, "Drone"},
    {5.0f, "Climb"},
};

ParameterEnumerationValue kFormEnumValues[] = {
    {8.0f, "8"},
    {16.0f, "16"},
    {32.0f, "32"},
    {64.0f, "64"},
};

ParameterEnumerationValue kPhraseEnumValues[] = {
    {2.0f, "2"},
    {4.0f, "4"},
    {8.0f, "8"},
};

ParameterEnumerationValue kRegisterEnumValues[] = {
    {0.0f, "Sub"},
    {1.0f, "Low"},
    {2.0f, "Mid"},
    {3.0f, "High"},
};

ParameterEnumerationValue kRoleEnumValues[] = {
    {0.0f, "Statement"},
    {1.0f, "Answer"},
    {2.0f, "Climb"},
    {3.0f, "Pedal"},
    {4.0f, "Breakdown"},
    {5.0f, "Cadence"},
    {6.0f, "Release"},
};

TransportSnapshot toCoreTransport(const TimePosition& timePos)
{
    TransportSnapshot transport;
    transport.playing = timePos.playing;
    transport.valid = timePos.bbt.valid;

    if (timePos.bbt.valid) {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1) +
                            (timePos.bbt.ticksPerBeat > 0.0
                                 ? (timePos.bbt.tick / timePos.bbt.ticksPerBeat)
                                 : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.bpm = timePos.bbt.beatsPerMinute;
    }

    return transport;
}

MidiEvent toDpfMidiEvent(const ScheduledMidiEvent& event)
{
    MidiEvent midiEvent {};
    midiEvent.frame = event.frame;
    midiEvent.size = 3;
    midiEvent.data[0] = static_cast<uint8_t>((event.type == MidiEventType::noteOn ? 0x90 : 0x80) |
                                             (event.channel & 0x0f));
    midiEvent.data[1] = event.data1;
    midiEvent.data[2] = event.data2;
    midiEvent.dataExt = nullptr;
    return midiEvent;
}

}  // namespace

class GroundPlugin : public Plugin
{
public:
    GroundPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        controls_ = downspout::ground::clampControls(controls_);
        engine_.controls = controls_;
        engine_.previousControls = controls_;
        syncStatusFromEngine();
    }

protected:
    const char* getLabel() const override
    {
        return "Ground";
    }

    const char* getDescription() const override
    {
        return "Long-form transport-synced MIDI bass generator that thinks in sections and cadences.";
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
        return d_cconst('G', 'r', 'n', 'd');
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
            parameter.ranges.max = static_cast<float>(static_cast<int>(ScaleId::count) - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kScaleEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kScaleEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamStyle:
            parameter.name = "Style";
            parameter.symbol = "style";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(StyleId::count) - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kStyleEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kStyleEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamChannel:
            parameter.name = "Channel";
            parameter.symbol = "channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamFormBars:
            parameter.name = "Form Bars";
            parameter.symbol = "form_bars";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 8.0f;
            parameter.ranges.max = 64.0f;
            parameter.ranges.def = 16.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kFormEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kFormEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamPhraseBars:
            parameter.name = "Phrase Bars";
            parameter.symbol = "phrase_bars";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 2.0f;
            parameter.ranges.max = 8.0f;
            parameter.ranges.def = 4.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kPhraseEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kPhraseEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamDensity:
            parameter.name = "Density";
            parameter.symbol = "density";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.45f;
            break;
        case kParamMotion:
            parameter.name = "Motion";
            parameter.symbol = "motion";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.55f;
            break;
        case kParamTension:
            parameter.name = "Tension";
            parameter.symbol = "tension";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.45f;
            break;
        case kParamCadence:
            parameter.name = "Cadence";
            parameter.symbol = "cadence";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.50f;
            break;
        case kParamRegister:
            parameter.name = "Register";
            parameter.symbol = "register";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 3.0f;
            parameter.ranges.def = 1.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kRegisterEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kRegisterEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamRegisterArc:
            parameter.name = "Register Arc";
            parameter.symbol = "register_arc";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.40f;
            break;
        case kParamSequence:
            parameter.name = "Sequence";
            parameter.symbol = "sequence";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.35f;
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
        case kParamActionNewForm:
            parameter.name = "New Form";
            parameter.symbol = "new_form";
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionNewPhrase:
            parameter.name = "New Phrase";
            parameter.symbol = "new_phrase";
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionMutateCell:
            parameter.name = "Mutate Cell";
            parameter.symbol = "mutate_cell";
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusPhrase:
            parameter.name = "Current Phrase";
            parameter.symbol = "status_phrase";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 32.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamStatusRole:
            parameter.name = "Current Role";
            parameter.symbol = "status_role";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(PhraseRoleId::count) - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kRoleEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kRoleEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";

        switch (index) {
        case downspout::ground::kStateControls:
            state.key = kStateKeyControls;
            state.label = "Controls";
            break;
        case downspout::ground::kStateForm:
            state.key = kStateKeyForm;
            state.label = "Form";
            break;
        case downspout::ground::kStateVariation:
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
        case kParamStyle: return static_cast<float>(static_cast<int>(controls_.style));
        case kParamChannel: return static_cast<float>(controls_.channel);
        case kParamFormBars: return static_cast<float>(controls_.formBars);
        case kParamPhraseBars: return static_cast<float>(controls_.phraseBars);
        case kParamDensity: return controls_.density;
        case kParamMotion: return controls_.motion;
        case kParamTension: return controls_.tension;
        case kParamCadence: return controls_.cadence;
        case kParamRegister: return static_cast<float>(controls_.reg);
        case kParamRegisterArc: return controls_.registerArc;
        case kParamSequence: return controls_.sequence;
        case kParamSeed: return static_cast<float>(controls_.seed);
        case kParamVary: return controls_.vary * 100.0f;
        case kParamActionNewForm:
        case kParamActionNewPhrase:
        case kParamActionMutateCell:
            return 0.0f;
        case kParamStatusPhrase:
            return static_cast<float>(statusPhrase_);
        case kParamStatusRole:
            return static_cast<float>(statusRole_);
        default:
            return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case kParamRootNote: controls_.rootNote = static_cast<int>(value); break;
        case kParamScale: controls_.scale = static_cast<ScaleId>(static_cast<int>(value)); break;
        case kParamStyle: controls_.style = static_cast<StyleId>(static_cast<int>(value)); break;
        case kParamChannel: controls_.channel = static_cast<int>(value); break;
        case kParamFormBars: controls_.formBars = static_cast<int>(value); break;
        case kParamPhraseBars: controls_.phraseBars = static_cast<int>(value); break;
        case kParamDensity: controls_.density = value; break;
        case kParamMotion: controls_.motion = value; break;
        case kParamTension: controls_.tension = value; break;
        case kParamCadence: controls_.cadence = value; break;
        case kParamRegister: controls_.reg = static_cast<int>(value); break;
        case kParamRegisterArc: controls_.registerArc = value; break;
        case kParamSequence: controls_.sequence = value; break;
        case kParamSeed: controls_.seed = static_cast<uint32_t>(value); break;
        case kParamVary: controls_.vary = value / 100.0f; break;
        case kParamActionNewForm: if (value > 0.5f) ++controls_.actionNewForm; break;
        case kParamActionNewPhrase: if (value > 0.5f) ++controls_.actionNewPhrase; break;
        case kParamActionMutateCell: if (value > 0.5f) ++controls_.actionMutateCell; break;
        case kParamStatusPhrase:
        case kParamStatusRole:
            break;
        }

        controls_ = downspout::ground::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyControls) == 0) {
            return String(downspout::ground::serializeControls(controls_).c_str());
        }
        if (std::strcmp(key, kStateKeyForm) == 0) {
            return String(downspout::ground::serializeFormState(engine_.form).c_str());
        }
        if (std::strcmp(key, kStateKeyVariation) == 0) {
            return String(downspout::ground::serializeVariationState(engine_.variation).c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";

        if (std::strcmp(key, kStateKeyControls) == 0) {
            const auto controls = downspout::ground::deserializeControls(text);
            if (controls.has_value()) {
                controls_ = *controls;
                engine_.controls = controls_;
                engine_.previousControls = controls_;
            }
            syncStatusFromEngine();
            return;
        }

        if (std::strcmp(key, kStateKeyForm) == 0) {
            const auto form = downspout::ground::deserializeFormState(text);
            if (form.has_value()) {
                engine_.form = *form;
                engine_.formValid = true;
                engine_.currentPhraseIndex = 0;
                engine_.currentRole = engine_.form.phraseCount > 0
                    ? engine_.form.phrases[0].role
                    : PhraseRoleId::statement;
            }
            syncStatusFromEngine();
            return;
        }

        if (std::strcmp(key, kStateKeyVariation) == 0) {
            const auto variation = downspout::ground::deserializeVariationState(text);
            if (variation.has_value()) {
                engine_.variation = *variation;
            }
        }
    }

    void activate() override
    {
        downspout::ground::activate(engine_, controls_);
        syncStatusFromEngine();
    }

    void deactivate() override
    {
        downspout::ground::deactivate(engine_);
        syncStatusFromEngine();
    }

    void run(const float**, float**, uint32_t frames) override
    {
        const BlockResult result = downspout::ground::processBlock(engine_,
                                                                   controls_,
                                                                   toCoreTransport(getTimePosition()),
                                                                   frames,
                                                                   getSampleRate());

        for (int i = 0; i < result.eventCount; ++i) {
            writeMidiEvent(toDpfMidiEvent(result.events[static_cast<std::size_t>(i)]));
        }

        statusPhrase_ = std::max(0, result.currentPhraseIndex + 1);
        statusRole_ = static_cast<int>(result.currentRole);
    }

private:
    void syncStatusFromEngine()
    {
        statusPhrase_ = std::max(0, engine_.currentPhraseIndex + 1);
        statusRole_ = static_cast<int>(engine_.currentRole);
    }

    Controls controls_ {};
    EngineState engine_ {};
    int statusPhrase_ = 1;
    int statusRole_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroundPlugin)
};

Plugin* createPlugin()
{
    return new GroundPlugin();
}

END_NAMESPACE_DISTRHO
