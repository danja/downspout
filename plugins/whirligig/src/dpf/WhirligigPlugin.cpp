#include "DistrhoPlugin.hpp"

#include "whirligig_core.hpp"
#include "whirligig_params.hpp"

#include <array>
#include <atomic>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::whirligig;

// A rotary speaker. The controls are ordinary automatable parameters, which DPF saves by symbol, so there is no plugin
// state. The two rotor speeds and angles and the sync lock are output parameters, measured from the audio, that the panel
// reads. A CC on the CC channel takes Speed over until the panel or host moves that parameter again.
class WhirligigPlugin : public Plugin
{
public:
    WhirligigPlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
        midi_.reserve(1024);
    }

protected:
    const char* getLabel() const override       { return "Whirligig"; }
    const char* getDescription() const override { return "A rotary speaker: a crossover splits the sound between a horn and a drum rotor, each with its own speed and inertia, picked up by two microphones for amplitude and Doppler. Free-running or locked to the host's tempo and bar position."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR, DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('W', 'h', 'r', 'l'); }

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
        if (core::isOutput(index)) return meters_[index - core::kStatusHornHz].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::isOutput(index)) return;   // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        if (index == core::kSpeed) core::releaseOverride(engine_);
    }

    // Stops the rotors and clears the filters and delay lines only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
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
            // Beats from the song start: whole bars, then the beat in the bar, then the tick within it.
            transport.beatPosition = static_cast<double>(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + static_cast<double>(tp.bbt.beat - 1)
                                   + (tp.bbt.ticksPerBeat > 0.0 ? tp.bbt.tick / tp.bbt.ticksPerBeat : 0.0);
        }
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, transport, midi_.data(),
                           static_cast<uint32_t>(midi_.size()), inputs[0], inputs[1], outputs[0], outputs[1]);
        meters_[0].store(engine_.meters.hornHz);
        meters_[1].store(engine_.meters.drumHz);
        meters_[2].store(engine_.meters.hornTurn);
        meters_[3].store(engine_.meters.drumTurn);
        meters_[4].store(engine_.meters.locked);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::vector<core::MidiEvent> midi_;
    std::array<std::atomic<float>, 5> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WhirligigPlugin)
};

Plugin* createPlugin()
{
    return new WhirligigPlugin();
}

END_NAMESPACE_DISTRHO
