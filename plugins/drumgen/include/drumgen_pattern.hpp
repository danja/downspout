#pragma once

#include "drumgen_core_types.hpp"

namespace downspout::drumgen {

[[nodiscard]] Controls clampControls(const Controls& raw);
[[nodiscard]] bool structuralControlsChanged(const Controls& a, const Controls& b);
[[nodiscard]] int stepsPerBeatForResolution(ResolutionId resolution);

void regeneratePattern(PatternState& pattern,
                       const Controls& controls,
                       const ::downspout::Meter& meter,
                       bool fillOnlyRefresh);

void refreshBar(PatternState& pattern,
                const Controls& controls,
                const ::downspout::Meter& meter,
                int barIndex);

void refreshFillBar(PatternState& pattern,
                    const Controls& controls,
                    const ::downspout::Meter& meter,
                    int barIndex);

}  // namespace downspout::drumgen
