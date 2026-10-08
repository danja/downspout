#include "DistrhoPlugin.hpp"

#include "retune_engine.hpp"
#include "retune_params.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::retune;

constexpr const char* kStateKeyScale = "scale_file";

}  // namespace

// MIDI in, retuned MIDI out. Notes become a pitch bend plus a note-on on a
// per-note channel, so the receiving synth must honour per-channel pitch bend at
// the configured range (MPE-style). The audio outputs are silent.
class RetunePlugin : public Plugin {
public:
    RetunePlugin()
        : Plugin(core::kParameterCount, 0, 1)
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            values_[i] = core::kParameterSpecs[i].defaultValue;
        out_.reserve(kMaxOut);
        std::atomic_store(&pending_, std::make_shared<const core::Scale>(core::equalTemperament()));
        applied_ = std::atomic_load(&pending_);
        engine_.setScale(applied_);
        engine_.setSettings(core::toSettings(values_[core::kRoot], values_[core::kBendRange]));
    }

protected:
    const char* getLabel() const override { return "Retune"; }
    const char* getDescription() const override
    {
        return "Retunes MIDI notes to a Scala (.scl) scale using one pitch bend per note.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 't', 'u', 'n'); }

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

    void initState(uint32_t, State& state) override
    {
        state.key = kStateKeyScale;
        state.label = "Scale file";
        state.hints = kStateIsFilenamePath;
        state.defaultValue = "";
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case core::kStatusDegrees: return static_cast<float>(degrees_.load());
        case core::kStatusNotes: return static_cast<float>(notes_.load());
        case core::kStatusLoaded: return loaded_.load() ? 1.0f : 0.0f;
        default: return index < core::kParameterCount ? values_[index] : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::kParameterSpecs[index].output)
            return;
        values_[index] = downspout::generative::clampParam(value, core::kParameterSpecs[index]);
        dirty_.store(true);
    }

    String getState(const char* key) const override
    {
        return std::strcmp(key, kStateKeyScale) == 0 ? String(path_.c_str()) : String();
    }

    // Runs on the host's main thread, never the audio thread. The scale is parsed
    // here and handed to the audio thread as an immutable shared_ptr.
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyScale) != 0)
            return;
        path_ = value != nullptr ? value : "";
        std::shared_ptr<const core::Scale> next;
        bool ok = false;
        if (!path_.empty()) {
            if (auto scale = core::loadSclFile(path_)) {
                next = std::make_shared<const core::Scale>(std::move(*scale));
                ok = true;
            }
        }
        if (!next)
            next = std::make_shared<const core::Scale>(core::equalTemperament());
        degrees_.store(next->size());
        loaded_.store(ok);
        std::atomic_store(&pending_, std::move(next));
    }

    void activate() override
    {
        std::vector<core::MidiOut> release;
        engine_.allNotesOff(release);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* events, uint32_t eventCount) override
    {
        std::fill(outputs[0], outputs[0] + frames, 0.0f);
        std::fill(outputs[1], outputs[1] + frames, 0.0f);

        if (dirty_.exchange(false))
            engine_.setSettings(core::toSettings(values_[core::kRoot], values_[core::kBendRange]));
        auto scale = std::atomic_load(&pending_);
        if (scale != applied_) {
            // Notes sounding under the old scale keep their carriers; release them
            // rather than leave bends that no longer describe the scale.
            out_.clear();
            engine_.allNotesOff(out_);
            flush(0);
            engine_.setScale(scale);
            applied_ = std::move(scale);
        }

        for (uint32_t i = 0; i < eventCount; ++i) {
            const MidiEvent& ev = events[i];
            if (ev.size < 2 || ev.size > MidiEvent::kDataSize)
                continue;  // system exclusive and anything unusual is dropped
            const int status = ev.data[0];
            const int data1 = ev.data[1];
            const int data2 = ev.size > 2 ? ev.data[2] : 0;
            const int channel = status & 0xF;
            out_.clear();
            switch (status >> 4) {
            case 0x9: engine_.noteOn(channel, data1, data2, out_); break;
            case 0x8: engine_.noteOff(channel, data1, out_); break;
            default: engine_.other(status, data1, data2, out_); break;
            }
            flush(ev.frame);
        }
        notes_.store(engine_.activeNotes());
    }

private:
    static constexpr std::size_t kMaxOut = 8192;

    void flush(const uint32_t frame)
    {
        for (const core::MidiOut& m : out_) {
            MidiEvent ev {};
            ev.frame = frame;
            ev.size = (m.status >> 4) == 0xC ? 2 : 3;
            ev.data[0] = m.status;
            ev.data[1] = m.data1;
            ev.data[2] = m.data2;
            writeMidiEvent(ev);
        }
        out_.clear();
    }

    std::array<float, core::kParameterCount> values_ {};
    core::Engine engine_;
    std::vector<core::MidiOut> out_;
    std::shared_ptr<const core::Scale> pending_;
    std::shared_ptr<const core::Scale> applied_;
    std::string path_;
    std::atomic<bool> dirty_ {false};
    std::atomic<bool> loaded_ {false};
    std::atomic<int> degrees_ {12};
    std::atomic<int> notes_ {0};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RetunePlugin)
};

Plugin* createPlugin() { return new RetunePlugin(); }

END_NAMESPACE_DISTRHO
