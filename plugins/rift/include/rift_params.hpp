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
    kParamSourceMode,
    kParamSampleBeats,
    kParamStatusAction,
    kParamStatusActivity,
    kParamChop,
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateSamplePath,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";
inline constexpr const char* kStateKeySamplePath = "sample_path";

}  // namespace downspout::rift
