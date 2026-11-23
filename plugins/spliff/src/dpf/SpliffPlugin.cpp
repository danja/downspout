#include "DistrhoPlugin.hpp"

#include "spliff_core.hpp"
#include "spliff_spectrum.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::spliff;

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
    kParamAttackDb,
    kParamSustainDb,
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
constexpr const char* kStateAttackDb      = "attack_db";
constexpr const char* kStateSustainDb     = "sustain_db";
constexpr const char* kStateAnalyzerSource = "analyser_source";

constexpr uint32_t kStateCount = 20;

// The original 19 control parameters keep their indices. Spectrum bins are read-only outputs; the analyser source is
// UI state so switching it never becomes an automation event.
inline constexpr uint32_t kSpectrumFirst = kParameterCount;
inline constexpr uint32_t kTotalParameterCount = kSpectrumFirst + core::kSpectrumBins;

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
        : Plugin(kTotalParameterCount, 0, kStateCount)
    {
        params_ = downspout::spliff::clampParameters(params_);
        core::resetSpectrum(analyser_);
        publishSpectrum();
    }

protected:
    const char* getLabel()       const override { return "Spliff"; }
    const char* getDescription() const override { return "Adaptive transient processor: 3-band Spiff-style Cut/Boost plus a threshold-free level-independent Attack/Sustain designer. Modulated by Drift via CC."; }
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
        // Controls must be automatable: without this flag VST3 hosts (notably REAPER) treat them as
        // read-only, ignore writes and report zero instead of the declared default.
        parameter.hints = kParameterIsAutomatable;
        // ParameterRanges is { def, min, max } — set the fields explicitly so a future edit cannot
        // silently invert a range (an inverted range makes VST3 hosts report every control as 0).
        switch (index) {
        case kParamMode:        parameter.name = "Mode";        parameter.symbol = "mode";        parameter.hints |= kParameterIsInteger; parameter.ranges = { 0.0f, 0.0f, 2.0f }; break;
        case kParamDepth:       parameter.name = "Depth";       parameter.symbol = "depth";       parameter.ranges = { 0.5f, 0.0f, 1.0f }; break;
        case kParamSensitivity: parameter.name = "Sensitivity"; parameter.symbol = "sensitivity"; parameter.ranges = { 0.5f, 0.0f, 1.0f }; break;
        case kParamSharpness:   parameter.name = "Sharpness";   parameter.symbol = "sharpness";   parameter.ranges = { 0.7f, 0.0f, 1.0f }; break;
        case kParamDecay:       parameter.name = "Decay";       parameter.symbol = "decay";       parameter.ranges = { 0.25f, 0.0f, 1.0f }; break;
        case kParamDecayTilt:   parameter.name = "Decay Tilt";  parameter.symbol = "decay_tilt";  parameter.ranges = { 0.0f, -1.0f, 1.0f }; break;
        case kParamSplitLow:    parameter.name = "Split Low";   parameter.symbol = "split_low";   parameter.ranges = { 250.0f, 20.0f, 2000.0f }; break;
        case kParamSplitHigh:   parameter.name = "Split High";  parameter.symbol = "split_high";  parameter.ranges = { 4000.0f, 500.0f, 12000.0f }; break;
        case kParamMix:         parameter.name = "Mix";         parameter.symbol = "mix";         parameter.ranges = { 100.0f, 0.0f, 100.0f }; break;
        case kParamTrim:        parameter.name = "Trim";        parameter.symbol = "trim";        parameter.ranges = { 0.0f, -12.0f, 12.0f }; break;
        case kParamBypass:      parameter.name = "Bypass";      parameter.symbol = "bypass";      parameter.hints |= kParameterIsInteger; parameter.ranges = { 0.0f, 0.0f, 1.0f }; break;
        case kParamDelta:       parameter.name = "Delta";       parameter.symbol = "delta";       parameter.hints |= kParameterIsInteger; parameter.ranges = { 0.0f, 0.0f, 1.0f }; break;
        case kParamCCDepth:       parameter.name = "CC Depth";  parameter.symbol = "cc_depth";    parameter.hints |= kParameterIsInteger; parameter.ranges = { downspout::spliff::kDefaultCCDepth, 0.0f, 127.0f }; break;
        case kParamCCSensitivity: parameter.name = "CC Sens";   parameter.symbol = "cc_sensitivity"; parameter.hints |= kParameterIsInteger; parameter.ranges = { downspout::spliff::kDefaultCCSensitivity, 0.0f, 127.0f }; break;
        case kParamCCDecay:       parameter.name = "CC Decay";  parameter.symbol = "cc_decay";    parameter.hints |= kParameterIsInteger; parameter.ranges = { downspout::spliff::kDefaultCCDecay, 0.0f, 127.0f }; break;
        case kParamCCMix:         parameter.name = "CC Mix";    parameter.symbol = "cc_mix";      parameter.hints |= kParameterIsInteger; parameter.ranges = { downspout::spliff::kDefaultCCMix, 0.0f, 127.0f }; break;
        case kParamCCChannel:     parameter.name = "CC Channel"; parameter.symbol = "cc_channel"; parameter.hints |= kParameterIsInteger; parameter.ranges = { 1.0f, 1.0f, 16.0f }; break;
        case kParamAttackDb:      parameter.name = "Attack";     parameter.symbol = "attack_db";  parameter.ranges = { 0.0f, -15.0f, 15.0f }; break;
        case kParamSustainDb:     parameter.name = "Sustain";    parameter.symbol = "sustain_db"; parameter.ranges = { 0.0f, -24.0f, 24.0f }; break;
        default:
            if (index >= kSpectrumFirst && index < kTotalParameterCount) {
                char symbol[24], name[32];
                const int band = static_cast<int>(index - kSpectrumFirst) + 1;
                std::snprintf(symbol, sizeof(symbol), "spectrum_%02d", band);
                std::snprintf(name, sizeof(name), "Spectrum Band %02d", band);
                parameter.name = name;
                parameter.symbol = symbol;
                parameter.unit = "dB";
                parameter.hints = kParameterIsOutput;
                parameter.ranges = { core::kSpectrumFloorDb, core::kSpectrumFloorDb, 6.0f };
            }
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct Info { const char* key; const char* label; const char* def; };
        static constexpr Info kInfo[kStateCount] = {
            { kStateMode,          "Mode",           "0"    },
            { kStateDepth,         "Depth",          "0.5"  },
            { kStateSensitivity,   "Sensitivity",    "0.5"  },
            { kStateSharpness,     "Sharpness",      "0.7"  },
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
            { kStateAttackDb,      "Attack",         "0"    },
            { kStateSustainDb,     "Sustain",        "0"    },
            { kStateAnalyzerSource, "Analyser source", "1"   },
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
        if (index >= kSpectrumFirst && index < kTotalParameterCount)
            return spectrum_[index - kSpectrumFirst].load(std::memory_order_relaxed);
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
        case kParamAttackDb:      return params_.attackDb;
        case kParamSustainDb:     return params_.sustainDb;
        default: return 0.0f;
        }
    }

    // Host automation / UI edits must actually reach the DSP. Spectrum indices are read-only outputs.
    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParameterCount || !std::isfinite(value))
            return;
        switch (index) {
        case kParamMode:          params_.mode          = value; break;
        case kParamDepth:         params_.depth         = value; break;
        case kParamSensitivity:   params_.sensitivity   = value; break;
        case kParamSharpness:     params_.sharpness     = value; break;
        case kParamDecay:         params_.decay         = value; break;
        case kParamDecayTilt:     params_.decayTilt     = value; break;
        case kParamSplitLow:      params_.splitLow      = value; break;
        case kParamSplitHigh:     params_.splitHigh     = value; break;
        case kParamMix:           params_.mix           = value; break;
        case kParamTrim:          params_.trim          = value; break;
        case kParamBypass:        params_.bypass        = value; break;
        case kParamDelta:         params_.delta         = value; break;
        case kParamCCDepth:       params_.ccDepth       = value; break;
        case kParamCCSensitivity: params_.ccSensitivity = value; break;
        case kParamCCDecay:       params_.ccDecay       = value; break;
        case kParamCCMix:         params_.ccMix         = value; break;
        case kParamCCChannel:     params_.ccChannel     = value; break;
        case kParamAttackDb:      params_.attackDb      = value; break;
        case kParamSustainDb:     params_.sustainDb     = value; break;
        default: return;
        }
        params_ = downspout::spliff::clampParameters(params_);
    }

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
        else if (std::strcmp(key, kStateAttackDb)      == 0) return text(params_.attackDb);
        else if (std::strcmp(key, kStateSustainDb)     == 0) return text(params_.sustainDb);
        else if (std::strcmp(key, kStateAnalyzerSource) == 0) return text(static_cast<float>(analyserSource_));
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
        else if (std::strcmp(key, kStateAttackDb)      == 0) { params_.attackDb = f(); }
        else if (std::strcmp(key, kStateSustainDb)     == 0) { params_.sustainDb = f(); }
        else if (std::strcmp(key, kStateAnalyzerSource) == 0) {
            analyserSource_ = static_cast<core::SpectrumSource>(
                std::clamp(static_cast<int>(std::lround(f())), 0, 2));
            core::resetSpectrum(analyser_);
            publishSpectrum();
        }
        params_ = downspout::spliff::clampParameters(params_);
    }

    void sampleRateChanged(double) override
    {
        engine_ = downspout::spliff::EngineState{};  // runtime only; patch untouched
        core::resetSpectrum(analyser_);
        publishSpectrum();
    }

    void activate() override
    {
        engine_            = downspout::spliff::EngineState{};
        ccDepthOverride_   = -1.0f;
        ccSensOverride_    = -1.0f;
        ccDecayOverride_   = -1.0f;
        ccMixOverride_     = -1.0f;
        core::resetSpectrum(analyser_);
        publishSpectrum();
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

        // Some hosts hand us a mono or partially connected bus. analyseBlock() folds a missing right
        // channel into the left, but the left pointer itself must be valid.
        const float* const inLeft  = inputs[0]  != nullptr ? inputs[0]  : outputs[0];
        const float* const outLeft = outputs[0] != nullptr ? outputs[0] : inputs[0];

        if (analyserSource_ == core::SpectrumSource::Input)
            core::analyseBlock(analyser_, static_cast<float>(getSampleRate()), frames,
                               inLeft, inputs[1]);

        float* delta[2] = { nullptr, nullptr };
        if (analyserSource_ == core::SpectrumSource::Delta) {
            if (deltaL_.size() < frames) {
                deltaL_.resize(frames);
                deltaR_.resize(frames);
            }
            delta[0] = deltaL_.data();
            delta[1] = deltaR_.data();
        }

        downspout::spliff::processBlock(
            engine_, params_, frames, getSampleRate(), inputs, outputs,
            ccDepthOverride_ >= 0.0f ? ccDepthOverride_ : params_.depth,
            ccSensOverride_  >= 0.0f ? ccSensOverride_  : params_.sensitivity,
            ccDecayOverride_ >= 0.0f ? ccDecayOverride_ : params_.decay,
            ccMixOverride_   >= 0.0f ? ccMixOverride_   : params_.mix,
            delta);

        if (analyserSource_ == core::SpectrumSource::Output)
            core::analyseBlock(analyser_, static_cast<float>(getSampleRate()), frames,
                               outLeft, outputs[1]);
        else if (analyserSource_ == core::SpectrumSource::Delta)
            core::analyseBlock(analyser_, static_cast<float>(getSampleRate()), frames,
                               deltaL_.data(), deltaR_.data());
        publishSpectrum();
    }

private:
    void publishSpectrum()
    {
        for (std::size_t k = 0; k < spectrum_.size(); ++k)
            spectrum_[k].store(analyser_.levelDb[k], std::memory_order_relaxed);
    }

    downspout::spliff::Parameters  params_ {};
    downspout::spliff::EngineState engine_ {};
    core::SpectrumSource analyserSource_ = core::SpectrumSource::Output;
    core::SpectrumState analyser_ {};
    std::array<std::atomic<float>, core::kSpectrumBins> spectrum_ {};
    std::vector<float> deltaL_ {}, deltaR_ {};
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
