#pragma once

#include <cstdint>

namespace downspout::cadence {

inline uint32_t cadence_mix_u32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

inline float cadence_signed_jitter(uint32_t seed) {
    const uint32_t mixed = cadence_mix_u32(seed);
    const float unit = (float)(mixed & 0x00FFFFFFu) / 16777215.0f;
    return unit * 2.0f - 1.0f;
}

struct CadenceRng {
    uint32_t state = 0xA341316Cu;

    void seed(uint32_t seed_value) {
        state = seed_value ? seed_value : 0xA341316Cu;
    }

    uint32_t next_u32() {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    float next_float() {
        return (float)(next_u32() & 0x00FFFFFFu) / 16777215.0f;
    }

    int next_int(int min_value, int max_value) {
        if (max_value <= min_value) {
            return min_value;
        }
        return min_value + (int)(next_u32() % (uint32_t)(max_value - min_value + 1));
    }
};

}  // namespace downspout::cadence
