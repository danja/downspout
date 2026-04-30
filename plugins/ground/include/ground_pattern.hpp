#pragma once

#include "ground_core_types.hpp"

namespace downspout::ground {

[[nodiscard]] int normalizeFormBars(int rawBars);
[[nodiscard]] int normalizePhraseBars(int rawPhraseBars, int formBars);
void regenerateForm(FormState& form, const Controls& controls, const ::downspout::Meter& meter);
void refreshPhrase(FormState& form, const Controls& controls, int phraseIndex);
void mutatePhraseCell(FormState& form, const Controls& controls, int phraseIndex, float strength);

}  // namespace downspout::ground
