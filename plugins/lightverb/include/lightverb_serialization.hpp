#pragma once
#include "lightverb_core.hpp"
#include <optional>
#include <string>
namespace downspout::lightverb {
[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);
} // namespace downspout::lightverb
