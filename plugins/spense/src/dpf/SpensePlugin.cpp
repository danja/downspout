#include "DistrhoPlugin.hpp"

#include "spense_core.hpp"
#include "spense_params.hpp"

START_NAMESPACE_DISTRHO

namespace core = downspout::spense;

// Stereo phase disperser. Every control is an ordinary automatable host parameter, which DPF
// saves by symbol, so there is no plugin state to keep in step. The one thing the host must be
// told is the latency: seven samples per stacked cascade.
class SpensePlugin : public Plugin
{
public:
    SpensePlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        updateLatency();
    }

protected:
    const char* getLabel() const override       { return "Spense"; }
    const char* getDescription() const override { return "Stereo phase disperser: up to eight cascades of eight all-pass filters that smear the phase of a signal without changing its level."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'p', 'n', 's'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_")       + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount)
            return;
        const auto& spec = core::kParameterSpecs[index];
        parameter.name   = spec.name;
        parameter.symbol = spec.symbol;
        parameter.unit   = spec.unit;
        parameter.hints  = kParameterIsAutomatable | (spec.logarithmic ? kParameterIsLogarithmic : 0) |
                           (spec.integer ? kParameterIsInteger : 0) | (spec.boolean ? kParameterIsBoolean : 0);
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= core::kParameterCount)
            return 0.0f;
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount)
            return;
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        if (index == core::kStack) updateLatency();
    }

    // Clears the filter memory only. The patch lives in the parameters and is not touched.
    void activate() override
    {
        core::resetState(engine_);
        updateLatency();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, inputs, outputs);
    }

private:
    void updateLatency() { setLatency(static_cast<uint32_t>(core::latencySamples(parameters_))); }

    core::Parameters  parameters_ {};
    core::EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpensePlugin)
};

Plugin* createPlugin()
{
    return new SpensePlugin();
}

END_NAMESPACE_DISTRHO
