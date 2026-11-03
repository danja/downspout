#pragma once

#include "silken_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::silken {

// Parameter indices are stable: never reorder, only append.
// 0-14 controls, then three output (status) parameters the panel reads.
enum ParameterId : std::uint32_t {
    kHpfOn = 0, kHpf, kPlosiveOn, kPlosiveThr, kEssOn, kEssFreq, kEssThr, kEssRange, kEssMode, kEssListen,
    kWarmth, kPresence, kAir, kSparkle, kOutput,
    kStatusEss, kStatusPlosive, kStatusDetector,
    kParameterCount
};
inline constexpr std::uint32_t kControlCount = kStatusEss;

[[nodiscard]] constexpr bool isOutput(const std::uint32_t index) noexcept { return index >= kStatusEss && index < kParameterCount; }

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
        {"hpf_on", "Rumble Filter", "", 0.0f, 1.0f, d.hpfOn, true, false},
        {"hpf", "Rumble Freq", "Hz", kHpfMin, kHpfMax, d.hpfHz, false, false},
        {"plosive_on", "Plosive Guard", "", 0.0f, 1.0f, d.plosiveOn, true, false},
        {"plosive_thr", "Plosive Threshold", "dB", kPlosiveThrMin, kPlosiveThrMax, d.plosiveThrDb, false, false},
        {"ess_on", "De-esser", "", 0.0f, 1.0f, d.essOn, true, false},
        {"ess_freq", "Ess Frequency", "Hz", kEssFreqMin, kEssFreqMax, d.essFreqHz, false, false},
        {"ess_thr", "Ess Threshold", "dB", kEssThrMin, kEssThrMax, d.essThrDb, false, false},
        {"ess_range", "Ess Range", "dB", 0.0f, kEssRangeMax, d.essRangeDb, false, false},
        {"ess_mode", "Ess Mode", "", 0.0f, 1.0f, d.essMode, true, false},
        {"ess_listen", "Ess Listen", "", 0.0f, 1.0f, d.essListen, true, false},
        {"warmth", "Warmth", "dB", -kToneMaxDb, kToneMaxDb, d.warmthDb, false, false},
        {"presence", "Presence", "dB", -kToneMaxDb, kToneMaxDb, d.presenceDb, false, false},
        {"air", "Air", "dB", -kToneMaxDb, kToneMaxDb, d.airDb, false, false},
        {"sparkle", "Sparkle", "", 0.0f, 1.0f, d.sparkle, false, false},
        {"output", "Output", "dB", kOutputMinDb, kOutputMaxDb, d.outputDb, false, false},
        {"status_ess", "De-ess Reduction", "dB", 0.0f, kEssRangeMax, 0.0f, false, true},
        {"status_plosive", "Plosive Reduction", "dB", 0.0f, kEssRangeMax, 0.0f, false, true},
        {"status_detector", "Ess Detector", "dB", -60.0f, 0.0f, -60.0f, false, true},
    }};
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kHpfOn:       return p.hpfOn;
    case kHpf:         return p.hpfHz;
    case kPlosiveOn:   return p.plosiveOn;
    case kPlosiveThr:  return p.plosiveThrDb;
    case kEssOn:       return p.essOn;
    case kEssFreq:     return p.essFreqHz;
    case kEssThr:      return p.essThrDb;
    case kEssRange:    return p.essRangeDb;
    case kEssMode:     return p.essMode;
    case kEssListen:   return p.essListen;
    case kWarmth:      return p.warmthDb;
    case kPresence:    return p.presenceDb;
    case kAir:         return p.airDb;
    case kSparkle:     return p.sparkle;
    case kOutput:      return p.outputDb;
    default:           return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::silken
