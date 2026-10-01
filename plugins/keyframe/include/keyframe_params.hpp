#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::keyframe {

// ── Parameters ───────────────────────────────────────────────────────────────
//
// Nine host-writable parameters followed by four read-only status parameters
// the panel uses to show the engine's live state: extrema density, the
// keyframe distance between the two playheads, the splice lamp and the
// reported latency. See docs/design.md.

enum class ParamId : std::uint32_t {
    // Transport
    time = 0,
    pitch,
    splice,
    threshold,
    maxSplice,
    hold,
    // Output
    mix,
    width,
    level,
    // Read-only status
    outDensity,
    outDrift,
    outSplice,
    outLatency,
};

inline constexpr std::size_t kParameterCount = 13;
inline constexpr std::size_t kInputParameterCount = 9;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    // Time is a rate, not a stretch factor: 1.0 is unity, 0.5 is half speed.
    // It cannot exceed 1.0 because a live input stream cannot be consumed
    // faster than it arrives; see docs/design.md.
    {"time", "Time", "x", 0.25f, 1.00f, 1.00f, false, false},
    {"pitch", "Pitch", "x", 0.25f, 4.00f, 1.00f, false, false},
    {"splice", "Splice", "kf", 2.0f, 64.0f, 16.0f, true, false},
    {"threshold", "Threshold", "dB", -96.0f, -24.0f, -60.0f, false, false},
    {"max_splice", "Max Splice", "ms", 20.0f, 500.0f, 250.0f, false, false},
    {"hold", "Hold", "", 0.0f, 1.0f, 0.0f, true, false},
    {"mix", "Mix", "", 0.0f, 1.0f, 0.60f, false, false},
    {"width", "Width", "", 0.0f, 1.0f, 0.60f, false, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.75f, false, false},
    {"out_density", "Keyframes", "kf/s", 0.0f, 24000.0f, 0.0f, false, true},
    {"out_drift", "Playhead Drift", "kf", -64.0f, 64.0f, 0.0f, false, true},
    {"out_splice", "Splice Lamp", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_latency", "Latency", "spl", 0.0f, 65536.0f, 8192.0f, false, true},
}};

// ── Fixed MIDI controller map ────────────────────────────────────────────────
//
// Downspout convention: an incoming CC writes through to the host parameter so
// automation lanes and the panel stay in agreement. CC 1 and CC 2 are the two
// rates that define the effect; CC 7 keeps its conventional volume meaning.

inline constexpr std::uint8_t kCcTime = 1;
inline constexpr std::uint8_t kCcPitch = 2;
inline constexpr std::uint8_t kCcLevel = 7;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 3> kControllerMap = {{
    {kCcTime, ParamId::time, "Time"},
    {kCcPitch, ParamId::pitch, "Pitch"},
    {kCcLevel, ParamId::level, "Output"},
}};

[[nodiscard]] inline bool controllerTarget(const std::uint8_t controller, ParamId& target) noexcept
{
    for (const ControllerMapping& mapping : kControllerMap)
    {
        if (mapping.controller == controller)
        {
            target = mapping.target;
            return true;
        }
    }
    return false;
}

// Spread a 0-127 controller value across the target parameter's full range.
[[nodiscard]] inline float controllerToParameter(const ParamId target, const std::uint8_t value) noexcept
{
    const auto& spec = kParameterSpecs[static_cast<std::size_t>(target)];
    const float normalized = static_cast<float>(value) / 127.0f;
    return spec.minimum + normalized * (spec.maximum - spec.minimum);
}

}  // namespace downspout::keyframe