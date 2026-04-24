#include "cadence_comping.hpp"

#include "cadence_rng.hpp"
#include "cadence_transport.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace downspout::cadence {
namespace {

struct CompCandidate {
    double rel = 0.0;
    double weight = 0.0;
    double accent = 0.8;
};

inline double absd(double value) {
    return value < 0.0 ? -value : value;
}

double gate_fraction_for_hit(const Controls& controls,
                             float comp,
                             int hit_index,
                             int hit_count,
                             double activity_ratio) {
    const double max_gate = cadence_note_length_fraction(controls);
    if (comp <= 0.02f) {
        return max_gate;
    }

    double gate = max_gate * (0.82 - (double)comp * 0.40);
    gate *= 0.94 - activity_ratio * 0.28;
    if (hit_count > 1) {
        gate *= hit_index == hit_count - 1 ? 0.92 : 0.60;
    }
    return clampd(gate, 0.08, max_gate);
}

void add_candidate(CompCandidate* candidates,
                   int* count,
                   double rel,
                   double weight,
                   double accent) {
    if (!candidates || !count || *count >= 24 || weight <= 0.0001) {
        return;
    }
    candidates[*count].rel = clampd(rel, 0.0, 0.98);
    candidates[*count].weight = weight;
    candidates[*count].accent = clampd(accent, 0.55, 1.15);
    *count += 1;
}

double candidate_jitter(uint32_t seed, int candidate_index, float comp, float vary) {
    const float amount = 0.04f + comp * 0.10f + vary * 0.06f;
    return (double)cadence_signed_jitter(seed ^ (uint32_t)(candidate_index * 2246822519u)) * (double)amount;
}

double max_bin_value(const SegmentCapture* segment) {
    if (!segment) {
        return 0.0;
    }
    double max_value = 0.0;
    for (int i = 0; i < CADENCE_TIMING_BINS; ++i) {
        if (segment->timing_bins[i] > max_value) {
            max_value = segment->timing_bins[i];
        }
    }
    return max_value;
}

double largest_gap_center(const SegmentCapture* segment) {
    if (!segment || segment->onset_total <= 0.0001) {
        return 0.5;
    }

    int active_bins[CADENCE_TIMING_BINS];
    int active_count = 0;
    const double threshold = max_bin_value(segment) * 0.20;
    for (int i = 0; i < CADENCE_TIMING_BINS; ++i) {
        if (segment->timing_bins[i] >= threshold && threshold > 0.0) {
            active_bins[active_count++] = i;
        }
    }
    if (active_count <= 0) {
        return 0.5;
    }

    double best_gap = -1.0;
    double best_center = 0.5;
    double previous = 0.0;
    for (int i = 0; i < active_count; ++i) {
        const double position = ((double)active_bins[i] + 0.5) / (double)CADENCE_TIMING_BINS;
        const double gap = position - previous;
        if (gap > best_gap) {
            best_gap = gap;
            best_center = previous + gap * 0.58;
        }
        previous = position;
    }
    const double tail_gap = 1.0 - previous;
    if (tail_gap > best_gap) {
        best_center = previous + tail_gap * 0.55;
    }
    return clampd(best_center, 0.08, 0.92);
}

void sort_hits(ScheduledCompHit* hits, int count) {
    std::sort(hits, hits + count, [](const ScheduledCompHit& a, const ScheduledCompHit& b) {
        return a.beat < b.beat;
    });
}

}  // namespace

void cadence_capture_timing_onset(SegmentCapture* segment,
                                  double segment_pos,
                                  double segment_beats,
                                  double weight) {
    if (!segment || segment_beats <= 0.0 || weight <= 0.0001) {
        return;
    }

    const double rel = clampd(segment_pos / segment_beats, 0.0, 0.999999);
    int bin = (int)std::floor(rel * (double)CADENCE_TIMING_BINS);
    bin = clampi(bin, 0, CADENCE_TIMING_BINS - 1);
    segment->timing_bins[bin] += weight;
    segment->onset_total += weight;
}

void cadence_reset_comp_playback(CompPlaybackState* state) {
    if (!state) {
        return;
    }
    *state = CompPlaybackState{};
}

