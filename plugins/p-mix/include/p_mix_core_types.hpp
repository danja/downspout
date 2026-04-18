#pragma once

#include <array>
#include <cstdint>

namespace downspout::pmix {

inline constexpr std::uint32_t kMaxChannels = 8;
inline constexpr float kFadeMinFraction = 0.125f;

struct Parameters {
    float granularity = 4.0f;
    float maintain = 50.0f;
    float fade = 25.0f;
    float cut = 25.0f;
    float fadeDurMax = 1.0f;
    float bias = 50.0f;
};

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

struct EngineState {
    float currentGain = 1.0f;
    float targetGain = 1.0f;
    float fadeStep = 0.0f;
    std::uint32_t fadeRemaining = 0;
    std::uint32_t rngState = 0x12345678u;
};

struct Boundary {
    std::uint32_t frame = 0;
    std::int64_t bar = 0;
};

using BoundaryList = std::array<Boundary, 16>;

}  // namespace downspout::pmix
