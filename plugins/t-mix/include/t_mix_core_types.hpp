#pragma once

#include <array>
#include <cstdint>

namespace downspout::tmix {

inline constexpr std::uint32_t kInputChannelCount = 8;
inline constexpr std::uint32_t kOutputChannelCount = 2;
inline constexpr float kMinimumLevelDb = -60.0f;
inline constexpr float kMaximumLevelDb = 12.0f;
inline constexpr std::uint8_t kProducerCcBase = 20;
inline constexpr std::uint8_t kProducerLifecycleCc = 19;
inline constexpr float kDefaultProducerSlewMs = 25.0f;

struct ChannelParameters {
    float levelDb = 0.0f;
    float pan = 0.0f;
    float mute = 0.0f;
    float solo = 0.0f;
};

struct Parameters {
    std::array<ChannelParameters, kInputChannelCount> channels {};
    float masterDb = 0.0f;
    float producerSlewMs = kDefaultProducerSlewMs;
    float producerControlChannel = 0.0f;
    float requireProducerGate = 0.0f;
};

struct AudioBlock {
    std::array<const float*, kInputChannelCount> inputs {};
    std::array<float*, kOutputChannelCount> outputs {};
};

struct MidiControlEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 3;
    std::array<std::uint8_t, 3> data {};
};

struct EngineState {
    std::array<float, kInputChannelCount> meters {};
    std::array<float, kInputChannelCount> producerGains {};
    std::array<float, kInputChannelCount> producerTargets {};
    bool producerActive = false;
};

struct OutputStatus {
    std::array<float, kInputChannelCount> meters {};
    std::array<float, kInputChannelCount> producerGains {};
    bool producerActive = false;
};

}  // namespace downspout::tmix
