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
    // Conductor reset (CC 24 = 127): applied at the next bar line, after which pattern position and the
    // evolve generation are counted from that bar. restartStep is the absolute step it began on.
    bool restartPending = false;
    std::int64_t restartStep = 0;
    std::int64_t lastBar = -1;
};

struct BlockResult {
    std::array<ScheduledMidiEvent, kMaxScheduledMidiEvents> events {};
    int eventCount = 0;
    int currentStep = -1;
};

[[nodiscard]] Controls clampControls(const Controls& controls);
[[nodiscard]] NotePresetId clampNotePreset(NotePresetId preset);
[[nodiscard]] const std::array<LaneSpec, kLaneCount>& laneSpecsForPreset(NotePresetId preset);
[[nodiscard]] int activeLaneCountForPreset(NotePresetId preset);
[[nodiscard]] const char* notePresetName(NotePresetId preset);
[[nodiscard]] PatternState makeDefaultPattern();
void sanitizePattern(PatternState& pattern);
void applyNotePreset(PatternState& pattern, NotePresetId preset);
void resizePattern(PatternState& pattern, int steps, ResolutionId resolution, const ::downspout::Meter& meter);
void setCell(PatternState& pattern, int lane, int step, bool active);
[[nodiscard]] bool cellActive(const PatternState& pattern, int lane, int step);
void clearPattern(PatternState& pattern);

// Applies a block's incoming MIDI before processBlock(). With controls.conductorCh set, on that channel:
// CC 21 Density, CC 22 Energy, CC 23 Mutation (shifts the evolve generation), CC 24 = 127 restarts the
// pattern and the evolve generation at the next bar line. CC 20 (Scene) is not used. Writes into `controls`.
void handleMidi(EngineState& state, Controls& controls, const MidiInputEvent* events, std::uint32_t count);

void activate(EngineState& state, const Controls& controls);
void deactivate(EngineState& state);
[[nodiscard]] BlockResult processBlock(EngineState& state,
                                       const Controls& controls,
                                       const TransportSnapshot& transport,
                                       std::uint32_t nframes,
                                       double sampleRate);

}  // namespace downspout::xoxolo
