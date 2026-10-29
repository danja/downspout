#pragma once

#include "heavyverb_core.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace downspout::heavyverb {

// Preset 0 is Custom (changes nothing). 1-10 are typical spaces, 11-15 are special effects.
// A preset is applied by the panel as ordinary parameter values, never by the engine, so a reloaded
// project keeps whatever the host saved.
inline constexpr int kPresetCount = 15;
inline constexpr int kTypicalPresets = 10;

struct Preset {
    const char* name;
    const char* note;   // one line shown on the panel
    int dimensions;
    std::array<float, kMaxDimensions> distance;
    std::array<float, kMaxDimensions> damping;
    std::array<float, kMaxDimensions> filter;
    float decay, preDelay, mix, width, modulation, lowCut, highCut, diffusion, early, output;
    int effect;
    float effectAmount, shift;
};

inline constexpr std::array<Preset, kPresetCount> kPresets = {{
    // ---- typical scenarios -------------------------------------------------------------------
    {"Master Glue", "short and quiet: binds a mix without an audible tail",
     2, {3.2f, 4.6f, 7.0f, 11.0f, 17.0f, 23.0f}, {55, 50, 35, 35, 35, 35}, {10, 10, 0, 0, 0, 0},
     0.6f, 0.0f, 10.0f, 100.0f, 20.0f, 120.0f, 10000.0f, 80.0f, 30.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Small Room", "a bedroom or booth: close walls, quick decay",
     3, {3.5f, 4.2f, 2.6f, 11.0f, 17.0f, 23.0f}, {45, 45, 50, 35, 35, 35}, {0, 0, 0, 0, 0, 0},
     0.5f, 2.0f, 20.0f, 100.0f, 25.0f, 80.0f, 12000.0f, 75.0f, 60.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Live Room", "a tracking room: lively, a little bright",
     3, {6.5f, 8.8f, 4.2f, 23.0f, 31.0f, 43.0f}, {35, 35, 40, 35, 35, 35}, {0, 0, 0, 0, 0, 0},
     0.9f, 6.0f, 22.0f, 100.0f, 30.0f, 70.0f, 14000.0f, 70.0f, 55.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Chamber", "stone echo chamber, bass kept out of the loop",
     3, {8.0f, 10.0f, 5.5f, 23.0f, 31.0f, 43.0f}, {50, 50, 55, 35, 35, 35}, {12, 12, 12, 0, 0, 0},
     1.4f, 10.0f, 24.0f, 110.0f, 35.0f, 100.0f, 11000.0f, 85.0f, 40.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Bright Plate", "dense and fast, no early reflections: the plate sound",
     4, {1.6f, 2.1f, 2.9f, 3.7f, 31.0f, 43.0f}, {8, 8, 10, 10, 35, 35}, {0, 0, 0, 0, 0, 0},
     2.0f, 10.0f, 25.0f, 120.0f, 55.0f, 150.0f, 18000.0f, 100.0f, 0.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Warm Hall", "a dark hall that ducks under the vocal",
     4, {14.0f, 19.0f, 11.0f, 26.0f, 31.0f, 43.0f}, {55, 55, 60, 55, 35, 35}, {-20, -20, -20, -20, 0, 0},
     2.2f, 20.0f, 22.0f, 110.0f, 40.0f, 80.0f, 9000.0f, 80.0f, 45.0f, 0.0f, kEffectDuck, 30.0f, 12.0f},
    {"Concert Hall", "a large hall with a clear pre-delay",
     3, {22.0f, 33.0f, 15.0f, 26.0f, 31.0f, 43.0f}, {45, 45, 50, 35, 35, 35}, {0, 0, 0, 0, 0, 0},
     2.8f, 25.0f, 25.0f, 120.0f, 40.0f, 70.0f, 13000.0f, 80.0f, 45.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Cathedral", "huge stone space: long, dark and slow to build",
     4, {30.0f, 48.0f, 24.0f, 62.0f, 31.0f, 43.0f}, {60, 60, 65, 65, 35, 35}, {8, 8, 8, 8, 0, 0},
     6.5f, 35.0f, 28.0f, 130.0f, 45.0f, 90.0f, 8000.0f, 85.0f, 35.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Tunnel", "one very long axis and two narrow ones: flutter and echoes",
     3, {85.0f, 4.2f, 4.0f, 23.0f, 31.0f, 43.0f}, {40, 45, 45, 35, 35, 35}, {0, 0, 0, 0, 0, 0},
     4.5f, 8.0f, 25.0f, 100.0f, 25.0f, 100.0f, 10000.0f, 55.0f, 60.0f, 0.0f, kEffectOff, 50.0f, 12.0f},
    {"Canyon", "distant walls: sparse, widely spaced echoes that bloom",
     2, {95.0f, 60.0f, 17.0f, 23.0f, 31.0f, 43.0f}, {50, 55, 35, 35, 35, 35}, {0, 0, 0, 0, 0, 0},
     3.5f, 30.0f, 28.0f, 140.0f, 30.0f, 100.0f, 9000.0f, 50.0f, 70.0f, 0.0f, kEffectOff, 50.0f, 12.0f},

    // ---- special effects ----------------------------------------------------------------------
    {"Shimmer Pad", "an octave up fed back through the hall",
     4, {18.0f, 24.0f, 30.0f, 41.0f, 31.0f, 43.0f}, {40, 40, 45, 45, 35, 35}, {0, 0, 0, 0, 0, 0},
     8.0f, 20.0f, 35.0f, 130.0f, 50.0f, 120.0f, 14000.0f, 85.0f, 25.0f, -2.0f, kEffectShimmer, 55.0f, 12.0f},
    {"Abyss", "an octave down: a dark, rumbling swell",
     5, {20.0f, 28.0f, 37.0f, 47.0f, 61.0f, 43.0f}, {65, 65, 65, 65, 65, 35}, {-35, -35, -35, -35, -35, 0},
     14.0f, 30.0f, 40.0f, 140.0f, 45.0f, 40.0f, 6000.0f, 90.0f, 20.0f, -2.0f, kEffectShimmer, 60.0f, -12.0f},
    {"Bloom", "each hit fades the reverb in: the tail swells after the note",
     4, {16.0f, 22.0f, 29.0f, 38.0f, 31.0f, 43.0f}, {45, 45, 50, 50, 35, 35}, {0, 0, 0, 0, 0, 0},
     5.0f, 0.0f, 40.0f, 130.0f, 40.0f, 100.0f, 12000.0f, 90.0f, 10.0f, 0.0f, kEffectSwell, 70.0f, 12.0f},
    {"Gated 80s", "a big room cut off abruptly after the hit",
     3, {5.0f, 6.5f, 4.2f, 23.0f, 31.0f, 43.0f}, {25, 25, 30, 35, 35, 35}, {0, 0, 0, 0, 0, 0},
     3.0f, 0.0f, 35.0f, 110.0f, 20.0f, 100.0f, 16000.0f, 90.0f, 50.0f, 0.0f, kEffectGate, 45.0f, 12.0f},
    {"Frozen Cloud", "a near-endless wash with a fifth on top: press Freeze to hold it",
     6, {19.0f, 27.0f, 36.0f, 47.0f, 59.0f, 73.0f}, {35, 35, 35, 35, 35, 35}, {0, 0, 0, 0, 0, 0},
     45.0f, 40.0f, 45.0f, 150.0f, 60.0f, 60.0f, 12000.0f, 100.0f, 0.0f, -3.0f, kEffectShimmer, 25.0f, 7.0f},
}};

