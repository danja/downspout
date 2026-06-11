#pragma once

#include <cstdint>

namespace downspout::ambo {

enum ParameterIndex : std::uint32_t {
    kParamChain = 0,
    kParamTime,
    kParamSpectral,
    kParamTape,
    kParamShimmer,
    kParamDelay,
    kParamDrive,
    kParamFeedback,
    kParamMix,
    kParamOutput,
    kParamStatusWet,
    kParamStatusFeedback,
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";

}  // namespace downspout::ambo
