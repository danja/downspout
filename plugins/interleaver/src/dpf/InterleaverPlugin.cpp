#include "DistrhoPlugin.hpp"

#include "interleaver_engine.hpp"
#include "interleaver_params.hpp"

#include <memory>

START_NAMESPACE_DISTRHO

namespace core = downspout::interleaver;

// Two stereo inputs (A on 1/2, B on 3/4) become one stereo output. Every control is an ordinary automatable
// host parameter, which DPF saves by symbol, so there is no plugin state. The two status outputs (tracked
// pitch and cycle length) are read-only parameters for the panel. Both modes are delayed by the same fixed
// latency, which is reported to the host.
class InterleaverPlugin : public Plugin
{
public:
    InterleaverPlugin()
        : Plugin(core::kParameterCount, 0, 0),
          engine_(std::make_unique<core::Engine>())
    {
        engine_->prepare(static_cast<float>(getSampleRate()));
        setLatency(core::kLatencySamples);
    }

protected:
    const char* getLabel() const override       { return "Interleaver"; }
    const char* getDescription() const override { return "Interleaves two stereo inputs into one stereo output: time-domain slicing (free or locked to the tracked pitch of either input) or phase-vocoder bin interleaving."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('I', 'l', 'v', 'R'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (input && index >= 2) {
            // Input B is a second bus so hosts can route it separately from A.
            port.groupId = 100;
            port.hints  |= kAudioPortIsSidechain;
            port.name    = index == 2 ? "B Left" : "B Right";
            port.symbol  = index == 2 ? "b_l" : "b_r";
            return;
        }
        port.groupId = kPortGroupStereo;
        if (input) {
            port.name   = index == 0 ? "A Left" : "A Right";
            port.symbol = index == 0 ? "a_l" : "a_r";
        } else {
            port.name   = index == 0 ? "Output Left" : "Output Right";
            port.symbol = index == 0 ? "out_l" : "out_r";
        }
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount)
            return;
        const auto& spec = core::kParameterSpecs[index];
        parameter.name   = spec.name;
        parameter.symbol = spec.symbol;
        parameter.unit   = spec.unit;
        parameter.hints  = (spec.output ? kParameterIsOutput : kParameterIsAutomatable) |
                           (spec.logarithmic ? kParameterIsLogarithmic : 0) |
                           (spec.integer ? kParameterIsInteger : 0) | (spec.boolean ? kParameterIsBoolean : 0);
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == core::kDetectedHz)
            return status_.detectedHz;
        if (index == core::kCycleMs)
            return status_.cycleMs;
        if (index >= core::kParameterCount)
            return 0.0f;
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kFirstOutputParameter)
            return;
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // Clears the signal memory only. The patch lives in the parameters and is not touched.
    void activate() override
    {
        engine_->reset();
    }

    void sampleRateChanged(double sampleRate) override
    {
        engine_->prepare(static_cast<float>(sampleRate));
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        engine_->process(parameters_, frames, inputs, outputs);
        status_ = engine_->status();
    }

private:
    core::Parameters parameters_ {};
    core::Status status_ {};
    std::unique_ptr<core::Engine> engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InterleaverPlugin)
};

Plugin* createPlugin()
{
    return new InterleaverPlugin();
}

END_NAMESPACE_DISTRHO
