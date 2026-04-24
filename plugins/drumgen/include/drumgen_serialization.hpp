#pragma once

#include "drumgen_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::drumgen {

[[nodiscard]] std::string serializeControls(const Controls& controls);
[[nodiscard]] std::string serializePatternState(const PatternState& pattern);
[[nodiscard]] std::string serializeVariationState(const VariationState& variation);

[[nodiscard]] std::optional<Controls> deserializeControls(const std::string& text);
[[nodiscard]] std::optional<PatternState> deserializePatternState(const std::string& text);
[[nodiscard]] std::optional<VariationState> deserializeVariationState(const std::string& text);

}  // namespace downspout::drumgen
