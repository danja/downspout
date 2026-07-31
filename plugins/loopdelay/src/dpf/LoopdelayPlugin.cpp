#include "DistrhoPlugin.hpp"
#include "loopdelay_core.hpp"
#include "loopdelay_serialization.hpp"

#include <algorithm>
#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {
using namespace downspout::loopdelay;
constexpr const char* kStateKey = "parameters";

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

class LoopdelayPlugin : public Plugin {
public:
    LoopdelayPlugin() : Plugin(kParameterCount, 0, 1) {}

protected:
    const char* getLabel() const override { return "Loopdelay"; }
    const char* getDescription() const override
    {
        return "Transport-synchronized stereo delay and capture looper with MIDI producer control.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    std::uint32_t getVersion() const override { return d_version(0, 1, 0); }
    std::int64_t getUniqueId() const override { return d_cconst('L', 'p', 'D', 'l'); }

    void initAudioPort(const bool input, const std::uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = kPortGroupStereo;
        port.name = index == 0 ? (input ? "Input Left" : "Output Left")
                               : (input ? "Input Right" : "Output Right");
        port.symbol = index == 0 ? (input ? "in_left" : "out_left")
                                 : (input ? "in_right" : "out_right");
    }

    void initParameter(const std::uint32_t index, Parameter& parameter) override
    {
        const auto& spec = kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (index == kMidiEnabled || index == kClear || index == kResetMidi
            || index == kStatusTimeMidi || index == kStatusFeedbackMidi
            || index == kRequireProducerGate || index == kStatusProducerActive)
            parameter.hints |= kParameterIsBoolean;
        parameter.ranges = {spec.defaultValue, spec.minimum, spec.maximum};
        if (index == kFreeTimeMs || index == kStatusDelayMs)
            parameter.unit = "ms";
        else if (index == kOutputDb)
            parameter.unit = "dB";
    }

    void initState(const std::uint32_t index, State& state) override
    {
        if (index == 0) {
            state.key = kStateKey;
            state.label = "Parameters";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(const std::uint32_t index) const override
    {
        switch (index) {
        case kClear: case kResetMidi: return 0.0f;
        case kStatusState: return static_cast<float>(status_.state);
        case kStatusDelayMs: return status_.delayMs;
        case kStatusFeedback: return status_.feedback;
        case kStatusTimeMidi: return status_.timeMidi ? 1.0f : 0.0f;
        case kStatusFeedbackMidi: return status_.feedbackMidi ? 1.0f : 0.0f;
        case kStatusLoopProgress: return status_.loopProgress;
        case kStatusInputPeak: return status_.inputPeak;
        case kStatusOutputPeak: return status_.outputPeak;
        case kStatusProducerActive: return status_.producerActive ? 1.0f : 0.0f;
        default: return parameters_.values[index];
        }
    }

    void setParameterValue(const std::uint32_t index, const float value) override
    {
        if (index >= kParameterCount || kParameterSpecs[index].output)
            return;
        if (index == kClear) {
            if (value >= 0.5f) requestClear(engine_);
            return;
        }
        if (index == kResetMidi) {
            if (value >= 0.5f) releaseMidiTakeover(engine_);
            return;
        }
        parameters_.values[index] = downspout::generative::clampParam(value, kParameterSpecs[index]);
    }

    String getState(const char* key) const override
    {
        return std::strcmp(key, kStateKey) == 0
            ? String(serializeParameters(parameters_).c_str()) : String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKey) != 0) return;
        const auto decoded = deserializeParameters(value != nullptr ? value : "");
        if (decoded.has_value()) parameters_ = *decoded;
    }

    void activate() override { prepare(engine_, getSampleRate()); reset(engine_); }
    void sampleRateChanged(const double rate) override { prepare(engine_, rate); }

    void run(const float** inputs, float** outputs, const std::uint32_t frames,
             const MidiEvent* midiEvents, const std::uint32_t midiEventCount) override
    {
        std::array<MidiControlEvent, 512> controls {};
        const std::uint32_t count = std::min<std::uint32_t>(midiEventCount, controls.size());
        for (std::uint32_t index = 0; index < count; ++index) {
            controls[index].frame = std::min(midiEvents[index].frame, frames > 0 ? frames - 1 : 0);
            controls[index].size = static_cast<std::uint8_t>(std::min<std::uint32_t>(midiEvents[index].size, 3));
            const std::uint8_t* source = midiEvents[index].size > MidiEvent::kDataSize
                ? midiEvents[index].dataExt : midiEvents[index].data;
            for (std::uint32_t byte = 0; byte < controls[index].size; ++byte)
                controls[index].data[byte] = source[byte];
        }
        const AudioBlock audio {inputs[0], inputs[1], outputs[0], outputs[1]};
        status_ = process(engine_, parameters_, toCoreTransport(getTimePosition()), frames,
                          audio, controls.data(), count);
        for (std::uint32_t index = 0; index < midiEventCount; ++index)
            writeMidiEvent(midiEvents[index]);
    }

private:
    Parameters parameters_ {};
    downspout::loopdelay::State engine_ {};
    OutputStatus status_ {};
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopdelayPlugin)
};

Plugin* createPlugin() { return new LoopdelayPlugin(); }
END_NAMESPACE_DISTRHO
