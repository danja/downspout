#pragma once

#include "campione_core_types.hpp"

#include <string>

namespace downspout::campione {

// Scale all samples so the peak absolute value equals 1.0.
// Returns empty string on success, error message on failure.
[[nodiscard]] std::string normalizeZone(SampleZone& zone);

// Remove leading and trailing silence below threshold_linear.
// Adjusts loopStart / loopEnd to stay within the trimmed range.
void trimZone(SampleZone& zone, float thresholdLinear = 0.001f);

// Apply linear fade-in over fadeInFrames at the start and
// fade-out over fadeOutFrames at the end.
void fadeZone(SampleZone& zone, uint32_t fadeInFrames, uint32_t fadeOutFrames);

// Reverse all frames in the zone.
void reverseZone(SampleZone& zone);

}  // namespace downspout::campione
