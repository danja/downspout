#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::heavyverb {

// A mastering-grade stereo reverb. Each "dimension" is a pair of parallel walls: two delay lines whose
// length is the wall spacing divided by the speed of sound, so a room is one to six independent
// axes. The 2 x N lines are coupled by a Householder (energy-preserving) feedback matrix, each loop
// has its own absorption and tone filter, and the decay time is the same for every line, so the
// tail is smooth however many dimensions are active. See docs/design.md.
//
//   in -> pre-delay -> [swell / freeze gate] -> early reflections + 4-stage diffusion -> FDN
//      -> (shimmer shifter in the feedback) -> low/high cut -> width -> [duck / gate] -> mix with dry
//
// The dry path is never touched: no latency, and Mix 0 returns the input bit for bit.

inline constexpr int kMaxDimensions = 6;
inline constexpr int kMaxLines = 2 * kMaxDimensions;
inline constexpr float kSpeedOfSound = 343.0f;   // m/s, so 1 m is 2.9 ms
inline constexpr float kMinDistance = 0.5f;
inline constexpr float kMaxDistance = 100.0f;

enum Effect : int {
    kEffectOff = 0,
    kEffectShimmer,   // pitch-shifted tail fed back into the reverb
    kEffectSwell,     // the reverb input fades in after each onset (bloom)
    kEffectDuck,      // the wet signal gets out of the way of the input
    kEffectGate,      // the wet signal is cut a set time after the input stops
    kEffectCount
};

struct Parameters {
    float dimensions = 3.0f;                                             // 1-6 active dimensions
    std::array<float, kMaxDimensions> distance {7.0f, 11.0f, 17.0f, 23.0f, 31.0f, 43.0f};   // metres between the walls
    std::array<float, kMaxDimensions> damping {35.0f, 35.0f, 35.0f, 35.0f, 35.0f, 35.0f};    // 0-100 %: air absorption, shortens high-frequency decay
    std::array<float, kMaxDimensions> filter {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};           // -100..+100 %: tone in the loop, - low-pass, + high-pass
    float decaySeconds   = 2.5f;     // 0.1-60 s, RT60 at mid frequencies
    float preDelayMs     = 12.0f;    // 0-250 ms
    float mixPercent     = 25.0f;    // 0-100 %, equal-power dry/wet
    float widthPercent   = 100.0f;   // 0-150 % of the wet signal's side level
    float modulation     = 35.0f;    // 0-100 % slow delay-length movement
    float lowCutHz       = 60.0f;    // 20-500 Hz on the wet signal
    float highCutHz      = 14000.0f; // 1000-20000 Hz on the wet signal
    float diffusion      = 70.0f;    // 0-100 % input smearing
    float earlyLevel     = 40.0f;    // 0-100 % early reflections
    float outputDb       = 0.0f;     // -24..+12 dB on the wet signal
    float effect         = 0.0f;     // Effect
    float effectAmount   = 50.0f;    // 0-100 %
    float shiftSemitones = 12.0f;    // -12..+24, used by Shimmer
    float freeze         = 0.0f;     // 0/1: hold the current tail indefinitely and stop taking input
    float preset         = 0.0f;     // 0 = Custom, 1-15; set by the panel, ignored by the engine
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;
[[nodiscard]] int dimensionCount(const Parameters& p) noexcept;   // 1-6

// Two delay-line lengths per dimension are the wall spacing over c scaled by these, so no two lines are equal.
[[nodiscard]] float lineLengthSeconds(const Parameters& p, int line) noexcept;

// Rotating-head pitch shifter: two crossfaded read heads on a short delay line. Exposed so the tests
// can measure its pitch accuracy.
struct PitchShifter {
    std::vector<float> buffer;
    uint32_t mask = 0, write = 0;
    float phase = 0.0f;
    float windowSamples = 0.0f;

    void prepare(float sampleRate);
    void reset() noexcept;
    [[nodiscard]] float process(float input, float ratio) noexcept;
};

// Everything one stereo reverb remembers. `prepare` allocates (not real-time safe); `processBlock` does not.
struct EngineState;
struct EngineDeleter { void operator()(EngineState*) const noexcept; };

[[nodiscard]] EngineState* createEngine(float sampleRate);
void destroyEngine(EngineState* state) noexcept;
void prepareEngine(EngineState& state, float sampleRate);   // reallocates for a new sample rate and clears
void resetEngine(EngineState& state) noexcept;               // clears the memory only; never touches the patch

// inputs and outputs are stereo; a null right input is treated as the left. In place is allowed.
void processBlock(EngineState&      state,
                  const Parameters& params,
                  uint32_t          frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept;

}  // namespace downspout::heavyverb
