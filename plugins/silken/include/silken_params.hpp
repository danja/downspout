#pragma once

#include "silken_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::silken {

// Parameter indices are stable: never reorder, only append.
// 0-14 controls, 15-17 output (status) parameters the panel reads, then controls and outputs added later (so saved projects keep their meaning).
enum ParameterId : std::uint32_t {
    kHpfOn = 0, kHpf, kPlosiveOn, kPlosiveThr, kEssOn, kEssFreq, kEssThr, kEssRange, kEssMode, kEssListen,
    kWarmth, kPresence, kAir, kSparkle, kOutput,
    kStatusEss, kStatusPlosive, kStatusDetector,
    kGateOn, kGateThr, kGateRange, kTameFreq, kTameThr, kTameAmount, kLevelOn, kLevelThr, kLevelRatio, kLevelMix, kMud, kDrive,
    kStatusGate, kStatusLevel,
    kParameterCount
};
inline constexpr std::uint32_t kOutputCount = 5;

[[nodiscard]] constexpr bool isOutput(const std::uint32_t index) noexcept
{
    return (index >= kStatusEss && index <= kStatusDetector) || index == kStatusGate || index == kStatusLevel;
}

// 0..4: the position of an output among the outputs (the order the plugin keeps its meters in).
[[nodiscard]] constexpr std::uint32_t outputSlot(const std::uint32_t index) noexcept
{
    return index <= kStatusDetector ? index - kStatusEss : index == kStatusGate ? 3u : 4u;
}

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
        {"gate_on", "Gate", "", 0.0f, 1.0f, d.gateOn, true, false},
        {"gate_thr", "Gate Threshold", "dB", kGateThrMin, kGateThrMax, d.gateThrDb, false, false},
        {"gate_range", "Gate Range", "dB", 0.0f, kGateRangeMax, d.gateRangeDb, false, false},
        {"tame_freq", "Harsh Frequency", "Hz", kTameFreqMin, kTameFreqMax, d.tameFreqHz, false, false},
        {"tame_thr", "Harsh Threshold", "dB", kTameThrMin, kTameThrMax, d.tameThrDb, false, false},
        {"tame_amount", "Harsh Amount", "dB", 0.0f, kTameAmountMax, d.tameAmountDb, false, false},
        {"level_on", "Leveller", "", 0.0f, 1.0f, d.levelOn, true, false},
        {"level_thr", "Leveller Threshold", "dB", kLevelThrMin, kLevelThrMax, d.levelThrDb, false, false},
        {"level_ratio", "Leveller Ratio", "", kLevelRatioMin, kLevelRatioMax, d.levelRatio, false, false},
        {"level_mix", "Leveller Mix", "", 0.0f, 1.0f, d.levelMix, false, false},
        {"mud", "Mud", "dB", -kToneMaxDb, kToneMaxDb, d.mudDb, false, false},
        {"drive", "Drive", "", 0.0f, 1.0f, d.drive, false, false},
        {"status_gate", "Gate Reduction", "dB", 0.0f, kGateRangeMax, 0.0f, false, true},
        {"status_level", "Leveller Reduction", "dB", 0.0f, kLevelMaxDb, 0.0f, false, true},
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
    case kGateOn:      return p.gateOn;
    case kGateThr:     return p.gateThrDb;
    case kGateRange:   return p.gateRangeDb;
    case kTameFreq:    return p.tameFreqHz;
    case kTameThr:     return p.tameThrDb;
    case kTameAmount:  return p.tameAmountDb;
    case kLevelOn:     return p.levelOn;
    case kLevelThr:    return p.levelThrDb;
    case kLevelRatio:  return p.levelRatio;
    case kLevelMix:    return p.levelMix;
    case kMud:         return p.mudDb;
    case kDrive:       return p.drive;
    default:           return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::silken
