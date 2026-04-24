#include "DistrhoPlugin.hpp"

#include "e_mix_engine.hpp"
#include "e_mix_serialization.hpp"

#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamTotalBars = 0,
    kParamDivision,
    kParamSteps,
    kParamOffset,
    kParamFadeBars,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateCount
};

constexpr const char* kStateKeyParameters = "parameters";

using CoreAudioBlock = downspout::emix::AudioBlock;
using CoreEngineState = downspout::emix::EngineState;
using CoreParameters = downspout::emix::Parameters;
using CoreTransport = downspout::emix::TransportSnapshot;

constexpr uint32_t kWrapperChannelCount = DISTRHO_PLUGIN_NUM_INPUTS < DISTRHO_PLUGIN_NUM_OUTPUTS
    ? DISTRHO_PLUGIN_NUM_INPUTS
    : DISTRHO_PLUGIN_NUM_OUTPUTS;

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
        transport.bpm = timePos.bbt.beatsPerMinute;
    }

    return transport;
}

}  // namespace

class EMixPlugin : public Plugin
{
public:
    EMixPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::emix::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override
    {
        return "e_mix";
    }

    const char* getDescription() const override
    {
        return "Transport-aware Euclidean audio gate ported from flues LV2 into downspout/DPF.";
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
        return d_cconst('E', 'M', 'i', 'x');
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
        parameter.hints = kParameterIsAutomatable | kParameterIsInteger;

        switch (index)
        {
        case kParamTotalBars:
            parameter.name = "Total Bars";
            parameter.symbol = "total_bars";
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 4096.0f;
            parameter.ranges.def = 128.0f;
            break;
        case kParamDivision:
            parameter.name = "Division";
            parameter.symbol = "division";
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 512.0f;
            parameter.ranges.def = 16.0f;
            break;
        case kParamSteps:
            parameter.name = "Steps";
            parameter.symbol = "steps";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 512.0f;
            parameter.ranges.def = 8.0f;
            break;
        case kParamOffset:
            parameter.name = "Offset";
            parameter.symbol = "offset";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 511.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamFadeBars:
            parameter.name = "Fade";
            parameter.symbol = "fade";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 4096.0f;
            parameter.ranges.def = 0.0f;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStateParameters)
        {
            state.key = kStateKeyParameters;
            state.label = "Parameters";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamTotalBars: return parameters_.totalBars;
        case kParamDivision: return parameters_.division;
        case kParamSteps: return parameters_.steps;
        case kParamOffset: return parameters_.offset;
        case kParamFadeBars: return parameters_.fadeBars;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamTotalBars: parameters_.totalBars = value; break;
        case kParamDivision: parameters_.division = value; break;
        case kParamSteps: parameters_.steps = value; break;
        case kParamOffset: parameters_.offset = value; break;
        case kParamFadeBars: parameters_.fadeBars = value; break;
        }

        parameters_ = downspout::emix::clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::emix::serializeParameters(parameters_).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;

        const std::string text = value != nullptr ? value : "";
        const auto parameters = downspout::emix::deserializeParameters(text);
        if (parameters.has_value())
            parameters_ = *parameters;
    }

    void activate() override
    {
        downspout::emix::activate(engineState_);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        std::array<const float*, downspout::emix::kMaxChannels> safeInputs {};
        std::array<float*, downspout::emix::kMaxChannels> safeOutputs {};

        for (uint32_t channel = 0; channel < kWrapperChannelCount; ++channel)
        {
            safeInputs[channel] = inputs[channel];
            safeOutputs[channel] = outputs[channel];
        }

        CoreAudioBlock audio;
        audio.inputs = safeInputs;
        audio.outputs = safeOutputs;
        audio.channelCount = kWrapperChannelCount;

        downspout::emix::processBlock(engineState_,
                                      parameters_,
                                      toCoreTransport(getTimePosition()),
                                      frames,
                                      getSampleRate(),
                                      audio);
    }

private:
    CoreParameters parameters_ {};
    CoreEngineState engineState_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EMixPlugin)
};

Plugin* createPlugin()
{
    return new EMixPlugin();
}

END_NAMESPACE_DISTRHO
