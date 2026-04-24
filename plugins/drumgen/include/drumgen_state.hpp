#pragma once

#include "drumgen_core_types.hpp"

namespace downspout::drumgen {

[[nodiscard]] PatternState sanitizePatternState(const PatternState& raw, bool* valid = nullptr);
[[nodiscard]] VariationState sanitizeVariationState(const VariationState& raw);

}  // namespace downspout::drumgen
