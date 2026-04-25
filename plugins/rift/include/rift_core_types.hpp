#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::rift {

inline constexpr std::uint32_t kMaxChannels = 8;
inline constexpr std::size_t kActionCount = 6;

enum class ActionType : std::uint8_t {
    Pass = 0,
    Repeat,
    Reverse,
    Skip,
    Smear,
    Slip,
};

inline constexpr std::array<const char*, kActionCount> kActionNames = {{
    "Pass",
    "Repeat",
    "Reverse",
    "Skip",
    "Smear",
    "Slip",
}};

struct Parameters {
    float grid = 8.0f;
    float density = 35.0f;
    float damage = 45.0f;
    float memoryBars = 2.0f;
    float drift = 20.0f;
    float pitch = 0.0f;
    float mix = 100.0f;
    float hold = 0.0f;
};

struct Triggers {
    std::uint32_t scatterSerial = 0;
    std::uint32_t recoverSerial = 0;
};

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

struct AudioBlock {
    std::array<const float*, kMaxChannels> inputs {};
    std::array<float*, kMaxChannels> outputs {};
    std::uint32_t channelCount = 2;
};

struct BlockSpec {
    ActionType action = ActionType::Pass;
    double readPosition = 0.0;
    double readRate = 1.0;
    std::uint32_t sourceStartFrame = 0;
    std::uint32_t sourceLengthFrames = 1;
    float wet = 0.0f;
    float dry = 1.0f;
    bool valid = false;
};

struct EngineState {
    std::vector<float> buffer;
    std::uint32_t bufferFrames = 0;
    std::uint32_t bufferChannels = 0;
    std::uint32_t writeHead = 0;
    std::uint32_t filledFrames = 0;
    double sampleRate = 0.0;

    bool transportWasPlaying = false;
    std::int64_t activeBlockSerial = -1;
    BlockSpec activeBlock {};
    BlockSpec transitionBlock {};
    std::uint32_t transitionFramesRemaining = 0;
    std::uint32_t transitionFramesTotal = 0;

    std::uint32_t lastScatterSerial = 0;
    std::uint32_t lastRecoverSerial = 0;
    std::uint32_t scatterBlocksRemaining = 0;
    std::uint32_t recoverBlocksRemaining = 0;

    std::uint32_t rngState = 0x2457ac13u;
};

struct ActionWeights {
    float repeat = 0.0f;
    float reverse = 0.0f;
    float skip = 0.0f;
    float smear = 0.0f;
    float slip = 0.0f;
};

struct OutputStatus {
    ActionType action = ActionType::Pass;
    float activity = 0.0f;
};

}  // namespace downspout::rift
