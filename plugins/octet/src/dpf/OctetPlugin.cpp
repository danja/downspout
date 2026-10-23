#include "DistrhoPlugin.hpp"

#include "octet_core.hpp"
#include "octet_params.hpp"

START_NAMESPACE_DISTRHO

namespace core = downspout::octet;

// Eight-band parametric equalizer. Every control is an ordinary automatable host parameter,
// which DPF saves by symbol, so there is no plugin state.
class OctetPlugin : public Plugin
{
public:
    OctetPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
    }

protected:
    const char* getLabel() const override       { return "Octet"; }
    const char* getDescription() const override { return "Eight-band parametric equalizer with 48 dB Butterworth cuts, stereo, left/right and mid/side modes and per-band channel assignment."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('O', 'c', 't', 't'); }

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
        const auto& spec = core::specs()[index];
        parameter.name   = spec.name;
        parameter.symbol = spec.symbol;
        parameter.unit   = spec.unit;
        parameter.hints  = kParameterIsAutomatable;
        if (spec.integer)     parameter.hints |= kParameterIsInteger;
        if (spec.logarithmic) parameter.hints |= kParameterIsLogarithmic;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < core::kParameterCount ? core::field(parameters_, index) : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount)
            return;
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // Clears the filter memory only. The patch lives in the parameters.
    void activate() override { core::resetState(engine_); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, inputs, outputs);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctetPlugin)
};

Plugin* createPlugin()
{
    return new OctetPlugin();
}

END_NAMESPACE_DISTRHO
