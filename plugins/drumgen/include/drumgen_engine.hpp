#pragma once

#include "drumgen_core_types.hpp"

#include <array>
#include <cstdint>

namespace downspout::drumgen {

struct EngineState {
    Controls controls {};
    Controls previousControls {};
    PatternState pattern {};
    VariationState variation {};
    bool patternValid = false;
    std::array<PendingNoteOff, kMaxPendingNoteOffs> pendingNoteOffs {};
    std::int64_t lastTransportStep = -1;
    bool wasPlaying = false;
};

struct BlockResult {
    std::array<ScheduledMidiEvent, kMaxScheduledMidiEvents> events {};
    int eventCount = 0;
};

void activate(EngineState& state, const Controls& controls);
void deactivate(EngineState& state);
[[nodiscard]] BlockResult processBlock(EngineState& state,
                                       const Controls& controls,
                                       const TransportSnapshot& transport,
                                       std::uint32_t nframes,
                                       double sampleRate);

}  // namespace downspout::drumgen
