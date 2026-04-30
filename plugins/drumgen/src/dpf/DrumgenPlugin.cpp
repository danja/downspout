#include "DistrhoPlugin.hpp"

#include "drumgen_engine.hpp"
#include "drumgen_pattern.hpp"
#include "drumgen_serialization.hpp"

#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamGenre = 0,
    kParamChannel,
    kParamKitMap,
    kParamBars,
    kParamResolution,
    kParamDensity,
    kParamVariation,
    kParamFillAmount,
    kParamSeed,
    kParamKickAmount,
    kParamBackbeatAmount,
    kParamHatAmount,
    kParamAuxAmount,
    kParamActionNew,
    kParamActionMutate,
    kParamActionFill,
    kParamTomAmount,
    kParamMetalAmount,
    kParamVary,
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

using CoreControls = downspout::drumgen::Controls;
using CoreEngineState = downspout::drumgen::EngineState;
using CoreTransport = downspout::drumgen::TransportSnapshot;
using CoreMidiEvent = downspout::drumgen::ScheduledMidiEvent;
using CoreMidiEventType = downspout::drumgen::MidiEventType;

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

MidiEvent toDpfMidiEvent(const CoreMidiEvent& event)
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

}  // namespace

class DrumgenPlugin : public Plugin
{
public:
    DrumgenPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        controls_ = downspout::drumgen::clampControls(controls_);
        engine_.controls = controls_;
        engine_.previousControls = controls_;
    }

