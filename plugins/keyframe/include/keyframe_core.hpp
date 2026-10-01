#pragma once

#include "keyframe_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::keyframe {

// ── Extrema-sampling time stretching ────────────────────────────────────────
//
// Implementation of "Keyframe Time Stretching via Extrema Sampling"
// (Nielsen, DAFx26). The analysis stage reduces each input channel to a
// timestamped set of local extrema ("keyframes") whose spacing encodes local
// information density, and the playback stage runs three playheads over that
// sparse buffer: a reference playhead marking where playback should be, an
// audio playhead running at the pitch rate, and a temporary playhead that
// crossfades in when the two drift more than K keyframes apart. The splice
// duration is the time span of K keyframes ahead of the reference, so it
// shortens into transients and lengthens through sparse material.
//
// See docs/design.md for the mapping from the paper's offline formulation to
// this real-time, block-driven wrapper.

// Analysis block boundary: a keyframe is forced here so the sparse buffer
// always has a baseline uniform sample rate, which is what makes real-time
// analysis and playback able to run one after the other (paper §2.7).
inline constexpr int kAnalysisBlock = 512;

// Minimum depth of the sparse buffer, in input samples: how far behind the
// newest analysed keyframe the reference playhead may sit at fastest. This is
// the reported latency, and the dry path is delayed by exactly this much so
// dry and wet align when Time is at unity.
inline constexpr int kLatencySamples = 8192;

// Maximum depth. The reference advances at the time rate, so slowing down
// pushes it further and further back; a live stream cannot back up without
// bound, so at this depth the reference rides the floor and the engine loops
// the material it holds. That is the honest consequence of asking a live input
// for unbounded stretch, and it matches the paper's own limitation about long
// splices producing audible repeats (paper §3.7).
inline constexpr int kMaxDepthSamples = 65536;

// Keyframe ring. Worst case is one keyframe per input sample across the
// maximum depth plus the longest splice lookahead (65536 + 500 ms at 48 kHz),
// with headroom. 131072 entries is about 2 MB of sparse buffer.
inline constexpr std::int64_t kRingCapacity = 1 << 17;
static_assert((kRingCapacity & (kRingCapacity - 1)) == 0, "ring must be a power of two");

// Dry/wet delay line, sized to the depth window.
inline constexpr int kDryRing = 1 << 16;
static_assert((kDryRing & (kDryRing - 1)) == 0, "dry ring must be a power of two");

// A splice shorter than this would be a click rather than a crossfade.
inline constexpr double kMinSpliceSamples = 128.0;

struct Parameters {
    float time = 1.00f;       // tau: input samples consumed per output sample
    float pitch = 1.00f;      // sigma: playhead read rate, i.e. the pitch ratio
    float splice = 16.0f;     // K: leash length in keyframes
    float threshold = -60.0f; // analysis epsilon, dB
    float maxSplice = 250.0f; // splice duration ceiling, ms
    float hold = 0.0f;       // freeze the reference playhead
    float mix = 0.60f;
    float width = 0.60f;
    float level = 0.75f;
};

// One sparse keyframe: an absolute input-sample position (fractional) and the
// channel values at that position. Positions come from the mid channel, so
// both channels share one sparse time base and the stereo image survives.
struct Keyframe {
    double pos = 0.0;
    float l = 0.0f;
    float r = 0.0f;
};

struct EngineState {
    double sampleRate = 48000.0;

    // Analysis
    std::array<Keyframe, static_cast<std::size_t>(kRingCapacity)> kf {};
    std::int64_t kfWrite = 0;
    // Position of the most recently written keyframe. Every playhead is held
    // below it, so it is the write head in time terms.
    double newestPos = 0.0;
    double inputIndex = 0.0;
    std::array<float, 5> histL {};  // x[n-4 .. n]
    std::array<float, 5> histR {};
    std::array<float, 5> histM {};
    float dPrev = 0.0f;
    float vPrev = 0.0f;
    int dPrevSign = 0;
    bool haveDerivative = false;
    int blockCounter = 0;

    // Playback: two cursors plus the splice cursor, each a continuous input
    // position and the index of the keyframe window that contains it.
    double refPos = 0.0;
    double playPos = 0.0;
    double tempPos = 0.0;
    std::int64_t mRef = 0;
    std::int64_t mPlay = 0;
    std::int64_t mTemp = 0;
    bool splicing = false;
    double tTemp = 0.0;
    double tDot = 0.0;

    // Dry path, delayed by the reported latency so dry/wet stays aligned.
    std::array<std::array<float, static_cast<std::size_t>(kDryRing)>, 2> dry {};
    int dryWrite = 0;

    // Freeze the reference playhead: the passage sustains indefinitely.
    bool hold = false;
    // Playback has started; before this both playheads sit at zero while the
    // sparse buffer fills to its minimum depth.
    bool primed = false;

    // Status
    float density = 0.0f;      // keyframes per second
    float drift = 0.0f;       // keyframes between the two playheads
    float spliceLamp = 0.0f;
    std::uint64_t spliceCount = 0;
    float clipLamp = 0.0f;

    // Smoothed controls
    float sTime = 1.0f, sPitch = 1.0f, sMix = 0.60f, sWidth = 0.60f, sLevel = 0.75f;
    float smoothCoeff = 0.001f;
    float dcL = 0.0f, dcR = 0.0f;
    bool initialized = false;
};

// Amplitude difference threshold, epsilon, from the dB parameter.
[[nodiscard]] float epsilonFromDb(const float decibels) noexcept;

// Worst-case reported latency in samples. Constant for the life of the plugin.
[[nodiscard]] inline int reportedLatencySamples() noexcept { return kLatencySamples; }

[[nodiscard]] Parameters clampParameters(const Parameters& raw) noexcept;
void activate(EngineState& state, double sampleRate);

void processBlock(EngineState& state,
                  const Parameters& params,
                  std::uint32_t nframes,
                  const float* const* inputs,
                  float* const* outputs) noexcept;

[[nodiscard]] float keyframesPerSecond(const EngineState& state) noexcept;
[[nodiscard]] float playheadDrift(const EngineState& state) noexcept;
[[nodiscard]] float spliceLamp(const EngineState& state) noexcept;
[[nodiscard]] std::uint64_t spliceCount(const EngineState& state) noexcept;
[[nodiscard]] float clipLamp(const EngineState& state) noexcept;

// Direct access to the sparse buffer, for deterministic tests.
[[nodiscard]] std::int64_t keyframeCount(const EngineState& state) noexcept;
[[nodiscard]] bool keyframeAt(const EngineState& state,
                              std::int64_t index,
                              double& pos,
                              float& l,
                              float& r) noexcept;

[[nodiscard]] std::string serializeParameters(const Parameters& params);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::keyframe