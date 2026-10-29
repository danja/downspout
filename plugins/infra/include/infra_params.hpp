#pragma once

#include "infra_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::infra {

// Parameter indices are stable: never reorder, only append.
// 0-4 controls, then four output (status) parameters the panel reads.
enum ParameterId : std::uint32_t {
    kWaveform = 0, kMultiple, kPhaseL, kPhaseR, kRange,
    kStatusRate, kStatusMultiple, kStatusLfoL, kStatusLfoR,
    kParameterCount
};
inline constexpr std::uint32_t kControlCount = kStatusRate;

[[nodiscard]] constexpr bool isOutput(const std::uint32_t index) noexcept { return index >= kStatusRate && index < kParameterCount; }

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
        {"waveform", "Waveform", "", 0.0f, static_cast<float>(kWaveformCount - 1), d.waveform, true, false},
        {"multiple", "Multiple", "", 0.0f, static_cast<float>(kMaxMultiple), d.multiple, true, false},
        {"phase_l", "Phase Left", "", 0.0f, 3.0f, d.phaseL, true, false},
        {"phase_r", "Phase Right", "", 0.0f, 3.0f, d.phaseR, true, false},
        {"range", "Range", "dB", 0.0f, 40.0f, d.rangeDb, false, false},
        {"status_rate", "LFO Rate", "Hz", 0.0f, 20.0f, 0.0f, false, true},
        {"status_multiple", "Multiple In Use", "", 0.0f, static_cast<float>(kMaxMultiple), 0.0f, true, true},
        {"status_lfo_l", "LFO Left", "", 0.0f, 1.0f, 1.0f, false, true},
        {"status_lfo_r", "LFO Right", "", 0.0f, 1.0f, 1.0f, false, true},
    }};
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kWaveform: return p.waveform;
    case kMultiple: return p.multiple;
    case kPhaseL:   return p.phaseL;
    case kPhaseR:   return p.phaseR;
    case kRange:    return p.rangeDb;
    default:        return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::infra
