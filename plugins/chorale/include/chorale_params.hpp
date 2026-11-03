#pragma once

#include "chorale_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::chorale {

// Parameter indices are stable: never reorder, only append.
// 0-9 controls, then eight output (status) parameters the panel reads.
enum ParameterId : std::uint32_t {
    kMode = 0, kTone, kMix, kRate, kDepth, kDelay, kVoices, kWidth, kInput, kCcChannel,
    kStatusTap1, kStatusTap2, kStatusTap3, kStatusTap4, kStatusRate, kStatusDepth, kStatusWidth, kStatusMix,
    kParameterCount
};
inline constexpr std::uint32_t kControlCount = kStatusTap1;

[[nodiscard]] constexpr bool isOutput(const std::uint32_t index) noexcept { return index >= kStatusTap1 && index < kParameterCount; }

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
        {"mode", "Mode", "", 0.0f, kModeCount - 1.0f, d.mode, true, false},
        {"tone", "Tone", "", 0.0f, 1.0f, d.tone, false, false},
        {"mix", "Mix", "", 0.0f, 1.0f, d.mix, false, false},
        {"rate", "Rate", "Hz", kRateMin, kRateMax, d.rate, false, false},
        {"depth", "Depth", "ms", kDepthMin, kDepthMax, d.depth, false, false},
        {"delay", "Delay", "ms", kDelayMin, kDelayMax, d.delay, false, false},
        {"voices", "Voices", "", 1.0f, static_cast<float>(kMaxVoices), d.voices, true, false},
        {"width", "Width", "", 0.0f, 1.0f, d.width, false, false},
        {"input", "Input", "", 0.0f, 1.0f, d.input, true, false},
        {"cc_channel", "CC Channel", "", 0.0f, 16.0f, d.ccChannel, true, false},
        {"status_tap1", "Voice 1 Sweep", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_tap2", "Voice 2 Sweep", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_tap3", "Voice 3 Sweep", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_tap4", "Voice 4 Sweep", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_rate", "Rate Now", "Hz", kRateMin, kRateMax, d.rate, false, true},
        {"status_depth", "Depth Now", "ms", kDepthMin, kDepthMax, d.depth, false, true},
        {"status_width", "Width Now", "", 0.0f, 1.0f, d.width, false, true},
        {"status_mix", "Mix Now", "", 0.0f, 1.0f, d.mix, false, true},
    }};
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kMode:      return p.mode;
    case kTone:      return p.tone;
    case kMix:       return p.mix;
    case kRate:      return p.rate;
    case kDepth:     return p.depth;
    case kDelay:     return p.delay;
    case kVoices:    return p.voices;
    case kWidth:     return p.width;
    case kInput:     return p.input;
    case kCcChannel: return p.ccChannel;
    default:         return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

// The controller a control hands back to when the panel or host moves it (-1: none).
[[nodiscard]] constexpr int ccTargetOf(const std::uint32_t index) noexcept
{
    return index == kRate ? kCcRate : index == kDepth ? kCcDepth : index == kWidth ? kCcWidth : index == kMix ? kCcMix : -1;
}

}  // namespace downspout::chorale
