#pragma once

#include "p_mix_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::pmix {

[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::pmix
