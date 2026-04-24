#pragma once

#include "cadence_comping.hpp"
#include "cadence_core_types.hpp"

#include <array>

namespace downspout::cadence {

struct EngineState {
    Controls controls = defaultControls();
    Controls previousControls = defaultControls();
    bool controlsInitialized = false;

    std::array<SegmentCapture, kMaxSegments> capture {};
    std::array<SegmentCapture, kMaxSegments> learnedCapture {};
    int learnedSegmentCount = 0;
    bool haveLearnedCapture = false;

    std::array<ChordSlot, kMaxSegments> baseProgression {};
    int baseSegmentCount = 0;
    std::array<ChordSlot, kMaxSegments> playback {};
    int playbackSegmentCount = 0;
    bool ready = false;

    std::array<bool, 128> heldNotes {};
    std::array<std::uint8_t, 128> heldVelocity {};

    std::array<std::uint8_t, kMaxChordNotes> activeHarmonyNotes {};
    std::uint8_t activeHarmonyCount = 0;
    int activeHarmonyChannel = 1;
    int lastInputChannel = 1;
    bool harmonyOffPending = false;
    double pendingHarmonyOffBeat = 0.0;
    CompPlaybackState compState {};

    VariationState variation = defaultVariationState();
    bool wasPlaying = false;
    double lastAbsBeatsStart = 0.0;
};

struct BlockResult {
    std::array<ScheduledMidiEvent, kMaxScheduledMidiEvents> events {};
    int eventCount = 0;
    bool ready = false;
};

void activate(EngineState& state);
void deactivate(EngineState& state);
[[nodiscard]] BlockResult processBlock(EngineState& state,
                                       const Controls& controls,
                                       const TransportSnapshot& transport,
                                       std::uint32_t nframes,
                                       double sampleRate,
                                       const InputMidiEvent* midiEvents,
                                       std::uint32_t midiEventCount);

}  // namespace downspout::cadence
