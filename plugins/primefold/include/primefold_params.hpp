#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::primefold {

// ── Parameters ─────────────────────────────────────────────────────────────

enum class ParamId : std::uint32_t {
    dry = 0,
    level2,
    level3,
    level5,
    feedback,
    damp,
    grain,
    mix,
    width,
    level,
    // Read-only status
    outLatency,
    outClip,
};

inline constexpr std::size_t kParameterCount = 12;
inline constexpr std::size_t kInputParameterCount = 10;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"dry", "Dry", "", 0.0f, 1.0f, 0.60f, false, false},
    {"lvl2", "2x", "", 0.0f, 1.0f, 0.70f, false, false},
    {"lvl3", "3x", "", 0.0f, 1.0f, 0.50f, false, false},
    {"lvl5", "5x", "", 0.0f, 1.0f, 0.40f, false, false},
    {"feedback", "Feedback", "", 0.0f, 0.90f, 0.35f, false, false},
    {"damp", "Damp", "", 0.0f, 1.0f, 0.50f, false, false},
    {"grain", "Grain", "", 0.0f, 2.0f, 1.0f, true, false},
    {"mix", "Mix", "", 0.0f, 1.0f, 0.60f, false, false},
    {"width", "Width", "", 0.0f, 1.0f, 0.60f, false, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.75f, false, false},
    {"out_latency", "Latency", "spl", 0.0f, 2048.0f, 1024.0f, false, true},
    {"out_clip", "Clip Lamp", "", 0.0f, 1.0f, 0.0f, false, true},
}};

inline constexpr std::array<const char*, 3> kGrainNames = {{
    "512", "1024", "2048",
}};

inline constexpr std::array<int, 3> kGrainSamples = {{512, 1024, 2048}};

// ── Fixed MIDI controller map ──────────────────────────────────────────────

inline constexpr std::uint8_t kCcFeedback = 1;
inline constexpr std::uint8_t kCcMix = 2;
inline constexpr std::uint8_t kCcLevel = 7;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
};

inline constexpr std::array<ControllerMapping, 3> kControllerMap = {{
    {kCcFeedback, ParamId::feedback},
    {kCcMix, ParamId::mix},
    {kCcLevel, ParamId::level},
}};

[[nodiscard]] inline bool controllerTarget(std::uint8_t controller, ParamId& target) noexcept
{
    for (const auto& m : kControllerMap)
        if (m.controller == controller) { target = m.target; return true; }
    return false;
}

[[nodiscard]] inline float controllerToParameter(ParamId target, std::uint8_t value) noexcept
{
    const auto& spec = kParameterSpecs[static_cast<std::size_t>(target)];
    return spec.minimum + (static_cast<float>(value) / 127.0f) * (spec.maximum - spec.minimum);
}

}  // namespace downspout::primefold
