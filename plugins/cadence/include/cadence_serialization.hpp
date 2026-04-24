#pragma once

#include "cadence_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::cadence {

[[nodiscard]] std::string serializeControls(const Controls& controls);
[[nodiscard]] std::optional<Controls> deserializeControls(const std::string& text);

[[nodiscard]] std::string serializeProgressionState(const ProgressionState& state);
[[nodiscard]] std::optional<ProgressionState> deserializeProgressionState(const std::string& text);

[[nodiscard]] std::string serializeVariationState(const VariationState& state);
[[nodiscard]] std::optional<VariationState> deserializeVariationState(const std::string& text);

}  // namespace downspout::cadence
