#include "DistrhoPlugin.hpp"

#include "chipper_core.hpp"

#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Stable parameter indices — never reorder.
enum ParameterIndex : uint32_t {
    kParamBitDepth = 0,
    kParamRateDiv,
    kParamJitter,
    kParamMix,
    kParamOutputGain,
    kParamCCBitDepth,
    kParamCCRateDiv,
    kParamCCJitter,
    kParamCCMix,
    kParamCCChannel,
    kParameterCount
};

constexpr const char* kStateBitDepth   = "bit_depth";
constexpr const char* kStateRateDiv    = "rate_div";
constexpr const char* kStateJitter     = "jitter";
constexpr const char* kStateMix        = "mix";
constexpr const char* kStateOutputGain = "output_gain";
constexpr const char* kStateCCBitDepth = "cc_bit_depth";
constexpr const char* kStateCCRateDiv  = "cc_rate_div";
constexpr const char* kStateCCJitter   = "cc_jitter";
constexpr const char* kStateCCMix      = "cc_mix";
constexpr const char* kStateCCChannel  = "cc_channel";

constexpr uint32_t kStateCount = 10;

float ccToBitDepth(uint8_t v) noexcept
{
    return 1.0f + std::round(static_cast<float>(v) / 127.0f * 15.0f);
}

float ccToRateDiv(uint8_t v) noexcept
{
    return 1.0f + std::round(static_cast<float>(v) / 127.0f * 63.0f);
}

float ccToJitter(uint8_t v) noexcept
{
    return static_cast<float>(v) / 127.0f;
}

float ccToMix(uint8_t v) noexcept
{
    return static_cast<float>(v) / 127.0f * 100.0f;
}

}  // namespace

class ChipperPlugin : public Plugin
{
public:
    ChipperPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        params_ = downspout::chipper::clampParameters(params_);
    }

