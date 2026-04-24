#include "cadence_harmony.hpp"

#include "cadence_rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace downspout::cadence {
namespace {

struct ScaleDef {
    const int* intervals = nullptr;
    int count = 0;
};

struct Candidate {
    uint8_t root_pc = 0;
    uint8_t quality = QUALITY_MAJOR;
    uint8_t note_count = 0;
    uint8_t intervals[CADENCE_MAX_CHORD_NOTES]{};
    uint16_t mask = 0;
};

struct RootPreference {
    uint16_t mask = 0;
    int primary_pc = 0;
    double primary_ratio = 0.0;
};

struct VoicingOption {
    int notes[CADENCE_MAX_CHORD_NOTES]{};
    double cost = 0.0;
};

static const int kScaleChromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const int kScaleMajor[] = {0, 2, 4, 5, 7, 9, 11};
static const int kScaleNatMinor[] = {0, 2, 3, 5, 7, 8, 10};
static const int kScaleHarmMinor[] = {0, 2, 3, 5, 7, 8, 11};
static const int kScalePentMajor[] = {0, 2, 4, 7, 9};
static const int kScalePentMinor[] = {0, 3, 5, 7, 10};
static const int kScaleBlues[] = {0, 3, 5, 6, 7, 10};
static const int kScaleDorian[] = {0, 2, 3, 5, 7, 9, 10};
static const int kScaleMixolydian[] = {0, 2, 4, 5, 7, 9, 10};
static const int kScalePhrygian[] = {0, 1, 3, 5, 7, 8, 10};
static const int kScaleLocrian[] = {0, 1, 3, 5, 6, 8, 10};
static const int kScalePhrygianDominant[] = {0, 1, 4, 5, 7, 8, 10};

static const ScaleDef kScales[SCALE_COUNT] = {
    {kScaleChromatic, 12},
    {kScaleMajor, 7},
    {kScaleNatMinor, 7},
    {kScaleHarmMinor, 7},
    {kScalePentMajor, 5},
    {kScalePentMinor, 5},
    {kScaleBlues, 6},
    {kScaleDorian, 7},
    {kScaleMixolydian, 7},
    {kScalePhrygian, 7},
    {kScaleLocrian, 7},
    {kScalePhrygianDominant, 7}
};

inline int clampi(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline float clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline double clampd(double value, double min_value, double max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline int wrap12(int value) {
    int out = value % 12;
    if (out < 0) {
        out += 12;
    }
    return out;
}

void sort_notes(uint8_t* notes, uint8_t count) {
    std::sort(notes, notes + count);
}

void sort_int_notes(int* notes, int count) {
    std::sort(notes, notes + count);
}

bool quality_uses_seventh(uint8_t quality) {
    return quality == QUALITY_DOM7 || quality == QUALITY_MAJ7 || quality == QUALITY_MIN7;
}

bool fill_quality_intervals(uint8_t quality, uint8_t* note_count, uint8_t* intervals) {
    if (!note_count || !intervals) {
        return false;
    }

    switch (quality) {
        case QUALITY_POWER:
            *note_count = 2;
            intervals[0] = 0;
            intervals[1] = 7;
            intervals[2] = 0;
            intervals[3] = 0;
            return true;
        case QUALITY_MAJOR:
            *note_count = 3;
            intervals[0] = 0;
            intervals[1] = 4;
            intervals[2] = 7;
            intervals[3] = 0;
            return true;
        case QUALITY_MINOR:
            *note_count = 3;
            intervals[0] = 0;
            intervals[1] = 3;
            intervals[2] = 7;
            intervals[3] = 0;
            return true;
        case QUALITY_SUS2:
            *note_count = 3;
            intervals[0] = 0;
            intervals[1] = 2;
            intervals[2] = 7;
            intervals[3] = 0;
            return true;
        case QUALITY_SUS4:
            *note_count = 3;
            intervals[0] = 0;
            intervals[1] = 5;
            intervals[2] = 7;
            intervals[3] = 0;
            return true;
        case QUALITY_DIM:
            *note_count = 3;
            intervals[0] = 0;
            intervals[1] = 3;
            intervals[2] = 6;
            intervals[3] = 0;
            return true;
        case QUALITY_DOM7:
            *note_count = 4;
            intervals[0] = 0;
            intervals[1] = 4;
            intervals[2] = 7;
            intervals[3] = 10;
            return true;
        case QUALITY_MAJ7:
            *note_count = 4;
            intervals[0] = 0;
            intervals[1] = 4;
            intervals[2] = 7;
            intervals[3] = 11;
            return true;
        case QUALITY_MIN7:
            *note_count = 4;
            intervals[0] = 0;
            intervals[1] = 3;
            intervals[2] = 7;
            intervals[3] = 10;
            return true;
        default:
            return false;
    }
}

uint16_t chord_mask(uint8_t root_pc, uint8_t quality) {
    uint8_t intervals[CADENCE_MAX_CHORD_NOTES]{};
    uint8_t note_count = 0;
    if (!fill_quality_intervals(quality, &note_count, intervals)) {
        return 0;
    }

    uint16_t mask = 0;
    for (uint8_t i = 0; i < note_count; ++i) {
        mask |= (uint16_t)(1u << wrap12((int)root_pc + (int)intervals[i]));
    }
    return mask;
}

double segment_jitter(uint32_t seed, int segment_index, int candidate_index, float amount, bool anchored) {
    if (amount <= 0.0001f) {
        return 0.0;
    }
    float scale = amount;
    if (anchored) {
        scale *= 0.35f;
    }
    return (double)cadence_signed_jitter(seed ^
                                         (uint32_t)(segment_index * 2246822519u) ^
                                         (uint32_t)(candidate_index * 3266489917u)) * (double)scale;
}

double transition_jitter(uint32_t seed,
                         int segment_index,
                         int previous_index,
                         int candidate_index,
                         float amount,
                         bool anchored) {
    if (amount <= 0.0001f) {
        return 0.0;
    }
    float scale = amount;
    if (anchored) {
        scale *= 0.45f;
    }
    return (double)cadence_signed_jitter(seed ^
                                         0x9E3779B9u ^
                                         (uint32_t)(segment_index * 1597334677u) ^
                                         (uint32_t)(previous_index * 3812015801u) ^
                                         (uint32_t)(candidate_index * 958689277u)) * (double)scale;
}

bool scale_contains_pc(int scale_index, int key, int note_pc) {
    scale_index = clampi(scale_index, 0, SCALE_COUNT - 1);
    key = wrap12(key);
    note_pc = wrap12(note_pc);

    const ScaleDef& scale = kScales[scale_index];
    const int rel = wrap12(note_pc - key);
    for (int i = 0; i < scale.count; ++i) {
        if (scale.intervals[i] == rel) {
            return true;
        }
    }
    return false;
}

int candidate_out_of_scale_count(const Candidate& candidate, const Controls& controls) {
    if (controls.scale == SCALE_CHROMATIC) {
        return 0;
    }

    int count = 0;
    for (uint8_t i = 0; i < candidate.note_count; ++i) {
        const int pc = wrap12(candidate.root_pc + candidate.intervals[i]);
        if (!scale_contains_pc(controls.scale, controls.key, pc)) {
            ++count;
        }
    }
    return count;
}

bool candidate_allowed_by_scale(const Candidate& candidate, const Controls& controls) {
    if (controls.scale == SCALE_CHROMATIC) {
        return true;
    }

    const float complexity = clampf(controls.complexity, 0.0f, 1.0f);
    if (complexity >= 0.78f) {
        return true;
    }

    return candidate_out_of_scale_count(candidate, controls) == 0;
}

int dominant_pc_for_segment(const SegmentCapture& segment, double* out_weight, double* out_total) {
    int best_pc = 0;
    double best_weight = -1.0;
    double total = 0.0;

    for (int pc = 0; pc < 12; ++pc) {
        const double weight = segment.duration[pc] + segment.onset[pc] * 1.6;
        total += weight;
        if (weight > best_weight) {
            best_weight = weight;
            best_pc = pc;
        }
    }

    if (out_weight) {
        *out_weight = best_weight > 0.0 ? best_weight : 0.0;
    }
    if (out_total) {
        *out_total = total;
    }
    return best_pc;
}

RootPreference preferred_roots_for_segment(const SegmentCapture& segment) {
    RootPreference pref{};
    double weights[12]{};
    double total = 0.0;

    for (int pc = 0; pc < 12; ++pc) {
        weights[pc] = segment.duration[pc] + segment.onset[pc] * 1.8;
        total += weights[pc];
    }

    if (total < 0.02) {
        pref.mask = 0x0FFFu;
        return pref;
    }

    int ranked[3] = {0, 0, 0};
    double ranked_weight[3] = {-1.0, -1.0, -1.0};
    for (int pc = 0; pc < 12; ++pc) {
        const double weight = weights[pc];
        for (int slot = 0; slot < 3; ++slot) {
            if (weight > ranked_weight[slot]) {
                for (int move = 2; move > slot; --move) {
                    ranked_weight[move] = ranked_weight[move - 1];
                    ranked[move] = ranked[move - 1];
                }
                ranked_weight[slot] = weight;
                ranked[slot] = pc;
                break;
            }
        }
    }

    pref.primary_pc = ranked[0];
    pref.primary_ratio = ranked_weight[0] / total;
    pref.mask |= (uint16_t)(1u << ranked[0]);

    if (ranked_weight[1] > total * 0.14 || pref.primary_ratio < 0.74) {
        pref.mask |= (uint16_t)(1u << ranked[1]);
    }
    if (ranked_weight[2] > total * 0.20 || pref.primary_ratio < 0.54) {
        pref.mask |= (uint16_t)(1u << ranked[2]);
    }

    return pref;
}

int register_center(int reg) {
    switch (reg) {
        case REGISTER_LOW:
            return 52;
        case REGISTER_HIGH:
            return 76;
        case REGISTER_MID:
        default:
            return 64;
    }
}

int nearest_midi_for_pc(int pc, int target) {
    int best_note = 60 + pc;
    int best_distance = 999;
    for (int octave = -1; octave <= 10; ++octave) {
        const int candidate = octave * 12 + pc;
        if (candidate < 0 || candidate > 127) {
            continue;
        }
        const int distance = std::abs(candidate - target);
        if (distance < best_distance) {
            best_distance = distance;
            best_note = candidate;
        }
    }
    return best_note;
}

void apply_spread_to_notes(int* notes, int count, int spread) {
    if (count < 3) {
        return;
    }

    sort_int_notes(notes, count);
    switch (spread) {
        case SPREAD_OPEN:
            notes[1] += 12;
            break;
        case SPREAD_DROP2:
            if (count >= 4) {
                notes[count - 2] -= 12;
            }
            break;
        case SPREAD_CLOSE:
        default:
            break;
    }
    sort_int_notes(notes, count);
}

double voicing_cost(const int* notes,
                    int count,
                    const uint8_t* previous_notes,
                    uint8_t previous_count,
                    int center) {
    if (count <= 0) {
        return 0.0;
    }

    double cost = 0.0;
    if (previous_count > 0 && previous_notes) {
        uint8_t prev[CADENCE_MAX_CHORD_NOTES];
        std::memcpy(prev, previous_notes, previous_count);
        sort_notes(prev, previous_count);

        for (int i = 0; i < count; ++i) {
            const int target_index = clampi((i * previous_count) / count, 0, previous_count - 1);
            cost += std::abs(notes[i] - (int)prev[target_index]) * 0.18;
        }
    }

    int sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += notes[i];
    }
    const double average = (double)sum / (double)count;
    cost += std::abs(average - (double)center) * 0.08;

    const int range = notes[count - 1] - notes[0];
    if (range > 20) {
        cost += (double)(range - 20) * 0.07;
    }
    if (notes[0] < 36) {
        cost += (double)(36 - notes[0]) * 0.12;
    }
    if (notes[count - 1] > 100) {
        cost += (double)(notes[count - 1] - 100) * 0.12;
    }

    return cost;
}

bool candidate_from_slot(const ChordSlot& slot, Candidate* out) {
    if (!out || !slot.valid) {
        return false;
    }

    uint8_t intervals[CADENCE_MAX_CHORD_NOTES]{};
    uint8_t note_count = 0;
    if (!fill_quality_intervals(slot.quality, &note_count, intervals)) {
        return false;
    }

    out->root_pc = slot.root_pc;
    out->quality = slot.quality;
    out->note_count = (uint8_t)clampi(slot.note_count > 0 ? slot.note_count : note_count, 0, note_count);
    out->mask = 0;
    for (uint8_t i = 0; i < out->note_count; ++i) {
        out->intervals[i] = intervals[i];
        out->mask |= (uint16_t)(1u << wrap12((int)out->root_pc + (int)out->intervals[i]));
    }
    return out->note_count > 0;
}

double candidate_continuity_bonus(const Candidate& candidate,
                                  const ChordSlot& reference_slot,
                                  float continuity,
                                  bool anchored) {
    if (!reference_slot.valid || continuity <= 0.0001f) {
        return 0.0;
    }

    const uint16_t reference_mask = chord_mask(reference_slot.root_pc, reference_slot.quality);
    const int interval = wrap12((int)candidate.root_pc - (int)reference_slot.root_pc);
    const int step = std::min(interval, 12 - interval);

    double bonus = 0.0;
    if (candidate.root_pc == reference_slot.root_pc) {
        bonus += 0.06 + continuity * 0.24;
    } else if (step <= 2) {
        bonus += 0.02 + continuity * 0.08;
    }
    if (candidate.quality == reference_slot.quality) {
        bonus += 0.04 + continuity * 0.12;
    }
    bonus += (double)__builtin_popcount((unsigned int)(candidate.mask & reference_mask)) * (0.01 + continuity * 0.02);

    return anchored ? bonus * 1.35 : bonus;
}

int build_candidates(const Controls& controls, Candidate* out) {
    int count = 0;
    for (int root = 0; root < 12; ++root) {
        for (uint8_t quality = QUALITY_POWER; quality <= QUALITY_MIN7; ++quality) {
            if (controls.chord_size == CHORD_SIZE_TRIADS && quality_uses_seventh(quality)) {
                continue;
            }

            Candidate candidate{};
            if (!fill_quality_intervals(quality, &candidate.note_count, candidate.intervals)) {
                continue;
            }

            candidate.root_pc = (uint8_t)root;
            candidate.quality = quality;
            candidate.mask = 0;
            for (uint8_t i = 0; i < candidate.note_count; ++i) {
                candidate.mask |= (uint16_t)(1u << wrap12(root + candidate.intervals[i]));
            }
            if (!candidate_allowed_by_scale(candidate, controls)) {
                continue;
            }
            out[count++] = candidate;
        }
    }
    return count;
}

double score_candidate(const SegmentCapture& segment,
                       const Candidate& candidate,
                       const Controls& controls) {
    const double movement = clampd((double)controls.movement, 0.0, 1.0);
    const double complexity = clampd((double)controls.complexity, 0.0, 1.0);
    double weights[12]{};
    double total = 0.0;
    for (int pc = 0; pc < 12; ++pc) {
        weights[pc] = segment.duration[pc] + segment.onset[pc] * 1.35;
        total += weights[pc];
    }
    if (total < 0.02) {
        return 0.0;
    }

    double dominant_weight = 0.0;
    double dominant_total = 0.0;
    const int dominant_pc = dominant_pc_for_segment(segment, &dominant_weight, &dominant_total);
    const double dominant_ratio = dominant_total > 1e-9 ? dominant_weight / dominant_total : 0.0;
    const RootPreference root_pref = preferred_roots_for_segment(segment);

    bool chord_pcs[12]{};
    double score = 0.0;
    for (uint8_t i = 0; i < candidate.note_count; ++i) {
        const int pc = wrap12(candidate.root_pc + candidate.intervals[i]);
        chord_pcs[pc] = true;

        double role_weight = 0.95;
        if (i == 0) {
            role_weight = 1.45;
        } else if (i == 1) {
            role_weight = 1.12;
        } else if (i == 2) {
            role_weight = 0.92;
        } else {
            role_weight = 0.72;
        }

        score += weights[pc] * role_weight;
        if (!scale_contains_pc(controls.scale, controls.key, pc)) {
            score -= 0.72 + (1.0 - complexity) * 0.90;
        }
    }

    for (int pc = 0; pc < 12; ++pc) {
        if (!chord_pcs[pc]) {
            score -= weights[pc] * 0.66;
        }
    }

    score += segment.onset[candidate.root_pc] * 0.95;
    score += segment.duration[candidate.root_pc] * 0.45;

    if (candidate.root_pc == root_pref.primary_pc) {
        score += 0.38 + movement * 0.96 + root_pref.primary_ratio * (0.28 + movement * 0.96);
    } else if (root_pref.mask & (uint16_t)(1u << candidate.root_pc)) {
        score += 0.10 + movement * 0.28;
    } else {
        score -= 0.24 + movement * 1.12 + root_pref.primary_ratio * (0.14 + movement * 0.82);
    }

    if (candidate.root_pc == dominant_pc) {
        score += 0.10 + movement * 0.76 + dominant_ratio * (0.14 + movement * 0.58);
    } else if (chord_pcs[dominant_pc]) {
        score += 0.05 + movement * 0.12 + dominant_ratio * (0.06 + movement * 0.18);
    } else {
        score -= 0.06 + movement * 0.34 + dominant_ratio * (0.12 + movement * 0.38);
    }

    if (scale_contains_pc(controls.scale, controls.key, candidate.root_pc)) {
        score += 0.22 + (1.0 - complexity) * 0.06;
    } else {
        score -= 1.10 - complexity * 0.28;
    }

    switch (candidate.quality) {
        case QUALITY_POWER:
            score -= 0.58 + (1.0 - complexity) * 0.72;
            break;
        case QUALITY_SUS2: {
            const double sus_weight = weights[wrap12(candidate.root_pc + 2)];
            const double major_third = weights[wrap12(candidate.root_pc + 4)];
            const double minor_third = weights[wrap12(candidate.root_pc + 3)];
            score -= 0.28 + (1.0 - complexity) * 0.34;
            score += sus_weight / (total + 1e-9) * 0.50;
            score += (major_third + minor_third < total * 0.18) ? 0.14 : -0.08;
            break;
        }
        case QUALITY_SUS4: {
            const double sus_weight = weights[wrap12(candidate.root_pc + 5)];
            const double major_third = weights[wrap12(candidate.root_pc + 4)];
            const double minor_third = weights[wrap12(candidate.root_pc + 3)];
            score -= 0.28 + (1.0 - complexity) * 0.34;
            score += sus_weight / (total + 1e-9) * 0.50;
            score += (major_third + minor_third < total * 0.18) ? 0.14 : -0.08;
            break;
        }
        case QUALITY_DIM:
            score -= 0.24 + (1.0 - complexity) * 0.16;
            score += weights[wrap12(candidate.root_pc + 6)] / (total + 1e-9) * 0.32;
            break;
        case QUALITY_DOM7:
        case QUALITY_MAJ7:
        case QUALITY_MIN7: {
            const int seventh_pc = wrap12(candidate.root_pc + candidate.intervals[3]);
            score -= 0.12 + (1.0 - complexity) * 0.24;
            score += weights[seventh_pc] / (total + 1e-9) * 0.36;
            break;
        }
        case QUALITY_MAJOR:
            score += weights[wrap12(candidate.root_pc + 4)] / (total + 1e-9) * 0.22;
            break;
        case QUALITY_MINOR:
            score += weights[wrap12(candidate.root_pc + 3)] / (total + 1e-9) * 0.22;
            break;
        default:
            break;
    }

    return score / (total + 1e-9);
}

int popcount16(uint16_t value) {
    int count = 0;
    while (value) {
        count += (value & 1u) ? 1 : 0;
        value >>= 1u;
    }
    return count;
}

double transition_score(const Candidate& previous,
                        const Candidate& next,
                        const Controls& controls,
                        int segment_index,
                        int segment_count) {
    const double movement = clampd((double)controls.movement, 0.0, 1.0);
    const int interval = wrap12((int)next.root_pc - (int)previous.root_pc);
    const int step = std::min(interval, 12 - interval);

    double score = 0.0;
    if (interval == 0 && previous.quality == next.quality) {
        score -= 0.10 + movement * 0.54;
    } else if (interval == 0) {
        score -= 0.04 + movement * 0.22;
    } else if (interval == 5 || interval == 7) {
        score += 0.18 + movement * 0.10;
    } else if (interval == 2 || interval == 10) {
        score += 0.08 + movement * 0.06;
    } else if (step == 1) {
        score += 0.03 + movement * 0.06;
    } else if (step == 6) {
        score -= 0.08 + movement * 0.12;
    } else {
        score += 0.02;
    }

    score += (double)popcount16(previous.mask & next.mask) * (0.14 - movement * 0.08);

    if (segment_index == segment_count - 1 && next.root_pc == controls.key) {
        score += 0.18 + (1.0 - movement) * 0.08;
    }
    if (segment_index == 0 && next.root_pc == controls.key) {
        score += 0.08 + (1.0 - movement) * 0.08;
    }
    if (next.quality == QUALITY_DIM) {
        score -= 0.08;
    }

    return score;
}

void build_voicing(const Candidate& candidate,
                   const Controls& controls,
                   const uint8_t* previous_notes,
                   uint8_t previous_count,
                   uint8_t velocity,
                   int segment_index,
                   const CadenceBuildOptions& options,
                   ChordSlot* out) {
    out->valid = false;
    out->root_pc = candidate.root_pc;
    out->quality = candidate.quality;
    out->note_count = candidate.note_count;
    out->velocity = velocity;

    if (candidate.note_count == 0) {
        return;
    }

    const int center = register_center(controls.reg);
    const int anchor_target = center - 7;
    const int base_root = nearest_midi_for_pc(candidate.root_pc, anchor_target);

    int base[CADENCE_MAX_CHORD_NOTES]{};
    for (uint8_t i = 0; i < candidate.note_count; ++i) {
        base[i] = base_root + candidate.intervals[i];
    }

    VoicingOption options_buffer[CADENCE_MAX_CHORD_NOTES * 3]{};
    int option_count = 0;

    for (uint8_t inversion = 0; inversion < candidate.note_count; ++inversion) {
        int notes[CADENCE_MAX_CHORD_NOTES]{};
        for (uint8_t i = 0; i < candidate.note_count; ++i) {
            notes[i] = base[i];
        }

        for (uint8_t i = 0; i < inversion; ++i) {
            notes[i] += 12;
        }
        sort_int_notes(notes, candidate.note_count);
        apply_spread_to_notes(notes, candidate.note_count, controls.spread);

        for (int shift = -1; shift <= 1; ++shift) {
            int shifted[CADENCE_MAX_CHORD_NOTES]{};
            bool valid = true;
            for (uint8_t i = 0; i < candidate.note_count; ++i) {
                shifted[i] = notes[i] + shift * 12;
                if (shifted[i] < 0 || shifted[i] > 127) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }

            sort_int_notes(shifted, candidate.note_count);
            VoicingOption& entry = options_buffer[option_count++];
            for (uint8_t i = 0; i < candidate.note_count; ++i) {
                entry.notes[i] = shifted[i];
            }
            entry.cost = voicing_cost(shifted, candidate.note_count, previous_notes, previous_count, center);
        }
    }

    if (option_count <= 0) {
        return;
    }

    std::sort(options_buffer, options_buffer + option_count, [](const VoicingOption& a, const VoicingOption& b) {
        return a.cost < b.cost;
    });

    int chosen_index = 0;
    if (options.voicing_jitter > 0.0001f && option_count > 1) {
        const double best_cost = options_buffer[0].cost;
        const double window = 0.35 + (double)clampf(options.voicing_jitter, 0.0f, 1.0f) * 1.40;
        int eligible = 1;
        while (eligible < option_count && options_buffer[eligible].cost <= best_cost + window) {
            ++eligible;
        }
        if (eligible > 1) {
            const uint32_t seed = options.seed ^
                                  (uint32_t)(segment_index * 2246822519u) ^
                                  (uint32_t)(candidate.root_pc * 3266489917u) ^
                                  (uint32_t)(candidate.quality * 668265263u);
            chosen_index = (int)(cadence_mix_u32(seed) % (uint32_t)eligible);
        }
    }

    const VoicingOption& chosen = options_buffer[chosen_index];
    for (uint8_t i = 0; i < candidate.note_count; ++i) {
        out->notes[i] = (uint8_t)clampi(chosen.notes[i], 0, 127);
    }
    sort_notes(out->notes, out->note_count);
    out->valid = true;
}

}  // namespace

void cadence_clear_capture(SegmentCapture* capture, int count) {
    if (!capture || count <= 0) {
        return;
    }
    std::memset(capture, 0, sizeof(SegmentCapture) * (size_t)count);
}

void cadence_copy_capture(SegmentCapture* dst, const SegmentCapture* src, int count) {
    if (!dst || !src || count <= 0) {
        return;
    }
    std::memcpy(dst, src, sizeof(SegmentCapture) * (size_t)count);
}

void cadence_clear_progression(ChordSlot* slots, int count) {
    if (!slots || count <= 0) {
        return;
    }
    std::memset(slots, 0, sizeof(ChordSlot) * (size_t)count);
}

void cadence_copy_progression(ChordSlot* dst, const ChordSlot* src, int count) {
    if (!dst || !src || count <= 0) {
        return;
    }
    std::memcpy(dst, src, sizeof(ChordSlot) * (size_t)count);
}

double cadence_segment_activity(const SegmentCapture& segment) {
    double total = 0.0;
    for (int pc = 0; pc < 12; ++pc) {
        total += segment.duration[pc];
        total += segment.onset[pc] * 1.35;
    }
    return total;
}

bool cadence_build_progression_from_capture(const SegmentCapture* capture,
                                            int segment_count,
                                            const Controls& controls,
                                            const ChordSlot* reference_slots,
                                            int reference_count,
                                            const CadenceBuildOptions& options,
                                            ChordSlot* out_slots) {
    if (!capture || !out_slots || segment_count <= 0 || segment_count > CADENCE_MAX_SEGMENTS) {
        return false;
    }

    double total_activity = 0.0;
    for (int i = 0; i < segment_count; ++i) {
        total_activity += cadence_segment_activity(capture[i]);
    }
    if (total_activity < 0.2) {
        return false;
    }

    Candidate candidates[CADENCE_MAX_CANDIDATES]{};
    const int candidate_count = build_candidates(controls, candidates);
    if (candidate_count <= 0) {
        return false;
    }

    double local_scores[CADENCE_MAX_SEGMENTS][CADENCE_MAX_CANDIDATES]{};
    double dp[CADENCE_MAX_SEGMENTS][CADENCE_MAX_CANDIDATES]{};
    int trace[CADENCE_MAX_SEGMENTS][CADENCE_MAX_CANDIDATES]{};
    RootPreference root_prefs[CADENCE_MAX_SEGMENTS]{};

    for (int s = 0; s < segment_count; ++s) {
        root_prefs[s] = preferred_roots_for_segment(capture[s]);
        const bool anchored = options.anchor_endpoints && (s == 0 || s == segment_count - 1);
        for (int c = 0; c < candidate_count; ++c) {
            local_scores[s][c] = score_candidate(capture[s], candidates[c], controls);
            if (!(root_prefs[s].mask & (uint16_t)(1u << candidates[c].root_pc))) {
                local_scores[s][c] -= 0.18 + (double)controls.movement * 0.86;
            }
            if (reference_slots && s < reference_count) {
                local_scores[s][c] += candidate_continuity_bonus(candidates[c],
                                                                 reference_slots[s],
                                                                 options.continuity,
                                                                 anchored);
            }
            local_scores[s][c] += segment_jitter(options.seed, s, c, options.local_jitter, anchored);
            dp[s][c] = -1.0e9;
            trace[s][c] = -1;
        }
    }

    for (int c = 0; c < candidate_count; ++c) {
        double start_bonus = 0.0;
        if (candidates[c].root_pc == controls.key) {
            start_bonus += 0.10;
        }
        dp[0][c] = local_scores[0][c] + start_bonus;
    }

    for (int s = 1; s < segment_count; ++s) {
        const bool anchored = options.anchor_endpoints && (s == 0 || s == segment_count - 1);
        for (int c = 0; c < candidate_count; ++c) {
            for (int p = 0; p < candidate_count; ++p) {
                double candidate_score = dp[s - 1][p] +
                                         local_scores[s][c] +
                                         transition_score(candidates[p], candidates[c], controls, s, segment_count);
                candidate_score += transition_jitter(options.seed, s, p, c, options.transition_jitter, anchored);
                if (candidate_score > dp[s][c]) {
                    dp[s][c] = candidate_score;
                    trace[s][c] = p;
                }
            }
        }
    }

    int best_index = 0;
    double best_score = dp[segment_count - 1][0];
    for (int c = 1; c < candidate_count; ++c) {
        if (dp[segment_count - 1][c] > best_score) {
            best_score = dp[segment_count - 1][c];
            best_index = c;
        }
    }

    int chosen[CADENCE_MAX_SEGMENTS]{};
    chosen[segment_count - 1] = best_index;
    for (int s = segment_count - 1; s > 0; --s) {
        const int prev = trace[s][chosen[s]];
        chosen[s - 1] = prev >= 0 ? prev : 0;
    }

    cadence_clear_progression(out_slots, segment_count);

    ChordSlot previous_slot{};
    for (int s = 0; s < segment_count; ++s) {
        const Candidate& candidate = candidates[chosen[s]];
        const double activity = cadence_segment_activity(capture[s]);
        const uint8_t velocity = (uint8_t)clampi((int)lround(76.0 + std::min(28.0, activity * 12.0)), 60, 110);
        build_voicing(candidate,
                      controls,
                      previous_slot.valid ? previous_slot.notes : nullptr,
                      previous_slot.valid ? previous_slot.note_count : 0,
                      velocity,
                      s,
                      options,
                      &out_slots[s]);
        previous_slot = out_slots[s];
    }

    return true;
}

bool cadence_revoice_progression(const ChordSlot* source_slots,
                                 int segment_count,
                                 const Controls& controls,
                                 const CadenceBuildOptions& options,
                                 ChordSlot* out_slots) {
    if (!source_slots || !out_slots || segment_count <= 0 || segment_count > CADENCE_MAX_SEGMENTS) {
        return false;
    }

    cadence_clear_progression(out_slots, segment_count);

    ChordSlot previous_slot{};
    bool any_valid = false;
    for (int s = 0; s < segment_count; ++s) {
        if (!source_slots[s].valid) {
            continue;
        }

        Candidate candidate{};
        if (!candidate_from_slot(source_slots[s], &candidate)) {
            continue;
        }

        build_voicing(candidate,
                      controls,
                      previous_slot.valid ? previous_slot.notes : nullptr,
                      previous_slot.valid ? previous_slot.note_count : 0,
                      source_slots[s].velocity,
                      s,
                      options,
                      &out_slots[s]);
        previous_slot = out_slots[s];
        any_valid = any_valid || out_slots[s].valid;
    }

    return any_valid;
}

}  // namespace downspout::cadence
