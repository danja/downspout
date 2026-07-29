#pragma once

#include <array>
#include <cstdint>

namespace downspout::tmix {

inline constexpr std::uint32_t kInputChannelCount = 8;
inline constexpr std::uint32_t kOutputChannelCount = 2;
inline constexpr float kMinimumLevelDb = -60.0f;
inline constexpr float kMaximumLevelDb = 12.0f;

struct ChannelParameters {
    float levelDb = 0.0f;
    float pan = 0.0f;
    float mute = 0.0f;
    float solo = 0.0f;
};

struct Parameters {
    std::array<ChannelParameters, kInputChannelCount> channels {};
    float masterDb = 0.0f;
};

struct AudioBlock {
    std::array<const float*, kInputChannelCount> inputs {};
    std::array<float*, kOutputChannelCount> outputs {};
};

struct EngineState {
    std::array<float, kInputChannelCount> meters {};
};

struct OutputStatus {
    std::array<float, kInputChannelCount> meters {};
};

}  // namespace downspout::tmix