protected:
    const char* getLabel() const override
    {
        return "DrumGen";
    }

    const char* getDescription() const override
    {
        return "Transport-synced polyphonic MIDI drum generator ported from flues LV2 into downspout/DPF.";
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
        return d_cconst('D', 'r', 'G', 'n');
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

        switch (index)
        {
        case kParamGenre:
            parameter.name = "Genre";
            parameter.symbol = "genre";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(downspout::drumgen::GenreId::count) - 1);
            parameter.ranges.def = 0.0f;
            break;
        case kParamChannel:
            parameter.name = "Channel";
            parameter.symbol = "channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 10.0f;
            break;
        case kParamKitMap:
            parameter.name = "Kit Map";
            parameter.symbol = "kit_map";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(downspout::drumgen::KitMapId::count) - 1);
            parameter.ranges.def = 0.0f;
            break;
        case kParamBars:
            parameter.name = "Bars";
            parameter.symbol = "bars";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = static_cast<float>(downspout::drumgen::kMinBars);
            parameter.ranges.max = static_cast<float>(downspout::drumgen::kMaxBars);
            parameter.ranges.def = 2.0f;
            break;
        case kParamResolution:
            parameter.name = "Resolution";
            parameter.symbol = "resolution";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(static_cast<int>(downspout::drumgen::ResolutionId::count) - 1);
            parameter.ranges.def = 1.0f;
            break;
        case kParamDensity:
            parameter.name = "Density";
            parameter.symbol = "density";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.58f;
            break;
        case kParamVariation:
            parameter.name = "Variation";
            parameter.symbol = "variation";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.35f;
            break;
        case kParamFillAmount:
            parameter.name = "Fill Amount";
            parameter.symbol = "fill_amount";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.30f;
            break;
        case kParamSeed:
            parameter.name = "Seed";
            parameter.symbol = "seed";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 65535.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamKickAmount:
            parameter.name = "Kick Amount";
            parameter.symbol = "kick_amt";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.78f;
            break;
        case kParamBackbeatAmount:
            parameter.name = "Backbeat Amount";
            parameter.symbol = "backbeat_amt";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.76f;
            break;
        case kParamHatAmount:
            parameter.name = "Hat Amount";
            parameter.symbol = "hat_amt";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.82f;
            break;
        case kParamAuxAmount:
            parameter.name = "Perc Amount";
            parameter.symbol = "aux_amt";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.28f;
            break;
        case kParamActionNew:
            parameter.name = "New";
            parameter.symbol = "new";
            parameter.hints = kParameterIsAutomatable|kParameterIsBoolean|kParameterIsInteger|kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionMutate:
            parameter.name = "Mutate";
            parameter.symbol = "mutate";
            parameter.hints = kParameterIsAutomatable|kParameterIsBoolean|kParameterIsInteger|kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamActionFill:
            parameter.name = "Fill";
            parameter.symbol = "fill";
            parameter.hints = kParameterIsAutomatable|kParameterIsBoolean|kParameterIsInteger|kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamTomAmount:
            parameter.name = "Tom Amount";
            parameter.symbol = "tom_amt";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.30f;
            break;
        case kParamMetalAmount:
            parameter.name = "Metal Amount";
            parameter.symbol = "metal_amt";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.26f;
            break;
        case kParamVary:
            parameter.name = "Vary";
            parameter.symbol = "vary";
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
        switch (index)
        {
        case kParamGenre: return static_cast<float>(static_cast<int>(controls_.genre));
        case kParamChannel: return static_cast<float>(controls_.channel);
        case kParamKitMap: return static_cast<float>(static_cast<int>(controls_.kitMap));
        case kParamBars: return static_cast<float>(controls_.bars);
        case kParamResolution: return static_cast<float>(static_cast<int>(controls_.resolution));
        case kParamDensity: return controls_.density;
        case kParamVariation: return controls_.variation;
        case kParamFillAmount: return controls_.fill;
        case kParamSeed: return static_cast<float>(controls_.seed);
        case kParamKickAmount: return controls_.kickAmt;
        case kParamBackbeatAmount: return controls_.backbeatAmt;
        case kParamHatAmount: return controls_.hatAmt;
        case kParamAuxAmount: return controls_.auxAmt;
        case kParamTomAmount: return controls_.tomAmt;
        case kParamMetalAmount: return controls_.metalAmt;
        case kParamVary: return controls_.vary * 100.0f;
        case kParamActionNew:
        case kParamActionMutate:
        case kParamActionFill:
            return 0.0f;
        default:
            return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamGenre: controls_.genre = static_cast<downspout::drumgen::GenreId>(static_cast<int>(value)); break;
        case kParamChannel: controls_.channel = static_cast<int>(value); break;
        case kParamKitMap: controls_.kitMap = static_cast<downspout::drumgen::KitMapId>(static_cast<int>(value)); break;
        case kParamBars: controls_.bars = static_cast<int>(value); break;
        case kParamResolution: controls_.resolution = static_cast<downspout::drumgen::ResolutionId>(static_cast<int>(value)); break;
        case kParamDensity: controls_.density = value; break;
        case kParamVariation: controls_.variation = value; break;
        case kParamFillAmount: controls_.fill = value; break;
        case kParamSeed: controls_.seed = static_cast<std::uint32_t>(value); break;
        case kParamKickAmount: controls_.kickAmt = value; break;
        case kParamBackbeatAmount: controls_.backbeatAmt = value; break;
        case kParamHatAmount: controls_.hatAmt = value; break;
        case kParamAuxAmount: controls_.auxAmt = value; break;
        case kParamTomAmount: controls_.tomAmt = value; break;
        case kParamMetalAmount: controls_.metalAmt = value; break;
        case kParamVary: controls_.vary = value / 100.0f; break;
        case kParamActionNew: if (value > 0.5f) ++controls_.actionNew; break;
        case kParamActionMutate: if (value > 0.5f) ++controls_.actionMutate; break;
        case kParamActionFill: if (value > 0.5f) ++controls_.actionFill; break;
        }

        controls_ = downspout::drumgen::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyControls) == 0)
            return String(downspout::drumgen::serializeControls(controls_).c_str());
        if (std::strcmp(key, kStateKeyPattern) == 0)
            return String(downspout::drumgen::serializePatternState(engine_.pattern).c_str());
        if (std::strcmp(key, kStateKeyVariation) == 0)
            return String(downspout::drumgen::serializeVariationState(engine_.variation).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";

        if (std::strcmp(key, kStateKeyControls) == 0)
        {
            const auto controls = downspout::drumgen::deserializeControls(text);
            if (controls.has_value())
            {
                controls_ = *controls;
                engine_.controls = controls_;
                engine_.previousControls = controls_;
            }
            return;
        }

        if (std::strcmp(key, kStateKeyPattern) == 0)
        {
            const auto pattern = downspout::drumgen::deserializePatternState(text);
            if (pattern.has_value())
            {
                engine_.pattern = *pattern;
                engine_.patternValid = true;
            }
            return;
        }

        if (std::strcmp(key, kStateKeyVariation) == 0)
        {
            const auto variation = downspout::drumgen::deserializeVariationState(text);
            if (variation.has_value())
                engine_.variation = *variation;
        }
    }

    void activate() override
    {
        downspout::drumgen::activate(engine_, controls_);
    }

    void deactivate() override
    {
        downspout::drumgen::deactivate(engine_);
    }

    void run(const float**, float**, uint32_t frames) override
    {
        const downspout::drumgen::BlockResult result =
            downspout::drumgen::processBlock(engine_,
                                             controls_,
                                             toCoreTransport(getTimePosition()),
                                             frames,
                                             getSampleRate());

        for (int index = 0; index < result.eventCount; ++index)
        {
            const MidiEvent midiEvent = toDpfMidiEvent(result.events[index]);
            writeMidiEvent(midiEvent);
        }
    }

private:
    CoreControls controls_ {};
    CoreEngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumgenPlugin)
};

Plugin* createPlugin()
{
    return new DrumgenPlugin();
}

END_NAMESPACE_DISTRHO
