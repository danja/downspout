#include "DistrhoPlugin.hpp"

#include "spliff_core.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Stable parameter indices — never reorder.
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamDepth,
    kParamSensitivity,
    kParamSharpness,
    kParamDecay,
    kParamDecayTilt,
    kParamSplitLow,
    kParamSplitHigh,
    kParamMix,
    kParamTrim,
    kParamBypass,
    kParamDelta,
    kParamCCDepth,
    kParamCCSensitivity,
    kParamCCDecay,
    kParamCCMix,
    kParamCCChannel,
    kParameterCount
};

constexpr const char* kStateMode          = "mode";
constexpr const char* kStateDepth         = "depth";
constexpr const char* kStateSensitivity   = "sensitivity";
constexpr const char* kStateSharpness     = "sharpness";
constexpr const char* kStateDecay         = "decay";
constexpr const char* kStateDecayTilt     = "decay_tilt";
constexpr const char* kStateSplitLow      = "split_low";
constexpr const char* kStateSplitHigh     = "split_high";
constexpr const char* kStateMix           = "mix";
constexpr const char* kStateTrim          = "trim";
constexpr const char* kStateBypass        = "bypass";
constexpr const char* kStateDelta         = "delta";
constexpr const char* kStateCCDepth       = "cc_depth";
constexpr const char* kStateCCSensitivity = "cc_sensitivity";
constexpr const char* kStateCCDecay       = "cc_decay";
constexpr const char* kStateCCMix         = "cc_mix";
constexpr const char* kStateCCChannel     = "cc_channel";

constexpr uint32_t kStateCount = 17;

float ccToUnit(uint8_t v) noexcept
{
    return static_cast<float>(v) / 127.0f;
}

float ccToMix(uint8_t v) noexcept
{
    return static_cast<float>(v) / 127.0f * 100.0f;
}

}  // namespace

class SpliffPlugin : public Plugin
{
public:
    SpliffPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        params_ = downspout::spliff::clampParameters(params_);
    }

