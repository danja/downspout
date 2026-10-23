#pragma once

#include "glue_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::glue {

// Parameter indices are stable: never reorder, only append.
enum ParameterIndex : std::uint32_t {
    kThreshold = 0,
    kRatio,
    kKnee,
    kAttack,
    kRelease,
    kAutoRelease,
    kScHpf,
    kDetector,
    kRange,
    kMakeup,
    kSoftClip,
    kMix,
    kControlCount,
    kStatusReduction = kControlCount,  // output: gain reduction in dB, 0 or positive
    kStatusLevel,                      // output: detector level in dB
    kSidechain,                        // appended after the outputs: parameter indices never move
    kCcChannel,
    kLookahead,
    kParameterCount
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
    bool output;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"threshold",    "Threshold",         "dB",  -60.0f,   0.0f, -18.0f, false, false, false},
    {"ratio",        "Ratio",             ":1",    1.1f,  20.0f,   4.0f, true,  false, false},
    {"knee",         "Knee",              "dB",    0.0f,  24.0f,   6.0f, false, false, false},
    {"attack",       "Attack",            "ms",    0.1f, 100.0f,  10.0f, true,  false, false},
    {"release",      "Release",           "ms",   20.0f,2000.0f, 200.0f, true,  false, false},
    {"auto_release", "Auto Release",      "",      0.0f,   1.0f,   0.0f, false, true,  false},
    {"sc_hpf",       "Sidechain HPF",     "Hz",   20.0f, 400.0f,  60.0f, true,  false, false},
    {"detector",     "Detector",          "",      0.0f,   1.0f,   0.0f, false, true,  false},
    {"range",        "Range",             "dB",    1.0f,  40.0f,  40.0f, false, false, false},
    {"makeup",       "Makeup",            "dB",    0.0f,  24.0f,   0.0f, false, false, false},
    {"soft_clip",    "Soft Clip",         "",      0.0f,   1.0f,   0.0f, false, true,  false},
    {"mix",          "Mix",               "%",     0.0f, 100.0f, 100.0f, false, false, false},
    {"status_reduction", "Gain Reduction","dB",    0.0f,  40.0f,   0.0f, false, false, true},
    {"status_level", "Detector Level",    "dB", -120.0f,  12.0f,-120.0f, false, false, true},
    {"sidechain",    "Sidechain",         "",      0.0f,   1.0f,   0.0f, false, true,  false},
    {"cc_channel",   "CC Channel",        "",      0.0f,  16.0f,   1.0f, false, true,  false},
    {"lookahead",    "Look-ahead",        "ms",    0.0f,  10.0f,   0.0f, false, false, false},
}};

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kThreshold:   return p.thresholdDb;
    case kRatio:       return p.ratio;
    case kKnee:        return p.kneeDb;
    case kAttack:      return p.attackMs;
    case kRelease:     return p.releaseMs;
    case kAutoRelease: return p.autoRelease;
    case kScHpf:       return p.scHpfHz;
    case kDetector:    return p.rms;
    case kRange:       return p.rangeDb;
    case kMakeup:      return p.makeupDb;
    case kSoftClip:    return p.softClip;
    case kMix:         return p.mix;
    case kSidechain:   return p.sidechain;
    case kCcChannel:   return p.ccChannel;
    case kLookahead:   return p.lookaheadMs;
    default:           return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

// The control a Drift CC overrides, or -1: the panel moving this control releases its override.
[[nodiscard]] constexpr int liveSlot(const std::uint32_t index) noexcept
{
    return index == kThreshold ? 0 : index == kRatio ? 1 : index == kMakeup ? 2 : index == kMix ? 3 : -1;
}

}  // namespace downspout::glue
