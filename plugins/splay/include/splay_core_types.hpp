#pragma once

#include "splay_params.hpp"

#include <array>
#include <cstddef>

namespace downspout::splay {

inline constexpr double kMaxDelayMs = 20.0;
// 20 ms at 192 kHz is 3840 samples; the extra headroom lets the fractional
// interpolator read one sample beyond the newest one without a second buffer.
inline constexpr std::size_t kMaxDelaySamples = 4096;
inline constexpr double kSmoothMs = 5.0;
// Meters are slower than the audio controls so they can be read in the panel.
inline constexpr double kMeterMs = 20.0;

struct EngineState {
    std::array<float, kMaxDelaySamples> line {};
    std::size_t write = 0;
    double sampleRate = 44100.0;
    std::size_t faults = 0;
    bool primed = false;

    // Smoothed copies of the writable controls. They snap to the current patch
    // on the first block after reset(), so the defaults stay bit-exact.
    float ll = 1.0f;
    float lr = 0.0f;
    float rl = 0.0f;
    float rr = 1.0f;
    float width = 1.0f;
    float delay = 0.0f;
    float invert = 1.0f;
    float level = 1.0f;
    float dry = 0.0f;

    float rotation = 0.0f;
    float panLaw = 0.0f;
    float midGain = 1.0f;
    float solo = 0.0f;
    float spectral = 0.0f;
    float crossover = 500.0f;
    float lowWidth = 1.0f;
    float highWidth = 1.0f;

    // One-pole crossover memory for the optional two-band width stage.
    float lowL = 0.0f;
    float lowR = 0.0f;

    // Smoothed read-only meters.
    float corr = 0.0f;
    float mono = 0.0f;
};

[[nodiscard]] inline std::size_t faultCount(const EngineState& state) noexcept
{
    return state.faults;
}

[[nodiscard]] inline float correlation(const EngineState& state) noexcept
{
    return state.corr;
}

[[nodiscard]] inline float monoCompatibility(const EngineState& state) noexcept
{
    return state.mono;
}

void prepare(EngineState& state, double sampleRate) noexcept;
void reset(EngineState& state) noexcept;
void processBlock(EngineState& state, const Parameters& parameters, std::uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept;

}  // namespace downspout::splay
