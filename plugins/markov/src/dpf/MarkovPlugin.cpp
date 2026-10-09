#include "DistrhoPlugin.hpp"

#include "markov_core.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::markov;

constexpr const char* kStateKeyModel = "model";  // the whole model: the matrix, and what was learned
constexpr const char* kStateKeyEdit = "edit";    // editor -> plugin: the matrix drawn on screen (144 digits)
constexpr std::uint32_t kStateModel = 0;
constexpr std::uint32_t kStateEdit = 1;
constexpr std::uint32_t kStateCount = 2;

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

// Markov-chain melody generator. The matrix, and anything learned from the MIDI input, are plugin state;
// the settings are ordinary parameters.
class MarkovPlugin : public Plugin {
public:
    MarkovPlugin() : Plugin(core::kParameterCount, 0, kStateCount)
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            params_[i] = core::kParameterSpecs[i].defaultValue;
        model_ = core::defaultModel();
    }

protected:
    const char* getLabel() const override { return "Markov"; }
    const char* getDescription() const override { return "Markov-chain melody generator with a visible, editable and learnable transition matrix."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'r', 'k', 'v'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        const auto& spec = core::kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (index == core::kClearLearned)
            parameter.hints |= kParameterIsTrigger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStateModel) {
            static const std::string defaultModelText = core::serializeModel(core::defaultModel());
            state.key = kStateKeyModel;
            state.label = "Model";
            state.defaultValue = defaultModelText.c_str();
        } else if (index == kStateEdit) {
            // Editor to plugin only; nothing to save (the matrix is saved with the model).
            state.key = kStateKeyEdit;
            state.label = "Edit";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case core::kStatusState: return static_cast<float>(statusState_.load());
        case core::kStatusLearned: return static_cast<float>(std::min<std::uint32_t>(learnedTotal_.load(), 1000000u));
        default: return index < core::kParameterCount ? params_[index] : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < core::kParameterCount && !core::kParameterSpecs[index].output)
            params_[index] = downspout::generative::clampParam(value, core::kParameterSpecs[index]);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyModel) == 0)
            return String(core::serializeModel(model_).c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (value == nullptr)
            return;
        if (std::strcmp(key, kStateKeyModel) == 0) {
            // A malformed state is ignored rather than half-applied.
            if (auto model = core::deserializeModel(value)) {
                model_ = *model;
                learnedTotal_.store(model_.learnedTotal);
            }
        } else if (std::strcmp(key, kStateKeyEdit) == 0) {
            // The editor sends only the matrix it shows, so it cannot overwrite what was learned meanwhile.
            const std::string text = value;
            if (text.size() == core::kCells) {
                for (std::size_t i = 0; i < text.size(); ++i)
                    if (text[i] >= '0' && text[i] <= '0' + core::kMaxWeight)
                        model_.base[i] = static_cast<std::uint8_t>(text[i] - '0');
            }
        }
    }

    void activate() override
    {
        core::reset(state_);
        framesSincePush_ = 0;
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        // Incoming MIDI: notes to learn from, and the Conductor CCs.
        std::array<downspout::generative::MidiEvent, 256> input {};
        const std::uint32_t inputCount = std::min<std::uint32_t>(midiEventCount, static_cast<std::uint32_t>(input.size()));
        for (std::uint32_t i = 0; i < inputCount; ++i) {
            const std::uint32_t length = std::min<std::uint32_t>(midiEvents[i].size, 4);
            const std::uint8_t* data = midiEvents[i].size > MidiEvent::kDataSize ? midiEvents[i].dataExt : midiEvents[i].data;
            input[i].frame = midiEvents[i].frame;
            input[i].size = static_cast<std::uint8_t>(length);
            std::copy_n(data, length, input[i].data.begin());
        }
        if (core::handleMidi(state_, model_, params_, input.data(), inputCount)) {
            learnedDirty_ = true;
            learnedTotal_.store(model_.learnedTotal);
        }

        const auto block = core::process(state_, model_, params_, toTransport(getTimePosition()), frames, getSampleRate());
        for (std::uint32_t i = 0; i < block.count; ++i) {
            MidiEvent ev {};
            ev.frame = block.events[i].frame;
            ev.size = 3;
            ev.data[0] = block.events[i].data[0];
            ev.data[1] = block.events[i].data[1];
            ev.data[2] = block.events[i].data[2];
            writeMidiEvent(ev);
        }
        statusState_.store(state_.statusState);

        // Tell the editor what has been learned, a few times a second at most.
        framesSincePush_ += frames;
        if (learnedDirty_ && framesSincePush_ >= static_cast<std::uint32_t>(getSampleRate() * 0.25)) {
            framesSincePush_ = 0;
            learnedDirty_ = false;
            updateStateValue(kStateKeyModel, String(core::serializeModel(model_).c_str()));
        }
    }

private:
    core::Params params_ {};
    core::Model model_ {};
    core::State state_ {};
    std::atomic<int> statusState_ {-1};
    std::atomic<std::uint32_t> learnedTotal_ {0};
    bool learnedDirty_ = false;
    std::uint32_t framesSincePush_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkovPlugin)
};

Plugin* createPlugin() { return new MarkovPlugin(); }

END_NAMESPACE_DISTRHO
