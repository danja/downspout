#pragma once

#include "gremlin_params.hpp"

#include <cstdint>

namespace downspout::gremlin {

inline constexpr std::uint32_t kParamInputLiveStart = 0;
inline constexpr std::uint32_t kParamInputHiddenStart = kParamInputLiveStart + static_cast<std::uint32_t>(kLiveParamCount);
inline constexpr std::uint32_t kParamMasterTrim = kParamInputHiddenStart + static_cast<std::uint32_t>(kHiddenParamCount);
inline constexpr std::uint32_t kParamMacroStart = kParamMasterTrim + 1;
inline constexpr std::uint32_t kParamMomentaryStart = kParamMacroStart + static_cast<std::uint32_t>(kMacroCount);
inline constexpr std::uint32_t kParamActionStart = kParamMomentaryStart + static_cast<std::uint32_t>(kMomentaryCount);
inline constexpr std::uint32_t kParamSceneTriggerStart = kParamActionStart + 6;
inline constexpr std::uint32_t kParamStatusStart = kParamSceneTriggerStart + static_cast<std::uint32_t>(kSceneCount);
inline constexpr std::uint32_t kParamStatusScene = kParamStatusStart + static_cast<std::uint32_t>(kEffectiveParamCount);
inline constexpr std::uint32_t kParamStatusActivity = kParamStatusScene + 1;
inline constexpr std::uint32_t kGremlinParameterCount = kParamStatusActivity + 1;

inline constexpr std::uint32_t kActionReseed = kParamActionStart + 0;
inline constexpr std::uint32_t kActionBurst = kParamActionStart + 1;
inline constexpr std::uint32_t kActionRandomSource = kParamActionStart + 2;
inline constexpr std::uint32_t kActionRandomDelay = kParamActionStart + 3;
inline constexpr std::uint32_t kActionRandomAll = kParamActionStart + 4;
inline constexpr std::uint32_t kActionPanic = kParamActionStart + 5;

}  // namespace downspout::gremlin
