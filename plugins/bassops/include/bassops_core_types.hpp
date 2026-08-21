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
    float sideShape = 0.0f;    // 0=linear, 100=hard clip (waveshaper on side channel)
    float wet       = 100.0f;  // 0=dry original input, 100=fully processed
};

inline constexpr std::uint32_t kMidDelayLen = kFirOrder / 2;  // 64 samples (FIR group delay)

struct FirBank {
    // LP → mid (clean low-end), HP(distorted mono) → side (harmonic stereo width).
    // LP + HP = delayed identity at shape=0, so outL=outR=delayed mono (phase-coherent).
    std::array<float, kFirTaps>    lp {};
    std::array<float, kFirTaps>    hp {};
    std::array<float, kFirTaps>    bufMid {};      // convolution buffer for LP path
    std::array<float, kFirTaps>    bufSide {};     // convolution buffer for HP path
    std::array<float, kMidDelayLen> dryDelayL {};  // delay dry L to match FIR latency
    std::array<float, kMidDelayLen> dryDelayR {};  // delay dry R to match FIR latency
    std::uint32_t pos      = 0;
    std::uint32_t delayPos = 0;
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
    float outputPeak = 0.0f;  // mean-abs output level, 0-1 with fast-release ballistic
};

struct EngineState {
    EnvFollower env {};
    FirBank     fir {};
    Meters      meters {};
};

}  // namespace downspout::bassops
