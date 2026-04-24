#pragma once

#include "cadence_core_types.hpp"

#include <cstdint>

namespace downspout::cadence {

struct CadenceBuildOptions {
    float local_jitter = 0.0f;
    float transition_jitter = 0.0f;
    float voicing_jitter = 0.0f;
    float continuity = 0.0f;
    bool anchor_endpoints = true;
    uint32_t seed = 1u;
};

void cadence_clear_capture(SegmentCapture* capture, int count = CADENCE_MAX_SEGMENTS);
void cadence_copy_capture(SegmentCapture* dst, const SegmentCapture* src, int count);
void cadence_clear_progression(ChordSlot* slots, int count = CADENCE_MAX_SEGMENTS);
void cadence_copy_progression(ChordSlot* dst, const ChordSlot* src, int count);
double cadence_segment_activity(const SegmentCapture& segment);
bool cadence_build_progression_from_capture(const SegmentCapture* capture,
                                            int segment_count,
                                            const Controls& controls,
                                            const ChordSlot* reference_slots,
                                            int reference_count,
                                            const CadenceBuildOptions& options,
                                            ChordSlot* out_slots);
bool cadence_revoice_progression(const ChordSlot* source_slots,
                                 int segment_count,
                                 const Controls& controls,
                                 const CadenceBuildOptions& options,
                                 ChordSlot* out_slots);

}  // namespace downspout::cadence
