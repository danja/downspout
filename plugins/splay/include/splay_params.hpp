#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::splay {

// A true stereo matrix first, then a mid/side width stage, then an image delay.
enum class ParamId : std::uint32_t {
    matrixLL = 0,
    matrixLR,
    matrixRL,
    matrixRR,
    width,
    imageDelay,
    invertR,
    level,
    bypass,
    count
};

inline constexpr std::size_t kParameterCount = static_cast<std::size_t>(ParamId::count);
inline constexpr std::array<const char*, 2> kOffOnNames = {{"Off", "On"}};

struct ParamSpec {
    const char* name;
    const char* symbol;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"L to L", "ll", "x", 0.0f, 2.0f, 1.0f, false},
    {"L to R", "lr", "x", 0.0f, 2.0f, 0.0f, false},
    {"R to L", "rl", "x", 0.0f, 2.0f, 0.0f, false},
    {"R to R", "rr", "x", 0.0f, 2.0f, 1.0f, false},
    {"Width", "width", "x", 0.0f, 2.0f, 1.0f, false},
    {"Image Delay", "image_delay", "ms", 0.0f, 20.0f, 0.0f, false},
    {"Invert R", "invert_r", "", 0.0f, 1.0f, 0.0f, true},
    {"Output", "level", "x", 0.0f, 2.0f, 1.0f, false},
    {"Bypass", "bypass", "", 0.0f, 1.0f, 0.0f, true},
}};

struct Parameters {
    float matrixLL = 1.0f;
    float matrixLR = 0.0f;
    float matrixRL = 0.0f;
    float matrixRR = 1.0f;
    float width = 1.0f;
    float imageDelay = 0.0f;
    float invertR = 0.0f;
    float level = 1.0f;
    float bypass = 0.0f;
};

inline constexpr std::array<float Parameters::* , kParameterCount> kParameterFields {{
    &Parameters::matrixLL, &Parameters::matrixLR, &Parameters::matrixRL, &Parameters::matrixRR,
    &Parameters::width, &Parameters::imageDelay, &Parameters::invertR, &Parameters::level,
    &Parameters::bypass,
}};

[[nodiscard]] inline int clampInt(const float value, const int low, const int high) noexcept
{
    return std::clamp(static_cast<int>(value), low, high);
}

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        const ParamSpec& spec = kParameterSpecs[i];
        float& value = p.*kParameterFields[i];
        if (!std::isfinite(value))
            value = spec.defaultValue;
        value = std::clamp(value, spec.minimum, spec.maximum);
        if (spec.integer)
            value = static_cast<float>(clampInt(value, static_cast<int>(spec.minimum),
                                                 static_cast<int>(spec.maximum)));
    }
    return p;
}

}  // namespace downspout::splay
