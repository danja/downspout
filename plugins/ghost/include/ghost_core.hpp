#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::ghost {

using downspout::generative::MidiBlock;
using downspout::generative::MidiEvent;
using downspout::generative::Transport;

// The four musical controls map 1:1 to Drift's default lane CCs so that
// routing Drift MIDI out -> Ghost MIDI in just works, like chipper/magneto.
inline constexpr float kDefaultCCSensitivity = 1.0f;
inline constexpr float kDefaultCCDensity     = 2.0f;
inline constexpr float kDefaultCCVelocity    = 3.0f;
inline constexpr float kDefaultCCDrag        = 4.0f;

struct Parameters {
    float sensitivity = 0.5f;  // 0-1 onset threshold (higher = catches more)
    float density     = 0.35f; // 0-1 ghost-fill probability per off-16th slot
    float velocity    = 90.0f; // 1-127 base note velocity
    float drag        = 0.15f; // 0-1 ghost lateness as a fraction of a 16th slot
    float mode        = 0.0f;  // 0 = Drums (ch 10), 1 = Notes (selectable ch)
    float channel     = 10.0f; // 1-16 response channel in Notes mode
    float baseNote    = 38.0f; // drums: ghost voice anchor; notes: scale root
    float passInput   = 1.0f;  // 0-1 forward incoming MIDI to the output
    float audioThru   = 0.0f;  // 0-1 pass incoming audio to the output (default off)
    float seed        = 7.0f;  // 1-65535 deterministic pattern stream
    float ccSensitivity = kDefaultCCSensitivity; // 0-127 CC# (0 = off)
    float ccDensity     = kDefaultCCDensity;
    float ccVelocity    = kDefaultCCVelocity;
    float ccDrag        = kDefaultCCDrag;
    float ccChannel     = 1.0f; // 1-16 MIDI channel for the four CCs
};

struct EngineState {
    float env = 0.0f;                 // fast envelope follower
    double blockEndQuarter = 0.0;     // end of the previous block, for seeks
    bool havePosition = false;
    double lastOnsetQuarter = -1000.0;
    std::int64_t lastSlot = 0;
    bool haveSlot = false;
    static constexpr std::uint32_t kMaxPending = 8;
    std::array<std::uint8_t, kMaxPending> pendingNotes {};
    std::array<std::uint8_t, kMaxPending> pendingChannels {};
    std::uint32_t pendingCount = 0;
    // Scheduled ghosts quantised beyond the current block (a 16th slot is
    // thousands of frames at typical tempos; blocks are much shorter).
    struct ScheduledGhost {
        double quarter = 0.0;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        std::uint8_t channel = 0;
    };
    std::array<ScheduledGhost, kMaxPending> scheduled {};
    std::uint32_t scheduledCount = 0;
    float lastLevel = 0.0f;
    std::uint64_t faults = 0;
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;
void resetState(EngineState& state) noexcept;

// Effective values are CC-overridden when active, else the panel value.
// Audio passes through sanitised; MIDI note/fill output is returned.
MidiBlock processBlock(EngineState&        state,
                       const Parameters&   params,
                       const Transport&    transport,
                       std::uint32_t       frames,
                       double              sampleRate,
                       const float* const* inputs,
                       float* const*       outputs,
                       const MidiEvent*    midiIn,
                       std::uint32_t       midiInCount,
                       float               effectiveSensitivity,
                       float               effectiveDensity,
                       float               effectiveVelocity,
                       float               effectiveDrag) noexcept;

[[nodiscard]] std::string               serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::ghost
