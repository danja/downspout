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
    kCcChannel,
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
    {"cutoff",     "Cutoff",         "Hz", 20.0f, 18000.0f, 1000.0f, true, false},
    {"resonance",  "Resonance",      "%",   0.0f,   100.0f,   20.0f, false, false},
    {"drive",      "Drive",          "dB",  0.0f,    24.0f,    0.0f, false, false},
    {"bass_comp",  "Bass Comp",      "%",   0.0f,   100.0f,   50.0f, false, false},
    {"mix",        "Mix",            "%",   0.0f,   100.0f,  100.0f, false, false},
    {"output",     "Output",         "dB", -24.0f,   12.0f,    0.0f, false, false},
    {"cc_channel", "CC Channel",     "",     0.0f,   16.0f,    1.0f, false, true },
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
    case kOutput:    return p.outputDb;
    default:         return p.ccChannel;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

// The control a Drift CC overrides, or -1: the panel moving this control releases its override.
[[nodiscard]] constexpr int liveSlot(const std::uint32_t index) noexcept
{
    return index == kCutoff ? 0 : index == kResonance ? 1 : index == kDrive ? 2 : index == kMix ? 3 : -1;
}

}  // namespace downspout::ladder
