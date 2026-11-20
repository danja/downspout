#include "DistrhoPlugin.hpp"

#include "fontana_engine.hpp"
#include "fontana_params.hpp"
#include "fontana_sample_loader.hpp"
#include "fontana_serialization.hpp"
#include "fontana_slices.hpp"
#include "fontana_soundfont.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::fontana;

constexpr std::size_t kPreviewBufferFrames = 512;

[[nodiscard]] float paramToFloat(const std::uint32_t index, const Parameters& p) noexcept
{
    switch (index) {
    case kParamMasterVolume: return p.masterVolume;
    case kParamMidiChannel: return p.midiChannel;
    case kParamBank: return p.bank;
    case kParamProgram: return p.program;
    case kParamTranspose: return p.transpose;
    case kParamBendRange: return p.pitchBendRange;
    case kParamAttack: return p.attackMs;
    case kParamRelease: return p.releaseMs;
    case kParamSourceMode: return p.sourceMode;
    case kParamSliceBaseNote: return p.sliceBaseNote;
    case kParamSliceLoop: return p.sliceLoop;
    case kParamFilterEnabled: return p.filterEnabled;
    case kParamFilterCutoff: return p.filterCutoff;
    case kParamStatusBank: return p.statusBank;
    case kParamStatusProgram: return p.statusProgram;
    case kParamStatusVoices: return p.statusVoices;
    case kParamStatusLastNote: return p.statusLastNote;
    case kParamStatusLoad: return p.statusLoad;
    default: return 0.0f;
    }
}

void setParamFromFloat(const std::uint32_t index, const float value, Parameters& p) noexcept
{
    switch (index) {
    case kParamMasterVolume: p.masterVolume = value; break;
    case kParamMidiChannel: p.midiChannel = value; break;
    case kParamBank: p.bank = value; break;
    case kParamProgram: p.program = value; break;
    case kParamTranspose: p.transpose = value; break;
    case kParamBendRange: p.pitchBendRange = value; break;
    case kParamAttack: p.attackMs = value; break;
    case kParamRelease: p.releaseMs = value; break;
    case kParamSourceMode: p.sourceMode = value; break;
    case kParamSliceBaseNote: p.sliceBaseNote = value; break;
    case kParamSliceLoop: p.sliceLoop = value; break;
    case kParamFilterEnabled: p.filterEnabled = value; break;
    case kParamFilterCutoff: p.filterCutoff = value; break;
    default: break;
    }
}

bool isOutputParameter(const std::uint32_t index) noexcept
{
    return index >= kParamStatusBank && index < kParameterCount;
}

struct SourcePaths {
    std::string soundFont;
    std::string sample;
};

}  // namespace

class FontanaPlugin : public Plugin
{
public:
    FontanaPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = clampParameters(parameters_);
        engine_.previewSerial = &previewSerial_;
        previewEngine_.previewSerial = &previewSerial_;
    }

