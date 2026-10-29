#pragma once

#include "heavyverb_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::heavyverb {

// Parameter indices are stable: never reorder, only append.
enum ParameterIndex : std::uint32_t {
    kDimensions = 0,
    kDistance1, kDistance2, kDistance3, kDistance4, kDistance5, kDistance6,
    kDamping1, kDamping2, kDamping3, kDamping4, kDamping5, kDamping6,
    kFilter1, kFilter2, kFilter3, kFilter4, kFilter5, kFilter6,
    kDecay,
    kPreDelay,
    kMix,
    kWidth,
    kModulation,
    kLowCut,
    kHighCut,
    kDiffusion,
    kEarly,
    kOutput,
    kEffect,
    kEffectAmount,
    kShift,
    kFreeze,
    kPreset,
    kParameterCount
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
    bool boolean;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"dimensions", "Dimensions",   "",    1.0f,   6.0f,    3.0f, false, true,  false},
    {"distance_1", "Distance 1",   "m",   0.5f, 100.0f,    7.0f, true,  false, false},
    {"distance_2", "Distance 2",   "m",   0.5f, 100.0f,   11.0f, true,  false, false},
    {"distance_3", "Distance 3",   "m",   0.5f, 100.0f,   17.0f, true,  false, false},
    {"distance_4", "Distance 4",   "m",   0.5f, 100.0f,   23.0f, true,  false, false},
    {"distance_5", "Distance 5",   "m",   0.5f, 100.0f,   31.0f, true,  false, false},
    {"distance_6", "Distance 6",   "m",   0.5f, 100.0f,   43.0f, true,  false, false},
    {"damping_1",  "Damping 1",    "%",   0.0f, 100.0f,   35.0f, false, false, false},
    {"damping_2",  "Damping 2",    "%",   0.0f, 100.0f,   35.0f, false, false, false},
    {"damping_3",  "Damping 3",    "%",   0.0f, 100.0f,   35.0f, false, false, false},
    {"damping_4",  "Damping 4",    "%",   0.0f, 100.0f,   35.0f, false, false, false},
    {"damping_5",  "Damping 5",    "%",   0.0f, 100.0f,   35.0f, false, false, false},
    {"damping_6",  "Damping 6",    "%",   0.0f, 100.0f,   35.0f, false, false, false},
    {"filter_1",   "Filter 1",     "%", -100.0f, 100.0f,    0.0f, false, false, false},
    {"filter_2",   "Filter 2",     "%", -100.0f, 100.0f,    0.0f, false, false, false},
    {"filter_3",   "Filter 3",     "%", -100.0f, 100.0f,    0.0f, false, false, false},
    {"filter_4",   "Filter 4",     "%", -100.0f, 100.0f,    0.0f, false, false, false},
    {"filter_5",   "Filter 5",     "%", -100.0f, 100.0f,    0.0f, false, false, false},
    {"filter_6",   "Filter 6",     "%", -100.0f, 100.0f,    0.0f, false, false, false},
    {"decay",      "Decay",        "s",   0.1f,  60.0f,    2.5f, true,  false, false},
    {"pre_delay",  "Pre-delay",    "ms",  0.0f, 250.0f,   12.0f, false, false, false},
    {"mix",        "Mix",          "%",   0.0f, 100.0f,   25.0f, false, false, false},
    {"width",      "Width",        "%",   0.0f, 150.0f,  100.0f, false, false, false},
    {"modulation", "Modulation",   "%",   0.0f, 100.0f,   35.0f, false, false, false},
    {"low_cut",    "Low Cut",      "Hz", 20.0f, 500.0f,   60.0f, true,  false, false},
    {"high_cut",   "High Cut",     "Hz", 1000.0f, 20000.0f, 14000.0f, true, false, false},
    {"diffusion",  "Diffusion",    "%",   0.0f, 100.0f,   70.0f, false, false, false},
    {"early",      "Early Level",  "%",   0.0f, 100.0f,   40.0f, false, false, false},
    {"output",     "Output",       "dB", -24.0f, 12.0f,    0.0f, false, false, false},
    {"effect",     "Effect",       "",    0.0f,   4.0f,    0.0f, false, true,  false},
    {"effect_amount", "Effect Amount", "%", 0.0f, 100.0f, 50.0f, false, false, false},
    {"shift",      "Shimmer Shift", "st", -12.0f, 24.0f,  12.0f, false, true,  false},
    {"freeze",     "Freeze",       "",    0.0f,   1.0f,    0.0f, false, true,  true },
    {"preset",     "Preset",       "",    0.0f,  15.0f,    0.0f, false, true,  false},
}};

// The defaults here must equal the core's Parameters{} (the tests check it).
inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    if (index >= kDistance1 && index <= kDistance6) return p.distance[index - kDistance1];
    if (index >= kDamping1 && index <= kDamping6)   return p.damping[index - kDamping1];
    if (index >= kFilter1 && index <= kFilter6)     return p.filter[index - kFilter1];
    switch (index) {
    case kDimensions:   return p.dimensions;
    case kDecay:        return p.decaySeconds;
    case kPreDelay:     return p.preDelayMs;
    case kMix:          return p.mixPercent;
    case kWidth:        return p.widthPercent;
    case kModulation:   return p.modulation;
    case kLowCut:       return p.lowCutHz;
    case kHighCut:      return p.highCutHz;
    case kDiffusion:    return p.diffusion;
    case kEarly:        return p.earlyLevel;
    case kOutput:       return p.outputDb;
    case kEffect:       return p.effect;
    case kEffectAmount: return p.effectAmount;
    case kShift:        return p.shiftSemitones;
    case kFreeze:       return p.freeze;
    default:            return p.preset;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::heavyverb
