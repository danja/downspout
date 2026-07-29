#pragma once

#include "t_mix_core_types.hpp"

#include <cstdint>

namespace downspout::tmix {

inline constexpr std::uint32_t kParamLevelBase = 0;
inline constexpr std::uint32_t kParamPanBase = kParamLevelBase + kInputChannelCount;
inline constexpr std::uint32_t kParamMuteBase = kParamPanBase + kInputChannelCount;
inline constexpr std::uint32_t kParamSoloBase = kParamMuteBase + kInputChannelCount;
inline constexpr std::uint32_t kParamMaster = kParamSoloBase + kInputChannelCount;
inline constexpr std::uint32_t kParamMeterBase = kParamMaster + 1;
inline constexpr std::uint32_t kParameterCount = kParamMeterBase + kInputChannelCount;

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";

}  // namespace downspout::tmix
