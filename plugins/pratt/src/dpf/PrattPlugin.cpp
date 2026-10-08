#include "DistrhoPlugin.hpp"

#include "pratt_engine.hpp"
#include "pratt_params.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::pratt;

constexpr const char* kStateKeySettings = "settings";

enum StateIndex : uint32_t {
    kStateSettings = 0,
    kStateCount
};

// Builds caches off the audio thread so the first note never pays for them and
// the host is never made to wait for it.
//
// Rules this class keeps:
//  * the audio thread only ever try_locks a mutex the worker holds for a few
//    instructions, never waits, and never signals a condition variable;
//  * the worker polls (wait_for) instead of being woken, so nothing on the audio
//    side makes a system call for it;
//  * work is cut into small steps (one pitch, 64 root indices, one drum filter),
//    and between steps the worker checks for shutdown or a newer request, then
//    sleeps 1 ms so it never hogs a core. A superseded or abandoned preload stops
//    within about one step, so destruction (host close, scan, sample-rate change)
//    joins promptly;
//  * a voice request has priority over the background work (roots, drum filters).
class Warmer {
public:
    explicit Warmer(core::Engine& engine)
        : engine_(engine), thread_([this] { run(); }) {}

    ~Warmer()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        wake_.notify_one();
        thread_.join();
    }

    // Audio-thread safe. Returns false if the worker held the lock this instant;
    // the caller simply asks again on a later block.
    bool request(const int preset, const core::VoiceTuning& tuning)
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        preset_ = preset;
        tuning_ = tuning;
        pending_ = true;
        return true;
    }

private:
    bool interrupted()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return quit_ || pending_;
    }

    void pause(const std::chrono::milliseconds length)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, length, [this] { return quit_; });
    }

    void buildVoice(const int preset, const core::VoiceTuning& tuning)
    {
        engine_.trimTables(tuning);
        // Middle outwards: the notes most likely to be played first.
        for (int step = 0; step <= 48; ++step)
        {
            for (const int pitch : {60 + step, 60 - step})
            {
                if (step == 0 && pitch != 60 + step)
                    continue;
                if (pitch < 12 || pitch > 108)
                    continue;
                if (interrupted())
                    return;
                engine_.preload(preset, tuning, pitch, pitch);
                pause(std::chrono::milliseconds(1));
            }
        }
    }

    void run()
    {
        int nextRoot = 1;
        int nextDrum = 27;
        for (;;)
        {
            int preset = 0;
            core::VoiceTuning tuning;
            bool haveRequest = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (quit_)
                    return;
                if (pending_)
                {
                    preset = preset_;
                    tuning = tuning_;
                    pending_ = false;
                    haveRequest = true;
                }
            }

            if (haveRequest)
            {
                buildVoice(preset, tuning);
                continue;
            }
            if (nextDrum <= 87)
            {
                engine_.preloadDrum(nextDrum++);
                pause(std::chrono::milliseconds(1));
            }
            else if (nextRoot <= core::kMaxIndex)
            {
                core::Engine::warmRoots(nextRoot, nextRoot + 63);
                nextRoot += 64;
                pause(std::chrono::milliseconds(1));
            }
            else
            {
                pause(std::chrono::milliseconds(20));  // idle: poll for the next request
            }
        }
    }

    core::Engine& engine_;
    std::mutex mutex_;
    std::condition_variable wake_;
    int preset_ = 0;
    core::VoiceTuning tuning_;
    bool pending_ = false;
    bool quit_ = false;
    std::thread thread_;  // last: starts after the other members exist
};

}  // namespace

class PrattPlugin : public Plugin {
public:
    PrattPlugin()
        : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, kStateCount)
    {
        startEngine(getSampleRate());
    }

    ~PrattPlugin() override { warmer_.reset(); }

