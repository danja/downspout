#pragma once

#include "ground_core_types.hpp"

#include <array>
#include <cstdint>

namespace downspout::ground {

struct EngineState {
    Controls controls {};
    Controls previousControls {};
    FormState form {};
    VariationState variation {};
    bool formValid = false;
    int activeNote = -1;
    std::int64_t lastTransportStep = -1;
    bool wasPlaying = false;
    int currentPhraseIndex = 0;
    PhraseRoleId currentRole = PhraseRoleId::statement;
};

struct BlockResult {
    std::array<ScheduledMidiEvent, kMaxScheduledMidiEvents> events {};
    int eventCount = 0;
    int currentPhraseIndex = 0;
    PhraseRoleId currentRole = PhraseRoleId::statement;
};

void activate(EngineState& state, const Controls& controls);
void deactivate(EngineState& state);
[[nodiscard]] BlockResult processBlock(EngineState& state,
                                       const Controls& controls,
                                       const TransportSnapshot& transport,
                                       std::uint32_t nframes,
                                       double sampleRate);

}  // namespace downspout::ground
