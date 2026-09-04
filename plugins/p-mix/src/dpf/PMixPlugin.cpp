#include "DistrhoPlugin.hpp"

#include "p_mix_engine.hpp"
#include "p_mix_serialization.hpp"

#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamGranularity = 0,
    kParamMaintain,
    kParamFade,
    kParamCut,
    kParamFadeDurMax,
    kParamBias,
    kParamMute,
    kParamSeed,
    kParamOppose,
    kParameterCount
};

[[nodiscard]] uint32_t seedToUint(float seed) {
    if (seed <= 0.0f) return 0u;
    return static_cast<uint32_t>(seed * static_cast<float>(0xFFFFFFFFu));
}

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateCount
};

constexpr const char* kStateKeyParameters = "parameters";

using CoreAudioBlock = downspout::pmix::AudioBlock;
using CoreEngineState = downspout::pmix::EngineState;
using CoreParameters = downspout::pmix::Parameters;
using CoreTransport = downspout::pmix::TransportSnapshot;

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
        transport.beatType = timePos.bbt.beatType;
        transport.bpm = timePos.bbt.beatsPerMinute;
        transport.meter = downspout::meterFromTimeSignature(timePos.bbt.beatsPerBar, timePos.bbt.beatType);
    }

    return transport;
}

} // namespace

class PMixPlugin : public Plugin
{
public:
    PMixPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::pmix::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override
    {
        return "P-Mix";
    }

    const char* getDescription() const override
    {
        return "Transport-aware probabilistic audio switcher.";
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
        return d_cconst('P', 'M', 'i', 'x');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);

        if (index < 2)
            port.groupId = kPortGroupStereo;

        if (input && index >= 2) {
            port.name   = index == 2 ? String("Sidechain L") : String("Sidechain R");
            port.symbol = index == 2 ? String("sidechain_l") : String("sidechain_r");
        } else {
            port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
            port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
        }
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

        switch (index)
        {
        case kParamGranularity:
            parameter.name = "Granularity";
            parameter.symbol = "granularity";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 32.0f;
            parameter.ranges.def = 4.0f;
            break;
        case kParamMaintain:
            parameter.name = "Maintain";
            parameter.symbol = "maintain";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 50.0f;
            break;
        case kParamFade:
            parameter.name = "Fade";
            parameter.symbol = "fade";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 25.0f;
            break;
        case kParamCut:
            parameter.name = "Cut";
            parameter.symbol = "cut";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 25.0f;
            break;
        case kParamFadeDurMax:
            parameter.name = "Fade Dur Max";
            parameter.symbol = "fade_dur_max";
            parameter.ranges.min = downspout::pmix::kFadeMinFraction;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamBias:
            parameter.name = "Bias";
            parameter.symbol = "bias";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 50.0f;
            break;
        case kParamMute:
            parameter.name = "Mute";
            parameter.symbol = "mute";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamSeed:
            parameter.hints = 0;
            parameter.name = "Seed";
            parameter.symbol = "seed";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamOppose:
            parameter.name = "Oppose";
            parameter.symbol = "oppose";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
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
        case kParamGranularity: return parameters_.granularity;
        case kParamMaintain: return parameters_.maintain;
        case kParamFade: return parameters_.fade;
        case kParamCut: return parameters_.cut;
        case kParamFadeDurMax: return parameters_.fadeDurMax;
        case kParamBias: return parameters_.bias;
        case kParamMute: return parameters_.mute;
        case kParamSeed: return parameters_.seed;
        case kParamOppose: return parameters_.oppose;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamGranularity: parameters_.granularity = value; break;
        case kParamMaintain: parameters_.maintain = value; break;
        case kParamFade: parameters_.fade = value; break;
        case kParamCut: parameters_.cut = value; break;
        case kParamFadeDurMax: parameters_.fadeDurMax = value; break;
        case kParamBias: parameters_.bias = value; break;
        case kParamMute: parameters_.mute = value; break;
        case kParamSeed: parameters_.seed = value; break;
        case kParamOppose: parameters_.oppose = value; break;
        }

        parameters_ = downspout::pmix::clampParameters(parameters_);

        if (index == kParamSeed) {
            const uint32_t s = seedToUint(parameters_.seed);
            if (s > 0u) engineState_.rngState = s;
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::pmix::serializeParameters(parameters_).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;

        const std::string text = value != nullptr ? value : "";
        const auto parameters = downspout::pmix::deserializeParameters(text);
        if (parameters.has_value())
            parameters_ = *parameters;
    }

    void activate() override
    {
        downspout::pmix::activate(engineState_, seedToUint(parameters_.seed));
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        std::array<const float*, downspout::pmix::kMaxChannels> safeInputs {};
        std::array<float*, downspout::pmix::kMaxChannels> safeOutputs {};

        for (uint32_t channel = 0; channel < kWrapperChannelCount; ++channel)
        {
            safeInputs[channel] = inputs[channel];
            safeOutputs[channel] = outputs[channel];
        }

        CoreAudioBlock audio;
        audio.inputs = safeInputs;
        audio.outputs = safeOutputs;
        audio.channelCount = kWrapperChannelCount;
        audio.sidechain[0] = (DISTRHO_PLUGIN_NUM_INPUTS > 2) ? inputs[2] : nullptr;
        audio.sidechain[1] = (DISTRHO_PLUGIN_NUM_INPUTS > 3) ? inputs[3] : nullptr;

        downspout::pmix::processBlock(engineState_,
                                      parameters_,
                                      toCoreTransport(getTimePosition()),
                                      frames,
                                      getSampleRate(),
                                      audio);
    }

private:
    CoreParameters parameters_ {};
    CoreEngineState engineState_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PMixPlugin)
};

Plugin* createPlugin()
{
    return new PMixPlugin();
}

END_NAMESPACE_DISTRHO
