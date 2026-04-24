#pragma once

#include <array>
#include <cstdint>

namespace downspout::emix {

inline constexpr std::uint32_t kMaxChannels = 8;

struct Parameters {
    float totalBars = 128.0f;
    float division = 16.0f;
    float steps = 8.0f;
    float offset = 0.0f;
    float fadeBars = 0.0f;
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
    bool transportWasPlaying = false;
    double cycleOriginBar = 0.0;
    double fallbackBarPos = 0.0;
    double fallbackBpm = 120.0;
    double fallbackBeatsPerBar = 4.0;
};

struct AudioBlock {
    std::array<const float*, kMaxChannels> inputs {};
    std::array<float*, kMaxChannels> outputs {};
    std::uint32_t channelCount = kMaxChannels;
};

}  // namespace downspout::emix
