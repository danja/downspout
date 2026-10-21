#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::grist {

using downspout::generative::ParamSpec;

inline constexpr int kDivisionCount = 19;  // 1/64 ... 8/1, with triplets and dotted values
inline constexpr int kMaxDensity = 4;      // grains per window
inline constexpr int kMaxGrains = 16;
inline constexpr int kScaleCount = 7;      // Free, Semitones, Octaves, Fifths + octaves, Major, Minor, Pentatonic
inline constexpr float kMaxWindowMs = 1000.0f;
inline constexpr float kMaxDelayMs = 1000.0f;
inline constexpr double kBufferSeconds = 7.0;  // longest grain delay: 2 windows of blur + 3 of pitch headroom + a reversed grain

enum Param : std::uint32_t {
    kMix,
    kWindow,
    kWindowSync,
    kWindowDivision,
    kPitch,
    kDensity,
    kBlur,
    kReverse,
    kSpread,
    kShape,
    kRange,
    kDirection,
    kScale,
    kVariance,
    kFeedback,
    kDelay,
    kDelaySync,
    kDelayDivision,
    kFilterOn,
    kFilterMode,
    kFilterFreq,
    kFilterWidth,
    kSeed,
    kStatusLatency,
    kStatusGrains,
    kStatusWindow,
    kStatusDelay,
    kParameterCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"mix", "Dry/wet", 0.0f, 1.0f, 0.5f},
    {"window", "Window", 1.0f, kMaxWindowMs, 50.0f},
    {"window_sync", "Window sync", 0, 1, 0, true},
    {"window_division", "Window division", 0, kDivisionCount - 1, 4, true},
    {"pitch", "Pitch", -24.0f, 24.0f, 0.0f},
    {"density", "Density", 1, kMaxDensity, 2, true},
    {"blur", "Blur", 0.0f, 2.0f, 0.5f},
    {"reverse", "Reverse", 0.0f, 1.0f, 0.0f},
    {"spread", "Spread", 0.0f, 1.0f, 0.3f},
    {"shape", "Shape", 0.0f, 1.0f, 0.0f},
    {"range", "Range", 0.0f, 24.0f, 0.0f},
    {"direction", "Direction", 0, 2, 2, true},
    {"scale", "Scale", 0, kScaleCount - 1, 1, true},
    {"variance", "Variance", 0.0f, 1.0f, 0.0f},
    {"feedback", "Feedback", 0.0f, 0.95f, 0.0f},
    {"delay", "Feedback delay", 1.0f, kMaxDelayMs, 100.0f},
    {"delay_sync", "Delay sync", 0, 1, 0, true},
    {"delay_division", "Delay division", 0, kDivisionCount - 1, 4, true},
    {"filter_on", "Feedback filter", 0, 1, 0, true},
    {"filter_mode", "Filter mode", 0, 2, 0, true},
    {"filter_freq", "Filter freq", 0.0f, 1.0f, 0.5f},
    {"filter_width", "Filter width", 0.0f, 1.0f, 0.7f},
    {"seed", "Seed", 1, 65535, 1, true},
    {"status_latency", "Latency", 0.0f, 6000.0f, 0.0f, false, true},
    {"status_grains", "Active grains", 0, kMaxGrains, 0, true, true},
    {"status_window", "Effective window", 0.0f, kMaxWindowMs, 0.0f, false, true},
    {"status_delay", "Effective delay", 0.0f, kMaxDelayMs, 0.0f, false, true},
}};

enum Direction { kUp = 0, kDown = 1, kBoth = 2 };
enum FilterMode { kLowpass = 0, kHighpass = 1, kBandpass = 2 };

// Small deterministic generator, so a seed reproduces a pass exactly.
struct Rng {
    std::uint64_t s = 0x9e3779b97f4a7c15ull;
    void seed(std::uint32_t seed) noexcept { s = (0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(seed) * 0xbf58476d1ce4e5b9ull)) | 1ull; }
    float next() noexcept  // [0, 1)
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<float>(((s * 0x2545f4914f6cdd1dull) >> 40) & 0xffffffu) / 16777216.0f;
    }
};

// Window length of a grain at position p (0..1), blending a Hann bell (shape 0) with a
// percussive curve (shape 1). Both average 0.5 over the grain, so Shape does not change level.
float shapeAt(float shape, float p) noexcept;

// Length of a tempo division, in quarter notes (index into the 19 divisions).
double divisionQuarters(int division) noexcept;
const char* divisionName(int division) noexcept;
const char* scaleName(int scale) noexcept;
const char* directionName(int direction) noexcept;
const char* filterModeName(int mode) noexcept;

// Window / feedback delay in ms, free or tied to `bpm`, clamped to the parameter range.
double windowMs(const std::array<float, kParameterCount>& p, double bpm) noexcept;
double delayMs(const std::array<float, kParameterCount>& p, double bpm) noexcept;
double filterHz(float amount) noexcept;  // 0..1 -> 20 Hz .. 15 kHz, exponential

// Average delay of the grains: Blur x Window / 2, plus (ratio - 1) x Window when pitched up
// (a read pointer that outruns the write pointer needs that much head start).
double latencyMs(double windowMs, double blur, double semitones) noexcept;

// Whether a grain shifted by `semitones` in total is on `scale` (counted from the input's pitch).
bool scaleAllows(int scale, int semitones) noexcept;

// One grain's pitch offset around `centre` semitones: within +-range in `direction`, on `scale`
// (Free: any value). 0 means "stay on PITCH".
float drawOffset(Rng& rng, int centre, float range, int direction, int scale) noexcept;

struct CloudGrain {
    float offset = 0.0f;  // semitones from PITCH
    float size = 1.0f;    // fraction of the window
};
// The pitches and lengths the engine would draw, for the panel's cloud sketch.
std::vector<CloudGrain> cloudPreview(float pitch, float range, int direction, float variance, int scale, int count,
                                     std::uint32_t seed);

struct Grain {
    bool active = false;
    double pos = 0.0;   // read position, in samples of the input history
    double step = 1.0;  // samples advanced per output sample (negative: reversed)
    int age = 0;
    int length = 1;
    float gainL = 1.0f;
    float gainR = 1.0f;
};

struct Svf {
    float s1 = 0.0f;
    float s2 = 0.0f;
};

struct State {
    double sampleRate = 0.0;
    std::vector<float> bufferL, bufferR;      // input history (plus feedback)
    std::vector<float> delayL, delayR;        // feedback delay line
    std::uint64_t written = 0;                // samples written to the history
    std::uint64_t delayWritten = 0;
    std::array<Grain, kMaxGrains> grains {};
    double untilNextGrain = 0.0;
    Rng rng {};
    int seededWith = -1;
    Svf filterL {}, filterR {};
    float mixSmooth = -1.0f;
    float shapeValue = -1.0f;
    std::array<float, 513> shapeTable {};
    int statusGrains = 0;
    float statusLatency = 0.0f;
    float statusWindow = 0.0f;
    float statusDelay = 0.0f;
};

void prepare(State&, double sampleRate);
void reset(State&) noexcept;

// Stereo in, stereo out; the buffers may alias. `bpm` ties the synced window and delay to the host.
void process(State&, const std::array<float, kParameterCount>&, std::uint32_t frames, double sampleRate, double bpm,
             const float* inL, const float* inR, float* outL, float* outR) noexcept;

}  // namespace downspout::grist