protected:
    const char* getLabel()       const override { return "Chipper"; }
    const char* getDescription() const override { return "1980s video game lo-fi: bit crushing, sample-rate reduction, clock jitter. Modulated by Drift via CC."; }
    const char* getMaker()       const override { return "danja"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()     const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('C', 'h', 'p', 'r'); }

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
        case kParamBitDepth:
            parameter.name   = "Bit Depth";
            parameter.symbol = "bit_depth";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 1.0f, 16.0f, 8.0f };
            break;
        case kParamRateDiv:
            parameter.name   = "Rate Divisor";
            parameter.symbol = "rate_div";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 1.0f, 64.0f, 8.0f };
            break;
        case kParamJitter:
            parameter.name   = "Jitter";
            parameter.symbol = "jitter";
            parameter.ranges = { 0.0f, 1.0f, 0.0f };
            break;
        case kParamMix:
            parameter.name   = "Mix";
            parameter.symbol = "mix";
            parameter.ranges = { 0.0f, 100.0f, 100.0f };
            break;
        case kParamOutputGain:
            parameter.name   = "Output Gain";
            parameter.symbol = "output_gain";
            parameter.ranges = { -12.0f, 12.0f, 0.0f };
            break;
        case kParamCCBitDepth:
            parameter.name   = "CC Bit Depth";
            parameter.symbol = "cc_bit_depth";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 0.0f, 127.0f, downspout::chipper::kDefaultCCBitDepth };
            break;
        case kParamCCRateDiv:
            parameter.name   = "CC Rate Div";
            parameter.symbol = "cc_rate_div";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 0.0f, 127.0f, downspout::chipper::kDefaultCCRateDiv };
            break;
        case kParamCCJitter:
            parameter.name   = "CC Jitter";
            parameter.symbol = "cc_jitter";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 0.0f, 127.0f, downspout::chipper::kDefaultCCJitter };
            break;
        case kParamCCMix:
            parameter.name   = "CC Mix";
            parameter.symbol = "cc_mix";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 0.0f, 127.0f, downspout::chipper::kDefaultCCMix };
            break;
        case kParamCCChannel:
            parameter.name   = "CC Channel";
            parameter.symbol = "cc_channel";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 1.0f, 16.0f, 1.0f };
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct Info { const char* key; const char* label; const char* def; };
        static constexpr Info kInfo[kStateCount] = {
            { kStateBitDepth,   "Bit Depth",   "8"   },
            { kStateRateDiv,    "Rate Div",    "8"   },
            { kStateJitter,     "Jitter",      "0"   },
            { kStateMix,        "Mix",         "100" },
            { kStateOutputGain, "Output Gain", "0"   },
            { kStateCCBitDepth, "CC Bit Depth","1"   },
            { kStateCCRateDiv,  "CC Rate Div", "2"   },
            { kStateCCJitter,   "CC Jitter",   "3"   },
            { kStateCCMix,      "CC Mix",      "4"   },
            { kStateCCChannel,  "CC Channel",  "1"   },
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
        case kParamBitDepth:   return params_.bitDepth;
        case kParamRateDiv:    return params_.rateDiv;
        case kParamJitter:     return params_.jitter;
        case kParamMix:        return params_.mix;
        case kParamOutputGain: return params_.outputGain;
        case kParamCCBitDepth: return params_.ccBitDepth;
        case kParamCCRateDiv:  return params_.ccRateDiv;
        case kParamCCJitter:   return params_.ccJitter;
        case kParamCCMix:      return params_.ccMix;
        case kParamCCChannel:  return params_.ccChannel;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t, float) override {}

    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        auto f = [&] { return static_cast<float>(std::atof(value)); };
        if      (std::strcmp(key, kStateBitDepth)   == 0) { params_.bitDepth   = f(); }
        else if (std::strcmp(key, kStateRateDiv)    == 0) { params_.rateDiv    = f(); }
        else if (std::strcmp(key, kStateJitter)     == 0) { params_.jitter     = f(); }
        else if (std::strcmp(key, kStateMix)        == 0) { params_.mix        = f(); }
        else if (std::strcmp(key, kStateOutputGain) == 0) { params_.outputGain = f(); }
        else if (std::strcmp(key, kStateCCBitDepth) == 0) { params_.ccBitDepth = f(); }
        else if (std::strcmp(key, kStateCCRateDiv)  == 0) { params_.ccRateDiv  = f(); }
        else if (std::strcmp(key, kStateCCJitter)   == 0) { params_.ccJitter   = f(); }
        else if (std::strcmp(key, kStateCCMix)      == 0) { params_.ccMix      = f(); }
        else if (std::strcmp(key, kStateCCChannel)  == 0) { params_.ccChannel  = f(); }
        params_ = downspout::chipper::clampParameters(params_);
    }

    void activate() override
    {
        engine_             = downspout::chipper::EngineState{};
        ccBitDepthOverride_ = -1.0f;
        ccRateDivOverride_  = -1.0f;
        ccJitterOverride_   = -1.0f;
        ccMixOverride_      = -1.0f;
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const int ccBitDepthNum = static_cast<int>(params_.ccBitDepth);
        const int ccRateDivNum  = static_cast<int>(params_.ccRateDiv);
        const int ccJitterNum   = static_cast<int>(params_.ccJitter);
        const int ccMixNum      = static_cast<int>(params_.ccMix);
        const int ccCh          = static_cast<int>(params_.ccChannel);

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const auto& ev = midiEvents[i];
            if (ev.size < 3) continue;
            const uint8_t status = ev.data[0];
            if ((status & 0xF0) != 0xB0) continue;
            if ((status & 0x0F) + 1 != ccCh) continue;

            const uint8_t evCC  = ev.data[1];
            const uint8_t evVal = ev.data[2];

            if (ccBitDepthNum > 0 && evCC == static_cast<uint8_t>(ccBitDepthNum))
                ccBitDepthOverride_ = ccToBitDepth(evVal);
            if (ccRateDivNum  > 0 && evCC == static_cast<uint8_t>(ccRateDivNum))
                ccRateDivOverride_  = ccToRateDiv(evVal);
            if (ccJitterNum   > 0 && evCC == static_cast<uint8_t>(ccJitterNum))
                ccJitterOverride_   = ccToJitter(evVal);
            if (ccMixNum      > 0 && evCC == static_cast<uint8_t>(ccMixNum))
                ccMixOverride_      = ccToMix(evVal);
        }

        if (ccBitDepthNum == 0) ccBitDepthOverride_ = -1.0f;
        if (ccRateDivNum  == 0) ccRateDivOverride_  = -1.0f;
        if (ccJitterNum   == 0) ccJitterOverride_   = -1.0f;
        if (ccMixNum      == 0) ccMixOverride_      = -1.0f;

        downspout::chipper::processBlock(
            engine_, params_, frames, inputs, outputs,
            ccBitDepthOverride_ >= 0.0f ? ccBitDepthOverride_ : params_.bitDepth,
            ccRateDivOverride_  >= 0.0f ? ccRateDivOverride_  : params_.rateDiv,
            ccJitterOverride_   >= 0.0f ? ccJitterOverride_   : params_.jitter,
            ccMixOverride_      >= 0.0f ? ccMixOverride_      : params_.mix);
    }

private:
    downspout::chipper::Parameters  params_ {};
    downspout::chipper::EngineState engine_ {};
    float ccBitDepthOverride_ = -1.0f;
    float ccRateDivOverride_  = -1.0f;
    float ccJitterOverride_   = -1.0f;
    float ccMixOverride_      = -1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipperPlugin)
};

Plugin* createPlugin()
{
    return new ChipperPlugin();
}

END_NAMESPACE_DISTRHO
