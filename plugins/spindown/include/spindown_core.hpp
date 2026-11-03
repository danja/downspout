#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::spindown {

// A tape-stop effect. The input is written to a ring buffer and read back by a head whose speed is the tape speed:
// stopping brings the speed from 1 to 0 (pitch and time fall together), starting brings it back. The head lags the live
// input by the time it spent slower than 1, so when the tape is up to speed again the output crossfades back to the live
// input. With the effect idle the output is the input, sample for sample, with no latency.
//
// This is a clean-room design from the general behaviour of a tape transport; it shares no code with any other plugin.

inline constexpr float kMinStopMs = 20.0f;
inline constexpr float kMaxStopMs = 4000.0f;
inline constexpr float kMinCurve = 0.5f;
inline constexpr float kMaxCurve = 4.0f;
inline constexpr float kMinLengthBeats = 0.25f;
inline constexpr float kMaxLengthBeats = 16.0f;
inline constexpr float kBufferSeconds = 10.0f;
inline constexpr float kCrossfadeMs = 12.0f;
inline constexpr double kFallbackBpm = 120.0;
inline constexpr double kFallbackBeatsPerBar = 4.0;

enum class Trigger : int { Manual = 0, Midi, Cycle };
inline constexpr int kTriggerCount = 3;
inline constexpr int kCycleCount = 4;   // 1, 2, 4 or 8 bars

// CC numbers on the CC channel (Drift's default lanes are CC 1-4).
inline constexpr int kCcStopTime = 1;
inline constexpr int kCcStartTime = 2;
inline constexpr int kCcCurve = 3;
inline constexpr int kCcStop = 4;       // 64 and above engages the stop (Manual trigger)

struct Parameters {
    float trigger  = 0.0f;     // Trigger
    float stop     = 0.0f;     // 0/1: tape stopped (Manual trigger)
    float stopMs   = 800.0f;   // time from full speed to standstill
    float startMs  = 300.0f;   // time from standstill to full speed
    float curve    = 1.5f;     // speed = (1 - progress)^curve; 1 is a straight ramp, higher drops fast then lingers
    float cycle    = 1.0f;     // index into 1, 2, 4, 8 bars (Cycle trigger)
    float length   = 2.0f;     // beats at the end of each cycle during which the tape is stopped (Cycle trigger)
    float ccChannel = 1.0f;    // 0 = ignore CC, 1-16
};

// What the host says about time.
struct Transport {
    bool   playing = false;
    bool   valid = false;       // bpm and beatPosition are real
    double bpm = 0.0;
    double beatsPerBar = 0.0;
    double beatPosition = 0.0;  // beats from the start of the song at the first sample of the block
};

struct MidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t status = 0;    // full status byte (channel in the low nibble)
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

// Read by the panel.
struct Meters {
    float speed = 1.0f;      // 0..1
    float delayMs = 0.0f;    // how far the output lags the live input
};

// CC takeover: a CC replaces the panel value until the panel/host moves that parameter again.
inline constexpr int kOverrideCount = 4;   // stopMs, startMs, curve, stop

struct EngineState {
    std::vector<float> ring[2];
    std::uint32_t mask = 0;
    std::uint32_t writePos = 0;
    double delay = 0.0;          // samples the head lags the live input
    double progress = 0.0;       // 0 = full speed, 1 = stopped
    float live = 1.0f;           // 1 = output is the live input
    int heldNotes = 0;
    std::array<float, kOverrideCount> overrideValue {};
    std::array<bool, kOverrideCount> overrideActive {};
    Meters meters {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

[[nodiscard]] int cycleBars(float cycleIndex) noexcept;

// Tape speed 0..1 for a stop progress 0..1.
[[nodiscard]] float speedFor(double progress, float curve) noexcept;

// Allocates the ring buffer for a sample rate (not real-time safe) and clears all runtime state. Never touches the patch.
void prepare(EngineState& state, float sampleRate);

// Clears the runtime state (ring, head, notes, CC takeovers); keeps the allocation.
void resetState(EngineState& state) noexcept;

// Drop the CC takeover of one control (the panel or host moved it). `control` is 0 stopMs, 1 startMs, 2 curve, 3 stop.
void releaseOverride(EngineState& state, int control) noexcept;

// True when the Cycle trigger holds the tape stopped at this beat position.
[[nodiscard]] bool cycleStopped(const Parameters& p, double beatPosition, double beatsPerBar) noexcept;

void processBlock(EngineState& state, const Parameters& params, float sampleRate, std::uint32_t frames, const Transport& transport,
                  const MidiEvent* events, std::uint32_t eventCount, const float* const* inputs, float* const* outputs) noexcept;

}  // namespace downspout::spindown
