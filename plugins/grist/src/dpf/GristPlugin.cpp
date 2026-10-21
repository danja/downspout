#include "DistrhoPlugin.hpp"

#include "grist_core.hpp"

#include <algorithm>
#include <array>

START_NAMESPACE_DISTRHO

namespace core = downspout::grist;

// Granular grain-delay effect. The history and feedback buffers are allocated in prepare(), so
// run() never allocates.
class GristPlugin : public Plugin {
public:
    GristPlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            values_[i] = core::kParameterSpecs[i].defaultValue;
        core::prepare(state_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Grist"; }
    const char* getDescription() const override { return "Granular grain-delay effect: windowed, pitch-scattered grains with a filtered feedback loop."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('G', 'r', 's', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        const auto& spec = core::kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case core::kStatusLatency: return state_.statusLatency;
        case core::kStatusGrains: return static_cast<float>(state_.statusGrains);
        case core::kStatusWindow: return state_.statusWindow;
        case core::kStatusDelay: return state_.statusDelay;
        default: return index < core::kParameterCount ? values_[index] : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < core::kParameterCount && !core::kParameterSpecs[index].output)
            values_[index] = downspout::generative::clampParam(value, core::kParameterSpecs[index]);
    }

    void sampleRateChanged(double sampleRate) override { core::prepare(state_, sampleRate); }

    // Clears what is sounding (history, grains, loop), never the settings.
    void activate() override { core::reset(state_); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const TimePosition& time = getTimePosition();
        const double bpm = time.bbt.valid && time.bbt.beatsPerMinute > 0.0 ? time.bbt.beatsPerMinute : 120.0;
        core::process(state_, values_, frames, getSampleRate(), bpm, inputs[0], inputs[1], outputs[0], outputs[1]);
    }

private:
    std::array<float, core::kParameterCount> values_ {};
    core::State state_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GristPlugin)
};

Plugin* createPlugin() { return new GristPlugin(); }

END_NAMESPACE_DISTRHO
