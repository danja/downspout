#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::trajectory {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// Thirteen host-writable parameters followed by one read-only status parameter.
//
// The first six are the Trajectory oscillator's own controls, in the order the
// original Flues experiment lists them (experiments/trajectory in the flues
// repository). They are also the six controller numbers the plugin answers to:
// ParamId 0..5 is CC 1..6, which is the block Drift's lanes emit (CC 1-4 by
// default, CC 5-6 for retargeted lanes). See kControllerMap below.
//
// The envelope is a full ADSR. The Flues original had attack and release only,
// and a reverb; the reverb is not carried over.

enum class ParamId : std::uint32_t {
    // Oscillator: a point bouncing inside a regular polygon
    sides = 0,
    startPosition,
    launchAngle,
    bounceJitter,
    mixX,
    mixY,
    // Envelope
    attack,
    decay,
    sustain,
    release,
    // Voice and output
    velocity,
    level,
    midiCh,
    // Read-only status
    outVoices,
};

inline constexpr std::size_t kParameterCount = 14;
inline constexpr std::size_t kInputParameterCount = 13;
inline constexpr std::size_t kVoiceCount = 8;
inline constexpr int kMinSides = 3;
inline constexpr int kMaxSides = 12;

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
    {"sides", "Sides", "", 3.0f, 12.0f, 6.0f, true, false},
    {"start_position", "Start Position", "deg", 0.0f, 360.0f, 0.0f, false, false},
    {"launch_angle", "Launch Angle", "deg", 0.0f, 360.0f, 45.0f, false, false},
    {"bounce_jitter", "Bounce Jitter", "deg", 0.0f, 10.0f, 0.0f, false, false},
    {"mix_x", "X Level", "", 0.0f, 1.0f, 0.0f, false, false},
    {"mix_y", "Y Level", "", 0.0f, 1.0f, 1.0f, false, false},

    {"attack", "Attack", "ms", 1.0f, 1000.0f, 10.0f, false, false},
    {"decay", "Decay", "ms", 5.0f, 3000.0f, 300.0f, false, false},
    {"sustain", "Sustain", "", 0.0f, 1.0f, 0.8f, false, false},
    {"release", "Release", "ms", 10.0f, 4000.0f, 120.0f, false, false},

    {"velocity", "Velocity", "", 0.0f, 1.0f, 0.5f, false, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.7f, false, false},
    {"midi_ch", "MIDI In", "", 0.0f, 17.0f, 17.0f, true, false},

    {"out_voices", "Active Voices", "", 0.0f, 8.0f, 0.0f, true, true},
}};

// 0 = Off, 1-16 = that MIDI channel only, 17 = all channels.
inline constexpr std::array<const char*, 18> kMidiChannelNames = {{
    "Off", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8",
    "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16", "All",
}};

[[nodiscard]] inline float clampParameter(const std::uint32_t index, const float raw) noexcept
{
    if (index >= kParameterCount)
        return 0.0f;
    const ParamSpec& spec = kParameterSpecs[index];
    const float value = std::isfinite(raw) ? raw : spec.defaultValue;
    const float clamped = std::min(std::max(value, spec.minimum), spec.maximum);
    return spec.integer ? std::round(clamped) : clamped;
}

// ── MIDI note to frequency ─────────────────────────────────────────────────

[[nodiscard]] inline double noteToHertz(const int note) noexcept
{
    return 440.0 * std::exp2((static_cast<double>(note) - 69.0) / 12.0);
}

// ── Fixed MIDI controller map ──────────────────────────────────────────────
//
// Downspout convention: incoming CC writes through to the host parameter so
// automation lanes and the panel stay in agreement. CC 1-6 are the six
// oscillator controls in ParamId order; Drift's four lanes emit CC 1-4 by
// default (plugins/drift/include/drift_core.hpp), so Drift drives Sides, Start
// Position, Launch Angle and Bounce Jitter with no configuration, and a lane
// retargeted to CC 5 or 6 reaches X Level or Y Level. CC 7 is channel volume.

inline constexpr std::uint8_t kCcAllSoundOff = 120;
inline constexpr std::uint8_t kCcAllNotesOff = 123;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 7> kControllerMap = {{
    {1, ParamId::sides, "Sides"},
    {2, ParamId::startPosition, "Start Position"},
    {3, ParamId::launchAngle, "Launch Angle"},
    {4, ParamId::bounceJitter, "Bounce Jitter"},
    {5, ParamId::mixX, "X Level"},
    {6, ParamId::mixY, "Y Level"},
    {7, ParamId::level, "Level"},
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
    const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(target)];
    const float normalized = static_cast<float>(std::min<int>(value, 127)) / 127.0f;
    return clampParameter(static_cast<std::uint32_t>(target),
                          spec.minimum + normalized * (spec.maximum - spec.minimum));
}

}  // namespace downspout::trajectory
