#include "DistrhoPlugin.hpp"

#include "fresco_core.hpp"
#include "fresco_params.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::fresco;

// Fresco: the controls are ordinary automatable parameters (saved by symbol). The picture is the one non-parameter datum: its
// file path is a DPF state, so a project reopens with the same picture. The file is decoded in setState on the host's main
// thread and handed to the audio thread as an immutable shared_ptr. The playhead, the column level and "loaded" are output
// parameters written by the audio thread (or, for "loaded", setState) that the panel reads.
class FrescoPlugin : public Plugin
{
public:
    FrescoPlugin() : Plugin(core::kParameterCount, 0, 1)
    {
        core::resetState(engine_);
        setLatency(core::kLatency);
        std::atomic_store(&pending_, std::make_shared<const core::Image>(core::defaultPicture()));
        for (auto& m : meters_) m.store(0.0f);
    }

protected:
    const char* getLabel() const override       { return "Fresco"; }
    const char* getDescription() const override { return "Paints a picture into the audio's spectrogram: a short-time Fourier transform replaces the spectrum with the picture's column under a moving playhead (height is frequency, brightness is level), keeping the input's phase or using a clean tone bank. Loads PNG, PGM/PPM and BMP files."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR, DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('F', 'r', 's', 'c'); }

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

    void initState(uint32_t, State& state) override
    {
        state.key = core::kStateKeyPicture;
        state.label = "Picture";
        state.hints = kStateIsFilenamePath;
        state.defaultValue = "";
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= core::kParameterCount) return 0.0f;
        if (core::isOutput(index)) return meters_[index - core::kStatusPlayhead].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::isOutput(index)) return;   // outputs are written by the audio side only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        return std::strcmp(key, core::kStateKeyPicture) == 0 ? String(path_.c_str()) : String();
    }

    // Host main thread. An empty or unreadable path falls back to the built-in picture so the plugin never goes silent for want of a file.
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, core::kStateKeyPicture) != 0) return;
        path_ = value != nullptr ? value : "";
        std::shared_ptr<const core::Image> next;
        bool ok = false;
        if (!path_.empty()) {
            core::DecodeResult r = core::loadImageFile(path_);
            if (r.ok) {
                next = std::make_shared<const core::Image>(std::move(r.image));
                ok = true;
            }
        }
        if (!next) next = std::make_shared<const core::Image>(core::defaultPicture());
        loaded_.store(ok);
        std::atomic_store(&pending_, std::move(next));
    }

    // Clears the running state only (frame buffers, playhead). The picture and the settings are untouched.
    void activate() override
    {
        core::resetState(engine_);
        for (std::size_t i = 0; i < meters_.size() - 1; ++i) meters_[i].store(0.0f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        if (auto next = std::atomic_load(&pending_); next != picture_) picture_ = std::move(next);

        const TimePosition& tp = getTimePosition();
        core::Transport transport;
        transport.playing = tp.playing;
        transport.valid = tp.bbt.valid && tp.bbt.beatsPerMinute > 0.0;
        if (transport.valid) {
            transport.bpm = tp.bbt.beatsPerMinute;
            transport.beatPosition = static_cast<double>(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + static_cast<double>(tp.bbt.beat - 1)
                                   + (tp.bbt.ticksPerBeat > 0.0 ? tp.bbt.tick / tp.bbt.ticksPerBeat : 0.0);
        }
        core::processBlock(engine_, parameters_, picture_.get(), static_cast<float>(getSampleRate()), frames, transport, inputs[0], inputs[1], outputs[0], outputs[1]);
        meters_[0].store(engine_.meters.playhead);
        meters_[1].store(engine_.meters.level);
        meters_[2].store(loaded_.load() ? 1.0f : 0.0f);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::string path_;
    std::shared_ptr<const core::Image> pending_;      // written by setState, read by run (atomic_load/atomic_store)
    std::shared_ptr<const core::Image> picture_;      // the audio thread's copy
    std::atomic<bool> loaded_ {false};
    std::array<std::atomic<float>, 3> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrescoPlugin)
};

Plugin* createPlugin()
{
    return new FrescoPlugin();
}

END_NAMESPACE_DISTRHO
