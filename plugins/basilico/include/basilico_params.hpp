#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::basilico {

enum class ParamId : std::uint32_t {
    model = 0,
    waveform,
    subLevel,
    body,
    bite,
    mute,
    glide,
    accent,
    cutoff,
    resonance,
    filterEnv,
    keyTrack,
    attack,
    decay,
    sustain,
    release,
    punch,
    driveType,
    drive,
    output,
    lfoFrequency,
    lfoDepth,
    wobbleSync,
    wobbleDivision,
    wobbleShape,
    ampWobble,
    phaseWobble,
    squelch,
    wobbleStart,
    bypass,
    harmonic,
    harmonicLevel,
    wobbleSubMix,
    envSubMix,
    detuneOffset,
    detuneLevel,
};

inline constexpr std::size_t kParameterCount = 36;

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"model", "Model", 0.0f, 6.0f, 1.0f, true},
    {"waveform", "Waveform", 0.0f, 4.0f, 2.0f, true},
    {"sub_level", "Sub Level", 0.0f, 1.0f, 0.55f, false},
    {"body", "Body", 0.0f, 1.0f, 0.55f, false},
    {"bite", "Bite", 0.0f, 1.0f, 0.35f, false},
    {"mute", "Mute", 0.0f, 1.0f, 0.35f, false},
    {"glide", "Glide", 0.0f, 1.0f, 0.18f, false},
    {"accent", "Accent", 0.0f, 1.0f, 0.45f, false},
    {"cutoff", "Filter Cutoff", 0.0f, 1.0f, 0.48f, false},
    {"resonance", "Resonance", 0.0f, 1.0f, 0.25f, false},
    {"filter_env", "Filter Env", 0.0f, 1.0f, 0.42f, false},
    {"key_track", "Key Track", 0.0f, 1.0f, 0.45f, false},
    {"attack", "Attack", 0.0f, 1.0f, 0.02f, false},
    {"decay", "Decay", 0.0f, 1.0f, 0.30f, false},
    {"sustain", "Sustain", 0.0f, 1.0f, 0.55f, false},
    {"release", "Release", 0.0f, 1.0f, 0.24f, false},
    {"punch", "Punch", 0.0f, 1.0f, 0.40f, false},
    {"drive_type", "Drive Type", 0.0f, 3.0f, 1.0f, true},
    {"drive", "Drive", 0.0f, 1.0f, 0.25f, false},
    {"output", "Output", 0.0f, 1.0f, 0.70f, false},
    {"lfo_frequency", "Wobble Rate", 0.05f, 20.0f, 0.50f, false},
    {"lfo_depth", "Filter Wobble", 0.0f, 1.0f, 0.0f, false},
    {"wobble_sync", "Wobble Sync", 0.0f, 1.0f, 0.0f, true},
    {"wobble_division", "Wobble Division", 0.0f, 7.0f, 3.0f, true},
    {"wobble_shape", "Wobble Shape", 0.0f, 4.0f, 0.0f, true},
    {"amp_wobble", "Amp Wobble", 0.0f, 1.0f, 0.0f, false},
    {"phase_wobble", "Phase Wobble", 0.0f, 1.0f, 0.0f, false},
    {"squelch", "Squelch", 0.0f, 1.0f, 0.0f, false},
    {"wobble_start", "Wobble Start", 0.0f, 360.0f, 0.0f, false},
    {"dpf_bypass", "Bypass", 0.0f, 1.0f, 0.0f, true},
    {"harmonic", "Harmonic", 2.0f, 8.0f, 2.0f, true},
    {"harmonic_level", "Harmonic Level", 0.0f, 1.0f, 0.0f, false},
    {"wobble_sub_mix", "Wobble Sub Mix", 0.0f, 1.0f, 0.0f, false},
    {"env_sub_mix", "Env Sub Mix", -1.0f, 1.0f, 0.0f, false},
    {"detune_offset", "Detune Offset", 0.0f, 50.0f, 0.0f, false},
    {"detune_level", "Detune Level", 0.0f, 1.0f, 0.0f, false},
}};

inline constexpr std::array<const char*, 7> kModelNames = {{
    "Upright",
    "Electric",
    "Dub",
    "Acid",
    "Industrial",
    "Reese",
    "Hoover",
}};

inline constexpr std::array<const char*, 5> kWaveformNames = {{
    "Sine",
    "Triangle",
    "Saw",
    "Square",
    "Pulse",
}};

inline constexpr std::array<const char*, 4> kDriveTypeNames = {{
    "Clean",
    "Amp",
    "Acid",
    "Fold",
}};

inline constexpr std::array<const char*, 2> kWobbleSyncNames = {{
    "Free",
    "Tempo",
}};

inline constexpr std::array<const char*, 8> kWobbleDivisionNames = {{
    "1/1",
    "1/2",
    "1/4",
    "1/8",
    "1/8T",
    "1/16",
    "1/16T",
    "1/32",
}};

inline constexpr std::array<const char*, 5> kWobbleShapeNames = {{
    "Sine",
    "Triangle",
    "Ramp Down",
    "Ramp Up",
    "Square",
}};

// Downspout convention: CC 1-4 are the four most audible controls so drift's
// default lane output maps naturally to the live panel.
inline constexpr std::uint8_t kCcCutoff = 1;
inline constexpr std::uint8_t kCcResonance = 2;
inline constexpr std::uint8_t kCcDrive = 3;
inline constexpr std::uint8_t kCcOutput = 4;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 4> kControllerMap = {{
    {kCcCutoff, ParamId::cutoff, "Cutoff"},
    {kCcResonance, ParamId::resonance, "Resonance"},
    {kCcDrive, ParamId::drive, "Drive"},
    {kCcOutput, ParamId::output, "Output"},
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

[[nodiscard]] inline float controllerToParameter(const ParamId target, const std::uint8_t value) noexcept
{
    const auto& spec = kParameterSpecs[static_cast<std::size_t>(target)];
    const float normalized = static_cast<float>(value) / 127.0f;
    return spec.minimum + normalized * (spec.maximum - spec.minimum);
}

} // namespace downspout::basilico
