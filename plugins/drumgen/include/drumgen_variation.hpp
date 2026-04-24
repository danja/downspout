#pragma once

#include "drumgen_core_types.hpp"

namespace downspout::drumgen {

void resetVariationProgress(VariationState& variation);
[[nodiscard]] bool applyLoopVariation(PatternState& pattern,
                                      VariationState& variation,
                                      const Controls& controls);

}  // namespace downspout::drumgen
