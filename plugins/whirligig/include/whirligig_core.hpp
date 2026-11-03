#pragma once

#include <array>
#include <cstdint>

namespace downspout::whirligig {

// A rotary speaker (Leslie-style) cabinet. The input is summed to mono and split at a crossover into a low band for the
// drum and a high band for the horn. Each rotor turns at its own speed, with its own inertia, and is picked up by two
// microphones. For each microphone a rotor produces two effects, both functions of the angle between the rotor and the
// microphone:
//   - amplitude: the source is directional, so it is loudest when it points at the microphone;
//   - Doppler: the source is nearer or further, so the delay it is heard through swings by +-r/c and the pitch with it.
//
// Clean-room from the geometry of a rotating directional source; it shares no code with any other rotary effect. The
// radii, the lobe shapes and the speed table are typical values chosen by ear, not measurements of a particular cabinet.

inline constexpr double kSpeedOfSound = 343.0;     // m/s
inline constexpr double kHornRadius = 0.17;        // m, by ear
inline constexpr double kDrumRadius = 0.19;        // m, by ear
inline constexpr double kBaseDelayMs = 0.7;        // the delay every path sits at, so Doppler can swing both ways (> max radius / c)
inline constexpr int kDelayRing = 1024;
inline constexpr int kDryRing = 512;

enum class Speed : int { Stop = 0, Slow = 1, Fast = 2 };
inline constexpr int kSpeedCount = 3;

// Beat-locked mode: each speed setting is a number of beats per turn, chosen from this table (index in the Parameters).
inline constexpr int kDivisionCount = 12;
inline constexpr std::array<double, kDivisionCount> kBeatsPerTurn = {8.0, 4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
inline constexpr double kFallbackBpm = 120.0;
inline constexpr double kLockSeconds = 0.5;        // time constant of the glide into phase lock
inline constexpr double kLockedTolerance = 0.02;   // turns

inline constexpr int kCcSpeed = 1;                 // 64 and above: Fast, below: Slow
inline constexpr int kCcBrake = 2;                 // 64 and above: Stop, below: Slow

struct Parameters {
    float speed = 1.0f;          // Speed
    float hornSlowHz = 0.8f;     // 48 rpm
    float hornFastHz = 6.7f;     // 400 rpm
    float drumSlowHz = 0.67f;    // 40 rpm
    float drumFastHz = 5.7f;     // 340 rpm
    float hornAccelS = 0.8f;     // time constant of the speed change
    float drumAccelS = 3.5f;
    float crossoverHz = 800.0f;
    float hornDepth = 0.6f;      // 0..1 amplitude modulation
    float hornDoppler = 1.0f;    // 0..1 of the full radius
    float drumDepth = 0.4f;
    float drumDoppler = 0.8f;
    float micAngle = 90.0f;      // degrees between the left and right microphones
    float hornLevelDb = 0.0f;
    float drumLevelDb = 0.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
    float ccChannel = 1.0f;      // 0 = ignore CC
    float sync = 0.0f;           // 0 = Free (Hz), 1 = Beat (locked to the host tempo and BBT position)
    float hornSlowDiv = 3.0f;    // index into kBeatsPerTurn: 2 beats per turn
    float hornFastDiv = 8.0f;    // 1/3 beat per turn
    float drumSlowDiv = 2.0f;    // 3 beats per turn
    float drumFastDiv = 7.0f;    // 1/2 beat per turn
    float syncPhase = 0.0f;      // degrees: where in the turn the rotors face the microphones at the start of a beat cycle
};

// What the host says about time. `beatPosition` is in beats from the start of the song at the first sample of the block.
struct Transport {
    bool   playing = false;
    bool   valid = false;       // bpm and beatPosition are real
    double bpm = 0.0;
    double beatPosition = 0.0;
};

struct MidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

// Read by the panel.
struct Meters {
    float hornHz = 0.0f;
    float drumHz = 0.0f;
    float hornTurn = 0.0f;       // 0..1 of a turn
    float drumTurn = 0.0f;
    float locked = 0.0f;         // 1 while both rotors are phase-locked to the transport
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<float, 2> z {};
};

struct EngineState {
    double hornTurn = 0.0, drumTurn = 0.0;     // turns, 0..1
    double hornHz = 0.0, drumHz = 0.0;         // current speeds
    std::array<Biquad, 2> lowPass {}, highPass {};  // each is two cascaded Butterworth sections: a Linkwitz-Riley 4th order
    std::array<float, kDelayRing> hornRing {}, drumRing {};
    int pos = 0;
    std::array<std::array<float, kDryRing>, 2> dryRing {};
    int dryPos = 0;
    float overrideSpeed = 0.0f;
    bool overrideActive = false;
    bool started = false;
    Meters meters {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// The rate a rotor is heading for at a speed setting. In Beat mode it comes from the tempo (the fallback tempo when `bpm` is
// not positive) and the beats-per-turn table.
[[nodiscard]] double targetHz(const Parameters& p, bool horn, double bpm = kFallbackBpm) noexcept;

// Beats per turn for a division index (clamped).
[[nodiscard]] double beatsPerTurn(float index) noexcept;

// The angle (turns, 0..1) a locked rotor has at a beat position.
[[nodiscard]] double lockedTurn(double beatPosition, double beatsPerTurn, double phaseDegrees) noexcept;

// Lobe of a source pointing at `rotorTurn` as heard by a microphone at `micTurn` (both in turns): 1 facing it, 0 facing away.
[[nodiscard]] double lobe(double rotorTurn, double micTurn) noexcept;

// Drop the CC takeover of Speed (the panel or host moved it).
void releaseOverride(EngineState& state) noexcept;

// Clears the running state (rotors stopped at angle 0, filters, delay lines). There is no patch in the state.
void resetState(EngineState& state) noexcept;

// The mean delay (samples) the dry signal is held at so wet and dry stay aligned.
[[nodiscard]] int dryDelaySamples(float sampleRate) noexcept;

void processBlock(EngineState& state, const Parameters& params, float sampleRate, std::uint32_t frames, const Transport& transport,
                  const MidiEvent* events, std::uint32_t eventCount, const float* inLeft, const float* inRight, float* outLeft, float* outRight) noexcept;

}  // namespace downspout::whirligig