protected:
    const char* getLabel()       const override { return "Spliff"; }
    const char* getDescription() const override { return "Adaptive transient processor in the Spiff manner: cuts or boosts only where transient energy lives, via a 3-band detector. Modulated by Drift via CC."; }
    const char* getMaker()       const override { return "danja"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()     const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'p', 'l', 'f'); }

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
        case kParamMode:        parameter.name = "Mode";        parameter.symbol = "mode";        parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 1.0f, 0.0f }; break;
        case kParamDepth:       parameter.name = "Depth";       parameter.symbol = "depth";       parameter.ranges = { 0.0f, 1.0f, 0.5f }; break;
        case kParamSensitivity: parameter.name = "Sensitivity"; parameter.symbol = "sensitivity"; parameter.ranges = { 0.0f, 1.0f, 0.5f }; break;
        case kParamSharpness:   parameter.name = "Sharpness";   parameter.symbol = "sharpness";   parameter.ranges = { 0.0f, 1.0f, 0.3f }; break;
        case kParamDecay:       parameter.name = "Decay";       parameter.symbol = "decay";       parameter.ranges = { 0.0f, 1.0f, 0.25f }; break;
        case kParamDecayTilt:   parameter.name = "Decay Tilt";  parameter.symbol = "decay_tilt";  parameter.ranges = { -1.0f, 1.0f, 0.0f }; break;
        case kParamSplitLow:    parameter.name = "Split Low";   parameter.symbol = "split_low";   parameter.ranges = { 20.0f, 2000.0f, 250.0f }; break;
        case kParamSplitHigh:   parameter.name = "Split High";  parameter.symbol = "split_high";  parameter.ranges = { 500.0f, 12000.0f, 4000.0f }; break;
        case kParamMix:         parameter.name = "Mix";         parameter.symbol = "mix";         parameter.ranges = { 0.0f, 100.0f, 100.0f }; break;
        case kParamTrim:        parameter.name = "Trim";        parameter.symbol = "trim";        parameter.ranges = { -12.0f, 12.0f, 0.0f }; break;
        case kParamBypass:      parameter.name = "Bypass";      parameter.symbol = "bypass";      parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 1.0f, 0.0f }; break;
        case kParamDelta:       parameter.name = "Delta";       parameter.symbol = "delta";       parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 1.0f, 0.0f }; break;
        case kParamCCDepth:       parameter.name = "CC Depth";  parameter.symbol = "cc_depth";    parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::spliff::kDefaultCCDepth }; break;
        case kParamCCSensitivity: parameter.name = "CC Sens";   parameter.symbol = "cc_sensitivity"; parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::spliff::kDefaultCCSensitivity }; break;
        case kParamCCDecay:       parameter.name = "CC Decay";  parameter.symbol = "cc_decay";    parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::spliff::kDefaultCCDecay }; break;
        case kParamCCMix:         parameter.name = "CC Mix";    parameter.symbol = "cc_mix";      parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::spliff::kDefaultCCMix }; break;
        case kParamCCChannel:     parameter.name = "CC Channel"; parameter.symbol = "cc_channel"; parameter.hints = kParameterIsInteger; parameter.ranges = { 1.0f, 16.0f, 1.0f }; break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct Info { const char* key; const char* label; const char* def; };
        static constexpr Info kInfo[kStateCount] = {
            { kStateMode,          "Mode",           "0"    },
            { kStateDepth,         "Depth",          "0.5"  },
            { kStateSensitivity,   "Sensitivity",    "0.5"  },
            { kStateSharpness,     "Sharpness",      "0.3"  },
            { kStateDecay,         "Decay",          "0.25" },
            { kStateDecayTilt,     "Decay Tilt",     "0"    },
            { kStateSplitLow,      "Split Low",      "250"  },
            { kStateSplitHigh,     "Split High",     "4000" },
            { kStateMix,           "Mix",            "100"  },
            { kStateTrim,          "Trim",           "0"    },
            { kStateBypass,        "Bypass",         "0"    },
            { kStateDelta,         "Delta",          "0"    },
            { kStateCCDepth,       "CC Depth",       "1"    },
            { kStateCCSensitivity, "CC Sens",        "2"    },
            { kStateCCDecay,       "CC Decay",       "3"    },
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
        case kParamDepth:       return params_.depth;
        case kParamSensitivity: return params_.sensitivity;
        case kParamSharpness:   return params_.sharpness;
        case kParamDecay:       return params_.decay;
        case kParamDecayTilt:   return params_.decayTilt;
        case kParamSplitLow:    return params_.splitLow;
        case kParamSplitHigh:   return params_.splitHigh;
        case kParamMix:         return params_.mix;
        case kParamTrim:        return params_.trim;
        case kParamBypass:      return params_.bypass;
        case kParamDelta:       return params_.delta;
        case kParamCCDepth:       return params_.ccDepth;
        case kParamCCSensitivity: return params_.ccSensitivity;
        case kParamCCDecay:       return params_.ccDecay;
        case kParamCCMix:         return params_.ccMix;
        case kParamCCChannel:     return params_.ccChannel;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t, float) override {}

    // The inverse of setState(). Without it (and DISTRHO_PLUGIN_WANT_FULL_STATE)
    // the host never asks the plugin for its values and saves the defaults from
    // initState() into every project.
    String getState(const char* key) const override
    {
        const auto text = [](const float value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
            return String(buffer);
        };
        if      (std::strcmp(key, kStateMode)          == 0) return text(params_.mode);
        else if (std::strcmp(key, kStateDepth)         == 0) return text(params_.depth);
        else if (std::strcmp(key, kStateSensitivity)   == 0) return text(params_.sensitivity);
        else if (std::strcmp(key, kStateSharpness)     == 0) return text(params_.sharpness);
        else if (std::strcmp(key, kStateDecay)         == 0) return text(params_.decay);
        else if (std::strcmp(key, kStateDecayTilt)     == 0) return text(params_.decayTilt);
        else if (std::strcmp(key, kStateSplitLow)      == 0) return text(params_.splitLow);
        else if (std::strcmp(key, kStateSplitHigh)     == 0) return text(params_.splitHigh);
        else if (std::strcmp(key, kStateMix)           == 0) return text(params_.mix);
        else if (std::strcmp(key, kStateTrim)          == 0) return text(params_.trim);
        else if (std::strcmp(key, kStateBypass)        == 0) return text(params_.bypass);
        else if (std::strcmp(key, kStateDelta)         == 0) return text(params_.delta);
        else if (std::strcmp(key, kStateCCDepth)       == 0) return text(params_.ccDepth);
        else if (std::strcmp(key, kStateCCSensitivity) == 0) return text(params_.ccSensitivity);
        else if (std::strcmp(key, kStateCCDecay)       == 0) return text(params_.ccDecay);
        else if (std::strcmp(key, kStateCCMix)         == 0) return text(params_.ccMix);
        else if (std::strcmp(key, kStateCCChannel)     == 0) return text(params_.ccChannel);
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        auto f = [&] { return static_cast<float>(std::atof(value)); };
        if      (std::strcmp(key, kStateMode)          == 0) { params_.mode = f(); }
        else if (std::strcmp(key, kStateDepth)         == 0) { params_.depth = f(); }
        else if (std::strcmp(key, kStateSensitivity)   == 0) { params_.sensitivity = f(); }
        else if (std::strcmp(key, kStateSharpness)     == 0) { params_.sharpness = f(); }
        else if (std::strcmp(key, kStateDecay)         == 0) { params_.decay = f(); }
        else if (std::strcmp(key, kStateDecayTilt)     == 0) { params_.decayTilt = f(); }
        else if (std::strcmp(key, kStateSplitLow)      == 0) { params_.splitLow = f(); }
        else if (std::strcmp(key, kStateSplitHigh)     == 0) { params_.splitHigh = f(); }
        else if (std::strcmp(key, kStateMix)           == 0) { params_.mix = f(); }
        else if (std::strcmp(key, kStateTrim)          == 0) { params_.trim = f(); }
        else if (std::strcmp(key, kStateBypass)        == 0) { params_.bypass = f(); }
        else if (std::strcmp(key, kStateDelta)         == 0) { params_.delta = f(); }
        else if (std::strcmp(key, kStateCCDepth)       == 0) { params_.ccDepth = f(); }
        else if (std::strcmp(key, kStateCCSensitivity) == 0) { params_.ccSensitivity = f(); }
        else if (std::strcmp(key, kStateCCDecay)       == 0) { params_.ccDecay = f(); }
        else if (std::strcmp(key, kStateCCMix)         == 0) { params_.ccMix = f(); }
        else if (std::strcmp(key, kStateCCChannel)     == 0) { params_.ccChannel = f(); }
        params_ = downspout::spliff::clampParameters(params_);
    }

    void activate() override
    {
        engine_            = downspout::spliff::EngineState{};
        ccDepthOverride_   = -1.0f;
        ccSensOverride_    = -1.0f;
        ccDecayOverride_   = -1.0f;
        ccMixOverride_     = -1.0f;
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const int ccDepthNum = static_cast<int>(params_.ccDepth);
        const int ccSensNum  = static_cast<int>(params_.ccSensitivity);
        const int ccDecayNum = static_cast<int>(params_.ccDecay);
        const int ccMixNum   = static_cast<int>(params_.ccMix);
        const int ccCh       = static_cast<int>(params_.ccChannel);

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const auto& ev = midiEvents[i];
            if (ev.size < 3) continue;
            const uint8_t status = ev.data[0];
            if ((status & 0xF0) != 0xB0) continue;
            if ((status & 0x0F) + 1 != ccCh) continue;

            const uint8_t evCC  = ev.data[1];
            const uint8_t evVal = ev.data[2];

            if (ccDepthNum > 0 && evCC == static_cast<uint8_t>(ccDepthNum))
                ccDepthOverride_ = ccToUnit(evVal);
            if (ccSensNum  > 0 && evCC == static_cast<uint8_t>(ccSensNum))
                ccSensOverride_  = ccToUnit(evVal);
            if (ccDecayNum > 0 && evCC == static_cast<uint8_t>(ccDecayNum))
                ccDecayOverride_ = ccToUnit(evVal);
            if (ccMixNum   > 0 && evCC == static_cast<uint8_t>(ccMixNum))
                ccMixOverride_   = ccToMix(evVal);
        }

        if (ccDepthNum == 0) ccDepthOverride_ = -1.0f;
        if (ccSensNum  == 0) ccSensOverride_  = -1.0f;
        if (ccDecayNum == 0) ccDecayOverride_ = -1.0f;
        if (ccMixNum   == 0) ccMixOverride_   = -1.0f;

        downspout::spliff::processBlock(
            engine_, params_, frames, getSampleRate(), inputs, outputs,
            ccDepthOverride_ >= 0.0f ? ccDepthOverride_ : params_.depth,
            ccSensOverride_  >= 0.0f ? ccSensOverride_  : params_.sensitivity,
            ccDecayOverride_ >= 0.0f ? ccDecayOverride_ : params_.decay,
            ccMixOverride_   >= 0.0f ? ccMixOverride_   : params_.mix);
    }

private:
    downspout::spliff::Parameters  params_ {};
    downspout::spliff::EngineState engine_ {};
    float ccDepthOverride_ = -1.0f;
    float ccSensOverride_  = -1.0f;
    float ccDecayOverride_ = -1.0f;
    float ccMixOverride_   = -1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpliffPlugin)
};

Plugin* createPlugin()
{
    return new SpliffPlugin();
}

END_NAMESPACE_DISTRHO
