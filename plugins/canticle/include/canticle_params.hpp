#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::canticle {

enum class ParamId : std::uint32_t {
    model = 0,
    tone,
    body,
    movement,
    attack,
    decay,
    sustain,
    release,
    detune,
    width,
    drive,
    output,
    metal,
    articulation,
    range,
    ensemble,
};

inline constexpr std::size_t kParameterCount = 16;

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"model", "Model", 0.0f, 4.0f, 0.0f, true},
    {"tone", "Tone", 0.0f, 1.0f, 0.52f, false},
    {"body", "Body", 0.0f, 1.0f, 0.58f, false},
    {"movement", "Movement", 0.0f, 1.0f, 0.20f, false},
    {"attack", "Attack", 0.0f, 1.0f, 0.10f, false},
    {"decay", "Decay", 0.0f, 1.0f, 0.34f, false},
    {"sustain", "Sustain", 0.0f, 1.0f, 0.78f, false},
    {"release", "Release", 0.0f, 1.0f, 0.42f, false},
    {"detune", "Detune", 0.0f, 1.0f, 0.18f, false},
    {"width", "Width", 0.0f, 1.0f, 0.62f, false},
    {"drive", "Drive", 0.0f, 1.0f, 0.10f, false},
    {"output", "Output", 0.0f, 1.0f, 0.68f, false},
    {"metal", "Metal", 0.0f, 1.0f, 0.0f, false},
    {"articulation", "Articulation", 0.0f, 3.0f, 0.0f, true},
    {"range", "Register", 0.0f, 3.0f, 0.0f, true},
    {"ensemble", "Ensemble", 0.0f, 3.0f, 0.0f, true},
}};

inline constexpr std::array<const char*, 5> kModelNames = {{
    "Keys",
    "Reed",
    "Pad",
    "Pluck",
    "Glass",
}};

inline constexpr std::array<const char*, 4> kArticulationNames = {{
    "Natural",
    "Short",
    "Sustain",
    "Bloom",
}};

inline constexpr std::array<const char*, 4> kRangeNames = {{
    "Natural",
    "Low",
    "High",
    "Open",
}};

inline constexpr std::array<const char*, 4> kEnsembleNames = {{
    "Solo",
    "Pair",
    "Chorus",
    "Wide",
}};

} // namespace downspout::canticle
