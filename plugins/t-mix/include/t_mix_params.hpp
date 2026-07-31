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
// Producer parameters are appended so existing projects retain every old ID.
inline constexpr std::uint32_t kParamProducerSlew = kParamMeterBase + kInputChannelCount;
inline constexpr std::uint32_t kParamProducerGainBase = kParamProducerSlew + 1;
inline constexpr std::uint32_t kParamProducerControlChannel = kParamProducerGainBase + kInputChannelCount;
inline constexpr std::uint32_t kParamRequireProducerGate = kParamProducerControlChannel + 1;
inline constexpr std::uint32_t kParamStatusProducerActive = kParamRequireProducerGate + 1;
inline constexpr std::uint32_t kParameterCount = kParamStatusProducerActive + 1;

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";

}  // namespace downspout::tmix
