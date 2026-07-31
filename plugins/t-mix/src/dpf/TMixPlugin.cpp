#include "DistrhoPlugin.hpp"

#include "t_mix_engine.hpp"
#include "t_mix_params.hpp"
#include "t_mix_serialization.hpp"

#include <cstring>
#include <array>
#include <algorithm>
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
        } else if (index == kParamProducerSlew) {
            parameter.name = "Producer Slew";
            parameter.symbol = "producer_slew_ms";
            parameter.unit = "ms";
            parameter.ranges = {kDefaultProducerSlewMs, 0.0f, 500.0f};
        } else if (inRange(index, kParamProducerGainBase)) {
            const uint32_t channel = index - kParamProducerGainBase;
            parameter.name = String("Channel ") + String(static_cast<int>(channel + 1)) + " Producer Gain";
            parameter.symbol = String("channel_") + String(static_cast<int>(channel + 1)) + "_producer_gain";
            parameter.hints = kParameterIsOutput;
            parameter.ranges = {1.0f, 0.0f, 1.0f};
        } else if (index == kParamProducerControlChannel) {
            parameter.name = "Producer Control Channel";
            parameter.symbol = "producer_control_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges = {0.0f, 0.0f, 16.0f};
        } else if (index == kParamRequireProducerGate) {
            parameter.name = "Require Producer Gate";
            parameter.symbol = "require_producer_gate";
            parameter.hints |= kParameterIsInteger | kParameterIsBoolean;
            parameter.ranges = {0.0f, 0.0f, 1.0f};
        } else if (index == kParamStatusProducerActive) {
            parameter.name = "Producer Bus Active";
            parameter.symbol = "producer_bus_active";
            parameter.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsBoolean;
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
        if (index == kParamProducerSlew)
            return parameters_.producerSlewMs;
        if (inRange(index, kParamProducerGainBase))
            return status_.producerGains[index - kParamProducerGainBase];
        if (index == kParamProducerControlChannel)
            return parameters_.producerControlChannel;
        if (index == kParamRequireProducerGate)
            return parameters_.requireProducerGate;
        if (index == kParamStatusProducerActive)
            return status_.producerActive ? 1.0f : 0.0f;
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
        else if (index == kParamProducerSlew)
            parameters_.producerSlewMs = value;
        else if (index == kParamProducerControlChannel)
            parameters_.producerControlChannel = value;
        else if (index == kParamRequireProducerGate)
            parameters_.requireProducerGate = value;
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

    void run(const float** inputs,
             float** outputs,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        AudioBlock audio;
        for (uint32_t channel = 0; channel < kInputChannelCount; ++channel)
            audio.inputs[channel] = inputs[channel];
        for (uint32_t channel = 0; channel < kOutputChannelCount; ++channel)
            audio.outputs[channel] = outputs[channel];
        std::array<MidiControlEvent, 512> controls {};
        const uint32_t count = std::min<uint32_t>(midiEventCount, controls.size());
        for (uint32_t index = 0; index < count; ++index) {
            controls[index].frame = std::min(midiEvents[index].frame, frames > 0 ? frames - 1 : 0);
            controls[index].size = static_cast<std::uint8_t>(std::min<uint32_t>(midiEvents[index].size, 3));
            const uint8_t* source = midiEvents[index].size > MidiEvent::kDataSize
                ? midiEvents[index].dataExt : midiEvents[index].data;
            for (uint32_t byte = 0; byte < controls[index].size; ++byte)
                controls[index].data[byte] = source[byte];
        }
        status_ = processBlock(engineState_, parameters_, frames, getSampleRate(), audio,
                               controls.data(), count);
        for (uint32_t index = 0; index < midiEventCount; ++index)
            writeMidiEvent(midiEvents[index]);
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
