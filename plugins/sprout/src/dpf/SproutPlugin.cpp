#include "DistrhoPlugin.hpp"

#include "sprout_core.hpp"

#include <algorithm>
#include <array>
#include <atomic>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::sprout;

core::Transport toTransport(const TimePosition& x)
{
    core::Transport t;
    t.valid = x.bbt.valid;
    t.playing = x.playing;
    if (t.valid) {
        t.bar = x.bbt.bar - 1;
        t.barBeat = x.bbt.beat - 1 + (x.bbt.ticksPerBeat > 0 ? x.bbt.tick / x.bbt.ticksPerBeat : 0);
        t.beatsPerBar = x.bbt.beatsPerBar;
        t.beatType = x.bbt.beatType;
        t.bpm = x.bbt.beatsPerMinute;
    }
    return t;
}

}  // namespace

// L-system MIDI generator. The grammar tables are built in the constructor, so
// run() only reads them.
class SproutPlugin : public Plugin {
public:
    SproutPlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        core::prepare();
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            values_[i] = core::kParameterSpecs[i].defaultValue;
    }

protected:
    const char* getLabel() const override { return "Sprout"; }
    const char* getDescription() const override { return "L-system MIDI generator: grammars grow into melodies."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'p', 'r', 't'); }

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
        case core::kStatusLength: return static_cast<float>(length_.load());
        case core::kStatusGeneration: return static_cast<float>(generation_.load());
        case core::kStatusStep: return static_cast<float>(step_.load());
        default: return index < core::kParameterCount ? values_[index] : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < core::kParameterCount && !core::kParameterSpecs[index].output)
            values_[index] = downspout::generative::clampParam(value, core::kParameterSpecs[index]);
    }

    void activate() override { core::reset(state_); }

    void run(const float**, float** outputs, uint32_t frames) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);
        const auto block = core::process(state_, values_, toTransport(getTimePosition()), frames, getSampleRate());
        for (std::uint32_t i = 0; i < block.count; ++i) {
            MidiEvent ev {};
            ev.frame = block.events[i].frame;
            ev.size = 3;
            ev.data[0] = block.events[i].data[0];
            ev.data[1] = block.events[i].data[1];
            ev.data[2] = block.events[i].data[2];
            writeMidiEvent(ev);
        }
        length_.store(state_.statusLength);
        generation_.store(state_.statusGeneration);
        step_.store(state_.statusStep);
    }

private:
    std::array<float, core::kParameterCount> values_ {};
    core::State state_ {};
    std::atomic<int> length_ {0};
    std::atomic<int> generation_ {0};
    std::atomic<int> step_ {0};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SproutPlugin)
};

Plugin* createPlugin() { return new SproutPlugin(); }

END_NAMESPACE_DISTRHO
