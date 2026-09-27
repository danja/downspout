#pragma once

#include "primefold_params.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::primefold {

inline constexpr int kRingSize = 8192;
inline constexpr int kRingMask = kRingSize - 1;
static_assert((kRingSize & (kRingSize - 1)) == 0, "ring must be power of two");
inline constexpr int kMaxWindow = 2048;

// SOLA jump-back search/fade extents (samples). Fixed so the worst-case
// voice delay — and therefore the reported latency — is a pure function of
// the Grain setting. See docs/design.md.
inline constexpr int kSolaSearch = 96;
inline constexpr int kSolaContext = 64;
inline constexpr int kSolaFade = 64;

// Explicit per-voice feedback delay: the causal element in every cycle.
// Never compensated; see docs/design.md.
inline constexpr int kFeedbackDelay = 256;
inline constexpr int kFbRing = 512;
inline constexpr float kFanIn = 0.57735026919f;  // 1/sqrt(3)

inline constexpr float kVoiceRatios[3] = {2.0f, 3.0f, 5.0f};

// Nominal jump-back per voice, chosen so every voice shares the same nominal
// delay: jump_N = K - fade*(N-1), average delay ~= K/2 for all voices.
[[nodiscard]] inline int voiceJumpBack(int window, float ratio) noexcept
{
    const int jump = window - static_cast<int>(kSolaFade * (ratio - 1.0f));
    return jump < 256 ? 256 : jump;
}

// Worst-case voice delay (trigger + jump + search), reported to the host.
[[nodiscard]] inline int worstVoiceDelay(int window) noexcept
{
    return window + 8 + kSolaSearch;
}

// Dry alignment delay: the voices' average delay (~K/2 for every ratio).
[[nodiscard]] inline int dryAlignDelay(int window) noexcept
{
    return window / 2;
}

struct Parameters {
    float dry = 0.60f;
    float level2 = 0.70f;
    float level3 = 0.50f;
    float level5 = 0.40f;
    float feedback = 0.35f;
    float damp = 0.50f;
    float grain = 1.0f;
    float mix = 0.60f;
    float width = 0.60f;
    float level = 0.75f;
};

// One SOLA pitch voice: a single fractional read pointer chases the write
// pointer at N samples per sample (exact ratio, no crossfade glide) and
// jumps back by ~grain samples whenever the delay runs short. Each jump
// lands on a correlation-matched offset shared across channels (stereo image
// preserved) and is hidden by a short Hann fade from a ghost continuation.
// Between jumps the output is a single tap: instantaneous pitch is exactly N.
struct SolaVoice {
    std::array<std::array<float, kRingSize>, 2> ring {};
    std::int64_t writePos = 4096;
    double readPos = 0.0;   // shared L/R, monotonic input-time
    double ghostPos = 0.0;  // pre-jump continuation during fades
    int ghostLeft = 0;
    // Per-voice input anti-alias low-pass (cutoff fs/2N): playing memory
    // faster by N maps input content above fs/2N past Nyquist.
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    std::array<float, 2> zx1 {}, zx2 {}, zy1 {}, zy2 {};
};

struct EngineState {
    double sampleRate = 48000.0;
    int window = 1024;
    std::array<SolaVoice, 3> voices {};
    // Explicit per-voice feedback delay (the causal element in every cycle).
    std::array<std::array<std::array<float, kFbRing>, 2>, 3> fbDelay {};
    std::array<std::array<int, 2>, 3> fbWrite {};
    std::array<std::array<float, 2>, 3> fbLp {};
    // Dry alignment delay (nominal voice average delay).
    std::array<std::array<float, kRingSize>, 2> dryDelay {};
    int dryWrite = 0;
    // Smoothed coefficients.
    float sDry = 0.60f, sL2 = 0.70f, sL3 = 0.50f, sL5 = 0.40f;
    float sFb = 0.35f, sDamp = 0.50f, sMix = 0.60f, sWidth = 0.60f, sLevel = 0.75f;
    float smoothCoeff = 0.001f;
    float dcDryL = 0.0f, dcDryR = 0.0f, dcWetL = 0.0f, dcWetR = 0.0f;
    float clipLamp = 0.0f;
    bool initialized = false;
};

[[nodiscard]] Parameters clampParameters(const Parameters& raw) noexcept;
void activate(EngineState& state, double sampleRate, int window);
void activate(EngineState& state, double sampleRate);

void processBlock(EngineState& state,
                  const Parameters& params,
                  std::uint32_t nframes,
                  const float* const* inputs,
                  float* const* outputs) noexcept;

[[nodiscard]] float currentLatencySamples(const EngineState& state) noexcept;
[[nodiscard]] float clipLamp(const EngineState& state) noexcept;

[[nodiscard]] std::string serializeParameters(const Parameters& params);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::primefold