protected:
    const char* getLabel() const override { return "Pratt"; }

    const char* getDescription() const override
    {
        return "Number-theoretic synthesizer and filter built from Pratt polynomials: "
               "eleven instrument voices, a stable all-pole Pratt filter with two cascading "
               "index controls, or both together.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('P', 'r', 'a', 't'); }

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
        if (index >= core::kParameterCount)
            return;

        const core::ParamSpec& spec = core::kParameterSpecs[index];

        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (spec.logarithmic)
            parameter.hints |= kParameterIsLogarithmic;

        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;

        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::mode:
        {
            static ParameterEnumerationValue values[3];
            for (int i = 0; i < 3; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kModeNames[static_cast<std::size_t>(i)];
            }
            setEnumeration(parameter, values, 3);
            break;
        }

        case core::ParamId::preset:
        {
            static ParameterEnumerationValue values[core::kPresetCount + 1];
            for (std::size_t i = 0; i < core::kPresetCount + 1; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kVoiceNames[i];
            }
            setEnumeration(parameter, values, static_cast<uint32_t>(core::kPresetCount + 1));
            break;
        }

        case core::ParamId::percussion:
        {
            static ParameterEnumerationValue values[] = {{0.0f, "Off"}, {1.0f, "On"}};
            setEnumeration(parameter, values, 2);
            break;
        }

        default:
            break;
        }
    }

    void initState(const uint32_t index, State& state) override
    {
        if (index != kStateSettings)
            return;
        state.key = kStateKeySettings;
        state.label = "Settings";
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeySettings) == 0)
            return String(core::serializeSettings(settings_).c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeySettings) != 0)
            return;
        // Bad input is rejected without touching the current settings.
        if (const auto loaded = core::deserializeSettings(value != nullptr ? value : ""))
        {
            settings_ = *loaded;
            dirty_ = true;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return settings_.v[index];
        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::outIndex: return static_cast<float>(statusIndex_.load());
        case core::ParamId::outVoices: return static_cast<float>(statusVoices_.load());
        default: return 0.0f;
        }
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // output parameters are read-only
        settings_.v[index] = core::clampParameter(index, value);
        dirty_ = true;
    }

    // Hosts instantiate plugins just to scan them. No worker exists until the
    // plugin is actually activated, and it is joined again on deactivate.
    void activate() override
    {
        engine_->allSoundOff();
        activated_ = true;
        if (!warmer_)
            warmer_ = std::make_unique<Warmer>(*engine_);
        lastWarm_ = {-2, 0, 0.0, 0.0};
        dirty_ = true;
    }

    void deactivate() override
    {
        activated_ = false;
        warmer_.reset();  // cancels within one step and joins
    }

    void sampleRateChanged(const double newSampleRate) override { startEngine(newSampleRate); }

    void run(const float** inputs,
             float** outputs,
             const uint32_t frames,
             const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        if (dirty_)
            applySettings();
        else if (wantedPreset_ >= 0)
            warmVoice(wantedPreset_);

        uint32_t position = 0;
        for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const MidiEvent& event = midiEvents[i];
            const uint32_t at = event.frame < frames ? event.frame : frames;
            if (at > position)
            {
                renderSpan(inputs, outputs, position, at - position);
                position = at;
            }
            const uint8_t* data = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
            engine_->handleMidi(data, static_cast<int>(event.size));
            if ((data[0] & 0xF0) == 0xC0 && event.size >= 2 && settings_[core::ParamId::preset] < 0.5f)
                warmVoice(core::presetForProgram(data[1] & 0x7F));
        }
        if (frames > position)
            renderSpan(inputs, outputs, position, frames - position);

        statusVoices_.store(engine_->activeVoices());
    }

private:
    static void setEnumeration(Parameter& parameter, ParameterEnumerationValue* values, const uint32_t count)
    {
        parameter.enumValues.count = count;
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = values;
        parameter.enumValues.deleteLater = false;
    }

    void renderSpan(const float** inputs, float** outputs, const uint32_t offset, const uint32_t count)
    {
        engine_->process(inputs != nullptr ? inputs[0] + offset : nullptr,
                         inputs != nullptr && DISTRHO_PLUGIN_NUM_INPUTS > 1 ? inputs[1] + offset : nullptr,
                         outputs[0] + offset, outputs[1] + offset, static_cast<int>(count));
    }

    void startEngine(const double sampleRate)
    {
        warmer_.reset();  // joins the worker before the engine it references goes away
        engine_ = std::make_unique<core::Engine>(sampleRate);
        if (activated_)
            warmer_ = std::make_unique<Warmer>(*engine_);
        dirty_ = true;
        lastWarm_ = {-2, 0, 0.0, 0.0};
    }

    void applySettings()
    {
        dirty_ = false;
        engine_->setParams(core::toEngineParams(settings_));
        statusIndex_.store(core::effectiveIndex(settings_));
        if (settings_[core::ParamId::mode] < 0.5f || settings_[core::ParamId::mode] > 1.5f)
        {
            const int preset = static_cast<int>(settings_[core::ParamId::preset]) - 1;
            warmVoice(preset >= 0 ? preset : 0);
        }
    }

    // Ask the worker to build the tables the next notes will need.
    void warmVoice(const int preset)
    {
        const WarmKey key{preset, static_cast<int>(settings_[core::ParamId::base]),
                          static_cast<double>(settings_[core::ParamId::brightness]),
                          static_cast<double>(settings_[core::ParamId::darkness]),
                          static_cast<int>(settings_[core::ParamId::timbre])};
        if (!warmer_ || key == lastWarm_)
        {
            wantedPreset_ = -1;
            return;
        }
        // If the worker is busy this instant, run() asks again on the next block.
        if (warmer_->request(preset, {key.base, key.brightness, key.darkness, key.timbre}))
        {
            lastWarm_ = key;
            wantedPreset_ = -1;
        }
        else
        {
            wantedPreset_ = preset;
        }
    }

    struct WarmKey {
        int preset;
        int base;
        double brightness;
        double darkness;
        int timbre = 1;
        bool operator==(const WarmKey& o) const
        {
            return preset == o.preset && base == o.base && brightness == o.brightness && darkness == o.darkness &&
                   timbre == o.timbre;
        }
    };

    core::Settings settings_;
    std::unique_ptr<core::Engine> engine_;
    std::unique_ptr<Warmer> warmer_;
    WarmKey lastWarm_{-2, 0, 0.0, 0.0};
    bool dirty_ = true;
    bool activated_ = false;
    int wantedPreset_ = -1;  // a warm-up request that found the worker busy
    std::atomic<int> statusIndex_{35};
    std::atomic<int> statusVoices_{0};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrattPlugin)
};

Plugin* createPlugin()
{
    return new PrattPlugin();
}

END_NAMESPACE_DISTRHO
