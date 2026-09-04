#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::chipper {

// Default CC assignments match Drift lanes 1–4.
inline constexpr float kDefaultCCBitDepth = 1.0f;
inline constexpr float kDefaultCCRateDiv  = 2.0f;
inline constexpr float kDefaultCCJitter   = 3.0f;
inline constexpr float kDefaultCCMix      = 4.0f;

struct Parameters {
    float bitDepth   = 8.0f;               // 1–16 bits (integer)
    float rateDiv    = 8.0f;               // 1–64 sample-hold divisor (integer)
    float jitter     = 0.0f;               // 0–1 fraction of rateDiv to randomise hold length
    float mix        = 100.0f;             // 0–100 dry/wet %
    float outputGain = 0.0f;               // -12 to +12 dB
    float ccBitDepth = kDefaultCCBitDepth; // 0–127 CC# for bit depth override (0 = off)
    float ccRateDiv  = kDefaultCCRateDiv;  // 0–127 CC# for rate div override (0 = off)
    float ccJitter   = kDefaultCCJitter;   // 0–127 CC# for jitter override (0 = off)
    float ccMix      = kDefaultCCMix;      // 0–127 CC# for mix override (0 = off)
    float ccChannel  = 1.0f;               // 1–16 MIDI channel for CC
};

struct EngineState {
    std::array<float, 2> heldSample {};  // one held value per channel
    int      holdCounter = 0;            // frames remaining on current hold
    uint32_t randState   = 0x12345678u;  // xorshift32 state for jitter
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Effective values: CC-overridden when active, else the panel value.
void processBlock(EngineState&        state,
                  const Parameters&   params,
                  uint32_t            frames,
                  const float* const* inputs,
                  float* const*       outputs,
                  float               effectiveBitDepth,
                  float               effectiveRateDiv,
                  float               effectiveJitter,
                  float               effectiveMix) noexcept;

[[nodiscard]] std::string               serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::chipper
