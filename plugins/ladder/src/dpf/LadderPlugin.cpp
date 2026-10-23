#include "DistrhoPlugin.hpp"

#include "ladder_core.hpp"
#include "ladder_params.hpp"

#include <algorithm>

START_NAMESPACE_DISTRHO

namespace core = downspout::ladder;

// Stereo ladder low-pass. Every control is an ordinary automatable host parameter, which
// DPF saves by symbol, so there is no plugin state to keep in step.
class LadderPlugin : public Plugin
{
public:
    LadderPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
    }

protected:
    const char* getLabel() const override       { return "Ladder"; }
    const char* getDescription() const override { return "Four-pole saturating low-pass ladder filter with self-oscillating resonance, bass compensation and Drift CC 1-4 control."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('L', 'a', 'd', 'r'); }

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
        parameter.hints  = kParameterIsAutomatable | (spec.logarithmic ? kParameterIsLogarithmic : 0) | (spec.integer ? kParameterIsInteger : 0);
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
        // The panel or the host has taken the control back from a controller.
        if (const int slot = core::liveSlot(index); slot >= 0) live_.value[static_cast<size_t>(slot)] = -1.0f;
    }

    // Clears the filter memory only. The patch lives in the parameters and is not touched.
    void activate() override
    {
        core::resetState(engine_);
        live_ = core::LiveControl {};
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        // CC 1-4 on the set channel override Cutoff, Resonance, Drive and Mix (the Drift convention). Applied at block start.
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size >= 3 && core::ccChannelMatches(e.data[0], parameters_.ccChannel))
                core::handleController(live_, e.data[1], e.data[2]);
        }
        const core::Parameters effective = core::withLive(parameters_, live_);
        core::processBlock(engine_, effective, static_cast<float>(getSampleRate()), frames, inputs, outputs);
    }

private:
    core::Parameters  parameters_ {};
    core::LiveControl live_ {};
    core::EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LadderPlugin)
};

Plugin* createPlugin()
{
    return new LadderPlugin();
}

END_NAMESPACE_DISTRHO
