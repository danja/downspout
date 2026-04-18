#pragma once

#include "bassgen_core_types.hpp"

namespace downspout::bassgen {

[[nodiscard]] PatternState sanitizePatternState(const PatternState& raw, bool* valid = nullptr);
[[nodiscard]] VariationState sanitizeVariationState(const VariationState& raw);

}  // namespace downspout::bassgen
