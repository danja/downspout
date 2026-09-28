#include "DistrhoPlugin.hpp"

#include "helterskelter_core.hpp"

#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Stable parameter indices — never reorder.
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamSensitivity,
    kParamDepth,
    kParamResonance,
    kParamBaseFreq,
    kParamDivision,
    kParamGateBeats,
    kParamAttack,
    kParamDecay,
    kParamSustain,
    kParamRelease,
    kParamInvert,
    kParamMix,
    kParamTrim,
    kParamBypass,
    kParamCCSensitivity,
    kParamCCDepth,
    kParamCCResonance,
    kParamCCMix,
    kParamCCChannel,
    kParameterCount
};

constexpr const char* kStateMode          = "mode";
constexpr const char* kStateSensitivity   = "sensitivity";
constexpr const char* kStateDepth         = "depth";
constexpr const char* kStateResonance     = "resonance";
constexpr const char* kStateBaseFreq      = "base_freq";
constexpr const char* kStateDivision      = "division";
constexpr const char* kStateGateBeats     = "gate_beats";
constexpr const char* kStateAttack        = "attack";
constexpr const char* kStateDecay         = "decay";
constexpr const char* kStateSustain       = "sustain";
constexpr const char* kStateRelease       = "release";
constexpr const char* kStateInvert        = "invert";
constexpr const char* kStateMix           = "mix";
constexpr const char* kStateTrim          = "trim";
constexpr const char* kStateBypass        = "bypass";
constexpr const char* kStateCCSensitivity = "cc_sensitivity";
constexpr const char* kStateCCDepth       = "cc_depth";
constexpr const char* kStateCCResonance   = "cc_resonance";
constexpr const char* kStateCCMix         = "cc_mix";
constexpr const char* kStateCCChannel     = "cc_channel";

constexpr uint32_t kStateCount = 20;

float ccToUnit(uint8_t v) noexcept
{
    return static_cast<float>(v) / 127.0f;
}

float ccToResonance(uint8_t v) noexcept
{
    return 0.5f + static_cast<float>(v) / 127.0f * 11.5f;
}

float ccToMix(uint8_t v) noexcept
{
    return static_cast<float>(v) / 127.0f * 100.0f;
}

downspout::generative::Transport toCoreTransport(const TimePosition& x)
{
    downspout::generative::Transport t;
    t.valid = x.bbt.valid;
    t.playing = x.playing;
    if (t.valid) {
        t.bar = x.bbt.bar - 1;
        t.barBeat = x.bbt.beat - 1 + (x.bbt.ticksPerBeat > 0 ? x.bbt.tick / x.bbt.ticksPerBeat : 0);
        t.beatsPerBar = x.bbt.beatsPerBar;
        t.beatType = x.bbt.beatType;
        t.bpm = x.bbt.beatsPerMinute;
    }
    return t;
}

}  // namespace

class HelterSkelterPlugin : public Plugin
{
public:
    HelterSkelterPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        params_ = downspout::helterskelter::clampParameters(params_);
    }

