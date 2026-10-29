#include "DistrhoPlugin.hpp"

#include "infra_core.hpp"
#include "infra_params.hpp"

#include <array>
#include <atomic>

START_NAMESPACE_DISTRHO

namespace core = downspout::infra;

// An LFO driving a stereo VCA, locked to the host's beat. The controls are ordinary automatable parameters, which DPF
// saves by symbol, so there is no plugin state. The LFO rate and the two channels' LFO values are output parameters,
// measured from the audio, that the panel reads.
class InfraPlugin : public Plugin
{
public:
    InfraPlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        for (auto& m : meters_) m.store(0.0f);
    }

protected:
    const char* getLabel() const override       { return "Infra"; }
    const char* getDescription() const override { return "A stereo VCA driven by a beat-locked LFO: a whole multiple of the host's beat rate between 1 and 20 Hz, eight waveforms, and a 0/90/180/270 degree phase for each channel."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR, DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('I', 'n', 'f', 'r'); }

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
        if (core::isOutput(index)) return meters_[index - core::kStatusRate].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::isOutput(index)) return;   // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // Clears the gain smoother and free-running phase only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
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
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, transport, inputs, outputs);
        meters_[0].store(engine_.meters.rateHz);
        meters_[1].store(engine_.meters.multiple);
        meters_[2].store(engine_.meters.lfo[0]);
        meters_[3].store(engine_.meters.lfo[1]);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::array<std::atomic<float>, 4> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InfraPlugin)
};

Plugin* createPlugin()
{
    return new InfraPlugin();
}

END_NAMESPACE_DISTRHO
