#pragma once

#include "worms_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::worms {

std::string serializeControls(const Controls& controls);
std::optional<Controls> deserializeControls(const std::string& text);

std::string serializePattern(const PatternState& pattern);
std::optional<PatternState> deserializePattern(const std::string& text);

}  // namespace downspout::worms