void cadence_plan_segment_comp(const SegmentCapture* learned_segment,
                               const Controls& controls,
                               const ChordSlot* slot,
                               int segment_index,
                               double segment_start_beat,
                               double segment_beats,
                               uint32_t seed,
                               CompPlaybackState* out) {
    if (!out) {
        return;
    }

    cadence_reset_comp_playback(out);
    out->segment_index = segment_index;
    out->segment_start_beat = segment_start_beat;
    out->segment_end_beat = segment_start_beat + segment_beats;

    if (!slot || !slot->valid || segment_beats <= 0.0) {
        return;
    }

    const float comp = clampf(controls.comp, 0.0f, 1.0f);
    const float vary = clampf(controls.vary, 0.0f, 1.0f);

    if (comp <= 0.02f) {
        out->hits[0].active = true;
        out->hits[0].beat = segment_start_beat;
        out->hits[0].off_beat = segment_start_beat + segment_beats * cadence_note_length_fraction(controls);
        out->hits[0].velocity = slot->velocity;
        out->hit_count = 1;
        return;
    }

    const double onset_total = learned_segment ? learned_segment->onset_total : 0.0;
    const double activity_ratio = clampd(onset_total / 2.4, 0.0, 1.0);

    int target_hits = 1 + (int)std::floor(comp * 2.8f);
    if (activity_ratio > 0.72 && target_hits > 1) {
        target_hits -= 1;
    }
    target_hits = clampi(target_hits, 1, CADENCE_MAX_COMP_HITS);

    CompCandidate candidates[24]{};
    int candidate_count = 0;

    const double boundary_weight = 0.58 + (1.0 - comp) * 0.30 + (learned_segment ? learned_segment->timing_bins[0] * 0.18 : 0.0);
    add_candidate(candidates, &candidate_count, 0.0, boundary_weight, 0.94);

    if (learned_segment && learned_segment->onset_total > 0.0001) {
        for (int i = 0; i < CADENCE_TIMING_BINS; ++i) {
            const double bin_weight = learned_segment->timing_bins[i];
            if (bin_weight <= 0.0001) {
                continue;
            }
            const double center = ((double)i + 0.5) / (double)CADENCE_TIMING_BINS;
            const double response_shift = (i == 0) ? 0.0 : (0.04 + (double)comp * 0.05);
            const double rel = clampd(center + response_shift, 0.0, 0.96);
            const double weight = bin_weight * (0.50 + (double)comp * 0.85);
            const double accent = i == 0 ? 0.98 : (0.72 + std::min(0.26, bin_weight * 0.08));
            add_candidate(candidates, &candidate_count, rel, weight, accent);
        }

        const double answer_center = largest_gap_center(learned_segment);
        add_candidate(candidates, &candidate_count,
                      answer_center,
                      (0.18 + (double)comp * 0.44) * (1.0 + (1.0 - activity_ratio) * 0.7),
                      0.82);

        if (comp > 0.32f) {
            add_candidate(candidates, &candidate_count,
                          0.78 + (double)comp * 0.08,
                          0.16 + (double)comp * 0.40,
                          0.76);
        }
    } else {
        add_candidate(candidates, &candidate_count, 0.24, 0.38 + (double)comp * 0.34, 0.82);
        add_candidate(candidates, &candidate_count, 0.52, 0.32 + (double)comp * 0.38, 0.78);
        add_candidate(candidates, &candidate_count, 0.80, 0.26 + (double)comp * 0.44, 0.74);
    }

    for (int i = 0; i < candidate_count; ++i) {
        candidates[i].weight += candidate_jitter(seed, i, comp, vary);
    }

    std::sort(candidates, candidates + candidate_count, [](const CompCandidate& a, const CompCandidate& b) {
        return a.weight > b.weight;
    });

    const double min_spacing = segment_beats * clampd(0.15 - (double)comp * 0.04, 0.08, 0.15);
    int chosen_count = 0;
    for (int i = 0; i < candidate_count && chosen_count < target_hits; ++i) {
        const double beat = segment_start_beat + candidates[i].rel * segment_beats;
        bool too_close = false;
        for (int j = 0; j < chosen_count; ++j) {
            if (absd(out->hits[j].beat - beat) < min_spacing) {
                too_close = true;
                break;
            }
        }
        if (too_close) {
            continue;
        }

        out->hits[chosen_count].active = true;
        out->hits[chosen_count].beat = beat;
        out->hits[chosen_count].velocity = (uint8_t)clampi((int)lround((double)slot->velocity * candidates[i].accent), 54, 118);
        ++chosen_count;
    }

    if (chosen_count <= 0) {
        out->hits[0].active = true;
        out->hits[0].beat = segment_start_beat;
        out->hits[0].velocity = slot->velocity;
        chosen_count = 1;
    }

    sort_hits(out->hits, chosen_count);
    for (int i = 0; i < chosen_count; ++i) {
        const double gate_fraction = gate_fraction_for_hit(controls, comp, i, chosen_count, activity_ratio);
        double off_beat = out->hits[i].beat + segment_beats * gate_fraction;
        if (i + 1 < chosen_count) {
            off_beat = std::min(off_beat, out->hits[i + 1].beat - segment_beats * 0.03);
        }
        off_beat = std::min(off_beat, segment_start_beat + segment_beats * cadence_note_length_fraction(controls));
        off_beat = clampd(off_beat,
                          out->hits[i].beat + segment_beats * 0.04,
                          segment_start_beat + segment_beats * 0.995);
        out->hits[i].off_beat = off_beat;
    }

    out->hit_count = chosen_count;
}
double cadence_next_comp_hit_beat(const CompPlaybackState* state) {
    if (!state || state->next_hit_index >= state->hit_count) {
        return std::numeric_limits<double>::infinity();
    }
    return state->hits[state->next_hit_index].beat;
}

