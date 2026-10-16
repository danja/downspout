#pragma once

#include "trajectory_envelope.hpp"
#include "trajectory_oscillator.hpp"
#include "trajectory_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::trajectory {

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint32_t size = 0;
    std::array<std::uint8_t, 3> data {};
};

// Eight-voice polygon-bounce synthesizer. Portable: no DPF, no allocation after
// construction. The wrapper forwards host parameter writes and MIDI; incoming
// CC writes through to the same parameter store, so the host, the panel and a
// controller never disagree about a value.
class Engine {
public:
    Engine();

    // Adopt a sample rate and clear all runtime state (voices, DC blocker,
    // smoothers). Parameters are left alone: a host calls this on activation and
    // on a sample-rate change, and neither may reset the patch.
    void activate(double sampleRate) noexcept;

    void setParameter(std::uint32_t index, float value) noexcept;
    [[nodiscard]] float getParameter(std::uint32_t index) const noexcept;

    // Render one block, applying `events` (sorted by frame) sample-accurately.
    void process(float* left,
                 float* right,
                 std::uint32_t frames,
                 const MidiMessage* events,
                 std::uint32_t eventCount) noexcept;

    void allNotesOff() noexcept;
    void allSoundOff() noexcept;

    [[nodiscard]] int activeVoices() const noexcept;

    // Whether voice `voice` still has an audible envelope, for tests.
    [[nodiscard]] bool voiceSounding(std::size_t voice) const noexcept
    {
        return voice < kVoiceCount && voices_[voice].envelope.active();
    }

private:
    struct Voice {
        Oscillator oscillator;
        Adsr envelope;
        int note = -1;
        bool held = false;
        float velocityGain = 1.0f;
        std::uint64_t age = 0;
    };

    void handleMidi(const MidiMessage& message) noexcept;
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    [[nodiscard]] std::size_t allocateVoice(int note) noexcept;
    void renderSegment(float* left, float* right, std::uint32_t frames) noexcept;

    std::array<float, kInputParameterCount> parameters_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    double sampleRate_ = 48000.0;
    std::uint64_t clock_ = 0;

    // One-pole smoothers for the controls a CC can step abruptly.
    float mixXSmooth_ = 0.0f;
    float mixYSmooth_ = 1.0f;
    float levelSmooth_ = 0.0f;
    float smoothCoefficient_ = 0.005f;

    // DC blocker: a bouncing point is rarely symmetric about the origin.
    float dcInput_ = 0.0f;
    float dcOutput_ = 0.0f;
};

}  // namespace downspout::trajectory
