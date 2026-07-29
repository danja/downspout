#include "DistrhoPlugin.hpp"

#include "t_mix_engine.hpp"
#include "t_mix_params.hpp"
#include "t_mix_serialization.hpp"

#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::tmix;

[[nodiscard]] bool inRange(uint32_t index, uint32_t base)
{
    return index >= base && index < base + kInputChannelCount;
}

}  // namespace

class TMixPlugin : public Plugin
{
public:
    TMixPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override { return "T-Mix"; }
    const char* getDescription() const override { return "Eight-input stereo audio mixer."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('T', 'M', 'i', 'x');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (input) {
            port.groupId = index;
            port.name = String("Channel ") + String(static_cast<int>(index + 1));
            port.symbol = String("channel_") + String(static_cast<int>(index + 1));
        } else {
            port.groupId = kPortGroupStereo;
            port.name = index == 0 ? "Output Left" : "Output Right";
            port.symbol = index == 0 ? "out_left" : "out_right";
        }
    }

    void initPortGroup(uint32_t groupId, PortGroup& group) override
    {
        if (groupId < kInputChannelCount) {
            group.name = String("Input ") + String(static_cast<int>(groupId + 1));
            group.symbol = String("input_") + String(static_cast<int>(groupId + 1));
        }
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        if (inRange(index, kParamLevelBase)) {
            const uint32_t channel = index - kParamLevelBase;
            parameter.name = String("Channel ") + String(static_cast<int>(channel + 1)) + " Level";
            parameter.symbol = String("channel_") + String(static_cast<int>(channel + 1)) + "_level";
            parameter.unit = "dB";
            parameter.ranges = {0.0f, kMinimumLevelDb, kMaximumLevelDb};
        } else if (inRange(index, kParamPanBase)) {
            const uint32_t channel = index - kParamPanBase;
            parameter.name = String("Channel ") + String(static_cast<int>(channel + 1)) + " Pan";
            parameter.symbol = String("channel_") + String(static_cast<int>(channel + 1)) + "_pan";
            parameter.ranges = {0.0f, -1.0f, 1.0f};
        } else if (inRange(index, kParamMuteBase)) {
            const uint32_t channel = index - kParamMuteBase;
            parameter.name = String("Channel ") + String(static_cast<int>(channel + 1)) + " Mute";
            parameter.symbol = String("channel_") + String(static_cast<int>(channel + 1)) + "_mute";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 1.0f};
        } else if (inRange(index, kParamSoloBase)) {
            const uint32_t channel = index - kParamSoloBase;
            parameter.name = String("Channel ") + String(static_cast<int>(channel + 1)) + " Solo";
            parameter.symbol = String("channel_") + String(static_cast<int>(channel + 1)) + "_solo";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 1.0f};
        } else if (index == kParamMaster) {
            parameter.name = "Master Level";
            parameter.symbol = "master_level";
            parameter.unit = "dB";
            parameter.ranges = {0.0f, kMinimumLevelDb, kMaximumLevelDb};
        } else if (inRange(index, kParamMeterBase)) {
            const uint32_t channel = index - kParamMeterBase;
            parameter.name = String("Channel ") + String(static_cast<int>(channel + 1)) + " Meter";
            parameter.symbol = String("channel_") + String(static_cast<int>(channel + 1)) + "_meter";
            parameter.hints = kParameterIsOutput;
            parameter.ranges = {0.0f, 0.0f, 1.0f};
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
        if (inRange(index, kParamLevelBase))
            return parameters_.channels[index - kParamLevelBase].levelDb;
        if (inRange(index, kParamPanBase))
            return parameters_.channels[index - kParamPanBase].pan;
        if (inRange(index, kParamMuteBase))
            return parameters_.channels[index - kParamMuteBase].mute;
        if (inRange(index, kParamSoloBase))
            return parameters_.channels[index - kParamSoloBase].solo;
        if (index == kParamMaster)
            return parameters_.masterDb;
        if (inRange(index, kParamMeterBase))
            return status_.meters[index - kParamMeterBase];
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (inRange(index, kParamLevelBase))
            parameters_.channels[index - kParamLevelBase].levelDb = value;
        else if (inRange(index, kParamPanBase))
            parameters_.channels[index - kParamPanBase].pan = value;
        else if (inRange(index, kParamMuteBase))
            parameters_.channels[index - kParamMuteBase].mute = value;
        else if (inRange(index, kParamSoloBase))
            parameters_.channels[index - kParamSoloBase].solo = value;
        else if (index == kParamMaster)
            parameters_.masterDb = value;
        parameters_ = clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(serializeParameters(parameters_).c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;
        const auto decoded = deserializeParameters(value != nullptr ? value : "");
        if (decoded.has_value())
            parameters_ = *decoded;
    }

    void activate() override
    {
        downspout::tmix::activate(engineState_);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        AudioBlock audio;
        for (uint32_t channel = 0; channel < kInputChannelCount; ++channel)
            audio.inputs[channel] = inputs[channel];
        for (uint32_t channel = 0; channel < kOutputChannelCount; ++channel)
            audio.outputs[channel] = outputs[channel];
        status_ = processBlock(engineState_, parameters_, frames, getSampleRate(), audio);
    }

private:
    Parameters parameters_ {};
    EngineState engineState_ {};
    OutputStatus status_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TMixPlugin)
};

Plugin* createPlugin()
{
    return new TMixPlugin();
}

END_NAMESPACE_DISTRHO
