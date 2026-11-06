#pragma once

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

}  // namespace downspout::zmorph