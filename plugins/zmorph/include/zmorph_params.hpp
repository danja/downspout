#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::zmorph {

enum ParamId : int {
    kCutoff = 0,
    kResonance,
    kMorph,
    kMix,
    kLevel,
    kParamCount
};

struct Params {
    float cutoff = 1000.0f;
    float resonance = 0.5f;
    float morph = 0.5f;
    float mix = 1.0f;
    float level = 0.0f;
    float noteCutoff = 0.0f;
    bool noteTracking = false;
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
};

inline constexpr int kParameterCount = 5;

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"cutoff", "Cutoff", "Hz", 20.0f, 20000.0f, 1000.0f, false},
    {"resonance", "Resonance", "", 0.0f, 0.99f, 0.5f, false},
    {"morph", "Morph", "", 0.0f, 1.0f, 0.5f, false},
    {"mix", "Mix", "", 0.0f, 1.0f, 1.0f, false},
    {"level", "Level", "dB", -24.0f, 12.0f, 0.0f, false},
}};

inline constexpr std::uint8_t kCcResonance = 1;
inline constexpr std::uint8_t kCcMorph = 2;
inline constexpr std::uint8_t kCcMix = 3;
inline constexpr std::uint8_t kCcLevel = 4;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 4> kControllerMap = {{
    {kCcResonance, kResonance, "Resonance"},
    {kCcMorph, kMorph, "Morph"},
    {kCcMix, kMix, "Mix"},
    {kCcLevel, kLevel, "Level"},
}};

[[nodiscard]] inline bool controllerTarget(const std::uint8_t controller, ParamId& target) noexcept
{
    for (const ControllerMapping& mapping : kControllerMap) {
        if (mapping.controller == controller) {
            target = mapping.target;
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline float controllerToParameter(const ParamId target, const std::uint8_t value) noexcept
{
    const auto& spec = kParameterSpecs[static_cast<std::size_t>(target)];
    const float normalised = static_cast<float>(value) / 127.0f;
    return spec.minimum + normalised * (spec.maximum - spec.minimum);
}

[[nodiscard]] inline float noteToCutoff(const int note) noexcept
{
    const float hertz = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
    return std::clamp(hertz, 20.0f, 20000.0f);
}

}  // namespace downspout::zmorph