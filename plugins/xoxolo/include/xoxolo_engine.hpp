#pragma once

#include "xoxolo_core_types.hpp"

#include <array>
#include <cstdint>

namespace downspout::xoxolo {

struct EngineState {
    Controls controls {};
    PatternState pattern {};
    std::array<PendingNoteOff, kMaxPendingNoteOffs> pendingNoteOffs {};
    bool wasPlaying = false;
    std::int64_t lastTransportStep = -1;
    int previousClearSerial = 0;
    int previousPreviewSerial = 0;
    int currentStep = -1;
};

struct BlockResult {
    std::array<ScheduledMidiEvent, kMaxScheduledMidiEvents> events {};
    int eventCount = 0;
    int currentStep = -1;
};

[[nodiscard]] Controls clampControls(const Controls& controls);
[[nodiscard]] PatternState makeDefaultPattern();
void sanitizePattern(PatternState& pattern);
void resizePattern(PatternState& pattern, int bars, ResolutionId resolution, const ::downspout::Meter& meter);
void setCell(PatternState& pattern, int lane, int step, bool active);
[[nodiscard]] bool cellActive(const PatternState& pattern, int lane, int step);
void clearPattern(PatternState& pattern);

void activate(EngineState& state, const Controls& controls);
void deactivate(EngineState& state);
[[nodiscard]] BlockResult processBlock(EngineState& state,
                                       const Controls& controls,
                                       const TransportSnapshot& transport,
                                       std::uint32_t nframes,
                                       double sampleRate);

}  // namespace downspout::xoxolo
