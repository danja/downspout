#pragma once

#include "cadence_core_types.hpp"

#include <stdint.h>

namespace downspout::cadence {

struct ScheduledCompHit {
    bool active = false;
    double beat = 0.0;
    double off_beat = 0.0;
    uint8_t velocity = 96;
};

struct CompPlaybackState {
    int segment_index = -1;
    double segment_start_beat = 0.0;
    double segment_end_beat = 0.0;
    ScheduledCompHit hits[CADENCE_MAX_COMP_HITS]{};
    int hit_count = 0;
    int next_hit_index = 0;
    bool release_pending = false;
    double release_beat = 0.0;
    double last_hit_beat = -1.0;
};

void cadence_capture_timing_onset(SegmentCapture* segment,
                                  double segment_pos,
                                  double segment_beats,
                                  double weight);
void cadence_reset_comp_playback(CompPlaybackState* state);
void cadence_plan_segment_comp(const SegmentCapture* learned_segment,
                               const Controls& controls,
                               const ChordSlot* slot,
                               int segment_index,
                               double segment_start_beat,
                               double segment_beats,
                               uint32_t seed,
                               CompPlaybackState* out);
double cadence_next_comp_hit_beat(const CompPlaybackState* state);
bool cadence_take_due_comp_hit(CompPlaybackState* state,
                               double target_beat,
                               ScheduledCompHit* out);
void cadence_set_comp_release(CompPlaybackState* state, double beat);
void cadence_clear_comp_release(CompPlaybackState* state);
double cadence_next_comp_release_beat(const CompPlaybackState* state);
void cadence_sync_comp_to_position(CompPlaybackState* state,
                                   double abs_beats,
                                   bool* should_sound,
                                   double* off_beat);

}  // namespace downspout::cadence
