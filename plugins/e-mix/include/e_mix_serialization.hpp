#pragma once

#include "e_mix_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::emix {

[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::emix
