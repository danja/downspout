#pragma once

#include "ground_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::ground {

[[nodiscard]] std::string serializeControls(const Controls& controls);
[[nodiscard]] std::string serializeFormState(const FormState& form);
[[nodiscard]] std::string serializeVariationState(const VariationState& variation);

[[nodiscard]] std::optional<Controls> deserializeControls(const std::string& text);
[[nodiscard]] std::optional<FormState> deserializeFormState(const std::string& text);
[[nodiscard]] std::optional<VariationState> deserializeVariationState(const std::string& text);

}  // namespace downspout::ground
