#pragma once

#include "energise_core.hpp"

#include <cstdint>

namespace downspout::energise {

// Macro grid alignment plus micro feel. sourceTime is never modified: the UI
// draws the arrow from sourceTime to quantisedTime.
void quantiseEvents(std::vector<DrumEvent>& events,
                    const GrooveMap& groove,
                    const RenderSettings& settings,
                    const std::uint32_t seed,
                    const double captureSeconds);

}  // namespace downspout::energise
