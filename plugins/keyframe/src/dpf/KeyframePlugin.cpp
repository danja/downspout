#include "DistrhoPlugin.hpp"

#include "keyframe_core.hpp"
#include "keyframe_params.hpp"

#include <array>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using CoreParameters = downspout::keyframe::Parameters;
using CoreState = downspout::keyframe::EngineState;
namespace core = downspout::keyframe;

constexpr const char* kStateKeyParameters = "parameters";

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateCount
};

using FieldPointer = float CoreParameters::*;

constexpr std::array<FieldPointer, core::kInputParameterCount> kFields = {{
    &CoreParameters::time,
    &CoreParameters::pitch,
    &CoreParameters::splice,
    &CoreParameters::threshold,
    &CoreParameters::maxSplice,
    &CoreParameters::hold,
    &CoreParameters::mix,
    &CoreParameters::width,
    &CoreParameters::level,
}};

}  // namespace

class KeyframePlugin : public Plugin {
public:
    KeyframePlugin()
        : Plugin(static_cast<uint32_t>(core::kParameterCount), 2, kStateCount)
    {
        parameters_ = core::clampParameters(parameters_);
        core::activate(engineState_, getSampleRate());
        setLatency(static_cast<uint32_t>(core::reportedLatencySamples()));
    }

protected:
    const char* getLabel() const override { return "Keyframe"; }

    const char* getDescription() const override
    {
        return "Extrema-sampling time stretch: the input is reduced to timestamped "
               "local extrema, and an overlap-add splice whose crossfade length is "
               "decided by keyframe spacing holds transients together. Fixed reported "
               "latency of 32768 samples.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('K', 'y', 'F', 'r'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount)
            return;

        const core::ParamSpec& spec = core::kParameterSpecs[index];
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;

        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        if (static_cast<core::ParamId>(index) == core::ParamId::hold)
        {
            static ParameterEnumerationValue values[] = {
                {0.0f, "Off"},
                {1.0f, "On"},
            };
            parameter.enumValues.count = 2;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = false;
        }
    }

    void initState(const uint32_t index, State& state) override
    {
        if (index != kStateParameters)
            return;
        state.key = kStateKeyParameters;
        state.label = "Parameters";
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(core::serializeParameters(parameters_).c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;
        const std::string text = (value != nullptr) ? value : "";
        if (const auto loaded = core::deserializeParameters(text))
            parameters_ = *loaded;
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_.*kFields[index];

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::outDensity: return core::keyframesPerSecond(engineState_);
        case core::ParamId::outDrift: return core::playheadDrift(engineState_);
        case core::ParamId::outSplice: return core::spliceLamp(engineState_);
        case core::ParamId::outLatency:
            return static_cast<float>(core::reportedLatencySamples());
        default: return 0.0f;
        }
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // output parameters are read-only
        parameters_.*kFields[index] = value;
        parameters_ = core::clampParameters(parameters_);
    }

    void activate() override
    {
        core::activate(engineState_, getSampleRate());
        setLatency(static_cast<uint32_t>(core::reportedLatencySamples()));
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        core::activate(engineState_, newSampleRate);
    }

    void run(const float** inputs,
             float** outputs,
             const uint32_t frames,
             const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        handleMidi(midiEvents, midiEventCount);
        core::processBlock(engineState_, parameters_, frames, inputs, outputs);
    }

private:
    void handleMidi(const MidiEvent* const events, const uint32_t count)
    {
        if (events == nullptr)
            return;
        for (uint32_t i = 0; i < count; ++i)
        {
            const MidiEvent& event = events[i];
            if (event.size < 3)
                continue;
            if ((event.data[0] & 0xF0) != 0xB0)
                continue;
            core::ParamId target = core::ParamId::time;
            if (!core::controllerTarget(event.data[1], target))
                continue;
            setParameterValue(static_cast<uint32_t>(target),
                              core::controllerToParameter(target, event.data[2]));
        }
    }

    CoreParameters parameters_;
    CoreState engineState_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyframePlugin)
};

Plugin* createPlugin()
{
    return new KeyframePlugin();
}

END_NAMESPACE_DISTRHO