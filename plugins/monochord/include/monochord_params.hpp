#pragma once

#include "monochord_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::monochord {

// Parameter indices are stable: never reorder, only append.
enum ParameterIndex : std::uint32_t {
    kExciter = 0, kPosition, kHardness, kBowPressure, kDecay, kDamping, kStiffness,
    kBodySize, kBodyLevel, kPickup, kRelease, kMaster, kVelocity, kBendRange,
    kParameterCount
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"exciter",       "Exciter",       "",   0.0f,  2.0f,   0.0f,   false, true },
    {"position",      "Position",      "",   0.05f, 0.5f,   0.2f,   false, false},
    {"hardness",      "Hardness",      "",   0.0f,  1.0f,   0.6f,   false, false},
    {"bow_pressure",  "Bow Pressure",  "",   0.0f,  1.0f,   0.5f,   false, false},
    {"decay",         "Decay",         "s",  0.1f,  20.0f,  3.0f,   true,  false},
    {"damping",       "Damping",       "",   0.0f,  1.0f,   0.3f,   false, false},
    {"stiffness",     "Stiffness",     "",   0.0f,  1.0f,   0.5f,   false, false},
    {"body_size",     "Body Size",     "",   0.5f,  2.0f,   1.0f,   true,  false},
    {"body_level",    "Body Level",    "",   0.0f,  1.0f,   0.3f,   false, false},
    {"pickup",        "Pickup",        "",   0.0f,  0.5f,   0.0f,   false, false},
    {"release",       "Release",       "ms", 5.0f,  2000.0f,250.0f, true,  false},
    {"master",        "Master",        "",   0.0f,  1.0f,   0.6f,   false, false},
    {"velocity",      "Velocity",      "",   0.0f,  1.0f,   0.7f,   false, false},
    {"bend_range",    "Bend Range",    "st", 0.0f,  24.0f,  2.0f,   false, false},
}};

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kExciter:     return p.exciter;
    case kPosition:    return p.position;
    case kHardness:    return p.hardness;
    case kBowPressure: return p.bowPressure;
    case kDecay:       return p.decaySec;
    case kDamping:     return p.damping;
    case kStiffness:   return p.stiffness;
    case kBodySize:    return p.bodySize;
    case kBodyLevel:   return p.bodyLevel;
    case kPickup:      return p.pickup;
    case kRelease:     return p.releaseMs;
    case kMaster:      return p.master;
    case kVelocity:    return p.velocity;
    case kBendRange:   return p.bendRange;
    default:           return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::monochord
