#pragma once

#include "magneto_core_types.hpp"

#include <optional>
#include <string>

namespace downspout::magneto {

// Stable textual state: "version=1\n" followed by one key=value line per
// host-writable parameter, keyed by the symbols in magneto_params.hpp.
[[nodiscard]] std::string serializeParameters(const Parameters& params);

// Strict parse. Any malformed line or unknown key rejects the whole document;
// a successful parse is clamped before it is returned.
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

inline constexpr int kStateVersion = 1;

}  // namespace downspout::magneto
