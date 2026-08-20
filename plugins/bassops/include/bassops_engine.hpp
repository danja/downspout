#pragma once

#include "bassops_core_types.hpp"

#include <cstdint>

namespace downspout::bassops {

struct AudioBlock {
    const float* mainL    = nullptr;  // input 1: main stereo L
    const float* mainR    = nullptr;  // input 2: main stereo R
    const float* controlL = nullptr;  // input 3: sidechain L (typically bass drum send)
    const float* controlR = nullptr;  // input 4: sidechain R
    float* outL = nullptr;
    float* outR = nullptr;
};

[[nodiscard]] Parameters clampParameters(const Parameters& raw);
void activate(EngineState& state);
void processBlock(EngineState& state,
                  const Parameters& parameters,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio);

// Latency introduced by the linear-phase FIR (frames)
inline constexpr std::uint32_t kLatencyFrames = kFirOrder / 2;

}  // namespace downspout::bassops
