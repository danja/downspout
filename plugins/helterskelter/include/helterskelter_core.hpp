#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::helterskelter {

using downspout::generative::Transport;

// The four most-playable controls map 1:1 to Drift's default lane CCs so that
// routing Drift MIDI out -> HelterSkelter MIDI in just works
// (chipper/ghost/spliff convention).
inline constexpr float kDefaultCCSensitivity = 1.0f;
inline constexpr float kDefaultCCDepth       = 2.0f;
inline constexpr float kDefaultCCResonance   = 3.0f;
inline constexpr float kDefaultCCMix         = 4.0f;

inline constexpr int kDivisionBeats[4] = { 1, 2, 4, 8 };

struct Parameters {
    float mode        = 0.0f;    // 0 = Envelope, 1 = BBT ADSR, 2 = Blend (max)
    float sensitivity = 0.6f;    // 0-1 envelope drive
    float depth       = 0.7f;    // 0-1 sweep range (0-4 octaves above base)
    float resonance   = 4.0f;    // 0.5-12 filter Q
    float baseFreq    = 400.0f;  // 100-2000 Hz pedal-down cutoff
    float division    = 2.0f;    // 0-3 index into kDivisionBeats (cycle length)
    float gateBeats   = 2.0f;    // 0.5-8 beats the ADSR gate stays high per cycle
    float attack      = 20.0f;   // 1-500 ms
    float decay       = 150.0f;  // 5-1000 ms
    float sustain     = 0.7f;    // 0-1 ADSR sustain level
    float release     = 200.0f;  // 5-2000 ms
    float invert      = 0.0f;    // 0-1 flip the BBT gate (low-then-high)
    float mix         = 100.0f;  // 0-100 dry/wet %
    float trim        = 0.0f;    // -12..+12 dB makeup gain, wet only
    float bypass      = 0.0f;    // 0 = active, 1 = bypassed (DSP still runs)
    float ccSensitivity = kDefaultCCSensitivity; // 0-127 CC# (0 = off)
    float ccDepth       = kDefaultCCDepth;
    float ccResonance   = kDefaultCCResonance;
    float ccMix         = kDefaultCCMix;
    float ccChannel     = 1.0f;  // 1-16 MIDI channel for the four CCs
};

struct FilterState {
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    bool haveCoeffs = false;
};

struct AdsrState {
    float level = 0.0f;
    float releaseStart = 0.0f;
    bool gateHigh = false;
};

struct EngineState {
    std::array<FilterState, 2> filters {};
    float env = 0.0f;            // envelope follower
    AdsrState adsr {};
    double prevPhase = 0.0;      // phase within the BBT cycle, in beats
    bool havePhase = false;
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Effective values are CC-overridden when active, else the panel value.
void processBlock(EngineState&        state,
                  const Parameters&   params,
                  const Transport&    transport,
                  std::uint32_t       frames,
                  double              sampleRate,
                  const float* const* inputs,
                  float* const*       outputs,
                  float               effectiveSensitivity,
                  float               effectiveDepth,
                  float               effectiveResonance,
                  float               effectiveMix) noexcept;

[[nodiscard]] std::string               serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::helterskelter
