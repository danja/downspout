#pragma once

#include "ambo_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::ambo {

[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::ambo
