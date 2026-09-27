#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::quefrency {

// ── Parameters ─────────────────────────────────────────────────────────────
// jig:paramIndex order from the source profile.json. Ranges, defaults and the
// CC map (70-80) are traceable to the source; do not reorder.

enum class ParamId : std::uint32_t {
    formantShift = 0,
    formantDepth,
    formantTilt,
    pitchShift,
    pitchFine,
    freqShift,
    harmonicDepth,
    lifter,
    estimator,
    mix,
    output,
    // Read-only status
    outLatency,
};

inline constexpr std::size_t kParameterCount = 12;
inline constexpr std::size_t kInputParameterCount = 11;

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
    {"formant_shift", "Formant shift", "st", -12.0f, 12.0f, 0.0f, false, false},
    {"formant_depth", "Formant depth", "%", 0.0f, 200.0f, 100.0f, false, false},
    {"formant_tilt", "Formant tilt", "dB/oct", -6.0f, 6.0f, 0.0f, false, false},
    {"pitch_shift", "Pitch shift", "st", -24.0f, 24.0f, 0.0f, false, false},
    {"pitch_fine", "Pitch fine", "cent", -100.0f, 100.0f, 0.0f, false, false},
    {"freq_shift", "Freq shift", "Hz", -1000.0f, 1000.0f, 0.0f, false, false},
    {"harmonic_depth", "Harmonic depth", "%", 0.0f, 200.0f, 100.0f, false, false},
    {"lifter", "Lifter", "ms", 0.5f, 5.0f, 1.5f, false, false},
    {"estimator", "Estimator", "", 0.0f, 1.0f, 0.0f, true, false},
    {"mix", "Mix", "", 0.0f, 1.0f, 1.0f, false, false},
    {"output", "Output", "dB", -24.0f, 12.0f, 0.0f, false, false},
    {"out_latency", "Latency", "spl", 0.0f, 4095.0f, 2047.0f, false, true},
}};

inline constexpr std::array<const char*, 2> kEstimatorNames = {{
    "Cepstral",
    "True envelope",
}};

// ── Fixed MIDI controller map ──────────────────────────────────────────────
// Source convention: CC 70 drives parameter 0 through CC 80 driving
// parameter 10, on any channel. Value 64 selects the port default wherever
// the default lies strictly inside the range, else the map is linear.

inline constexpr std::uint8_t kFirstCc = 70;

[[nodiscard]] inline bool controllerTarget(std::uint8_t controller, ParamId& target) noexcept
{
    if (controller < kFirstCc || controller >= kFirstCc + kInputParameterCount)
        return false;
    target = static_cast<ParamId>(controller - kFirstCc);
    return true;
}

[[nodiscard]] inline float controllerToParameter(ParamId target, std::uint8_t value) noexcept
{
    const auto& spec = kParameterSpecs[static_cast<std::size_t>(target)];
    const float v = static_cast<float>(value > 127 ? 127 : value);
    if (spec.minimum < spec.defaultValue && spec.defaultValue < spec.maximum)
    {
        if (v <= 64.0f)
            return spec.minimum + (spec.defaultValue - spec.minimum) * v / 64.0f;
        return spec.defaultValue + (spec.maximum - spec.defaultValue) * (v - 64.0f) / 63.0f;
    }
    return spec.minimum + (spec.maximum - spec.minimum) * v / 127.0f;
}

}  // namespace downspout::quefrency
