#include "DistrhoPlugin.hpp"

#include "melevolve_core.hpp"
#include "melevolve_messages.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::melevolve;
namespace messages = downspout::melevolve::messages;

constexpr std::uint32_t kStateSession = 0;
constexpr std::uint32_t kStateAction = 1;
constexpr std::uint32_t kStateCount = 2;

core::Transport toTransport(const TimePosition& position)
{
    core::Transport result;
    result.valid = position.bbt.valid;
    result.playing = position.playing;
    if (result.valid) {
        result.bar = static_cast<double>(position.bbt.bar - 1);
        result.barBeat = static_cast<double>(position.bbt.beat - 1)
            + (position.bbt.ticksPerBeat > 0.0
                   ? static_cast<double>(position.bbt.tick / position.bbt.ticksPerBeat)
                   : 0.0);
        result.beatsPerBar = position.bbt.beatsPerBar;
        result.beatType = position.bbt.beatType;
        result.bpm = position.bbt.beatsPerMinute;
    }
    return result;
}

float clamped(const std::uint32_t index, const float value)
{
    const auto& spec = core::kParameterSpecs[static_cast<std::size_t>(index)];
    return std::clamp(value, spec.minimum, spec.maximum);
}

}  // namespace

class MelevolvePlugin : public Plugin {
public:
    MelevolvePlugin() : Plugin(core::kParameterCount, 0, kStateCount)
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            parameters_[static_cast<std::size_t>(i)] = core::kParameterSpecs[static_cast<std::size_t>(i)].defaultValue;
        syncRuntimeSettings();
    }

protected:
    const char* getLabel() const override { return "Melevolve"; }
    const char* getDescription() const override
    {
        return "Melody evolution as a live MIDI generator, with five voted rows, an internal clock, MIDI Clock and host BBT sync.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    std::uint32_t getVersion() const override { return d_version(0, 1, 0); }
    std::int64_t getUniqueId() const override { return d_cconst('M', 'l', 'v', 'E'); }

    void initParameter(const std::uint32_t index, Parameter& parameter) override
    {
        const auto& spec = core::kParameterSpecs[static_cast<std::size_t>(index)];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    void initState(const std::uint32_t index, State& state) override
    {
        if (index == kStateSession) {
            static const std::string defaultSession = core::serializeSession(core::Session {});
            state.key = messages::kSession;
            state.label = "Session";
            state.defaultValue = defaultSession.c_str();
        } else if (index == kStateAction) {
            state.key = messages::kAction;
            state.label = "Action";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(const std::uint32_t index) const override
    {
        return index < core::kParameterCount ? parameters_[static_cast<std::size_t>(index)] : 0.0f;
    }

    void setParameterValue(const std::uint32_t index, const float value) override
    {
        if (index > core::kSendClock || value != value)
            return;
        parameters_[static_cast<std::size_t>(index)] = clamped(index, value);
        syncFromParameters();
    }

    String getState(const char* const key) const override
    {
        if (std::strcmp(key, messages::kSession) == 0)
            return String(core::serializeSession(session_).c_str());
        return String();
    }

    void setState(const char* const key, const char* const value) override
    {
        if (value == nullptr)
            return;
        if (std::strcmp(key, messages::kSession) == 0) {
            if (const auto session = core::deserializeSession(value)) {
                session_ = *session;
                resetRuntime();
                syncParametersFromSession();
            }
        } else if (std::strcmp(key, messages::kAction) == 0) {
            core::applyAction(session_, engine_, core::parseAction(value));
            syncParametersFromSession();
        }
    }

    void activate() override
    {
        // Activation clears the clock and the playhead; the patch itself must survive.
        resetRuntime();
    }

    void run(const float**, float** outputs, const std::uint32_t frames,
             const MidiEvent* midiEvents, const std::uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        std::array<core::MidiEvent, 256> inputs {};
        const std::uint32_t inputCount = std::min<std::uint32_t>(midiEventCount, static_cast<std::uint32_t>(inputs.size()));
        for (std::uint32_t i = 0; i < inputCount; ++i) {
            const std::uint32_t size = std::min<std::uint32_t>(midiEvents[i].size, 4u);
            const std::uint8_t* const data = midiEvents[i].size > MidiEvent::kDataSize
                ? midiEvents[i].dataExt : midiEvents[i].data;
            inputs[static_cast<std::size_t>(i)].frame = midiEvents[i].frame;
            inputs[static_cast<std::size_t>(i)].size = static_cast<std::uint8_t>(size);
            std::copy_n(data, size, inputs[static_cast<std::size_t>(i)].data.begin());
        }

        const core::MidiBlock block = core::process(session_, engine_, toTransport(getTimePosition()),
                                                      inputs.data(), inputCount, frames, getSampleRate());
        for (std::uint32_t i = 0; i < block.count; ++i) {
            const core::MidiEvent& source = block.events[static_cast<std::size_t>(i)];
            MidiEvent event {};
            event.frame = source.frame;
            event.size = source.size;
            std::copy_n(source.data.begin(), source.size, event.data);
            writeMidiEvent(event);
        }

        const core::StatusView status = core::statusView(session_, engine_);
        parameters_[static_cast<std::size_t>(core::kStatusPlaying)] = static_cast<float>(status.playing);
        parameters_[static_cast<std::size_t>(core::kStatusGeneration)] = static_cast<float>(status.generation);
        parameters_[static_cast<std::size_t>(core::kStatusRound)] = static_cast<float>(status.round);
        parameters_[static_cast<std::size_t>(core::kStatusClockBpm)] = static_cast<float>(status.clockBpm);
        parameters_[static_cast<std::size_t>(core::kStatusClockTick)] = static_cast<float>(status.clockTick);
        parameters_[static_cast<std::size_t>(core::kStatusProgress)] = static_cast<float>(status.progress);
        parameters_[static_cast<std::size_t>(core::kStatusClockRunning)] = status.clockRunning ? 1.0f : 0.0f;
        parameters_[static_cast<std::size_t>(core::kStatusLastNote)] = static_cast<float>(status.lastNote);
        parameters_[static_cast<std::size_t>(core::kStatusLastVelocity)] = static_cast<float>(status.lastVelocity);
    }

private:
    core::Params parameters_ {};
    core::Session session_ {};
    core::Engine engine_ {};

    void resetRuntime()
    {
        engine_ = core::Engine {};
        syncRuntimeSettings();
    }

    void syncRuntimeSettings()
    {
        engine_.source = session_.settings.clockSource;
        engine_.settingsSendClock = session_.settings.sendClock;
    }

    void syncFromParameters()
    {
        core::applySettings(session_, engine_, core::settingsFromParams(parameters_));
        syncParametersFromSession();
    }

    void syncParametersFromSession()
    {
        const core::Params validated = core::paramsFromSettings(session_.settings);
        for (std::uint32_t i = 0; i <= core::kSendClock; ++i)
            parameters_[static_cast<std::size_t>(i)] = validated[static_cast<std::size_t>(i)];
        syncRuntimeSettings();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelevolvePlugin)
};

Plugin* createPlugin() { return new MelevolvePlugin(); }

END_NAMESPACE_DISTRHO
