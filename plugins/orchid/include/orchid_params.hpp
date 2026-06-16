#pragma once

#include <cstdint>

namespace downspout::orchid {

enum ParameterIndex : std::uint32_t {
    kParamSensitivity = 0,
    kParamPeriodicity,
    kParamStabilityMs,
    kParamPitchLow,
    kParamPitchHigh,
    kParamGrid,
    kParamHoldUnits,
    kParamReleaseMs,
    kParamCooldownMs,
    kParamLoopPeriods,
    kParamMix,
    kParamLiveUnder,
    kParamCaptureTiming,
    kParamRetrigger,
    kParamStatusState,
    kParamStatusConfidence,
    kParamStatusPitch,
    kParamStatusRms,
    kParamStatusHoldProgress,
    kParamStatusTransport,
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";

}  // namespace downspout::orchid