[[nodiscard]] inline Parameters presetParameters(const int number) noexcept
{
    Parameters p;
    if (number < 1 || number > kPresetCount)
        return p;
    const Preset& s = kPresets[static_cast<size_t>(number - 1)];
    p.dimensions = static_cast<float>(s.dimensions);
    p.distance = s.distance;
    p.damping = s.damping;
    p.filter = s.filter;
    p.decaySeconds = s.decay;
    p.preDelayMs = s.preDelay;
    p.mixPercent = s.mix;
    p.widthPercent = s.width;
    p.modulation = s.modulation;
    p.lowCutHz = s.lowCut;
    p.highCutHz = s.highCut;
    p.diffusion = s.diffusion;
    p.earlyLevel = s.early;
    p.outputDb = s.output;
    p.effect = static_cast<float>(s.effect);
    p.effectAmount = s.effectAmount;
    p.shiftSemitones = s.shift;
    p.freeze = 0.0f;
    p.preset = static_cast<float>(number);
    return p;
}

// A new random space from `seed`, drawn from plausible territory (every value inside its range, decay and
// distances log-spread, filters mostly gentle). Mix, Output, Effect, Effect Amount, Shimmer Shift and Freeze
// are the player's level and effect choices and are carried over from `base`, so a press never makes the
// reverb louder, switches an effect on, or freezes the tail. The result is Custom (preset 0).
[[nodiscard]] inline Parameters randomiseParameters(const Parameters& base, const std::uint32_t seed) noexcept
{
    std::uint32_t state = seed * 2654435761u + 0x9e3779b9u;
    if (state == 0u) state = 0x9e3779b9u;
    const auto next = [&state]() noexcept {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
    };
    const auto range = [&next](const float lo, const float hi) noexcept { return lo + (hi - lo) * next(); };
    const auto logRange = [&next](const float lo, const float hi) noexcept {
        return std::exp(std::log(lo) + (std::log(hi) - std::log(lo)) * next());
    };

    Parameters p = base;
    p.dimensions = std::round(range(2.0f, 6.0f));
    for (int i = 0; i < kMaxDimensions; ++i) {
        const auto k = static_cast<size_t>(i);
        p.distance[k] = std::round(logRange(1.5f, 60.0f) * 10.0f) * 0.1f;
        p.damping[k] = std::round(range(10.0f, 70.0f));
        // Half the walls stay flat; the rest tilt the tone either way by a moderate amount.
        const float tilt = range(-45.0f, 45.0f);
        p.filter[k] = next() < 0.5f ? 0.0f : std::round(tilt);
    }
    p.decaySeconds = std::round(logRange(0.4f, 14.0f) * 10.0f) * 0.1f;
    p.preDelayMs = std::round(range(0.0f, 60.0f));
    p.modulation = std::round(range(10.0f, 60.0f));
    p.diffusion = std::round(range(40.0f, 95.0f));
    p.earlyLevel = std::round(range(10.0f, 70.0f));
    p.widthPercent = std::round(range(80.0f, 130.0f));
    p.lowCutHz = std::round(logRange(40.0f, 200.0f));
    p.highCutHz = std::round(logRange(6000.0f, 18000.0f) / 100.0f) * 100.0f;
    p.preset = 0.0f;
    return clampParameters(p);
}

}  // namespace downspout::heavyverb
