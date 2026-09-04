#pragma once

#include "worms_core_types.hpp"

#include <array>

namespace downspout::worms {

struct EngineState {
    Controls controls {};
    Controls previousControls {};
    PatternState pattern {};
    bool patternValid      = false;
    int  activeNote        = -1;
    std::int64_t lastTransportStep = -1;
    bool wasPlaying        = false;
};

struct BlockResult {
    std::array<ScheduledMidiEvent, kMaxScheduledEvents> events {};
    int eventCount = 0;
};

Controls clampControls(const Controls& controls);

void activate(EngineState& state, const Controls& controls);
void deactivate(EngineState& state, BlockResult& result);

[[nodiscard]] BlockResult processBlock(EngineState& state,
                                       const Controls& controls,
                                       const TransportSnapshot& transport,
                                       std::uint32_t nframes,
                                       double sampleRate);

}  // namespace downspout::worms
