#pragma once

#include "cadence_harmony.hpp"

namespace downspout::cadence {

void cadence_reset_variation_progress(VariationState* variation);
bool cadence_apply_cycle_variation(const SegmentCapture* learned_capture,
                                   int learned_segment_count,
                                   const Controls& controls,
                                   VariationState* variation,
                                   const ChordSlot* base_slots,
                                   int base_segment_count,
                                   const ChordSlot* previous_playback,
                                   int previous_count,
                                   ChordSlot* out_slots);

}  // namespace downspout::cadence
