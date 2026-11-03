#pragma once

#include "fresco_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::fresco {

// Parameter indices are stable: never reorder, only append.
enum ParameterId : std::uint32_t {
    kLength = 0, kRange, kContrast, kMinHz, kMaxHz, kLogAxis, kInvert, kSource, kFollow, kTransport, kMix, kOutput,
    kStatusPlayhead, kStatusLevel, kStatusLoaded,
    kParameterCount
};
inline constexpr std::uint32_t kControlCount = kStatusPlayhead;
inline constexpr const char* kStateKeyPicture = "picture";

[[nodiscard]] constexpr bool isOutput(const std::uint32_t index) noexcept { return index >= kStatusPlayhead && index < kParameterCount; }

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool integer;
    bool output;
};

[[nodiscard]] inline const std::array<ParameterSpec, kParameterCount>& specs()
{
    static const Parameters d;
    static const std::array<ParameterSpec, kParameterCount> s = {{
        {"length", "Length", "s", 0.5f, 120.0f, d.lengthS, false, false},
        {"range", "Range", "dB", 20.0f, 100.0f, d.rangeDb, false, false},
        {"contrast", "Contrast", "", 0.25f, 4.0f, d.contrast, false, false},
        {"min_hz", "Bottom", "Hz", 20.0f, 10000.0f, d.minHz, false, false},
        {"max_hz", "Top", "Hz", 200.0f, 20000.0f, d.maxHz, false, false},
        {"log_axis", "Log Axis", "", 0.0f, 1.0f, d.logAxis, true, false},
        {"invert", "Invert", "", 0.0f, 1.0f, d.invert, true, false},
        {"source", "Source", "", 0.0f, static_cast<float>(kSourceCount - 1), d.source, true, false},
        {"follow", "Follow Input", "", 0.0f, 1.0f, d.follow, false, false},
        {"transport", "Follow Host", "", 0.0f, 1.0f, d.transport, true, false},
        {"mix", "Mix", "", 0.0f, 1.0f, d.mix, false, false},
        {"output", "Output", "dB", -24.0f, 12.0f, d.outputDb, false, false},
        {"status_playhead", "Playhead", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_level", "Column Level", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_loaded", "Picture Loaded", "", 0.0f, 1.0f, 0.0f, true, true},
    }};
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kLength:    return p.lengthS;
    case kRange:     return p.rangeDb;
    case kContrast:  return p.contrast;
    case kMinHz:     return p.minHz;
    case kMaxHz:     return p.maxHz;
    case kLogAxis:   return p.logAxis;
    case kInvert:    return p.invert;
    case kSource:    return p.source;
    case kFollow:    return p.follow;
    case kTransport: return p.transport;
    case kMix:       return p.mix;
    case kOutput:    return p.outputDb;
    default:         return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::fresco
