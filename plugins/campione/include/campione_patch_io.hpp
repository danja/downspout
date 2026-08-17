#pragma once

#include "campione_core_types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace downspout::campione {

// Zone metadata + parameters bundled for patch file round-trip.
// zone.data is always empty; only path and metadata are stored.
struct PatchData {
    std::string             label;
    Parameters              parameters;
    std::vector<SampleZone> zones;
};

// Write patch to a Turtle RDF file (.ttl).
// Returns "" on success, error description on failure.
std::string savePatch(const PatchData& patch, const std::string& filePath);

// Read a Turtle RDF patch file written by savePatch.
// Returns {PatchData, ""} on success, {{}, error} on failure.
std::pair<std::optional<PatchData>, std::string> loadPatch(const std::string& filePath);

}  // namespace downspout::campione
