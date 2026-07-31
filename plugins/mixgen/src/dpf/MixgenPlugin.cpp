#include "DistrhoPlugin.hpp"
#include "mixgen_core.hpp"

#include <array>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::mixgen;

Transport toCoreTransport(const TimePosition& position)
{
    Transport transport;
    transport.valid = position.bbt.valid;
    transport.playing = position.playing;
    if (transport.valid) {
        transport.bar = position.bbt.bar - 1;
        transport.barBeat = position.bbt.beat - 1
            + (position.bbt.ticksPerBeat > 0.0 ? position.bbt.tick / position.bbt.ticksPerBeat : 0.0);
        transport.beatsPerBar = position.bbt.beatsPerBar;
        transport.beatType = position.bbt.beatType;
        transport.bpm = position.bbt.beatsPerMinute;
    }
    return transport;
}

} // namespace

class MixgenPlugin : public Plugin {
public:
    MixgenPlugin()
        : Plugin(kParameterCount, 0, 0)
    {
        for (std::uint32_t index = 0; index < kParameterCount; ++index)
            parameters_[index] = kParameterSpecs[index].defaultValue;
    }

protected:
    const char* getLabel() const override { return "Mixgen"; }
    const char* getDescription() const override
    {
        return "Transport-synced random, quasi-random, and Euclidean producer for T-Mix.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    std::uint32_t getVersion() const override { return d_version(0, 1, 0); }
    std::int64_t getUniqueId() const override { return d_cconst('M', 'x', 'G', 'n'); }

    void initParameter(const std::uint32_t index, Parameter& parameter) override
    {
        const auto& spec = kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (index == kEnabled)
            parameter.hints |= kParameterIsBoolean;
        parameter.ranges = {spec.defaultValue, spec.minimum, spec.maximum};
    }

    float getParameterValue(const std::uint32_t index) const override
    {
        if (index == kStatusStep)
            return static_cast<float>(state_.statusStep);
        if (index == kStatusEvents)
            return static_cast<float>(state_.statusEvents);
        if (index >= kStatusGainBase && index < kParameterCount)
            return state_.gains[index - kStatusGainBase];
        return parameters_[index];
    }

    void setParameterValue(const std::uint32_t index, const float value) override
    {
        if (index < kParameterCount && !kParameterSpecs[index].output)
            parameters_[index] = downspout::generative::clampParam(value, kParameterSpecs[index]);
    }

    void activate() override { reset(state_); }

    void run(const float** inputs, float** outputs, const std::uint32_t frames) override
    {
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            outputs[0][frame] = inputs[0][frame];
            outputs[1][frame] = inputs[1][frame];
        }
        const MidiBlock block = process(state_, parameters_, toCoreTransport(getTimePosition()),
                                        frames, getSampleRate());
        for (std::uint32_t index = 0; index < block.count; ++index) {
            MidiEvent event {};
            event.frame = block.events[index].frame;
            event.size = block.events[index].size;
            event.data[0] = block.events[index].data[0];
            event.data[1] = block.events[index].data[1];
            event.data[2] = block.events[index].data[2];
            writeMidiEvent(event);
        }
    }

private:
    std::array<float, kParameterCount> parameters_ {};
    downspout::mixgen::State state_ {};
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixgenPlugin)
};

Plugin* createPlugin() { return new MixgenPlugin(); }

END_NAMESPACE_DISTRHO
