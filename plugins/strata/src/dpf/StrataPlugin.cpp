#include "DistrhoPlugin.hpp"

#include "strata_core.hpp"
#include "strata_params.hpp"

#include <array>

START_NAMESPACE_DISTRHO

namespace core = downspout::strata;

// Eight-voice wavetable synthesizer. Every control is an ordinary automatable host parameter,
// which DPF saves by symbol, so there is no plugin state.
class StrataPlugin : public Plugin
{
public:
    StrataPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        (void)core::bank();  // build the tables here, off the audio thread
    }

protected:
    const char* getLabel() const override       { return "Strata"; }
    const char* getDescription() const override { return "Eight-voice wavetable synthesizer with band-limited tables, warp, unison, filter, envelopes and an LFO."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 't', 'r', 't'); }

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

    // Silences the voices only. The patch lives in the parameters.
    void activate() override { core::resetState(engine_); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < midiEventCount && count < events_.size(); ++i) {
            const MidiEvent& in = midiEvents[i];
            if (in.size < 2) continue;
            const uint8_t status = in.data[0] & 0xF0;
            core::MidiEvent e;
            e.frame = in.frame;
            if (status == 0x90 && in.size >= 3)       { e.type = core::MidiEvent::Type::NoteOn;     e.data1 = in.data[1]; e.data2 = in.data[2]; }
            else if (status == 0x80)                  { e.type = core::MidiEvent::Type::NoteOff;    e.data1 = in.data[1]; }
            else if (status == 0xB0 && in.size >= 3)  { e.type = core::MidiEvent::Type::Controller; e.data1 = in.data[1]; e.data2 = in.data[2]; }
            else if (status == 0xE0 && in.size >= 3)  { e.type = core::MidiEvent::Type::PitchBend;  e.bend = (static_cast<int>(in.data[2]) << 7 | in.data[1]) - 8192; }
            else continue;
            events_[count++] = e;
        }
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, events_.data(), count, outputs[0], outputs[1]);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::array<core::MidiEvent, 512> events_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StrataPlugin)
};

Plugin* createPlugin()
{
    return new StrataPlugin();
}

END_NAMESPACE_DISTRHO
