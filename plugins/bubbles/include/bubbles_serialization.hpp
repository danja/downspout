#pragma once

#include "bubbles_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::bubbles {

std::string                serializeParameters(const Parameters& parameters);
std::optional<Parameters>  deserializeParameters(const std::string& text);

}  // namespace downspout::bubbles
