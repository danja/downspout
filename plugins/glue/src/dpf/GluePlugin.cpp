#include "DistrhoPlugin.hpp"

#include "glue_core.hpp"
#include "glue_params.hpp"

#include <atomic>

START_NAMESPACE_DISTRHO

namespace core = downspout::glue;

// Stereo-linked bus compressor. The controls are ordinary automatable parameters, which DPF
// saves by symbol, so there is no plugin state. Gain reduction and detector level are output
// parameters the panel reads; they are measured from the audio.
class GluePlugin : public Plugin
{
public:
    GluePlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
    }

protected:
    const char* getLabel() const override       { return "Glue"; }
    const char* getDescription() const override { return "Stereo-linked bus compressor with soft knee, sidechain high-pass, peak/RMS detector and program-dependent auto release."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('G', 'l', 'u', 'e'); }

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
        parameter.hints  = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)     parameter.hints |= kParameterIsInteger;
        if (spec.logarithmic) parameter.hints |= kParameterIsLogarithmic;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case core::kStatusReduction: return reduction_.load();
        case core::kStatusLevel:     return level_.load();
        default:
            return index < core::kControlCount ? core::field(parameters_, index) : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kControlCount)
            return;  // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // Clears the detector and gain memory only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        reduction_.store(0.0f);
        level_.store(-120.0f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, inputs, outputs);
        reduction_.store(engine_.meters.reductionDb);
        level_.store(engine_.meters.levelDb);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::atomic<float> reduction_ {0.0f};
    std::atomic<float> level_ {-120.0f};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GluePlugin)
};

Plugin* createPlugin()
{
    return new GluePlugin();
}

END_NAMESPACE_DISTRHO
