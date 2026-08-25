#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::damiano {

inline constexpr std::uint32_t kMaxChannels = 8;

enum Mode : int {
    kModeSoft      = 0,
    kModeTanh      = 1,
    kModeFuzz      = 2,
    kModeOverdrive  = 3,
    kModeTube      = 4,
    kModeWavefold  = 5,
    kModeCount     = 6
};

struct Parameters {
    float mode       = static_cast<float>(kModeTanh);
    float drive      = 2.0f;    // 1.0–10.0
    float tone       = 50.0f;   // 0–100 (50 = flat)
    float foldCount  = 2.0f;    // 1–8, wavefold iterations
    float mix        = 100.0f;  // 0–100 dry/wet
    float outputGain = 0.0f;    // –24 to +24 dB
    float ccDrive    = 0.0f;    // 0–127 CC number (0 = disabled)
    float ccChannel  = 1.0f;    // 1–16
};

struct EngineState {
    // One-pole low-pass state per channel for tone shelf
    std::array<float, kMaxChannels> toneLp {};
    // Cached tanh compensation (avoids recomputing when drive unchanged)
    float tanhComp = 1.0f;
    float cachedDrive = -1.0f;
};

struct AudioBlock {
    std::array<const float*, kMaxChannels> inputs {};
    std::array<float*, kMaxChannels>       outputs {};
    std::uint32_t channelCount = 2;
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

void processBlock(EngineState&      state,
                  const Parameters& params,
                  std::uint32_t     frames,
                  double            sampleRate,
                  const AudioBlock& audio,
                  float             effectiveDrive) noexcept;

[[nodiscard]] std::string          serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::damiano
