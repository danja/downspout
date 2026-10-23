#include "DistrhoPlugin.hpp"

#include "trio_core.hpp"
#include "trio_params.hpp"

#include <array>
#include <atomic>

START_NAMESPACE_DISTRHO

namespace core = downspout::trio;

// Three-band dynamics. The controls are ordinary automatable parameters, which DPF saves by
// symbol, so there is no plugin state. The per-band gain and level are output parameters,
// measured from the audio, that the panel reads.
class TrioPlugin : public Plugin
{
public:
    TrioPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        for (auto& m : meters_) m.store(0.0f);
        for (int b = 0; b < core::kBands; ++b) meters_[core::levelMeter(b) - core::kFirstMeter].store(-120.0f);
    }

protected:
    const char* getLabel() const override       { return "Trio"; }
    const char* getDescription() const override { return "Three-band downward and upward dynamics with adjustable crossovers, a linked detector per band and live gain meters."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('T', 'r', 'i', 'o'); }

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
        parameter.hints  = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)     parameter.hints |= kParameterIsInteger;
        if (spec.logarithmic) parameter.hints |= kParameterIsLogarithmic;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= core::kParameterCount)
            return 0.0f;
        if (index >= core::kFirstMeter)
            return meters_[index - core::kFirstMeter].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kControlCount)
            return;  // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // Clears the filter and detector memory only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
        for (int b = 0; b < core::kBands; ++b) meters_[core::levelMeter(b) - core::kFirstMeter].store(-120.0f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, inputs, outputs);
        for (int b = 0; b < core::kBands; ++b) {
            meters_[core::gainMeter(b) - core::kFirstMeter].store(engine_.meters.gainDb[static_cast<size_t>(b)]);
            meters_[core::levelMeter(b) - core::kFirstMeter].store(engine_.meters.levelDb[static_cast<size_t>(b)]);
        }
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::array<std::atomic<float>, 2 * core::kBands> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrioPlugin)
};

Plugin* createPlugin()
{
    return new TrioPlugin();
}

END_NAMESPACE_DISTRHO
