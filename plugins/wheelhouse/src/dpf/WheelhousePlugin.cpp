#include "DistrhoPlugin.hpp"

#include "wheelhouse_core.hpp"
#include "wheelhouse_params.hpp"

#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::wheelhouse;

// A tonewheel organ. Every control is an ordinary automatable parameter, which DPF saves by symbol, so there is no plugin
// state. The keys held, the tone wheels' phases and the percussion are runtime state that activate() clears.
class WheelhousePlugin : public Plugin
{
public:
    WheelhousePlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        core::resetState(engine_);
        midi_.reserve(2048);
    }

protected:
    const char* getLabel() const override       { return "Wheelhouse"; }
    const char* getDescription() const override { return "A tonewheel organ: 91 tone wheels, nine drawbars for each of the upper and lower manuals, two for the pedals, percussion, scanner vibrato and chorus, key click and overdrive. Upper, lower and pedals answer their own MIDI channels."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR, DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('W', 'h', 'l', 'h'); }

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
        parameter.hints  = kParameterIsAutomatable;
        if (spec.integer) parameter.hints |= kParameterIsInteger;
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
        if (index >= core::kParameterCount) return;
        core::setField(parameters_, index, value);
        parameters_ = core::clampParameters(parameters_);
    }

    // Releases the keys and restarts the wheels and effects only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        midi_.clear();
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size < 1 || e.size > MidiEvent::kDataSize) continue;
            core::MidiEvent m;
            m.frame = e.frame;
            m.status = e.data[0];
            m.data1 = e.size > 1 ? e.data[1] : 0;
            m.data2 = e.size > 2 ? e.data[2] : 0;
            midi_.push_back(m);
        }
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, midi_.data(),
                           static_cast<uint32_t>(midi_.size()), outputs[0], outputs[1]);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::vector<core::MidiEvent> midi_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WheelhousePlugin)
};

Plugin* createPlugin()
{
    return new WheelhousePlugin();
}

END_NAMESPACE_DISTRHO
