#pragma once

#include "strata_tables.hpp"

#include <array>
#include <cstdint>

namespace downspout::strata {

// Eight-voice wavetable synthesizer: two band-limited table oscillators with warp, a sine sub,
// unison, a resonant filter, two envelopes and an LFO. Inspired by the Wavetable device in
// oxide-studio's oxide-dsp, but a redesign (see docs/design.md).

inline constexpr int kVoices = 8;
inline constexpr int kMaxUnison = 8;

enum class WarpMode : int { None = 0, Bend, Sync, Fold };
enum class UnisonMode : int { Classic = 0, Shimmer, Noise };
enum class FilterType : int { LowPass12 = 0, LowPass24, HighPass12, BandPass, Notch };
enum class LfoShape : int { Sine = 0, Triangle, Saw, Square, SampleHold };

struct OscParams {
    float table      = 0.0f;    // Category 0..4
    float position   = 0.2f;    // 0..1 across the eight frames
    float warpMode   = 0.0f;    // WarpMode
    float warpAmount = 0.0f;    // 0..1
    float transpose  = 0.0f;    // -24..+24 semitones
    float fineCents  = 0.0f;    // -50..+50
    float level      = 0.8f;    // 0..1
};

struct Parameters {
    std::array<OscParams, 2> osc {};
    float subLevel   = 0.3f;    // 0..1
    float subOctave  = 0.0f;    // 0 = one octave down, 1 = two
    float unisonMode   = 0.0f;  // UnisonMode
    float unisonVoices = 2.0f;  // 1..8
    float unisonAmount = 0.15f; // 0..1
    float filterType = 0.0f;    // FilterType
    float cutoffHz   = 4000.0f; // 20..20000
    float resonance  = 0.2f;    // 0..1
    float keyTrack   = 0.0f;    // 0..1 (1 = the cutoff follows the note)
    float envAmount  = 0.0f;    // -1..+1 (x 4 octaves of cutoff)
    float envPosition = 0.0f;   // -1..+1 (filter envelope to both table positions)
    float ampAttackMs = 5.0f, ampDecayMs = 300.0f, ampSustain = 0.7f, ampReleaseMs = 250.0f;
    float fltAttackMs = 10.0f, fltDecayMs = 400.0f, fltSustain = 0.3f, fltReleaseMs = 300.0f;
    float lfoRateHz  = 2.0f;    // 0.05..20
    float lfoShape   = 0.0f;    // LfoShape
    float lfoPosition = 0.0f;   // 0..1 (+-0.5 of the table at 1)
    float lfoPitch   = 0.0f;    // 0..1 (+-1 semitone at 1)
    float lfoCutoff  = 0.0f;    // 0..1 (+-2 octaves at 1)
    float master     = 0.6f;    // 0..1
    float velocity   = 0.7f;    // 0..1 (0 = every note at full level)
    float bendRange  = 2.0f;    // 0..24 semitones
    float playMode   = 0.0f;    // PlayMode
    float glideMs    = 0.0f;    // 0..2000: portamento from the previous note (0 = off)
};

// Poly: every note its own voice. Mono: one voice, last note wins, a new note retriggers the envelopes and a released
// note returns to the one still held. Legato: the same, but the envelopes carry on while notes overlap.
enum class PlayMode : int { Poly = 0, Mono, Legato };

struct HeldNote {
    std::uint8_t note = 0, channel = 0, velocity = 0;
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

struct MidiEvent {
    enum class Type : std::uint8_t { NoteOn, NoteOff, Controller, PitchBend };
    std::uint32_t frame = 0;
    Type type = Type::NoteOn;
    std::uint8_t data1 = 0;   // note or controller number
    std::uint8_t data2 = 0;   // velocity or controller value
    std::uint8_t channel = 0; // 0-15: notes and bends are per channel (Retune sends one note per channel, each with its own bend)
    int bend = 0;             // PitchBend: -8192..8191
};

// Filter coefficients for the trapezoidal SVF: g = tan(pi fc / sr), k = 1 / Q.
struct FilterCoeff {
    double g = 0.0, k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
};

// Trapezoidal state-variable filter (zero-delay feedback), one per channel per voice.
struct Svf {
    double ic1 = 0.0, ic2 = 0.0;
};

struct Envelope {
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Release };
    Stage stage = Stage::Idle;
    double value = 0.0;
};

struct Voice {
    bool active = false;
    int channel = 0;          // the MIDI channel the note arrived on
    double glide = 0.0;       // semitones still to slide: the sounding pitch is note + glide, and glide decays to 0
    double pendingGlide = 0.0;
    // A stolen voice fades out over about 2 ms, then starts the note that stole it (no click from cutting it off).
    bool stealing = false;
    int pendingNote = -1, pendingVelocity = 0, pendingChannel = 0;
    bool held = false;        // key down
    bool sustained = false;   // key up but the pedal holds it
    int note = 60;
    float velocity = 1.0f;
    std::uint64_t age = 0;
    std::array<std::array<double, kMaxUnison>, 2> phase {};
    // Fold's antiderivative antialiasing needs the previous table sample and the cosine of its argument.
    std::array<std::array<float, kMaxUnison>, 2> foldPrev {};
    std::array<std::array<float, kMaxUnison>, 2> foldCosPrev {};
    double subPhase = 0.0;
    std::array<float, kMaxUnison> noiseDetune {};   // slow random drift, Noise unison
    // Recomputed every 32 samples (control rate): the per-voice unison detune ratio and pan gains.
    std::array<double, kMaxUnison> detuneRatio {};
    std::array<float, kMaxUnison> panL {}, panR {};
    int controlCountdown = 0;
    // Also at control rate: the oscillators' and sub's base frequencies and the filter's coefficients.
    std::array<double, 2> oscHz {};
    double subHz = 0.0;
    FilterCoeff coeff1 {}, coeff2 {};
    std::array<Svf, 2> filter1 {};                  // left, right
    std::array<Svf, 2> filter2 {};                  // second stage of the 24 dB low-pass
    Envelope amp, flt;
};

struct EngineState {
    std::array<Voice, kVoices> voices {};
    std::uint64_t counter = 0;
    std::uint32_t rng = 0x2468ace1u;
    double lfoPhase = 0.0;
    float lfoHeld = 0.0f;
    bool sustainPedal = false;
    std::array<HeldNote, 16> held {};     // keys down, oldest first (the mono modes play the newest)
    int heldCount = 0;
    int lastNote = -1;                    // the previous note, for portamento
    std::array<float, 16> bend {};        // semitones per MIDI channel, smoothed
    std::array<float, 16> bendTarget {};  // semitones per MIDI channel
    // Smoothed copies of the parameters that would otherwise step at a block edge.
    float posSmooth[2] {0.2f, 0.2f};
    float levelSmooth[2] {0.8f, 0.8f};
    float subSmooth = 0.3f;
    float cutoffSmooth = 4000.0f;
    float masterSmooth = 0.6f;
    double dcX[2] {0.0, 0.0}, dcY[2] {0.0, 0.0};   // output DC blocker memory
    bool primed = false;
};

// Number of voices currently sounding (including releasing ones).
[[nodiscard]] int activeVoices(const EngineState& state) noexcept;

// Clears voices and filters only; never touches the patch.
void resetState(EngineState& state) noexcept;

// Renders `frames` stereo frames, replacing the contents of the outputs. `events` must be sorted by frame.
void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  std::uint32_t     frames,
                  const MidiEvent*  events,
                  std::uint32_t     eventCount,
                  float*            outLeft,
                  float*            outRight) noexcept;

// The envelope shape, exposed so the tests can hold the audio to it.
// Attack is linear in time; decay and release are one-pole exponentials.
[[nodiscard]] double envelopeStep(Envelope& env, bool gate, double attackMs, double decayMs, double sustain, double releaseMs,
                                  double sampleRate) noexcept;

// The phase warp: maps a cycle phase (0..1) to the phase the table is read at, and reports how much faster
// than the note the read runs there (the mip choice needs it). Shared by the audio and the panel's waveform.
[[nodiscard]] double warpPhase(WarpMode mode, double amount, double phase, double* slope = nullptr) noexcept;

// One LFO step (-1..+1), exposed for the tests.
[[nodiscard]] double lfoStep(EngineState& state, LfoShape shape, double rateHz, double sampleRate) noexcept;

// The state-variable filter's response for a given cutoff, resonance and type, from the same
// coefficients the audio uses. dB, small signal.
[[nodiscard]] double filterResponseDb(FilterType type, double cutoffHz, double resonance, double hz, double sampleRate) noexcept;

}  // namespace downspout::strata