protected:
    const char* getLabel() const override { return "Fontana"; }
    const char* getDescription() const override
    {
        return "SF2/SF3 SoundFont player with WAV/Ogg sample loading, slice mapping, and an auditioning file browser.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('F', 'n', 't', 'n'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        parameter.hints = isOutputParameter(index)
                              ? kParameterIsOutput | kParameterIsInteger
                              : kParameterIsAutomatable;

        switch (index) {
        case kParamMasterVolume:
            parameter.name = "Master Volume";
            parameter.symbol = "master_volume";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.8f;
            break;
        case kParamMidiChannel:
            parameter.name = "MIDI Channel";
            parameter.symbol = "midi_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamBank:
            parameter.name = "Bank";
            parameter.symbol = "bank";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamProgram:
            parameter.name = "Preset";
            parameter.symbol = "program";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamTranspose:
            parameter.name = "Transpose";
            parameter.symbol = "transpose";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = -24.0f;
            parameter.ranges.max = 24.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamBendRange:
            parameter.name = "Bend Range";
            parameter.symbol = "bend_range";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 24.0f;
            parameter.ranges.def = 2.0f;
            break;
        case kParamAttack:
            parameter.name = "Attack";
            parameter.symbol = "attack";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2000.0f;
            parameter.ranges.def = 4.0f;
            break;
        case kParamRelease:
            parameter.name = "Release";
            parameter.symbol = "release";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 5000.0f;
            parameter.ranges.def = 120.0f;
            break;
        case kParamSourceMode:
            parameter.name = "Source";
            parameter.symbol = "source_mode";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamSliceBaseNote:
            parameter.name = "Slice Base Note";
            parameter.symbol = "slice_base_note";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 108.0f;
            parameter.ranges.def = 36.0f;
            break;
        case kParamSliceLoop:
            parameter.name = "Slice Loop";
            parameter.symbol = "slice_loop";
            parameter.hints |= kParameterIsInteger | kParameterIsBoolean;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamFilterEnabled:
            parameter.name = "Filter Enable";
            parameter.symbol = "filter_enabled";
            parameter.hints |= kParameterIsInteger | kParameterIsBoolean;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamFilterCutoff:
            parameter.name = "Filter Cutoff";
            parameter.symbol = "filter_cutoff";
            parameter.hints |= kParameterIsLogarithmic;
            parameter.ranges.min = 100.0f;
            parameter.ranges.max = 20000.0f;
            parameter.ranges.def = 20000.0f;
            break;
        case kParamStatusBank:
            parameter.name = "Effective Bank";
            parameter.symbol = "status_bank";
            parameter.ranges.min = -1.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = -1.0f;
            break;
        case kParamStatusProgram:
            parameter.name = "Effective Preset";
            parameter.symbol = "status_program";
            parameter.ranges.min = -1.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = -1.0f;
            break;
        case kParamStatusVoices:
            parameter.name = "Active Voices";
            parameter.symbol = "status_voices";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(downspout::fontana::kMaxVoices);
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusLastNote:
            parameter.name = "Last Note";
            parameter.symbol = "status_last_note";
            parameter.ranges.min = -1.0f;
            parameter.ranges.max = 127.0f;
            parameter.ranges.def = -1.0f;
            break;
        case kParamStatusLoad:
            parameter.name = "Load Status";
            parameter.symbol = "status_load";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 3.0f;
            parameter.ranges.def = 0.0f;
            break;
        }
    }

    void initState(const uint32_t index, State& state) override
    {
        switch (index) {
        case kStateSettings:
            state.key = kStateKeySettings;
            state.label = "Settings";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
            break;
        case kStateSoundFontPath:
            state.key = kStateKeySoundFontPath;
            state.label = "SoundFont Path";
            state.hints = kStateIsFilenamePath;
            state.defaultValue = "";
            break;
        case kStateSamplePath:
            state.key = kStateKeySamplePath;
            state.label = "Sample Path";
            state.hints = kStateIsFilenamePath;
            state.defaultValue = "";
            break;
        case kStateSliceMap:
            state.key = kStateKeySliceMap;
            state.label = "Slice Map";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
            break;
        case kStatePreviewPath:
            state.key = kStateKeyPreviewPath;
            state.label = "Preview Path";
            state.hints = kStateIsFilenamePath;
            state.defaultValue = "";
            break;
        case kStatePreviewTrigger:
            state.key = kStateKeyPreviewTrigger;
            state.label = "Preview Trigger";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "0";
            break;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return paramToFloat(index, parameters_);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (isOutputParameter(index))
            return;
        setParamFromFloat(index, value, parameters_);
        parameters_ = clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeySettings) == 0)
            return String(serializeSettings(parameters_).c_str());
        if (std::strcmp(key, kStateKeySoundFontPath) == 0)
            return String(paths_.soundFont.c_str());
        if (std::strcmp(key, kStateKeySamplePath) == 0)
            return String(paths_.sample.c_str());
        if (std::strcmp(key, kStateKeySliceMap) == 0)
            return String(serializeSliceSet(sliceSnapshot()).c_str());
        if (std::strcmp(key, kStateKeyPreviewPath) == 0)
            return String(previewPath_.c_str());
        if (std::strcmp(key, kStateKeyPreviewTrigger) == 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u", previewSerial_.load(std::memory_order_relaxed));
            return String(buf);
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";
        if (std::strcmp(key, kStateKeySettings) == 0) {
            const auto restored = deserializeSettings(text);
            if (restored.has_value()) {
                Parameters next = parameters_;
                next.masterVolume = restored->masterVolume;
                next.bank = restored->bank;
                next.program = restored->program;
                next.pitchBendRange = restored->pitchBendRange;
                next.transpose = restored->transpose;
                next.sliceBaseNote = restored->sliceBaseNote;
                parameters_ = clampParameters(next);
            }
            return;
        }
        if (std::strcmp(key, kStateKeySoundFontPath) == 0) {
            loadSoundFontPath(text);
            return;
        }
        if (std::strcmp(key, kStateKeySamplePath) == 0) {
            loadSamplePath(text);
            return;
        }
        if (std::strcmp(key, kStateKeySliceMap) == 0) {
            loadSliceMap(text);
            return;
        }
        if (std::strcmp(key, kStateKeyPreviewPath) == 0) {
            previewPath_ = text;
            loadPreviewPath(text);
            return;
        }
        if (std::strcmp(key, kStateKeyPreviewTrigger) == 0) {
            unsigned long serial = std::strtoul(text.c_str(), nullptr, 10);
            previewSerial_.store(static_cast<std::uint32_t>(serial), std::memory_order_release);
            return;
        }
    }

    void activate() override
    {
        downspout::fontana::activate(engine_, getSampleRate());
        downspout::fontana::activate(previewEngine_, getSampleRate());
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        downspout::fontana::activate(engine_, newSampleRate);
        downspout::fontana::activate(previewEngine_, newSampleRate);
    }

    void run(const float** inputs, float** outputs, const uint32_t frames,
             const MidiEvent* midiEvents, const uint32_t midiEventCount) override
    {
        (void)inputs;
        std::array<MidiInputEvent, 256> coreEvents {};
        std::uint32_t coreCount = 0;
        for (std::uint32_t i = 0; i < midiEventCount && coreCount < coreEvents.size(); ++i) {
            const MidiEvent& event = midiEvents[i];
            if (event.size == 0 || event.size > 4)
                continue;
            MidiInputEvent& core = coreEvents[coreCount++];
            core.frame = event.frame;
            core.size = static_cast<std::uint8_t>(event.size);
            std::memcpy(core.data, event.data, event.size);
        }

        std::array<float*, 2> mainOutputs { outputs[0], outputs[1] };
        AudioBlock mainAudio { { mainOutputs[0], mainOutputs[1] }, 2 };
        const std::shared_ptr<const SoundFont> font =
            std::atomic_load_explicit(&fontBank_, std::memory_order_acquire);
        const std::shared_ptr<const SampleSource> sample =
            std::atomic_load_explicit(&sampleBank_, std::memory_order_acquire);
        const std::shared_ptr<const SliceSet> slices =
            std::atomic_load_explicit(&sliceBank_, std::memory_order_acquire);

        processBlock(engine_, parameters_, font.get(), sample.get(), slices.get(),
                     0u, coreEvents.data(), coreCount, frames, getSampleRate(), mainAudio);

        if (frames > 0 && frames <= kPreviewBufferFrames) {
            const std::shared_ptr<const SampleSource> preview =
                std::atomic_load_explicit(&previewBank_, std::memory_order_acquire);
            std::fill_n(previewLeft_.begin(), static_cast<std::size_t>(frames), 0.0f);
            std::fill_n(previewRight_.begin(), static_cast<std::size_t>(frames), 0.0f);
            std::array<float*, 2> previewOutputs { previewLeft_.data(), previewRight_.data() };
            AudioBlock previewAudio { { previewOutputs[0], previewOutputs[1] }, 2 };
            processBlock(previewEngine_, previewParameters_, font.get(), preview.get(), nullptr,
                         previewSerial_.load(std::memory_order_acquire), nullptr, 0u,
                         frames, getSampleRate(), previewAudio);
            if (outputs[0] != nullptr)
                for (std::uint32_t i = 0; i < frames; ++i)
                    outputs[0][i] += previewLeft_[static_cast<std::size_t>(i)];
            if (outputs[1] != nullptr)
                for (std::uint32_t i = 0; i < frames; ++i)
                    outputs[1][i] += previewRight_[static_cast<std::size_t>(i)];
        }
    }

