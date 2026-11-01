#pragma once

#include "mouth_params.hpp"

#include <array>
#include <cstddef>

namespace downspout::mouth {

// Controllers 1-4 on the set channel override Vowel X, Vowel Y, Shift and Resonance (the Drift
// convention) until the panel or the host moves that control again. -1 = not overridden.
struct LiveControl {
    std::array<float, 4> value {{-1.0f, -1.0f, -1.0f, -1.0f}};  // normalised 0..1
};

[[nodiscard]] bool ccChannelMatches(unsigned char status, float ccChannel) noexcept;
void handleController(LiveControl& live, int controller, int value) noexcept;
[[nodiscard]] Parameters withLive(const Parameters& p, const LiveControl& live) noexcept;

struct Channel {
    std::array<float, kFormants> ic1 {};
    std::array<float, kFormants> ic2 {};
    float airLow = 0.0f;
};

struct Engine {
    double sampleRate = 48000.0;
    std::array<Channel, 2> channel {};

    // Smoothed copies of the controls that shape the filters.
    float x = 50.0f, y = 95.0f, shift = 0.0f, resonance = 50.0f, spread = 0.0f;
    float mix = 1.0f;
    float output = 1.0f;
    bool primed = false;

    // Per channel, per formant coefficients of the current sub-block.
    struct Coefficients {
        std::array<float, kFormants> a1 {}, a2 {}, a3 {}, k {}, gain {};
        float makeup = 1.0f;
    };
    std::array<Coefficients, 2> coefficients {};
};

void activate(Engine& engine, double sampleRate) noexcept;
// Clears the filter memory only; never touches the patch.
void reset(Engine& engine) noexcept;

// inR may be null (the left input is used for both sides). Outputs may alias the inputs.
void processBlock(Engine& engine, const Parameters& parameters, const float* inL, const float* inR, float* outL,
                  float* outR, std::size_t frames) noexcept;

// The magnitude of the combined filter at `hz`, in dB, for the plot and the tests. Same
// formants as the audio path (channel 0 = left, 1 = right), continuous-time band-passes.
[[nodiscard]] float responseDb(const Parameters& parameters, double sampleRate, float hz, int channel = 0) noexcept;

}  // namespace downspout::mouth
