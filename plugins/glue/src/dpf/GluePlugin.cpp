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
    const char* getDescription() const override { return "Stereo-linked bus compressor with soft knee, sidechain input and high-pass, peak/RMS detector, program-dependent auto release and Drift CC 1-4 control."; }
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
        if (input && index >= 2) {
            // Inputs 3 and 4 are the optional sidechain (the Sidechain control chooses whether the detector listens to it).
            port.groupId = 100;
            port.hints  |= kAudioPortIsSidechain;
            port.name    = index == 2 ? "Sidechain L" : "Sidechain R";
            port.symbol  = index == 2 ? "sc_l" : "sc_r";
            return;
        }
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_")       + String(static_cast<int>(index + 1));
    }

    void initPortGroup(const uint32_t groupId, PortGroup& portGroup) override
    {
        if (groupId == 100) {
            portGroup.name   = "Sidechain";
            portGroup.symbol = "sidechain";
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
            return core::field(parameters_, index);
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::kParameterSpecs[index].output)
            return;  // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        if (index == core::kLookahead) updateLatency();
        // The panel or the host has taken the control back from a controller.
        if (const int slot = core::liveSlot(index); slot >= 0) live_.active[static_cast<size_t>(slot)] = false;
    }

    // Clears the detector and gain memory only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        updateLatency();
        live_ = core::LiveControl {};
        reduction_.store(0.0f);
        level_.store(-120.0f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        // CC 1-4 on the set channel override Threshold, Ratio, Makeup and Mix (the Drift convention). Applied at block start.
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size >= 3 && core::ccChannelMatches(e.data[0], parameters_.ccChannel))
                core::handleController(live_, e.data[1], e.data[2]);
        }
        const core::Parameters effective = core::withLive(parameters_, live_);
        core::processBlock(engine_, effective, static_cast<float>(getSampleRate()), frames, inputs, outputs, inputs + 2);
        reduction_.store(engine_.meters.reductionDb);
        level_.store(engine_.meters.levelDb);
    }

    void sampleRateChanged(double) override { updateLatency(); }

private:
    // The look-ahead delay is the plugin's latency; tell the host so it compensates.
    void updateLatency() { setLatency(static_cast<uint32_t>(core::lookaheadSamples(parameters_, static_cast<float>(getSampleRate())))); }

    core::Parameters  parameters_ {};
    core::LiveControl live_ {};
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
