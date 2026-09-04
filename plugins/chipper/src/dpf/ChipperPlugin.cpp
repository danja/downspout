#include "DistrhoPlugin.hpp"

#include "chipper_core.hpp"

#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamBitDepth = 0,
    kParamRateDiv,
    kParamJitter,
    kParamMix,
    kParamOutputGain,
    kParameterCount
};

constexpr const char* kStateBitDepthKey   = "bit_depth";
constexpr const char* kStateRateDivKey    = "rate_div";
constexpr const char* kStateJitterKey     = "jitter";
constexpr const char* kStateMixKey        = "mix";
constexpr const char* kStateOutputGainKey = "output_gain";

}  // namespace

class ChipperPlugin : public Plugin
{
public:
    ChipperPlugin()
        : Plugin(kParameterCount, 0, 5)
    {
        params_ = downspout::chipper::clampParameters(params_);
    }

protected:
    const char* getLabel()       const override { return "Chipper"; }
    const char* getDescription() const override { return "1980s video game lo-fi processor: bit crushing, sample-rate reduction, clock jitter."; }
    const char* getMaker()       const override { return "danja"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()     const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('C', 'h', 'p', 'r');
    }

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
            parameter.ranges = {1.0f, 16.0f, 8.0f};
            break;
        case kParamRateDiv:
            parameter.name   = "Rate Divisor";
            parameter.symbol = "rate_div";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = {1.0f, 64.0f, 8.0f};
            break;
        case kParamJitter:
            parameter.name   = "Jitter";
            parameter.symbol = "jitter";
            parameter.ranges = {0.0f, 1.0f, 0.0f};
            break;
        case kParamMix:
            parameter.name   = "Mix";
            parameter.symbol = "mix";
            parameter.ranges = {0.0f, 100.0f, 100.0f};
            break;
        case kParamOutputGain:
            parameter.name   = "Output Gain";
            parameter.symbol = "output_gain";
            parameter.ranges = {-12.0f, 12.0f, 0.0f};
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct Info { const char* key; const char* label; const char* def; };
        static constexpr Info kInfo[] = {
            { kStateBitDepthKey,   "Bit Depth",   "8"   },
            { kStateRateDivKey,    "Rate Div",    "8"   },
            { kStateJitterKey,     "Jitter",      "0"   },
            { kStateMixKey,        "Mix",         "100" },
            { kStateOutputGainKey, "Output Gain", "0"   },
        };
        if (index < 5) {
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
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t, float) override {}  // driven through setState

    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        if (std::strcmp(key, kStateBitDepthKey) == 0) {
            params_.bitDepth   = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateRateDivKey) == 0) {
            params_.rateDiv    = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateJitterKey) == 0) {
            params_.jitter     = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateMixKey) == 0) {
            params_.mix        = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateOutputGainKey) == 0) {
            params_.outputGain = static_cast<float>(std::atof(value));
        }
        params_ = downspout::chipper::clampParameters(params_);
    }

    void activate() override
    {
        engine_ = downspout::chipper::EngineState{};
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        downspout::chipper::processBlock(engine_, params_, frames, inputs, outputs);
    }

private:
    downspout::chipper::Parameters  params_ {};
    downspout::chipper::EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipperPlugin)
};

Plugin* createPlugin()
{
    return new ChipperPlugin();
}

END_NAMESPACE_DISTRHO
