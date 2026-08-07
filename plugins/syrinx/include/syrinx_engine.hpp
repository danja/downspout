#pragma once

#include "syrinx_params.hpp"
#include "SyrinxVoice.hpp"
#include "modules/DCBlocker.hpp"
#include "modules/ReverbModule.hpp"
#include "modules/Distortion.hpp"

#include <array>
#include <cstdint>

namespace downspout::syrinx {

static constexpr int kMaxVoices = 8;

struct StereoFrame {
    float left  = 0.0f;
    float right = 0.0f;
};

class Engine {
public:
    explicit Engine(float sampleRate = 48000.0f);

    void setSampleRate(float sampleRate);

    [[nodiscard]] float getParameter(std::uint32_t index) const;
    void setParameter(std::uint32_t index, float value);

    // MIDI
    void handleMidi(const std::uint8_t* data, std::uint32_t size);
    void handleNoteOn(std::uint8_t note, std::uint8_t velocity);
    void handleNoteOff(std::uint8_t note);
    void handleCC(std::uint8_t cc, std::uint8_t value);

    [[nodiscard]] StereoFrame processStereo();
    void reset();

    // Randomize the selected preset's parameters (UI action)
    void randomizeSelectedPreset(std::uint32_t seed);

private:
    void rebuildFx();
    [[nodiscard]] int allocateVoice();

    float sampleRate_;
    std::array<float, kParameterCount> params_{};
    std::uint32_t noiseStream_ = 0;

    std::array<SyrinxVoice, kMaxVoices> voices_;

    ReverbModule reverbL_, reverbR_;
    DCBlocker    dcL_, dcR_;
    Distortion   distL_, distR_;
};

} // namespace downspout::syrinx
