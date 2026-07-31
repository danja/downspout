#pragma once

#include "loopdelay_core.hpp"

#include <optional>
#include <string>

namespace downspout::loopdelay {

[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

} // namespace downspout::loopdelay
