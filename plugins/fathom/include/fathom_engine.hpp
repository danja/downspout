#pragma once

#include "fathom_dsp.hpp"
#include "fathom_generator.hpp"
#include "fathom_params.hpp"
#include "fathom_processor.hpp"

#include <array>
#include <cstdint>

namespace downspout::fathom {

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint32_t size = 0;
    std::array<std::uint8_t, 3> data {};
};

// Attack/release envelope with a sustain at full level: a drone voice holds until
// told to let go.
class Envelope {
public:
    enum class Stage : std::uint8_t { idle, attack, sustain, release };

    void reset() noexcept
    {
        stage_ = Stage::idle;
        level_ = 0.0f;
    }
    void gateOn() noexcept { stage_ = Stage::attack; }
    void gateOff() noexcept
    {
        if (stage_ != Stage::idle)
            stage_ = Stage::release;
    }

    [[nodiscard]] float process(const float attackStep, const float releaseStep) noexcept
    {
        switch (stage_)
        {
        case Stage::attack:
            level_ += attackStep;
            if (level_ >= 1.0f)
            {
                level_ = 1.0f;
                stage_ = Stage::sustain;
            }
            break;
        case Stage::release:
            level_ -= releaseStep;
            if (level_ <= 0.0f)
            {
                level_ = 0.0f;
                stage_ = Stage::idle;
            }
            break;
        case Stage::sustain:
        case Stage::idle:
            break;
        }
        return level_;
    }

    [[nodiscard]] float level() const noexcept { return level_; }
    [[nodiscard]] bool active() const noexcept { return stage_ != Stage::idle; }
    [[nodiscard]] bool releasing() const noexcept { return stage_ == Stage::release; }

private:
    Stage stage_ = Stage::idle;
    float level_ = 0.0f;
};

// Four-voice drone synthesizer. Portable: no DPF. The wrapper forwards host
// parameter writes and MIDI; an incoming CC writes through to the same parameter
// store so the host, the panel and a controller never disagree.
class Engine {
public:
    Engine();

    // Adopt a sample rate and clear runtime state (voices, filters, the delay
    // network). Parameters are left alone: a host calls this on activation and on
    // a sample-rate change, and neither may reset the patch. Allocates.
    void activate(double sampleRate);

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

private:
    struct Voice {
        Generator generator;
        Envelope envelope;
        Svf lowpass;
        std::array<Svf, 2> notches;
        int note = -1;
        bool gate = false;     // sounding on purpose (key down, or hold)
        bool keyDown = false;  // the key itself is down
        float velocityGain = 1.0f;
        float pan = 0.0f;
        double pitch = 69.0;   // glided pitch, in semitones
        std::uint64_t age = 0;
    };

    void handleMidi(const MidiMessage& message) noexcept;
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    void setHold(bool on) noexcept;
    [[nodiscard]] std::size_t allocateVoice(int note) noexcept;
    void renderSegment(float* left, float* right, std::uint32_t frames) noexcept;

    std::array<float, kInputParameterCount> parameters_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    Processor processor_;
    double sampleRate_ = 48000.0;
    std::uint64_t clock_ = 0;
    double lastPitch_ = 69.0;
    bool havePitch_ = false;
    double bend_ = 0.0;  // semitones

    // Smoothers.
    float mixSmooth_ = 0.35f;
    float levelSmooth_ = 0.6f;
    float fieldSmooth_ = 1.0f;
    float notchSmooth_ = 0.0f;
    float smoothCoefficient_ = 0.005f;

    // DC blocker, per side.
    std::array<float, 2> dcInput_ {};
    std::array<float, 2> dcOutput_ {};
};

}  // namespace downspout::fathom
