#pragma once

#include "ladder_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::ladder {

// Parameter indices are stable: never reorder, only append.
enum ParameterIndex : std::uint32_t {
    kCutoff = 0,
    kResonance,
    kDrive,
    kBassComp,
    kMix,
    kOutput,
    kParameterCount
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"cutoff",     "Cutoff",         "Hz", 20.0f, 18000.0f, 1000.0f, true },
    {"resonance",  "Resonance",      "%",   0.0f,   100.0f,   20.0f, false},
    {"drive",      "Drive",          "dB",  0.0f,    24.0f,    0.0f, false},
    {"bass_comp",  "Bass Comp",      "%",   0.0f,   100.0f,   50.0f, false},
    {"mix",        "Mix",            "%",   0.0f,   100.0f,  100.0f, false},
    {"output",     "Output",         "dB", -24.0f,   12.0f,    0.0f, false},
}};

// The defaults here must equal the core's Parameters{} (the tests check it).
inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    switch (index) {
    case kCutoff:    return p.cutoffHz;
    case kResonance: return p.resonance;
    case kDrive:     return p.driveDb;
    case kBassComp:  return p.bassComp;
    case kMix:       return p.mix;
    default:         return p.outputDb;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::ladder
