#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace downspout::box {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// Box is a stereo audio effect: the input excites a wooden enclosure and the
// output is the sound the wood radiates. It models the *structure*, not the
// strings. The portable engine derives a bank of rectangular-plate modes from
// geometry and material properties, adds the Helmholtz resonance of a soundhole
// and two low body-cavity modes, then sums them with positions derived from the
// excitation and microphone XY pads.
//
// Every physical property has a deliberately wide range: a 0.05x body in spruce
// and a 4x body in a stiff composite are both possible, and either can be driven
// far past a realistic string force for effect.
//
// Parameters 0-20 are host-writable; 21-25 are read-only processor status.
// Randomise is a UI action that commits ordinary parameter values (as in
// HeavyVerb); its slot is a read-only action count, not a host trigger.

enum class ParamId : std::uint32_t {
    // Structure
    body = 0,
    size,
    thickness,
    stiffness,
    density,
    damping,
    hole,
    volume,
    depth,
    // Excitation and radiation
    drive,
    nonlinear,
    excX,
    excY,
    micX,
    micY,
    width,
    mix,
    output,
    // Integration
    bypass,
    ccChannel,
    seed,
    randomise,
    // Read-only status
    outBodyHz,
    outAirHz,
    outDecay,
    outQ,
};

inline constexpr std::size_t kParameterCount = 26;
inline constexpr std::size_t kWritableParameterCount = 21;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
    bool trigger = false;
    bool logarithmic = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"body", "Body", "", 0.0f, 3.0f, 0.0f, true, false, false, false},
    {"size", "Body Size", "x", 0.05f, 4.0f, 1.0f, false, false, false, true},
    {"thickness", "Thickness", "mm", 0.5f, 40.0f, 3.0f, false, false, false, true},
    {"stiffness", "Stiffness", "GPa", 0.5f, 40.0f, 11.0f, false, false, false, true},
    {"density", "Density", "kg/m3", 100.0f, 1500.0f, 450.0f, false, false, false, true},
    {"damping", "Damping", "x", 0.02f, 8.0f, 1.0f, false, false, false, true},
    {"hole", "Soundhole", "mm", 0.0f, 300.0f, 90.0f, false, false, false, false},
    {"volume", "Air Volume", "L", 1.0f, 2000.0f, 15.0f, false, false, false, true},
    {"depth", "Body Depth", "mm", 20.0f, 500.0f, 100.0f, false, false, false, true},

    {"drive", "Drive", "dB", 0.0f, 36.0f, 6.0f, false, false, false, false},
    {"nonlinear", "Wood Nonlinearity", "%", 0.0f, 100.0f, 15.0f, false, false, false, false},
    {"exc_x", "Excitation X", "%", 0.0f, 100.0f, 50.0f, false, false, false, false},
    {"exc_y", "Excitation Y", "%", 0.0f, 100.0f, 35.0f, false, false, false, false},
    {"mic_x", "Mic X", "%", 0.0f, 100.0f, 50.0f, false, false, false, false},
    {"mic_y", "Mic Y", "%", 0.0f, 100.0f, 50.0f, false, false, false, false},
    {"width", "Stereo Width", "%", 0.0f, 100.0f, 30.0f, false, false, false, false},
    {"mix", "Mix", "%", 0.0f, 100.0f, 35.0f, false, false, false, false},
    {"output", "Output", "dB", -24.0f, 24.0f, 0.0f, false, false, false, false},

    {"bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, true, false, false, false},
    {"cc_channel", "CC Channel", "", 1.0f, 16.0f, 1.0f, true, false, false, false},
    {"seed", "Seed", "", 1.0f, 9999.0f, 1.0f, true, false, false, false},
    {"randomise_count", "Randomise Count", "", 0.0f, 9999.0f, 0.0f, true, true, false, false},

    {"out_body_hz", "Body Mode", "Hz", 10.0f, 20000.0f, 0.0f, false, true, false, false},
    {"out_air_hz", "Air Mode", "Hz", 0.0f, 20000.0f, 0.0f, false, true, false, false},
    {"out_decay", "Decay", "s", 0.0f, 20.0f, 0.0f, false, true, false, false},
    {"out_q", "Mode Q", "", 0.0f, 100.0f, 0.0f, false, true, false, false},
}};

inline constexpr std::array<const char*, 4> kBodyNames = {
    "Guitar", "Piano", "Cabinet", "Free Plate",
};

// ── Fixed MIDI controller map ──────────────────────────────────────────────
//
// CC 1-4 are Drift's four default lanes, so they drive the four controls that
// change the sound most: the body's size, its damping, the soundhole (the air
// resonance) and the drive into the wood.

inline constexpr std::uint8_t kCcSize = 1;
inline constexpr std::uint8_t kCcDamping = 2;
inline constexpr std::uint8_t kCcHole = 3;
inline constexpr std::uint8_t kCcDrive = 4;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 4> kControllerMap = {{
    {kCcSize, ParamId::size, "Body Size"},
    {kCcDamping, ParamId::damping, "Damping"},
    {kCcHole, ParamId::hole, "Soundhole"},
    {kCcDrive, ParamId::drive, "Drive"},
}};

[[nodiscard]] constexpr std::uint32_t index(const ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

// CC 1-4 map to these four parameters, nothing else.
[[nodiscard]] constexpr std::optional<ParamId> controllerTarget(const std::uint8_t controller) noexcept
{
    for (const auto& mapping : kControllerMap) {
        if (mapping.controller == controller)
            return mapping.target;
    }
    return std::nullopt;
}

// Shared by the core, the wrapper and the UI: a slider position and a MIDI CC
// mean exactly the same thing. Log parameters use a log track so the low end of
// a 0.05x-4x body is usable rather than crushed into two pixels.
[[nodiscard]] inline float normalizedToValue(const std::uint32_t parameter, const float t) noexcept
{
    const ParamSpec& spec = kParameterSpecs[parameter];
    const float clampedT = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    if (!spec.logarithmic || spec.minimum <= 0.0f)
        return spec.minimum + clampedT * (spec.maximum - spec.minimum);
    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return std::exp(lo + clampedT * (hi - lo));
}

[[nodiscard]] inline float valueToNormalized(const std::uint32_t parameter, const float value) noexcept
{
    const ParamSpec& spec = kParameterSpecs[parameter];
    if (!spec.logarithmic || spec.minimum <= 0.0f) {
        const float span = spec.maximum - spec.minimum;
        return span > 0.0f ? (value - spec.minimum) / span : 0.0f;
    }
    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return (hi > lo) ? (std::log(value > spec.minimum ? value : spec.minimum) - lo) / (hi - lo) : 0.0f;
}

[[nodiscard]] inline float controllerToValue(const ParamId target, const std::uint8_t value) noexcept
{
    return normalizedToValue(index(target), static_cast<float>(value) / 127.0f);
}

}  // namespace downspout::box
