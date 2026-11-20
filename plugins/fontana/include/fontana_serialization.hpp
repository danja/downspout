#pragma once

#include "fontana_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::fontana {

// Stable versioned text contracts. Only metadata is stored: sample data is
// re-read from the referenced files when a session is reopened.
[[nodiscard]] std::string serializeSliceSet(const SliceSet& slices);
[[nodiscard]] std::optional<SliceSet> deserializeSliceSet(const std::string& text);

[[nodiscard]] std::string serializeSettings(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeSettings(const std::string& text);

}  // namespace downspout::fontana