protected:
    const char* getLabel()       const override { return "HelterSkelter"; }
    const char* getDescription() const override { return "Automatic wah pedal: resonant lowpass driven by the input envelope and/or a BBT-synced ADSR cycle. Modulated by Drift via CC."; }
    const char* getMaker()       const override { return "danja"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()     const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('H', 'l', 't', 'r'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input "  : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_"     : "out_")    + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = 0;
        switch (index) {
        case kParamMode:        parameter.name = "Mode";        parameter.symbol = "mode";        parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 2.0f, 0.0f }; break;
        case kParamSensitivity: parameter.name = "Sensitivity"; parameter.symbol = "sensitivity"; parameter.ranges = { 0.0f, 1.0f, 0.6f }; break;
        case kParamDepth:       parameter.name = "Depth";       parameter.symbol = "depth";       parameter.ranges = { 0.0f, 1.0f, 0.7f }; break;
        case kParamResonance:   parameter.name = "Resonance";   parameter.symbol = "resonance";   parameter.ranges = { 0.5f, 12.0f, 4.0f }; break;
        case kParamBaseFreq:    parameter.name = "Base Freq";   parameter.symbol = "base_freq";   parameter.ranges = { 100.0f, 2000.0f, 400.0f }; break;
        case kParamDivision:    parameter.name = "Division";    parameter.symbol = "division";    parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 3.0f, 2.0f }; break;
        case kParamGateBeats:   parameter.name = "Gate Beats";  parameter.symbol = "gate_beats";  parameter.ranges = { 0.5f, 8.0f, 2.0f }; break;
        case kParamAttack:      parameter.name = "Attack";      parameter.symbol = "attack";      parameter.ranges = { 1.0f, 500.0f, 20.0f }; break;
        case kParamDecay:       parameter.name = "Decay";       parameter.symbol = "decay";       parameter.ranges = { 5.0f, 1000.0f, 150.0f }; break;
        case kParamSustain:     parameter.name = "Sustain";     parameter.symbol = "sustain";     parameter.ranges = { 0.0f, 1.0f, 0.7f }; break;
        case kParamRelease:     parameter.name = "Release";     parameter.symbol = "release";     parameter.ranges = { 5.0f, 2000.0f, 200.0f }; break;
        case kParamInvert:      parameter.name = "Invert";      parameter.symbol = "invert";      parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 1.0f, 0.0f }; break;
        case kParamMix:         parameter.name = "Mix";         parameter.symbol = "mix";         parameter.ranges = { 0.0f, 100.0f, 100.0f }; break;
        case kParamTrim:        parameter.name = "Trim";        parameter.symbol = "trim";        parameter.ranges = { -12.0f, 12.0f, 0.0f }; break;
        case kParamBypass:      parameter.name = "Bypass";      parameter.symbol = "bypass";      parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 1.0f, 0.0f }; break;
        case kParamCCSensitivity: parameter.name = "CC Sens";   parameter.symbol = "cc_sensitivity"; parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::helterskelter::kDefaultCCSensitivity }; break;
        case kParamCCDepth:       parameter.name = "CC Depth";  parameter.symbol = "cc_depth";    parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::helterskelter::kDefaultCCDepth }; break;
        case kParamCCResonance:   parameter.name = "CC Res";    parameter.symbol = "cc_resonance"; parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::helterskelter::kDefaultCCResonance }; break;
        case kParamCCMix:         parameter.name = "CC Mix";    parameter.symbol = "cc_mix";      parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::helterskelter::kDefaultCCMix }; break;
        case kParamCCChannel:     parameter.name = "CC Channel"; parameter.symbol = "cc_channel"; parameter.hints = kParameterIsInteger; parameter.ranges = { 1.0f, 16.0f, 1.0f }; break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct Info { const char* key; const char* label; const char* def; };
        static constexpr Info kInfo[kStateCount] = {
            { kStateMode,          "Mode",           "0"    },
            { kStateSensitivity,   "Sensitivity",    "0.6"  },
            { kStateDepth,         "Depth",          "0.7"  },
            { kStateResonance,     "Resonance",      "4"    },
            { kStateBaseFreq,      "Base Freq",      "400"  },
            { kStateDivision,      "Division",       "2"    },
            { kStateGateBeats,     "Gate Beats",     "2"    },
            { kStateAttack,        "Attack",         "20"   },
            { kStateDecay,         "Decay",          "150"  },
            { kStateSustain,       "Sustain",        "0.7"  },
            { kStateRelease,       "Release",        "200"  },
            { kStateInvert,        "Invert",         "0"    },
            { kStateMix,           "Mix",            "100"  },
            { kStateTrim,          "Trim",           "0"    },
            { kStateBypass,        "Bypass",         "0"    },
            { kStateCCSensitivity, "CC Sens",        "1"    },
            { kStateCCDepth,       "CC Depth",       "2"    },
            { kStateCCResonance,   "CC Res",         "3"    },
            { kStateCCMix,         "CC Mix",         "4"    },
            { kStateCCChannel,     "CC Channel",     "1"    },
        };
        if (index < kStateCount) {
            state.key          = kInfo[index].key;
            state.label        = kInfo[index].label;
            state.hints        = kStateIsOnlyForDSP;
            state.defaultValue = kInfo[index].def;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamMode:        return params_.mode;
        case kParamSensitivity: return params_.sensitivity;
        case kParamDepth:       return params_.depth;
        case kParamResonance:   return params_.resonance;
        case kParamBaseFreq:    return params_.baseFreq;
        case kParamDivision:    return params_.division;
        case kParamGateBeats:   return params_.gateBeats;
        case kParamAttack:      return params_.attack;
        case kParamDecay:       return params_.decay;
        case kParamSustain:     return params_.sustain;
        case kParamRelease:     return params_.release;
        case kParamInvert:      return params_.invert;
        case kParamMix:         return params_.mix;
        case kParamTrim:        return params_.trim;
        case kParamBypass:      return params_.bypass;
        case kParamCCSensitivity: return params_.ccSensitivity;
        case kParamCCDepth:       return params_.ccDepth;
        case kParamCCResonance:   return params_.ccResonance;
        case kParamCCMix:         return params_.ccMix;
        case kParamCCChannel:     return params_.ccChannel;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t, float) override {}

    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        auto f = [&] { return static_cast<float>(std::atof(value)); };
        if      (std::strcmp(key, kStateMode)          == 0) { params_.mode = f(); }
        else if (std::strcmp(key, kStateSensitivity)   == 0) { params_.sensitivity = f(); }
        else if (std::strcmp(key, kStateDepth)         == 0) { params_.depth = f(); }
        else if (std::strcmp(key, kStateResonance)     == 0) { params_.resonance = f(); }
        else if (std::strcmp(key, kStateBaseFreq)      == 0) { params_.baseFreq = f(); }
        else if (std::strcmp(key, kStateDivision)      == 0) { params_.division = f(); }
        else if (std::strcmp(key, kStateGateBeats)     == 0) { params_.gateBeats = f(); }
        else if (std::strcmp(key, kStateAttack)        == 0) { params_.attack = f(); }
        else if (std::strcmp(key, kStateDecay)         == 0) { params_.decay = f(); }
        else if (std::strcmp(key, kStateSustain)       == 0) { params_.sustain = f(); }
        else if (std::strcmp(key, kStateRelease)       == 0) { params_.release = f(); }
        else if (std::strcmp(key, kStateInvert)        == 0) { params_.invert = f(); }
        else if (std::strcmp(key, kStateMix)           == 0) { params_.mix = f(); }
        else if (std::strcmp(key, kStateTrim)          == 0) { params_.trim = f(); }
        else if (std::strcmp(key, kStateBypass)        == 0) { params_.bypass = f(); }
        else if (std::strcmp(key, kStateCCSensitivity) == 0) { params_.ccSensitivity = f(); }
        else if (std::strcmp(key, kStateCCDepth)       == 0) { params_.ccDepth = f(); }
        else if (std::strcmp(key, kStateCCResonance)   == 0) { params_.ccResonance = f(); }
        else if (std::strcmp(key, kStateCCMix)         == 0) { params_.ccMix = f(); }
        else if (std::strcmp(key, kStateCCChannel)     == 0) { params_.ccChannel = f(); }
        params_ = downspout::helterskelter::clampParameters(params_);
    }

    void activate() override
    {
        engine_          = downspout::helterskelter::EngineState{};
        ccSensOverride_  = -1.0f;
        ccDepthOverride_ = -1.0f;
        ccResOverride_   = -1.0f;
        ccMixOverride_   = -1.0f;
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const int ccSensNum = static_cast<int>(params_.ccSensitivity);
        const int ccDepthNum = static_cast<int>(params_.ccDepth);
        const int ccResNum  = static_cast<int>(params_.ccResonance);
        const int ccMixNum  = static_cast<int>(params_.ccMix);
        const int ccCh      = static_cast<int>(params_.ccChannel);

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const auto& ev = midiEvents[i];
            if (ev.size < 3) continue;
            const uint8_t status = ev.data[0];
            if ((status & 0xF0) != 0xB0) continue;
            if ((status & 0x0F) + 1 != ccCh) continue;

            const uint8_t evCC  = ev.data[1];
            const uint8_t evVal = ev.data[2];

            if (ccSensNum > 0 && evCC == static_cast<uint8_t>(ccSensNum))
                ccSensOverride_  = ccToUnit(evVal);
            if (ccDepthNum > 0 && evCC == static_cast<uint8_t>(ccDepthNum))
                ccDepthOverride_ = ccToUnit(evVal);
            if (ccResNum  > 0 && evCC == static_cast<uint8_t>(ccResNum))
                ccResOverride_   = ccToResonance(evVal);
            if (ccMixNum   > 0 && evCC == static_cast<uint8_t>(ccMixNum))
                ccMixOverride_   = ccToMix(evVal);
        }

        if (ccSensNum == 0) ccSensOverride_  = -1.0f;
        if (ccDepthNum == 0) ccDepthOverride_ = -1.0f;
        if (ccResNum  == 0) ccResOverride_   = -1.0f;
        if (ccMixNum   == 0) ccMixOverride_   = -1.0f;

        downspout::helterskelter::processBlock(
            engine_, params_, toCoreTransport(getTimePosition()), frames, getSampleRate(),
            inputs, outputs,
            ccSensOverride_  >= 0.0f ? ccSensOverride_  : params_.sensitivity,
            ccDepthOverride_ >= 0.0f ? ccDepthOverride_ : params_.depth,
            ccResOverride_   >= 0.0f ? ccResOverride_   : params_.resonance,
            ccMixOverride_   >= 0.0f ? ccMixOverride_   : params_.mix);
    }

private:
    downspout::helterskelter::Parameters  params_ {};
    downspout::helterskelter::EngineState engine_ {};
    float ccSensOverride_  = -1.0f;
    float ccDepthOverride_ = -1.0f;
    float ccResOverride_   = -1.0f;
    float ccMixOverride_   = -1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelterSkelterPlugin)
};

Plugin* createPlugin()
{
    return new HelterSkelterPlugin();
}

END_NAMESPACE_DISTRHO
