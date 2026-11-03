#include "DistrhoPlugin.hpp"

#include "spindown_core.hpp"
#include "spindown_params.hpp"

#include <array>
#include <atomic>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::spindown;

// A tape-stop effect. The controls are ordinary automatable parameters, which DPF saves by symbol, so there is no plugin
// state. The tape speed and lag are output parameters, measured from the audio, that the panel reads. A CC on the CC
// channel takes a control over until the panel or host moves that parameter again.
class SpindownPlugin : public Plugin
{
public:
    SpindownPlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        core::prepare(engine_, static_cast<float>(getSampleRate()));
        for (auto& m : meters_) m.store(0.0f);
        meters_[0].store(1.0f);
        midi_.reserve(256);
    }

protected:
    const char* getLabel() const override       { return "Spindown"; }
    const char* getDescription() const override { return "Tape stop and start: the tape slows to a standstill, pitch and time falling together, then spins back up. Trigger it by hand, from held MIDI notes, or on a bar cycle locked to the host."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR, DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'p', 'd', 'n'); }

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
        if (index == core::kStopTime || index == core::kStartTime) parameter.hints |= kParameterIsLogarithmic;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= core::kParameterCount) return 0.0f;
        if (core::isOutput(index)) return meters_[index - core::kStatusSpeed].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::isOutput(index)) return;   // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        core::releaseOverride(engine_, core::overrideIndex(index));
    }

    // Clears the tape and any held notes only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
        meters_[0].store(1.0f);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        core::prepare(engine_, static_cast<float>(newSampleRate));
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
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

        const TimePosition& tp = getTimePosition();
        core::Transport transport;
        transport.playing = tp.playing;
        transport.valid = tp.bbt.valid && tp.bbt.beatsPerMinute > 0.0;
        if (transport.valid) {
            transport.bpm = tp.bbt.beatsPerMinute;
            transport.beatsPerBar = tp.bbt.beatsPerBar;
            // Beats from the song start: whole bars, then the beat in the bar, then the tick within it.
            transport.beatPosition = static_cast<double>(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + static_cast<double>(tp.bbt.beat - 1)
                                   + (tp.bbt.ticksPerBeat > 0.0 ? tp.bbt.tick / tp.bbt.ticksPerBeat : 0.0);
        }
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, transport,
                           midi_.data(), static_cast<uint32_t>(midi_.size()), inputs, outputs);
        meters_[0].store(engine_.meters.speed);
        meters_[1].store(engine_.meters.delayMs);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::vector<core::MidiEvent> midi_;
    std::array<std::atomic<float>, 2> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpindownPlugin)
};

Plugin* createPlugin()
{
    return new SpindownPlugin();
}

END_NAMESPACE_DISTRHO
