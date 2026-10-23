#pragma once

#include <array>
#include <cstdint>

namespace downspout::monochord {

// Eight-voice waveguide string: a plucked, bowed or hammered string with a tuned loop, a loss filter, stiffness,
// a pickup comb and a small body. Inspired by the Tension device in oxide-studio's oxide-dsp, but a redesign
// (see docs/design.md): the source's loop filters add delay that it does not compensate, so it plays flat.

inline constexpr int kVoices = 8;
inline constexpr int kDelaySize = 16384;      // fits 20 Hz up to 192 kHz

enum class Exciter : int { Pluck = 0, Bow, Hammer };

struct Parameters {
    float exciter     = 0.0f;   // Exciter
    float position    = 0.2f;   // 0.05..0.5: where along the string it is plucked, struck or bowed
    float hardness    = 0.6f;   // 0..1: a soft pluck or a wide hammer at 0, a sharp one at 1
    float bowPressure = 0.5f;   // 0..1
    float decaySec    = 3.0f;   // 0.1..20: time for the fundamental to fall 60 dB
    float damping     = 0.3f;   // 0..1: how much faster the high partials die
    float stiffness   = 0.5f;   // 0..1: sharpens the upper partials (a thick string); the squared setting is the fraction of a quarter period of dispersion
    float bodySize    = 1.0f;   // 0.5..2: scales the body resonances
    float bodyLevel   = 0.3f;   // 0..1
    float pickup      = 0.0f;   // 0..0.5: pickup comb position along the string; 0 = off
    float releaseMs   = 250.0f; // 5..2000: how fast a released string is damped
    float master      = 0.6f;   // 0..1
    float velocity    = 0.7f;   // 0..1 (0 = every note at full level)
    float bendRange   = 2.0f;   // 0..24 semitones
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

struct MidiEvent {
    enum class Type : std::uint8_t { NoteOn, NoteOff, Controller, PitchBend };
    std::uint32_t frame = 0;
    Type type = Type::NoteOn;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint8_t channel = 0;   // 0-15: notes and bends are per channel (Retune sends one note per channel, each with its own bend)
    int bend = 0;
};

// The coefficients of one string loop: an integer delay, a first-order all-pass for the fraction, a one-pole
// low-pass for damping, a first-order all-pass for stiffness, and the loop gain.
struct LoopTuning {
    int    delay = 100;        // whole samples
    double fractionCoeff = 0.0;
    double lowpass = 0.0;      // pole of the damping filter; 0 = none
    int    stiffStages = 0;    // identical first-order all-pass sections that make the string stiff (0 = none)
    double stiffCoeff = 0.0;   // their coefficient (negative: more delay at low frequencies than at high)
    double gain = 0.999;       // per loop pass at the fundamental, after the low-pass
};

struct Biquad {
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
};

struct Voice {
    bool active = false;
    int channel = 0;          // the MIDI channel the note arrived on
    bool held = false;
    bool sustained = false;
    int note = 60;
    float velocity = 1.0f;     // the note's loudness after the Velocity control (what scales the excitation)
    std::uint64_t age = 0;
    std::array<float, kDelaySize> line {};
    int writePos = 0;
    LoopTuning tuning {};
    double lowpassState = 0.0;
    double fracX1 = 0.0, fracY1 = 0.0;       // fractional-delay all-pass memory
    std::array<double, 4> stiffX1 {}, stiffY1 {};   // stiffness all-pass memory, one pair per stage
    double bowSpeed = 0.0;
    bool bowing = false;     // the bow is on the string now
    bool bowed = false;      // this voice was started by the bow (its output follows the velocity)
    double envelope = 1.0;                   // damper after note-off
    double peak = 0.0;                       // output peak follower, to free a voice whose string has died
    int controlCountdown = 0;
    std::array<Biquad, 2> body {};
};

struct EngineState {
    std::array<Voice, kVoices> voices {};
    std::uint64_t counter = 0;
    bool sustainPedal = false;
    std::array<float, 16> bend {}, bendTarget {};   // semitones per MIDI channel (smoothed, target)
    float masterSmooth = 0.6f;
    double dcX = 0.0, dcY = 0.0;
    bool primed = false;
};

[[nodiscard]] int activeVoices(const EngineState& state) noexcept;
void resetState(EngineState& state) noexcept;

// Renders `frames` frames (mono, written to both outputs), replacing the contents.
void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  std::uint32_t     frames,
                  const MidiEvent*  events,
                  std::uint32_t     eventCount,
                  float*            outLeft,
                  float*            outRight) noexcept;

// Works out the loop for a fundamental: the all-pass fraction is chosen so that the phase delay of the whole loop
// at that frequency is exactly one period, which is what keeps the string in tune. Exposed for the tests.
[[nodiscard]] LoopTuning tuneLoop(double frequencyHz, const Parameters& params, double sampleRate) noexcept;

// Total phase delay of the loop at `hz`, in samples, from the coefficients in use.
[[nodiscard]] double loopPhaseDelay(const LoopTuning& t, double hz, double sampleRate) noexcept;

// The displacement written into the string at note-on (zero mean, peak 1; all zero for the bow), n points along the string.
// The panel plots it and the tests check it.
void excitation(Exciter exciter, int n, const Parameters& params, float* out) noexcept;

}  // namespace downspout::monochord
