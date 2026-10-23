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
    const char* getDescription() const override { return "Three-band downward and upward dynamics with adjustable crossovers, a linked detector per band, live gain meters and Drift CC 1-4 control."; }
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
        if (input && index >= 2) {
            // Inputs 3 and 4 are the optional sidechain (the Sidechain control chooses whether the detectors listen to it).
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
        if (index >= core::kFirstMeter && index < core::kFirstMeter + core::kMeterCount)
            return meters_[index - core::kFirstMeter].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || (index >= core::kFirstMeter && index < core::kFirstMeter + core::kMeterCount))
            return;  // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        // The panel or the host has taken the control back from a controller.
        if (const int slot = core::liveSlot(index); slot >= 0) live_.active[static_cast<size_t>(slot)] = false;
    }

    // Clears the filter and detector memory only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        live_ = core::LiveControl {};
        for (auto& m : meters_) m.store(0.0f);
        for (int b = 0; b < core::kBands; ++b) meters_[core::levelMeter(b) - core::kFirstMeter].store(-120.0f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        // CC 1-4 on the set channel override Amount, Time, Input and Upward limit (the Drift convention). Applied at block start.
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size >= 3 && core::ccChannelMatches(e.data[0], parameters_.ccChannel))
                core::handleController(live_, e.data[1], e.data[2]);
        }
        const core::Parameters effective = core::withLive(parameters_, live_);
        core::processBlock(engine_, effective, static_cast<float>(getSampleRate()), frames, inputs, outputs, inputs + 2);
        for (int b = 0; b < core::kBands; ++b) {
            meters_[core::gainMeter(b) - core::kFirstMeter].store(engine_.meters.gainDb[static_cast<size_t>(b)]);
            meters_[core::levelMeter(b) - core::kFirstMeter].store(engine_.meters.levelDb[static_cast<size_t>(b)]);
        }
    }

private:
    core::Parameters  parameters_ {};
    core::LiveControl live_ {};
    core::EngineState engine_ {};
    std::array<std::atomic<float>, 2 * core::kBands> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrioPlugin)
};

Plugin* createPlugin()
{
    return new TrioPlugin();
}

END_NAMESPACE_DISTRHO
