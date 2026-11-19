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

struct EngineState {
    std::array<float, kMaxDelaySamples> line {};
    std::size_t write = 0;
    double sampleRate = 44100.0;
    std::size_t faults = 0;
    bool primed = false;

    // Smoothed copies of the continuous controls. They are snapped to the
    // current patch on the first block after reset(), so the defaults are
    // bit-exact pass-through and automation does not click.
    float ll = 1.0f;
    float lr = 0.0f;
    float rl = 0.0f;
    float rr = 1.0f;
    float width = 1.0f;
    float delay = 0.0f;
    float invert = 1.0f;
    float level = 1.0f;
    float dry = 0.0f;
};

[[nodiscard]] inline std::size_t faultCount(const EngineState& state) noexcept
{
    return state.faults;
}

void prepare(EngineState& state, double sampleRate) noexcept;
void reset(EngineState& state) noexcept;
void processBlock(EngineState& state, const Parameters& parameters, std::uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept;

}  // namespace downspout::splay
