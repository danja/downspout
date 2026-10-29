#pragma once

#include "spense_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::spense {

// Parameter indices are stable: never reorder, only append.
enum ParameterIndex : std::uint32_t {
    kStack = 0,
    kFrequency,
    kResonance,
    kDrift,
    kPreGain,
    kPostGain,
    kClip,
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
    {"stack",     "Stack",       "",    1.0f,    8.0f,    1.0f, false, true,  false},
    {"frequency", "Frequency",   "Hz",  40.0f,   20000.0f, 1000.0f, true, false, false},
    {"resonance", "Resonance",   "Q",   0.1f,    10.0f,   0.707f, true, false, false},
    {"drift",     "Drift",       "",    0.0f,    1.0f,    0.0f, false, false, false},
    {"pre_gain",  "Pre Gain",    "dB", -100.0f,  12.0f,   0.0f, false, false, false},
    {"post_gain", "Post Gain",   "dB", -100.0f,  12.0f,   0.0f, false, false, false},
    {"clip",      "Clip at 0 dB", "",   0.0f,    1.0f,    0.0f, false, true,  true },
}};

// The defaults here must equal the core's Parameters{} (the tests check it).
inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    switch (index) {
    case kStack:     return p.stack;
    case kFrequency: return p.frequencyHz;
    case kResonance: return p.resonance;
    case kDrift:     return p.drift;
    case kPreGain:   return p.preGainDb;
    case kPostGain:  return p.postGainDb;
    default:         return p.clip;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::spense
