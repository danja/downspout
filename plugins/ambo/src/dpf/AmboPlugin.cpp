#include "DistrhoPlugin.hpp"

#include "ambo_engine.hpp"
#include "ambo_params.hpp"
#include "ambo_serialization.hpp"

#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

using CoreAudioBlock = downspout::ambo::AudioBlock;
using CoreEngineState = downspout::ambo::EngineState;
using CoreOutputStatus = downspout::ambo::OutputStatus;
using CoreParameters = downspout::ambo::Parameters;

using downspout::ambo::kParamChain;
using downspout::ambo::kParamBypass;
using downspout::ambo::kParamDelay;
using downspout::ambo::kParamDrive;
using downspout::ambo::kParamFeedback;
using downspout::ambo::kParamMix;
using downspout::ambo::kParamOutput;
using downspout::ambo::kParamShimmer;
using downspout::ambo::kParamSpectral;
using downspout::ambo::kParamStatusFeedback;
using downspout::ambo::kParamStatusWet;
using downspout::ambo::kParamTape;
using downspout::ambo::kParamTime;
using downspout::ambo::kParameterCount;
using downspout::ambo::kStateCount;
using downspout::ambo::kStateKeyParameters;
using downspout::ambo::kStateParameters;

constexpr uint32_t kWrapperChannelCount = DISTRHO_PLUGIN_NUM_INPUTS < DISTRHO_PLUGIN_NUM_OUTPUTS
    ? DISTRHO_PLUGIN_NUM_INPUTS
    : DISTRHO_PLUGIN_NUM_OUTPUTS;

}  // namespace

class AmboPlugin : public Plugin
{
public:
    AmboPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::ambo::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override
    {
        return "Ambo";
    }

    const char* getDescription() const override
    {
        return "Composable stereo ambient effects chain.";
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
        return d_cconst('A', 'm', 'b', 'o');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);

        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 1.0f;
        parameter.ranges.def = 0.0f;

        switch (index) {
        case kParamChain:
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
            parameter.name = "Chain";
            parameter.symbol = "chain";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(downspout::ambo::kChainCount - 1);
            parameter.ranges.def = 0.0f;
            break;
        case kParamTime:
            parameter.name = "Time";
            parameter.symbol = "time";
            parameter.ranges.def = 0.30f;
            break;
        case kParamSpectral:
            parameter.name = "Spectral";
            parameter.symbol = "spectral";
            parameter.ranges.def = 0.24f;
            break;
        case kParamTape:
            parameter.name = "Tape";
            parameter.symbol = "tape";
            parameter.ranges.def = 0.22f;
            break;
        case kParamShimmer:
            parameter.name = "Shimmer";
            parameter.symbol = "shimmer";
            parameter.ranges.def = 0.36f;
            break;
        case kParamDelay:
            parameter.name = "Delay";
            parameter.symbol = "delay";
            parameter.ranges.def = 0.28f;
            break;
        case kParamDrive:
            parameter.name = "Drive";
            parameter.symbol = "drive";
            parameter.ranges.def = 0.12f;
            break;
        case kParamFeedback:
            parameter.name = "Feedback";
            parameter.symbol = "feedback";
            parameter.ranges.max = 0.96f;
            parameter.ranges.def = 0.18f;
            break;
        case kParamMix:
            parameter.name = "Mix";
            parameter.symbol = "mix";
            parameter.ranges.def = 0.55f;
            break;
        case kParamOutput:
            parameter.name = "Output";
            parameter.symbol = "output";
            parameter.ranges.min = -24.0f;
            parameter.ranges.max = 12.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusWet:
            parameter.hints = kParameterIsOutput;
            parameter.name = "Wet Activity";
            parameter.symbol = "wet_activity";
            break;
        case kParamStatusFeedback:
            parameter.hints = kParameterIsOutput;
            parameter.name = "Feedback Activity";
            parameter.symbol = "feedback_activity";
            break;
        case kParamBypass:
            parameter.initDesignation(kParameterDesignationBypass);
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStateParameters) {
            state.key = kStateKeyParameters;
            state.label = "Parameters";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamChain: return parameters_.chain;
        case kParamTime: return parameters_.time;
        case kParamSpectral: return parameters_.spectral;
        case kParamTape: return parameters_.tape;
        case kParamShimmer: return parameters_.shimmer;
        case kParamDelay: return parameters_.delay;
        case kParamDrive: return parameters_.drive;
        case kParamFeedback: return parameters_.feedback;
        case kParamMix: return parameters_.mix;
        case kParamOutput: return parameters_.output;
        case kParamStatusWet: return status_.wetEnergy;
        case kParamStatusFeedback: return status_.feedbackEnergy;
        case kParamBypass: return parameters_.bypass;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case kParamChain: parameters_.chain = value; break;
        case kParamTime: parameters_.time = value; break;
        case kParamSpectral: parameters_.spectral = value; break;
        case kParamTape: parameters_.tape = value; break;
        case kParamShimmer: parameters_.shimmer = value; break;
        case kParamDelay: parameters_.delay = value; break;
        case kParamDrive: parameters_.drive = value; break;
        case kParamFeedback: parameters_.feedback = value; break;
        case kParamMix: parameters_.mix = value; break;
        case kParamOutput: parameters_.output = value; break;
        case kParamBypass: parameters_.bypass = value; break;
        default: break;
        }

        parameters_ = downspout::ambo::clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::ambo::serializeParameters(parameters_).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;

        const std::string text = value != nullptr ? value : "";
        const auto parameters = downspout::ambo::deserializeParameters(text);
        if (parameters.has_value())
            parameters_ = *parameters;
    }

    void activate() override
    {
        downspout::ambo::activate(engineState_, getSampleRate());
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        std::array<const float*, downspout::ambo::kMaxChannels> safeInputs {};
        std::array<float*, downspout::ambo::kMaxChannels> safeOutputs {};

        for (uint32_t channel = 0; channel < kWrapperChannelCount; ++channel) {
            safeInputs[channel] = inputs[channel];
            safeOutputs[channel] = outputs[channel];
        }

        CoreAudioBlock audio;
        audio.inputs = safeInputs;
        audio.outputs = safeOutputs;
        audio.channelCount = kWrapperChannelCount;

        status_ = downspout::ambo::processBlock(engineState_, parameters_, frames, getSampleRate(), audio);
    }

private:
    CoreParameters parameters_ {};
    CoreEngineState engineState_ {};
    CoreOutputStatus status_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmboPlugin)
};

Plugin* createPlugin()
{
    return new AmboPlugin();
}

END_NAMESPACE_DISTRHO
