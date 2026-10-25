#pragma once

#include <array>
#include <cstdint>

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

struct Parameters {
    float duckDepth = 80.0f;   // 0-100 %
    float attackMs  = 10.0f;   // 1-500 ms
    float releaseMs = 100.0f;  // 10-2000 ms
    float cutoffHz  = 200.0f;  // 50-5000 Hz
    float sideShape = 0.0f;    // 0=linear, 100=hard clip (waveshaper on side channel)
    float wet       = 100.0f;  // 0=dry original input, 100=fully processed
    int   bassPhase = 0;       // 0=off, 1=left bass shifted 90 deg vs right, 2=right vs left
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

struct Meters {
    float inputPeak  = 0.0f;  // peak of main input this block, 0-1 with ballistic decay
    float scLevel    = 0.0f;  // sidechain envelope follower output, 0-1
    float duckGain   = 1.0f;  // current VCA gain, 0-1 (1=no duck, 0=full duck)
    float outputPeak = 0.0f;  // mean-abs output level, 0-1 with fast-release ballistic
};

struct EngineState {
    EnvFollower  env {};
    FirBank      fir {};
    BassQuad     quad {};
    Meters       meters {};
    BusMidiState bus {};
};

}  // namespace downspout::bassops
