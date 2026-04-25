#pragma once

#include "rift_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::rift {

[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::rift
