#pragma once

#include <cstdint>

namespace downspout::rift {

enum ParameterIndex : std::uint32_t {
    kParamGrid = 0,
    kParamDensity,
    kParamDamage,
    kParamMemoryBars,
    kParamDrift,
    kParamPitch,
    kParamMix,
    kParamHold,
    kParamScatter,
    kParamRecover,
    kParamBlend,
    kParamStatusAction,
    kParamStatusActivity,
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";

}  // namespace downspout::rift
