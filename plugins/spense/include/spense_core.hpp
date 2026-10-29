#pragma once

#include <array>
#include <cstdint>

namespace downspout::spense {

// Stereo phase disperser: a stack of cascades of second-order all-pass filters. A port of the
// DSP in ~/github/Dispenser (MIT, JUCE/AVX2); see docs/design.md for what was kept and changed.
//
//   input -> Pre Gain -> Stack x (8 all-pass stages) -> Post Gain -> optional 0 dB clip -> output

inline constexpr int kStagesPerCascade = 8;
inline constexpr int kMaxStack = 8;
// The stages in a cascade are chained through a one-sample delay each (that is what let the source
// run the eight in parallel SIMD lanes), so a cascade delays the signal by seven samples.
inline constexpr int kLatencyPerCascade = kStagesPerCascade - 1;
inline constexpr float kMinGainDb = -100.0f;   // the bottom of the gain controls is silence

struct Parameters {
    float stack       = 1.0f;      // 1-8 cascades in series (8 all-pass stages each)
    float frequencyHz = 1000.0f;   // 40-20000 Hz centre of the dispersion
    float resonance   = 0.707f;    // 0.1-10 Q; low Q is a broad, gentle smear, high Q a narrow, long one
    float drift       = 0.0f;      // 0-1; spreads the eight stages of a cascade apart in frequency
    float preGainDb   = 0.0f;      // -100 (silence) to +12 dB before the cascade
    float postGainDb  = 0.0f;      // -100 (silence) to +12 dB after it
    float clip        = 0.0f;      // 0/1: hard clip at 0 dBFS after Post Gain
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;
[[nodiscard]] int stackCount(const Parameters& p) noexcept;               // stack rounded to 1-8
[[nodiscard]] int latencySamples(const Parameters& p) noexcept;           // stack x 7, what the host should compensate
[[nodiscard]] float gainFromDb(float db) noexcept;                        // <= -100 dB is exactly silence

// A linear ramp to a new target over a fixed number of samples (JUCE's SmoothedValue, 10 ms).
struct Ramp {
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0;

    void snap(float value) noexcept { current = target = value; step = 0.0f; remaining = 0; }
    void setTarget(float value, int steps) noexcept;
    [[nodiscard]] float next() noexcept;
};

// Per-channel filter memory of one cascade: two integrators and the last output per stage.
struct CascadeState {
    std::array<float, kStagesPerCascade> s1 {}, s2 {}, out {};
};

struct EngineState {
    std::array<std::array<CascadeState, kMaxStack>, 2> channels {};
    Ramp frequency, resonance, drift, preGain, postGain;
    int activeStack = 1;
    bool primed = false;   // the first block snaps to its targets instead of gliding from zero
};

// Clears the filter memory only; never touches the patch.
void resetState(EngineState& state) noexcept;

// inputs and outputs are stereo; a null right input is treated as the left. In place is allowed.
void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  uint32_t          frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept;

// ---- Analysis: what the panel plots and what the tests hold against the real filter ---------

// Centre frequency of stage `stage` (0-7) of a cascade on `channel` (0 left, 1 right) once the
// drift has settled. Left and right use different offsets so the drift also widens the image.
[[nodiscard]] float stageFrequencyHz(const Parameters& p, int stage, int channel, float sampleRate) noexcept;

// Phase of the whole stack (all-pass stages and the pure seven-sample delays) in radians, wrapped to +-pi.
[[nodiscard]] double phaseRadians(const Parameters& p, double hz, int channel, double sampleRate) noexcept;

// Group delay of the stack's all-pass stages in samples, not counting the fixed seven-sample
// delay per cascade (that is latency, not dispersion). This is the curve the panel draws.
[[nodiscard]] double groupDelaySamples(const Parameters& p, double hz, int channel, double sampleRate) noexcept;

}  // namespace downspout::spense
