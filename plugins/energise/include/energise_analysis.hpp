#pragma once

#include "energise_core.hpp"

#include <cstdint>
#include <vector>

namespace downspout::energise {

struct DetectedOnset {
    double timeSec = 0.0;
    float strength = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
};

// Portable offline analyser. Each stage is a separate function so a different
// onset detector, grid fitter or classifier can replace just one stage.
[[nodiscard]] std::vector<DetectedOnset> detectOnsets(const CaptureBuffer& capture);

[[nodiscard]] GrooveMap fitTempoGrid(const CaptureBuffer& capture,
                                     const std::vector<DetectedOnset>& onsets,
                                     const int gridDivisions,
                                     const float driftAllowance);

[[nodiscard]] std::vector<DrumEvent> classifyEvents(const CaptureBuffer& capture,
                                                    const std::vector<DetectedOnset>& onsets,
                                                    const GrooveMap& groove);

[[nodiscard]] bool analyseCapture(const CaptureBuffer& capture,
                                  const int gridDivisions,
                                  const float driftAllowance,
                                  GrooveMap& groove,
                                  std::vector<DrumEvent>& events);

}  // namespace downspout::energise
