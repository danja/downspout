#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::chipper {

struct Parameters {
    float bitDepth   = 8.0f;   // 1–16 bits (integer)
    float rateDiv    = 8.0f;   // 1–64 sample-hold divisor (integer)
    float jitter     = 0.0f;   // 0–1 fraction of rateDiv to randomise hold length
    float mix        = 100.0f; // 0–100 dry/wet %
    float outputGain = 0.0f;   // -12 to +12 dB
};

struct EngineState {
    std::array<float, 2> heldSample {};  // one held value per channel
    int      holdCounter = 0;            // frames remaining on current hold
    uint32_t randState   = 0x12345678u;  // xorshift32 state for jitter
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// inputs/outputs: pointer-to-pointer with at least 2 channels each
void processBlock(EngineState&      state,
                  const Parameters& params,
                  uint32_t          frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept;

[[nodiscard]] std::string               serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::chipper
