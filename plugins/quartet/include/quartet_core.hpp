#pragma once

#include "downspout/dsp/svf.hpp"

#include <array>
#include <cstdint>

namespace downspout::quartet {

// Four-operator FM synthesizer: eleven algorithms, band-limited operator waveforms, optional 2x
// oversampling, one pitch envelope and one filter. Inspired by the Operator device in oxide-studio's
// oxide-dsp, but a redesign (see docs/design.md).

inline constexpr int kOperators = 4;      // A (0), B (1), C (2), D (3): a modulator always has a higher index than its target
inline constexpr int kAlgorithms = 11;
inline constexpr int kVoices = 8;
inline constexpr int kWaveSize = 2048;
inline constexpr int kWaveMips = 11;      // harmonic limits 1024 >> k
inline constexpr int kWavePad = 4;

enum class Wave : int { Sine = 0, Saw, Square };
enum class FilterType : int { LowPass12 = 0, LowPass24, HighPass12, BandPass, Notch };

struct OperatorParams {
    float wave      = 0.0f;     // Wave
    float fixedMode = 0.0f;     // 0 follows the note (ratio), 1 is a fixed frequency
    float ratio     = 1.0f;     // 0.25..32 times the note
    float fineCents = 0.0f;     // -50..+50
    float fixedHz   = 440.0f;   // 10..20000
    float level     = 0.8f;     // 0..1: output level of a carrier, FM amount of a modulator
    float attackMs = 5.0f, decayMs = 300.0f, sustain = 0.7f, releaseMs = 250.0f;
};

struct Parameters {
    std::array<OperatorParams, kOperators> op {};
    float algorithm   = 0.0f;   // 0..10
    float feedback    = 0.15f;  // 0..1: operator D into itself
    float depth       = 1.0f;   // 0..4: phase modulation of a full-level modulator, in cycles
    float pitchEnvOn  = 0.0f;
    float pitchEnvSemitones = 12.0f;   // -48..+48
    float pitchAttackMs = 1.0f, pitchDecayMs = 100.0f, pitchSustain = 0.0f, pitchReleaseMs = 100.0f;
    float filterType  = 1.0f;   // FilterType
    float cutoffHz    = 8000.0f;
    float resonance   = 0.15f;
    float keyTrack    = 0.0f;   // 0..1
    float master      = 0.6f;   // 0..1
    float velocity    = 0.7f;   // 0..1 (0 = every note at full level)
    float bendRange   = 2.0f;   // 0..24 semitones
    float quality     = 1.0f;   // 0 = 1x, 1 = 2x oversampling
    float playMode    = 0.0f;   // PlayMode
    float glideMs     = 0.0f;   // 0..2000: portamento from the previous note (0 = off)
    Parameters();               // operators B, C, D as modulators of A
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// One algorithm: which operators modulate each operator, and which are heard.
struct Algorithm {
    std::array<unsigned, kOperators> modulators;  // bitmask of operators (bit i = operator i) that modulate this one
    unsigned carriers;                             // bitmask of operators mixed to the output
};
[[nodiscard]] const Algorithm& algorithm(int index) noexcept;
[[nodiscard]] int carrierCount(const Algorithm& a) noexcept;
// Layer of an operator in the algorithm: 0 for a carrier, 1 + the highest layer it modulates otherwise.
[[nodiscard]] int operatorLayer(const Algorithm& a, int op) noexcept;

struct MidiEvent {
    enum class Type : std::uint8_t { NoteOn, NoteOff, Controller, PitchBend };
    std::uint32_t frame = 0;
    Type type = Type::NoteOn;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint8_t channel = 0;   // 0-15: notes and bends are per channel (Retune sends one note per channel, each with its own bend)
    int bend = 0;
};

struct Envelope {
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Release };
    Stage stage = Stage::Idle;
    double value = 0.0;
};

// The trapezoidal state-variable filter is shared (include/downspout/dsp/svf.hpp).
using FilterCoeff = downspout::dsp::SvfCoeff;
using Svf = downspout::dsp::Svf;

inline constexpr int kDecimatorTaps = 63;

// Poly: every note its own voice. Mono: one voice, last note wins, a new note retriggers the envelopes and a released
// note returns to the one still held. Legato: the same, but the envelopes carry on while notes overlap.
enum class PlayMode : int { Poly = 0, Mono, Legato };

struct HeldNote {
    std::uint8_t note = 0, channel = 0, velocity = 0;
};

struct Voice {
    bool active = false;
    double glide = 0.0;       // semitones still to slide: the sounding pitch is note + glide, and glide decays to 0
    double pendingGlide = 0.0;
    int channel = 0;          // the MIDI channel the note arrived on
    // A stolen voice fades out over about 2 ms, then starts the note that stole it (no click from cutting it off).
    bool stealing = false;
    float fade = 1.0f;
    int pendingNote = -1, pendingVelocity = 0, pendingChannel = 0;
    bool held = false;
    bool sustained = false;
    int note = 60;
    float velocity = 1.0f;
    std::uint64_t age = 0;
    std::array<double, kOperators> phase {};
    double feedbackY1 = 0.0, feedbackY2 = 0.0;
    std::array<Envelope, kOperators> env {};
    Envelope pitchEnv;
    Svf filter1, filter2;
    // Control rate (every 32 samples): operator frequencies and the filter.
    std::array<double, kOperators> opHz {};
    FilterCoeff coeff1 {}, coeff2 {};
    int controlCountdown = 0;
    // Half-band decimator history (the oversampled signal).
    std::array<float, kDecimatorTaps> history {};
    int historyPos = 0;
};

struct EngineState {
    std::array<Voice, kVoices> voices {};
    std::uint64_t counter = 0;
    bool sustainPedal = false;
    std::array<HeldNote, 16> held {};     // keys down, oldest first (the mono modes play the newest)
    int heldCount = 0;
    int lastNote = -1;                    // the previous note, for portamento
    std::array<float, 16> bend {}, bendTarget {};   // semitones per MIDI channel (smoothed, target)
    float cutoffSmooth = 8000.0f, masterSmooth = 0.6f;
    double dcX = 0.0, dcY = 0.0;
    bool primed = false;
};

[[nodiscard]] int activeVoices(const EngineState& state) noexcept;
void resetState(EngineState& state) noexcept;

// Renders `frames` frames (the output is mono; it is written to both channels), replacing the outputs.
void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  std::uint32_t     frames,
                  const MidiEvent*  events,
                  std::uint32_t     eventCount,
                  float*            outLeft,
                  float*            outRight) noexcept;

// Attack linear in time; decay and release one-pole exponentials. Exposed so the tests can hold the audio to it.
[[nodiscard]] double envelopeStep(Envelope& env, bool gate, double attackMs, double decayMs, double sustain, double releaseMs,
                                  double sampleRate) noexcept;

// The band-limited operator waveform read, for the panel and the tests: wave, phase 0..1, harmonics allowed.
[[nodiscard]] float readWave(Wave wave, double phase, double allowedHarmonics) noexcept;
[[nodiscard]] int waveMipForHarmonics(double allowed) noexcept;
[[nodiscard]] int waveHarmonicLimit(int mip) noexcept;
[[nodiscard]] const float* waveTable(Wave wave, int mip) noexcept;   // kWaveSize + kWavePad floats, index 1 is phase 0

// The half-band decimator's taps (63, symmetric, unity DC gain).
[[nodiscard]] const std::array<float, kDecimatorTaps>& decimatorTaps() noexcept;

// The filter's modelled response in dB (same coefficients as the audio).
[[nodiscard]] double filterResponseDb(FilterType type, double cutoffHz, double resonance, double hz, double sampleRate) noexcept;

}  // namespace downspout::quartet
