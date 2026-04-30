#pragma once

#include "bassgen_core_types.hpp"

namespace downspout::bassgen {

[[nodiscard]] Controls clampControls(const Controls& raw);
[[nodiscard]] bool structuralControlsChanged(const Controls& a, const Controls& b);
[[nodiscard]] int stepsPerBeatForSubdivision(SubdivisionId subdivision);
[[nodiscard]] int registerOffset(int reg);

void regeneratePattern(PatternState& pattern,
                       const Controls& controls,
                       const ::downspout::Meter& meter,
                       bool regenRhythm,
                       bool regenNotes);

void partialNoteMutation(PatternState& pattern,
                         const Controls& controls,
                         float strength);

}  // namespace downspout::bassgen
