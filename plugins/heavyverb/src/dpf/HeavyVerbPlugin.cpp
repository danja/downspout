#include "DistrhoPlugin.hpp"

#include "heavyverb_core.hpp"
#include "heavyverb_params.hpp"

#include <memory>

START_NAMESPACE_DISTRHO

namespace core = downspout::heavyverb;

// Mastering reverb. Every control, the preset number and Freeze included, is an ordinary automatable host
// parameter, which DPF saves by symbol, so there is no plugin state to keep in step. The dry path is not
// processed, so there is no latency to report. Presets are applied by the panel, not here.
class HeavyVerbPlugin : public Plugin
{
public:
    HeavyVerbPlugin()
        : Plugin(core::kParameterCount, 0, 0),
          engine_(core::createEngine(static_cast<float>(getSampleRate())))
    {
    }

protected:
    const char* getLabel() const override       { return "HeavyVerb"; }
    const char* getDescription() const override { return "High-quality stereo reverb for the master bus: one to six dimensions, each with its own wall distance, damping and feedback filter, plus shimmer, swell, duck, gate and freeze, and fifteen presets."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('H', 'v', 'V', 'b'); }

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
    }

    // Clears the reverb memory only. The patch lives in the parameters and is not touched.
    void activate() override
    {
        core::resetEngine(*engine_);
    }

    // A new sample rate changes every delay length, so the buffers are reallocated and cleared.
    void sampleRateChanged(double sampleRate) override
    {
        core::prepareEngine(*engine_, static_cast<float>(sampleRate));
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        core::processBlock(*engine_, parameters_, frames, inputs, outputs);
    }

private:
    core::Parameters parameters_ {};
    std::unique_ptr<core::EngineState, core::EngineDeleter> engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeavyVerbPlugin)
};

Plugin* createPlugin()
{
    return new HeavyVerbPlugin();
}

END_NAMESPACE_DISTRHO
