#include "DistrhoPlugin.hpp"
#include "harmonic_atlas_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {
using namespace downspout::harmonic_atlas;

Transport transportFrom(const TimePosition& time)
{
    Transport result;
    result.valid = time.bbt.valid;
    result.playing = time.playing;
    if (time.bbt.valid) {
        result.bar = time.bbt.bar - 1;
        result.barBeat = time.bbt.beat - 1
            + (time.bbt.ticksPerBeat > 0.0 ? time.bbt.tick / time.bbt.ticksPerBeat : 0.0);
        result.beatsPerBar = time.bbt.beatsPerBar;
        result.beatType = time.bbt.beatType;
        result.bpm = time.bbt.beatsPerMinute;
    }
    return result;
}
}

class HarmonicAtlasPlugin : public Plugin {
public:
    HarmonicAtlasPlugin() : Plugin(kParameterCount, 0, 0)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            parameters_[i] = kParameterSpecs[i].defaultValue;
    }
protected:
    const char* getLabel() const override { return "HarmonicAtlas"; }
    const char* getDescription() const override { return "Autonomous transport-synced harmony generator."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    std::uint32_t getVersion() const override { return d_version(0, 1, 0); }
    std::int64_t getUniqueId() const override { return d_cconst('H','A','t','l'); }
    void initParameter(const std::uint32_t index, Parameter& parameter) override
    {
        const auto& spec = kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer) parameter.hints |= kParameterIsInteger;
        parameter.ranges = {spec.defaultValue, spec.minimum, spec.maximum};
    }
    float getParameterValue(const std::uint32_t index) const override
    {
        if (index == kStatusRoot) return static_cast<float>(state_.statusRoot);
        if (index == kStatusChord) return static_cast<float>(std::max<std::int64_t>(0, state_.lastChord));
        return parameters_[index];
    }
    void setParameterValue(const std::uint32_t index, const float value) override
    {
        if (index < kParameterCount && !kParameterSpecs[index].output)
            parameters_[index] = downspout::generative::clampParam(value, kParameterSpecs[index]);
    }
    void activate() override { reset(state_); }
    void run(const float**, float** outputs, const std::uint32_t frames,
             const MidiEvent* midiEvents, const std::uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        // Apply Conductor CC overrides before processing (CC 20–24 on configured channel)
        const int condCh = static_cast<int>(std::lround(parameters_[kConductorChannel]));
        if (condCh > 0) {
            for (std::uint32_t i = 0; i < midiEventCount; ++i) {
                const auto& ev = midiEvents[i];
                if (ev.size >= 3 && (ev.data[0] & 0xf0) == 0xb0 && (ev.data[0] & 0x0f) == condCh - 1) {
                    const auto cc  = ev.data[1];
                    const auto val = ev.data[2];
                    switch (cc) {
                    case 20: // Scene → movement style (0=Tonal,1=Modal,2=Chromatic,3=Neo-Riemannian)
                        parameters_[kStyle] = std::clamp(static_cast<float>(val / 32), 0.0f, 3.0f);
                        break;
                    case 22: // Energy → tension (0–1)
                        parameters_[kTension] = val / 127.0f;
                        break;
                    case 23: // Mutation → inversion range (0–3)
                        parameters_[kInversionRange] = std::clamp(static_cast<float>(val / 32), 0.0f, 3.0f);
                        break;
                    case 24: // Reset → discard current harmonic position
                        if (val == 127) reset(state_);
                        break;
                    default: break;
                    }
                }
            }
        }

        std::array<downspout::generative::MidiEvent, 512> input {};
        const auto count = std::min<std::uint32_t>(midiEventCount, input.size());
        for (std::uint32_t i = 0; i < count; ++i) {
            input[i].frame = midiEvents[i].frame;
            input[i].size = static_cast<std::uint8_t>(std::min<std::uint32_t>(midiEvents[i].size, 4));
            const auto* data = midiEvents[i].size > MidiEvent::kDataSize ? midiEvents[i].dataExt : midiEvents[i].data;
            for (std::uint8_t byte = 0; byte < input[i].size; ++byte) input[i].data[byte] = data[byte];
        }
        const auto block = process(state_, parameters_, transportFrom(getTimePosition()),
                                   frames, getSampleRate(), input.data(), count);
        for (std::uint32_t i = 0; i < block.count; ++i) {
            MidiEvent event {};
            event.frame = block.events[i].frame;
            event.size = block.events[i].size;
            for (std::uint8_t byte = 0; byte < event.size; ++byte) event.data[byte] = block.events[i].data[byte];
            writeMidiEvent(event);
        }
    }
private:
    std::array<float, kParameterCount> parameters_ {};
    downspout::harmonic_atlas::State state_ {};
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicAtlasPlugin)
};

Plugin* createPlugin() { return new HarmonicAtlasPlugin(); }
END_NAMESPACE_DISTRHO
