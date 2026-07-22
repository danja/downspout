#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::ambo {

inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::size_t kModuleCount = 6;
inline constexpr std::size_t kChainCount = 4;

enum class ModuleId : std::uint8_t {
    Time = 0,
    Spectral,
    Tape,
    Shimmer,
    Delay,
    Drive,
};

inline constexpr std::array<const char*, kChainCount> kChainNames = {{
    "Drift",
    "Bloom",
    "Haze",
    "Fracture",
}};

struct Parameters {
    float chain = 0.0f;
    float time = 0.30f;
    float spectral = 0.24f;
    float tape = 0.22f;
    float shimmer = 0.36f;
    float delay = 0.28f;
    float drive = 0.12f;
    float feedback = 0.18f;
    float mix = 0.55f;
    float output = 0.0f;
    float bypass = 0.0f;
};

struct AudioBlock {
    std::array<const float*, kMaxChannels> inputs {};
    std::array<float*, kMaxChannels> outputs {};
    std::uint32_t channelCount = kMaxChannels;
};

struct EngineState {
    double sampleRate = 0.0;
    std::uint32_t bufferFrames = 0;
    std::uint32_t writeHead = 0;
    std::uint32_t delayWriteHead = 0;
    std::uint32_t shimmerWriteHead = 0;
    std::uint32_t diffusionWriteHead = 0;

    std::vector<float> timeBuffer;
    std::vector<float> delayBuffer;
    std::vector<float> shimmerBuffer;
    std::vector<float> diffusionBuffer;

    std::array<float, kMaxChannels> spectralLow {};
    std::array<float, kMaxChannels> spectralMid {};
    std::array<float, kMaxChannels> spectralFreeze {};
    std::array<float, kMaxChannels> spectralAir {};
    std::array<float, kMaxChannels> tapeLow {};
    std::array<float, kMaxChannels> tapeHigh {};
    std::array<float, kMaxChannels> driveDc {};
    std::array<float, kMaxChannels> feedback {};

    float timePhase = 0.0f;
    float tapePhase = 0.0f;
    float spectralPhase = 0.0f;
    float shimmerPhase = 0.0f;

    Parameters smoothedParameters {};
    bool smoothingInitialized = false;
    int activeChain = 0;
    int pendingChain = 0;
    float chainTransition = 1.0f;
    bool chainFadingOut = false;
};

struct OutputStatus {
    float wetEnergy = 0.0f;
    float feedbackEnergy = 0.0f;
};

}  // namespace downspout::ambo
