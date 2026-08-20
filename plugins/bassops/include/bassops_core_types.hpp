#pragma once

#include <array>
#include <cstdint>

namespace downspout::bassops {

// Linear-phase FIR order (must be even); latency = kFirOrder/2 frames
inline constexpr std::uint32_t kFirOrder = 128;
inline constexpr std::uint32_t kFirTaps  = kFirOrder + 1;

struct Parameters {
    float duckDepth = 80.0f;   // 0-100 %
    float attackMs  = 10.0f;   // 1-500 ms
    float releaseMs = 100.0f;  // 10-2000 ms
    float cutoffHz  = 200.0f;  // 50-5000 Hz
};

struct FirBank {
    std::array<float, kFirTaps> lp {};
    std::array<float, kFirTaps> hp {};
    std::array<float, kFirTaps> bufMid {};
    std::array<float, kFirTaps> bufSide {};
    std::uint32_t pos = 0;
    float lastCutoffHz   = -1.0f;
    float lastSampleRate = 0.0f;
};

struct EnvFollower {
    float envelope = 0.0f;
};

struct Meters {
    float inputPeak  = 0.0f;  // peak of main input this block, 0-1 with ballistic decay
    float scLevel    = 0.0f;  // sidechain envelope follower output, 0-1
    float duckGain   = 1.0f;  // current VCA gain, 0-1 (1=no duck, 0=full duck)
    float outputPeak = 0.0f;  // peak of output this block, 0-1 with ballistic decay
};

struct EngineState {
    EnvFollower env {};
    FirBank     fir {};
    Meters      meters {};
};

}  // namespace downspout::bassops
