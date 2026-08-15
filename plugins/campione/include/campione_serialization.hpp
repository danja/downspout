#pragma once

#include "campione_core_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace downspout::campione {

[[nodiscard]] std::string serializeParameters(const Parameters& parameters);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

// Serialize zone metadata (sourcePath, rootNote, ranges, loop points).
// Audio data is NOT included; paths are stored for reload on project open.
[[nodiscard]] std::string serializeZones(const std::vector<SampleZone>& zones);

// Deserialize zone metadata; returned zones have empty data vectors.
// The caller must load WAV data using sourcePath before playback.
[[nodiscard]] std::optional<std::vector<SampleZone>> deserializeZones(const std::string& text);

}  // namespace downspout::campione
