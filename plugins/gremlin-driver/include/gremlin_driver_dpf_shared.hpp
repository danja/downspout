#pragma once

#include "gremlin_driver_params.hpp"

#include <cstdint>

namespace downspout::gremlin_driver {

inline constexpr std::uint32_t kParamClockMode = 0;
inline constexpr std::uint32_t kParamBpm = 1;
inline constexpr std::uint32_t kParamLaneStart = 2;
inline constexpr std::uint32_t kParamTriggerStart = kParamLaneStart + static_cast<std::uint32_t>(kLaneCount * 5);
inline constexpr std::uint32_t kParamRandomize = kParamTriggerStart + static_cast<std::uint32_t>(kTriggerCount * 3);
inline constexpr std::uint32_t kParamStatusLaneStart = kParamRandomize + 1;
inline constexpr std::uint32_t kParamStatusTriggerStart = kParamStatusLaneStart + static_cast<std::uint32_t>(kLaneCount);
inline constexpr std::uint32_t kParamStatusTransport = kParamStatusTriggerStart + static_cast<std::uint32_t>(kTriggerCount);
inline constexpr std::uint32_t kParamStatusBpm = kParamStatusTransport + 1;
inline constexpr std::uint32_t kGremlinDriverParameterCount = kParamStatusBpm + 1;

}  // namespace downspout::gremlin_driver