bool cadence_take_due_comp_hit(CompPlaybackState* state,
                               double target_beat,
                               ScheduledCompHit* out) {
    if (!state || state->next_hit_index >= state->hit_count) {
        return false;
    }

    const ScheduledCompHit& hit = state->hits[state->next_hit_index];
    if (!hit.active || hit.beat > target_beat + CADENCE_BEAT_EPSILON) {
        return false;
    }

    if (out) {
        *out = hit;
    }
    state->last_hit_beat = hit.beat;
    state->next_hit_index += 1;
    return true;
}

void cadence_set_comp_release(CompPlaybackState* state, double beat) {
    if (!state) {
        return;
    }
    state->release_pending = true;
    state->release_beat = beat;
}

void cadence_clear_comp_release(CompPlaybackState* state) {
    if (!state) {
        return;
    }
    state->release_pending = false;
    state->release_beat = 0.0;
}

double cadence_next_comp_release_beat(const CompPlaybackState* state) {
    if (!state || !state->release_pending) {
        return std::numeric_limits<double>::infinity();
    }
    return state->release_beat;
}

void cadence_sync_comp_to_position(CompPlaybackState* state,
                                   double abs_beats,
                                   bool* should_sound,
                                   double* off_beat) {
    if (should_sound) {
        *should_sound = false;
    }
    if (off_beat) {
        *off_beat = 0.0;
    }
    if (!state) {
        return;
    }

    cadence_clear_comp_release(state);
    state->next_hit_index = state->hit_count;

    int sounding_index = -1;
    for (int i = 0; i < state->hit_count; ++i) {
        if (!state->hits[i].active) {
            continue;
        }
        if (state->hits[i].beat <= abs_beats + CADENCE_BEAT_EPSILON) {
            if (state->hits[i].off_beat > abs_beats + CADENCE_BEAT_EPSILON) {
                sounding_index = i;
            }
            state->next_hit_index = i + 1;
        } else {
            state->next_hit_index = i;
            break;
        }
    }

    if (sounding_index >= 0) {
        if (should_sound) {
            *should_sound = true;
        }
        if (off_beat) {
            *off_beat = state->hits[sounding_index].off_beat;
        }
        cadence_set_comp_release(state, state->hits[sounding_index].off_beat);
    }
}

}  // namespace downspout::cadence