private:
    [[nodiscard]] SliceSet sliceSnapshot() const
    {
        const std::shared_ptr<const SliceSet> slices =
            std::atomic_load_explicit(&sliceBank_, std::memory_order_acquire);
        return slices != nullptr ? *slices : SliceSet {};
    }

    void setLoadStatus(const float status)
    {
        parameters_.statusLoad = status;
    }

    void loadSoundFontPath(const std::string& path)
    {
        paths_.soundFont = path;
        if (path.empty()) {
            std::atomic_store_explicit(&fontBank_, std::shared_ptr<const SoundFont> {},
                                       std::memory_order_release);
            setLoadStatus(0.0f);
            return;
        }
        setLoadStatus(1.0f);
        SoundFontLoadResult loaded = loadSoundFont(path);
        if (!loaded.error.empty()) {
            loadError_ = loaded.error;
            std::atomic_store_explicit(&fontBank_, std::shared_ptr<const SoundFont> {},
                                       std::memory_order_release);
            setLoadStatus(3.0f);
            return;
        }
        loadError_.clear();
        std::atomic_store_explicit(&fontBank_,
                                   std::make_shared<const SoundFont>(std::move(loaded.bank)),
                                   std::memory_order_release);
        setLoadStatus(2.0f);
    }

    void loadSamplePath(const std::string& path)
    {
        paths_.sample = path;
        if (path.empty()) {
            std::atomic_store_explicit(&sampleBank_, std::shared_ptr<const SampleSource> {},
                                       std::memory_order_release);
            std::atomic_store_explicit(&sliceBank_, std::shared_ptr<const SliceSet> {},
                                       std::memory_order_release);
            setLoadStatus(0.0f);
            return;
        }
        setLoadStatus(1.0f);
        SampleLoadResult loaded = loadSampleSource(path);
        if (!loaded.error.empty()) {
            loadError_ = loaded.error;
            std::atomic_store_explicit(&sampleBank_, std::shared_ptr<const SampleSource> {},
                                       std::memory_order_release);
            setLoadStatus(3.0f);
            return;
        }
        loadError_.clear();
        const auto sample = std::make_shared<const SampleSource>(std::move(loaded.source));
        std::atomic_store_explicit(&sampleBank_, sample, std::memory_order_release);
        SliceSet slices = equalSlices(*sample, 8, static_cast<int>(std::lround(parameters_.sliceBaseNote)));
        slices.sourcePath = path;
        slices.frameCount = sample->frameCount();
        slices.sampleRate = sample->sampleRate;
        std::atomic_store_explicit(&sliceBank_, std::make_shared<const SliceSet>(std::move(slices)),
                                   std::memory_order_release);
        setLoadStatus(2.0f);
    }

    void loadSliceMap(const std::string& text)
    {
        if (text.empty()) {
            std::atomic_store_explicit(&sliceBank_, std::shared_ptr<const SliceSet> {},
                                       std::memory_order_release);
            return;
        }
        const auto restored = deserializeSliceSet(text);
        if (!restored.has_value())
            return;
        std::atomic_store_explicit(&sliceBank_,
                                   std::make_shared<const SliceSet>(*restored),
                                   std::memory_order_release);
    }

    void loadPreviewPath(const std::string& path)
    {
        if (path.empty()) {
            std::atomic_store_explicit(&previewBank_, std::shared_ptr<const SampleSource> {},
                                       std::memory_order_release);
            return;
        }
        SampleLoadResult loaded = loadPreviewSource(path);
        if (!loaded.error.empty()) {
            std::atomic_store_explicit(&previewBank_, std::shared_ptr<const SampleSource> {},
                                       std::memory_order_release);
            return;
        }
        std::atomic_store_explicit(&previewBank_,
                                   std::make_shared<const SampleSource>(std::move(loaded.source)),
                                   std::memory_order_release);
    }

    Parameters parameters_ {};
    Parameters previewParameters_ {};
    EngineState engine_ {};
    EngineState previewEngine_ {};
    SourcePaths paths_ {};
    std::string previewPath_ {};
    std::string loadError_ {};
    std::shared_ptr<const SoundFont> fontBank_ {};
    std::shared_ptr<const SampleSource> sampleBank_ {};
    std::shared_ptr<const SliceSet> sliceBank_ {};
    std::shared_ptr<const SampleSource> previewBank_ {};
    std::atomic<std::uint32_t> previewSerial_ {0u};
    std::array<float, kPreviewBufferFrames> previewLeft_ {};
    std::array<float, kPreviewBufferFrames> previewRight_ {};
};

Plugin* createPlugin()
{
    return new FontanaPlugin();
}

END_NAMESPACE_DISTRHO
