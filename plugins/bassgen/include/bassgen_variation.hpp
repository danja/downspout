#pragma once

#include "bassgen_core_types.hpp"

namespace downspout::bassgen {

void resetVariationProgress(VariationState& variation);
[[nodiscard]] bool applyLoopVariation(PatternState& pattern,
                                      VariationState& variation,
                                      const Controls& controls,
                                      double beatsPerBar);

}  // namespace downspout::bassgen
