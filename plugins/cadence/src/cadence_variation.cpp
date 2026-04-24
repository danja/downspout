#include "cadence_variation.hpp"

#include "cadence_rng.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::cadence {
namespace {

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

int cycles_between_mutations(float vary) {
    const float target_cycles = 1.0f + 7.0f * powf(1.0f - vary, 2.5f);
    return clampi((int)ceilf(target_cycles - 1e-6f), 1, 64);
}

uint32_t controls_seed(const Controls& controls) {
    uint32_t seed = 0xC4D3A91Bu;
    seed ^= (uint32_t)(controls.key & 0xFF);
    seed ^= (uint32_t)(controls.scale & 0xFF) << 8;
    seed ^= (uint32_t)(controls.cycle_bars & 0xFF) << 16;
    seed ^= (uint32_t)(controls.granularity & 0xFF) << 24;
    seed ^= cadence_mix_u32((uint32_t)lroundf(controls.complexity * 1000.0f));
    seed ^= cadence_mix_u32((uint32_t)lroundf(controls.movement * 1000.0f));
    seed ^= cadence_mix_u32((uint32_t)(controls.chord_size & 0xFF) << 1);
    seed ^= cadence_mix_u32((uint32_t)(controls.reg & 0xFF) << 2);
    seed ^= cadence_mix_u32((uint32_t)(controls.spread & 0xFF) << 3);
    return seed;
}

CadenceBuildOptions options_for_vary(float vary,
                                     uint32_t seed,
                                     int mutation_serial,
                                     float roll,
                                     bool* prefer_revoice) {
    CadenceBuildOptions options{};
    options.seed = seed ^ cadence_mix_u32((uint32_t)mutation_serial * 2246822519u);
    options.anchor_endpoints = vary < 0.92f;
    *prefer_revoice = false;

    if (vary < 0.20f) {
        options.voicing_jitter = clampf(0.28f + vary * 1.8f, 0.0f, 1.0f);
        options.continuity = 0.95f;
        *prefer_revoice = true;
        return options;
    }

    if (vary < 0.45f) {
        options.voicing_jitter = clampf(0.30f + vary * 1.2f, 0.0f, 1.0f);
        options.local_jitter = 0.05f + vary * 0.12f;
        options.transition_jitter = 0.03f + vary * 0.08f;
        options.continuity = 0.82f;
        *prefer_revoice = roll < 0.58f;
        return options;
    }

    if (vary < 0.75f) {
        options.voicing_jitter = clampf(0.36f + vary * 0.75f, 0.0f, 1.0f);
        options.local_jitter = 0.10f + vary * 0.20f;
        options.transition_jitter = 0.06f + vary * 0.14f;
        options.continuity = 0.56f;
        *prefer_revoice = roll < 0.24f;
        return options;
    }

    options.voicing_jitter = clampf(0.55f + vary * 0.45f, 0.0f, 1.0f);
    options.local_jitter = 0.18f + vary * 0.28f;
    options.transition_jitter = 0.10f + vary * 0.20f;
    options.continuity = vary >= 0.999f ? 0.0f : 0.18f;
    options.anchor_endpoints = vary >= 0.999f ? false : true;
    *prefer_revoice = roll < 0.10f;
    return options;
}

}  // namespace

void cadence_reset_variation_progress(VariationState* variation) {
    if (!variation) {
        return;
    }
    variation->version = kVariationStateVersion;
    variation->completed_cycles = 0;
    variation->last_mutation_cycle = 0;
    variation->mutation_serial = 0;
}

bool cadence_apply_cycle_variation(const SegmentCapture* learned_capture,
                                   int learned_segment_count,
                                   const Controls& controls,
                                   VariationState* variation,
                                   const ChordSlot* base_slots,
                                   int base_segment_count,
                                   const ChordSlot* previous_playback,
                                   int previous_count,
                                   ChordSlot* out_slots) {
    if (!variation || !base_slots || !out_slots || base_segment_count <= 0) {
        return false;
    }

    if (variation->version != kVariationStateVersion) {
        cadence_reset_variation_progress(variation);
    }

    if (variation->completed_cycles < std::numeric_limits<int64_t>::max()) {
        variation->completed_cycles += 1;
    }

    const float vary = clampf(controls.vary, 0.0f, 1.0f);
    if (vary <= 0.0001f) {
        return false;
    }

    const int interval_cycles = cycles_between_mutations(vary);
    if ((variation->completed_cycles - variation->last_mutation_cycle) < interval_cycles) {
        return false;
    }

    variation->last_mutation_cycle = variation->completed_cycles;
    if (variation->mutation_serial < std::numeric_limits<int32_t>::max()) {
        variation->mutation_serial += 1;
    }

    const uint32_t seed = controls_seed(controls) ^
                          cadence_mix_u32((uint32_t)variation->completed_cycles) ^
                          cadence_mix_u32((uint32_t)variation->mutation_serial * 3266489917u);

    CadenceRng rng;
    rng.seed(seed);
    const float roll = rng.next_float();

    bool prefer_revoice = false;
    const CadenceBuildOptions options = options_for_vary(vary, seed, variation->mutation_serial, roll, &prefer_revoice);

    if (prefer_revoice || !learned_capture || learned_segment_count != base_segment_count) {
        return cadence_revoice_progression(base_slots, base_segment_count, controls, options, out_slots);
    }

    if (cadence_build_progression_from_capture(learned_capture,
                                               learned_segment_count,
                                               controls,
                                               previous_playback,
                                               previous_count,
                                               options,
                                               out_slots)) {
        return true;
    }

    return cadence_revoice_progression(base_slots, base_segment_count, controls, options, out_slots);
}

}  // namespace downspout::cadence
