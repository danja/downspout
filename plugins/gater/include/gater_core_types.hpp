#pragma once

#include <cstdint>

namespace downspout::gater {

inline constexpr std::uint32_t kMaxChannels = 2; // Stereo input/output

struct Parameters {
    float gate = 0.0f; // MIDI control threshold or logic?
    // How to map MIDI input to output selection?
    // Maybe a simple switch parameter that can be automated or controlled by MIDI?
};

struct TransportSnapshot {
    bool valid = false;
    // ... potentially other transport data
};

struct EngineState {
    int activeOutput = 0; // 0 or 1
};

}  // namespace downspout::gater
