#include "DistrhoPlugin.hpp"

#include "silken_core.hpp"
#include "silken_params.hpp"

#include <array>
#include <atomic>

START_NAMESPACE_DISTRHO

namespace core = downspout::silken;

// A vocal clean-up and polish strip. The controls are ordinary automatable parameters, which DPF saves by symbol, so there is no plugin
// state. The de-esser and plosive gain reduction and the detector level are output parameters, measured from the audio, that the panel reads.
class SilkenPlugin : public Plugin
{
public:
    SilkenPlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
    }

protected:
    const char* getLabel() const override       { return "Silken"; }
    const char* getDescription() const override { return "A vocal clean-up and polish strip: rumble filter, plosive guard, de-esser (band or wide, with listen), warmth / presence / air EQ, sparkle and output level. No latency."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR, DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'l', 'k', 'n'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = kPortGroupStereo;
        port.name    = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol  = String(input ? "in_" : "out_")       + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount) return;
        const auto& spec = core::specs()[index];
        parameter.name   = spec.name;
        parameter.symbol = spec.symbol;
        parameter.unit   = spec.unit;
        parameter.hints  = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer) parameter.hints |= kParameterIsInteger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= core::kParameterCount) return 0.0f;
        if (core::isOutput(index)) return meters_[index - core::kStatusEss].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::isOutput(index)) return;   // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // Clears filters and detectors only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
    }

    void sampleRateChanged(double) override { core::resetState(engine_); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, inputs[0], inputs[1], outputs[0], outputs[1]);
        meters_[0].store(engine_.meters.essReductionDb);
        meters_[1].store(engine_.meters.plosiveReductionDb);
        meters_[2].store(engine_.meters.detectorDb);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::array<std::atomic<float>, 3> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilkenPlugin)
};

Plugin* createPlugin()
{
    return new SilkenPlugin();
}

END_NAMESPACE_DISTRHO
