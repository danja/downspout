#pragma once

#include "ground_core_types.hpp"

namespace downspout::ground {

void resetVariationProgress(VariationState& variation);
[[nodiscard]] bool applyLoopVariation(FormState& form,
                                      VariationState& variation,
                                      const Controls& controls,
                                      double beatsPerBar,
                                      int currentPhraseIndex);

}  // namespace downspout::ground
