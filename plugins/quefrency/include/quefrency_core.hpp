#pragma once

#include "quefrency_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::quefrency {

// Largest frame the core ever runs (source MAX_N). All buffers are fixed at
// these sizes; nothing allocates after activate().
inline constexpr std::size_t kMaxN = 4096;
inline constexpr std::size_t kMaxBins = kMaxN / 2 + 1;
inline constexpr std::size_t kChannels = 2;
inline constexpr std::size_t kTrueEnvelopeIterations = 4;
inline constexpr float kMagnitudeFloor = 1e-9f;
inline constexpr float kPeakRange = 9.2f;
inline constexpr float kExponentLimit = 40.0f;

// Frame size follows the sample rate, as in the source: the same span of
// time at 44.1 and 96 kHz. Latency is always N - 1.
[[nodiscard]] inline std::size_t frameSizeFor(double sampleRate) noexcept
{
    return sampleRate > 50000.0 ? 4096 : 2048;
}

[[nodiscard]] inline std::size_t latencyFor(std::size_t n) noexcept
{
    return n > 0 ? n - 1 : 0;
}

struct Parameters {
    float formantShift = 0.0f;
    float formantDepth = 100.0f;
    float formantTilt = 0.0f;
    float pitchShift = 0.0f;
    float pitchFine = 0.0f;
    float freqShift = 0.0f;
    float harmonicDepth = 100.0f;
    float lifter = 1.5f;
    float estimator = 0.0f;
    float mix = 1.0f;
    float output = 0.0f;
};

struct ChannelState {
    std::array<float, kMaxN> inFifo {};
    std::array<float, kMaxN> accum {};
    std::array<float, kMaxN> outFifo {};
    std::size_t filled = 0;
    std::array<float, kMaxN> dry {};
    std::size_t dryPos = 0;
    std::array<float, kMaxBins> lastPhase {};
    std::array<float, kMaxBins> rotation {};
};

// Scratch buffers used within one frame and meaningless between frames, so
// both channels share them — as in the source.
struct ScratchState {
    std::array<float, kMaxN> re {}, im {}, cre {}, cim {}, yre {}, yim {};
    std::array<float, kMaxBins> logMag {}, logEnv {}, logEnvOut {}, upper {}, nextRotation {};
    std::array<std::uint16_t, kMaxBins> peaks {};
};

struct EngineState {
    double sampleRate = 48000.0;
    std::size_t n = 0;
    std::size_t hop = 0;
    std::array<ChannelState, kChannels> channels {};
    ScratchState scratch {};
    // FFT tables for the active frame size.
    std::array<float, kMaxN / 2> fftCos {};
    std::array<float, kMaxN / 2> fftSin {};
    std::array<std::uint16_t, kMaxN> fftBitrev {};
    std::array<float, kMaxN> window {};
    // Tilt as a natural-log gain per bin, rebuilt when tilt or rate changes.
    std::array<float, kMaxBins> tilt {};
    Parameters params {};
    // Derived per-frame values, refreshed by setParameter (source set_param).
    float formantRatio = 1.0f;
    float formantDepthLin = 1.0f;
    float formantTiltDb = 0.0f;
    float pitchSemitones = 0.0f;
    float pitchCents = 0.0f;
    float pitchRatio = 1.0f;
    float freqShiftHz = 0.0f;
    float harmonicDepthLin = 1.0f;
    float lifterMs = 1.5f;
    bool trueEnvelope = false;
    float mixLin = 1.0f;
    float gainLin = 1.0f;
    bool initialized = false;
};

static_assert(sizeof(EngineState) < 1024u * 1024u, "EngineState grew unexpectedly large");

[[nodiscard]] Parameters clampParameters(const Parameters& raw) noexcept;
void activate(EngineState& state, double sampleRate);
void setParameter(EngineState& state, std::uint32_t index, float value);

// A raw MIDI event. Returns true when it drove a parameter (CC 70-80).
bool applyMidiEvent(EngineState& state, std::uint8_t status, std::uint8_t d1, std::uint8_t d2);

void processBlock(EngineState& state,
                  std::uint32_t nframes,
                  const float* const* inputs,
                  float* const* outputs) noexcept;

[[nodiscard]] float currentLatencySamples(const EngineState& state) noexcept;

[[nodiscard]] std::string serializeParameters(const Parameters& params);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::quefrency
