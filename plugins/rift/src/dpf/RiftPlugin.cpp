#include "DistrhoPlugin.hpp"

#include "rift_engine.hpp"
#include "rift_params.hpp"
#include "rift_serialization.hpp"

#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

using CoreAudioBlock = downspout::rift::AudioBlock;
using CoreEngineState = downspout::rift::EngineState;
using CoreOutputStatus = downspout::rift::OutputStatus;
using CoreParameters = downspout::rift::Parameters;
using CoreTransport = downspout::rift::TransportSnapshot;
using CoreTriggers = downspout::rift::Triggers;

using downspout::rift::ActionType;
using downspout::rift::kActionCount;
using downspout::rift::kActionNames;
using downspout::rift::kParamDamage;
using downspout::rift::kParamDensity;
using downspout::rift::kParamDrift;
using downspout::rift::kParamGrid;
using downspout::rift::kParamHold;
using downspout::rift::kParamMemoryBars;
using downspout::rift::kParamMix;
using downspout::rift::kParamPitch;
using downspout::rift::kParamRecover;
using downspout::rift::kParamScatter;
using downspout::rift::kParamStatusAction;
using downspout::rift::kParamStatusActivity;
using downspout::rift::kParameterCount;
using downspout::rift::kStateCount;
using downspout::rift::kStateKeyParameters;
using downspout::rift::kStateParameters;

ParameterEnumerationValue kActionEnumValues[] = {
    {0.0f, "Pass"},
    {1.0f, "Repeat"},
    {2.0f, "Reverse"},
    {3.0f, "Skip"},
    {4.0f, "Smear"},
    {5.0f, "Slip"},
};

constexpr std::uint32_t kWrapperChannelCount =
    DISTRHO_PLUGIN_NUM_INPUTS < DISTRHO_PLUGIN_NUM_OUTPUTS ? DISTRHO_PLUGIN_NUM_INPUTS : DISTRHO_PLUGIN_NUM_OUTPUTS;

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

class RiftPlugin : public Plugin
{
public:
    RiftPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::rift::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override
    {
        return "rift";
    }

    const char* getDescription() const override
    {
        return "Transport-aware stereo micro-loop disruption effect with repeat, reverse, smear, and slip actions.";
    }

    const char* getMaker() const override
    {
        return "Downspout";
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
        return d_cconst('R', 'f', 'x', 't');
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

        switch (index)
        {
        case kParamGrid:
            parameter.name = "Grid";
            parameter.symbol = "grid";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 8.0f;
            break;
        case kParamDensity:
            parameter.name = "Density";
            parameter.symbol = "density";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 35.0f;
            break;
        case kParamDamage:
            parameter.name = "Damage";
            parameter.symbol = "damage";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 45.0f;
            break;
        case kParamMemoryBars:
            parameter.name = "Memory";
            parameter.symbol = "memory_bars";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 8.0f;
            parameter.ranges.def = 2.0f;
            break;
        case kParamDrift:
            parameter.name = "Drift";
            parameter.symbol = "drift";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 20.0f;
            break;
        case kParamPitch:
            parameter.name = "Pitch";
            parameter.symbol = "pitch";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = -12.0f;
            parameter.ranges.max = 12.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamMix:
            parameter.name = "Mix";
            parameter.symbol = "mix";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 100.0f;
            break;
        case kParamHold:
            parameter.name = "Hold";
            parameter.symbol = "hold";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamScatter:
            parameter.name = "Scatter";
            parameter.symbol = "scatter";
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamRecover:
            parameter.name = "Recover";
            parameter.symbol = "recover";
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusAction:
            parameter.name = "Current Action";
            parameter.symbol = "status_action";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(kActionCount - 1);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(kActionCount);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kActionEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamStatusActivity:
            parameter.name = "Current Activity";
            parameter.symbol = "status_activity";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
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
        case kParamGrid: return parameters_.grid;
        case kParamDensity: return parameters_.density;
        case kParamDamage: return parameters_.damage;
        case kParamMemoryBars: return parameters_.memoryBars;
        case kParamDrift: return parameters_.drift;
        case kParamPitch: return parameters_.pitch;
        case kParamMix: return parameters_.mix;
        case kParamHold: return parameters_.hold;
        case kParamScatter: return scatterValue_;
        case kParamRecover: return recoverValue_;
        case kParamStatusAction: return statusAction_;
        case kParamStatusActivity: return statusActivity_;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamGrid: parameters_.grid = value; break;
        case kParamDensity: parameters_.density = value; break;
        case kParamDamage: parameters_.damage = value; break;
        case kParamMemoryBars: parameters_.memoryBars = value; break;
        case kParamDrift: parameters_.drift = value; break;
        case kParamPitch: parameters_.pitch = value; break;
        case kParamMix: parameters_.mix = value; break;
        case kParamHold: parameters_.hold = value; break;
        case kParamScatter:
            if (value >= 0.5f && scatterValue_ < 0.5f)
                triggers_.scatterSerial += 1u;
            scatterValue_ = value;
            break;
        case kParamRecover:
            if (value >= 0.5f && recoverValue_ < 0.5f)
                triggers_.recoverSerial += 1u;
            recoverValue_ = value;
            break;
        default:
            break;
        }

        parameters_ = downspout::rift::clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::rift::serializeParameters(parameters_).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;

        const std::string text = value != nullptr ? value : "";
        const auto parameters = downspout::rift::deserializeParameters(text);
        if (parameters.has_value())
            parameters_ = *parameters;
    }

    void activate() override
    {
        downspout::rift::activate(engineState_, getSampleRate(), kWrapperChannelCount);
        statusAction_ = 0.0f;
        statusActivity_ = 0.0f;
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        std::array<const float*, downspout::rift::kMaxChannels> safeInputs {};
        std::array<float*, downspout::rift::kMaxChannels> safeOutputs {};

        for (uint32_t channel = 0; channel < kWrapperChannelCount; ++channel)
        {
            safeInputs[channel] = inputs[channel];
            safeOutputs[channel] = outputs[channel];
        }

        CoreAudioBlock audio;
        audio.inputs = safeInputs;
        audio.outputs = safeOutputs;
        audio.channelCount = kWrapperChannelCount;

        const CoreOutputStatus status = downspout::rift::processBlock(engineState_,
                                                                      parameters_,
                                                                      triggers_,
                                                                      toCoreTransport(getTimePosition()),
                                                                      frames,
                                                                      getSampleRate(),
                                                                      audio);
        statusAction_ = static_cast<float>(status.action);
        statusActivity_ = status.activity;
    }

private:
    CoreParameters parameters_ {};
    CoreTriggers triggers_ {};
    CoreEngineState engineState_ {};
    float scatterValue_ = 0.0f;
    float recoverValue_ = 0.0f;
    float statusAction_ = 0.0f;
    float statusActivity_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RiftPlugin)
};

Plugin* createPlugin()
{
    return new RiftPlugin();
}

END_NAMESPACE_DISTRHO
