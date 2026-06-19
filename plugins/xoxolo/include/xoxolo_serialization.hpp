#pragma once

#include "xoxolo_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::xoxolo {

[[nodiscard]] std::string serializePatternState(const PatternState& pattern);
[[nodiscard]] std::optional<PatternState> deserializePatternState(const std::string& text);

}  // namespace downspout::xoxolo
