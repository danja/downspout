#include "DistrhoPlugin.hpp"

#include "sprout_core.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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
    SproutPlugin() : Plugin(core::kParameterCount, 0, 1)
    {
        core::prepare();
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            values_[i] = core::kParameterSpecs[i].defaultValue;
        applyGrammar(core::presetRules(core::kPresetCustom));
    }

    ~SproutPlugin() override { delete active_.load(); }

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

    // The one piece of non-parameter data: the text of the Custom grammar.
    void initState(uint32_t index, State& state) override
    {
        if (index != 0) return;
        state.key = kGrammarKey;
        state.label = "Custom grammar";
        state.defaultValue = core::presetRules(core::kPresetCustom);
        state.hints = kStateIsHostWritable;
    }

    String getState(const char* key) const override
    {
        if (key != nullptr && std::strcmp(key, kGrammarKey) == 0) return String(grammarText_.c_str());
        return String();
    }

    // Text that does not parse is ignored, so the grammar in force is always a valid one.
    void setState(const char* key, const char* value) override
    {
        if (key != nullptr && value != nullptr && std::strcmp(key, kGrammarKey) == 0) applyGrammar(value);
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

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        // Incoming notes and CC: held notes for the Pitch source, and the two CC sets, which
        // write straight into the parameter values (like magneto) so panel, automation and
        // controller agree on what the plugin is using.
        std::array<downspout::generative::MidiEvent, 256> input {};
        const std::uint32_t inputCount = std::min<std::uint32_t>(midiEventCount, static_cast<std::uint32_t>(input.size()));
        for (std::uint32_t i = 0; i < inputCount; ++i) {
            const std::uint32_t length = std::min<std::uint32_t>(midiEvents[i].size, 4);
            const std::uint8_t* data = midiEvents[i].size > MidiEvent::kDataSize ? midiEvents[i].dataExt : midiEvents[i].data;
            input[i].frame = midiEvents[i].frame;
            input[i].size = static_cast<std::uint8_t>(length);
            std::copy_n(data, length, input[i].data.begin());
        }
        core::handleMidi(state_, values_, input.data(), inputCount);

        // Take the grammar for this block, announcing it in inUse_ so applyGrammar() does not
        // free it underneath us; the re-check covers a swap between the load and the store.
        const core::CustomGrammar* grammar = nullptr;
        do {
            grammar = active_.load();
            inUse_.store(grammar);
        } while (grammar != active_.load());
        const auto block = core::process(state_, values_, toTransport(getTimePosition()), frames, getSampleRate(), grammar);
        inUse_.store(nullptr);
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
    static constexpr const char* kGrammarKey = "grammar";

    // Builds the grammar off the audio thread and swaps it in. The old one is freed once the
    // audio thread is not using it (on a later call, if it is right now).
    void applyGrammar(const char* text)
    {
        auto parsed = core::parseCustom(text);
        if (!parsed.grammar) return;
        grammarText_ = parsed.grammar->text;
        const core::CustomGrammar* old = active_.exchange(parsed.grammar.release());
        if (old != nullptr) retired_.emplace_back(old);
        const core::CustomGrammar* busy = inUse_.load();
        retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                      [busy](const std::unique_ptr<const core::CustomGrammar>& g) { return g.get() != busy; }),
                       retired_.end());
    }

    std::atomic<const core::CustomGrammar*> active_ {nullptr};
    std::atomic<const core::CustomGrammar*> inUse_ {nullptr};
    std::vector<std::unique_ptr<const core::CustomGrammar>> retired_;  // main thread only
    std::string grammarText_;                                            // main thread only
    std::array<float, core::kParameterCount> values_ {};
    core::State state_ {};
    std::atomic<int> length_ {0};
    std::atomic<int> generation_ {0};
    std::atomic<int> step_ {0};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SproutPlugin)
};

Plugin* createPlugin() { return new SproutPlugin(); }

END_NAMESPACE_DISTRHO
