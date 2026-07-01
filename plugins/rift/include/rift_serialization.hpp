#pragma once

#include "rift_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::rift {

[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);
[[nodiscard]] std::string serializeSequencePattern(const SequencePattern& pattern);
[[nodiscard]] std::optional<SequencePattern> deserializeSequencePattern(const std::string& text);
[[nodiscard]] bool sequencePatternHasCells(const SequencePattern& pattern);

}  // namespace downspout::rift
