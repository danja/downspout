#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace downspout::bassops {

// Linear-phase FIR order (must be even); latency = kFirOrder/2 frames
inline constexpr std::uint32_t kFirOrder = 128;
inline constexpr std::uint32_t kFirTaps  = kFirOrder + 1;

// Producer Control Bus MIDI CC contract
inline constexpr std::uint8_t kProducerLifecycleCC = 19;
inline constexpr std::uint8_t kDuckDepthCC         = 34;
inline constexpr std::uint8_t kWetCC               = 35;

struct MidiControlEvent {
    std::uint32_t frame = 0;
    std::uint8_t  size  = 0;
    std::uint8_t  data[3] {};
};

struct BusMidiState {
    bool  producerActive = false;
    bool  duckMidiActive = false;
    bool  wetMidiActive  = false;
    float duckMidiValue  = 0.0f;    // 0-100 %
    float wetMidiValue   = 100.0f;  // 0-100 %
};

// ── Input delay ("delay ducking") ──────────────────────────────────────────────
//
// The main inputs can be shifted later in time, so a bass note lands just after the kick that
// ducks it instead of on top of it. Two controls add together: a tempo-locked one in fractions
// of a beat, and a free one in milliseconds. It is a pure time shift (no feedback, no mix) and is
// deliberately not reported to the host as latency.
inline constexpr std::size_t kBeatSteps = 12;

// Delay in beats for each step of the beat slider. Step 0 is off.
inline constexpr std::array<double, kBeatSteps> kBeatFractions = {{
    0.0, 1.0 / 64.0, 1.0 / 32.0, 1.0 / 16.0, 1.0 / 12.0, 1.0 / 8.0,
    1.0 / 6.0, 1.0 / 4.0, 1.0 / 3.0, 1.0 / 2.0, 3.0 / 4.0, 1.0,
}};

inline constexpr std::array<const char*, kBeatSteps> kBeatNames = {{
    "Off", "1/64", "1/32", "1/16", "1/12", "1/8", "1/6", "1/4", "1/3", "1/2", "3/4", "1",
}};

inline constexpr float  kMaxDelayMs      = 200.0f;
inline constexpr double kMaxDelaySeconds = 2.0;     // the longest total delay; longer requests are cut to it
inline constexpr double kDelayFadeSeconds = 0.010;  // crossfade when the delay changes
inline constexpr double kDefaultBpm      = 120.0;   // used when the host gives no tempo

// What the host says about time. Only the tempo matters here.
struct Transport {
    double bpm = kDefaultBpm;  // <= 0 or not finite: treated as kDefaultBpm
};

struct Parameters {
    float duckDepth = 80.0f;   // 0-100 %
    float attackMs  = 10.0f;   // 1-500 ms
    float releaseMs = 100.0f;  // 10-2000 ms
    float cutoffHz  = 200.0f;  // 50-5000 Hz
    float sideShape = 0.0f;    // 0=linear, 100=hard clip (waveshaper on side channel)
    float wet       = 100.0f;  // 0=dry original input, 100=fully processed
    int   bassPhase = 0;       // 0=off, 1=left bass shifted 90 deg vs right, 2=right vs left
    int   delayBeat = 0;       // 0..kBeatSteps-1: main-input delay in fractions of a beat (0 = off)
    float delayMs   = 0.0f;    // 0-200 ms: main-input delay, added to delayBeat
};

inline constexpr std::uint32_t kMidDelayLen = kFirOrder / 2;  // 64 samples (FIR group delay)

struct FirBank {
    // mid = mono delayed by the FIR latency (bufMid); side = HP(shaped - mono), the
    // harmonics the waveshaper adds above the cutoff. Zero at shape=0, so outL=outR.
    // (lp is only used to build hp = delta - lp.)
    std::array<float, kFirTaps>    lp {};
    std::array<float, kFirTaps>    hp {};
    std::array<float, kFirTaps>    bufMid {};      // convolution buffer for LP path (also read for bass)
    std::array<float, kFirTaps>    bufSide {};     // convolution buffer for HP path
    std::array<float, kMidDelayLen> dryDelayL {};  // delay dry L to match FIR latency
    std::array<float, kMidDelayLen> dryDelayR {};  // delay dry R to match FIR latency
    std::uint32_t pos      = 0;
    std::uint32_t delayPos = 0;
    float lastCutoffHz   = -1.0f;
    float lastSampleRate = 0.0f;
};

// Quadrature allpass pair (12-pole Hilbert network). Both paths are flat in magnitude
// and differ by 90 degrees in phase. Runs only while Parameters::bassPhase != 0.
inline constexpr std::uint32_t kQuadSections = 4;

struct AllpassSection {
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

struct BassQuad {
    std::array<AllpassSection, kQuadSections> pathA {};
    std::array<AllpassSection, kQuadSections> pathB {};
    float delayB = 0.0f;   // path B takes its input one sample late
    bool  running = false;  // state is live; cleared when the feature is switched off
};

struct EnvFollower {
    float envelope = 0.0f;
    float mainEnvL = 0.0f;  // |main L| and |main R|, to spot a mono feed
    float mainEnvR = 0.0f;
};

// Two taps one crossfade apart, so a change of delay is a smooth blend, never a jump or a pitch glide.
struct InputDelay {
    std::vector<float> bufL;
    std::vector<float> bufR;
    std::size_t write = 0;
    std::size_t current = 0;   // delay in samples now sounding
    std::size_t previous = 0;  // delay being faded out
    std::size_t fadeLeft = 0;  // samples left in the crossfade; 0 = settled on `current`
    std::size_t fadeLength = 1;
    bool primed = false;       // false until the first block after activation, which starts settled
};

struct Meters {
    float inputPeak  = 0.0f;  // peak of main input this block, 0-1 with ballistic decay
    float scLevel    = 0.0f;  // sidechain envelope follower output, 0-1
    float duckGain   = 1.0f;  // current VCA gain, 0-1 (1=no duck, 0=full duck)
    float outputPeak = 0.0f;  // mean-abs output level, 0-1 with fast-release ballistic
    float delayMs    = 0.0f;  // total input delay now requested (beat part at the host tempo + ms), in ms
};

struct EngineState {
    EnvFollower  env {};
    FirBank      fir {};
    BassQuad     quad {};
    Meters       meters {};
    BusMidiState bus {};
    InputDelay   delay {};
};

}  // namespace downspout::bassops
