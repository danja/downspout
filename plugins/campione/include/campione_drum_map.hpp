#pragma once

#include "campione_core_types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace downspout::campione {

struct DrumAssignment {
    int         gmNote     = -1;    // -1 = no confident match; keep current note
    float       confidence = 0.0f;
    std::string evidence;           // matched GM note name, for diagnostics
};

// Score a single zone's source filename against the GM percussion alias table.
// Returns (gmNote, confidence) pairs sorted by descending confidence.
// Exposed for unit testing.
std::vector<std::pair<int, float>> scoreFilename(const std::string& sourcePath);

// Assign GM percussion notes to a set of zones from their source paths.
// Runs the Hungarian algorithm over the cost matrix built from per-zone filename scores
// so that no two zones receive the same GM note.
// Zones with no confident match (confidence < 0.30) get gmNote = -1 (keep current).
std::vector<DrumAssignment> assignDrumNotes(const std::vector<std::string>& sourcePaths);

// Convenience overload: extracts sourcePath from each SampleZone.
std::vector<DrumAssignment> assignDrumNotes(const std::vector<SampleZone>& zones);

} // namespace downspout::campione
