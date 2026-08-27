#pragma once

#include "skream_core.hpp"

namespace downspout::skream {

struct PresetDef {
    const char* name;
    Parameters  params;
};

// 10 presets: inputGain, cutoff, scream, resonance, mix, outputGain, track
// CC params left at defaults (0/0/1 = disabled).
inline const PresetDef kPresets[] = {
    { "Classic Growl",    { 0.0f,  85.0f, 46.5f, 100.0f, 100.0f,  -6.0f,  0.0f } },
    { "Wobbly Bass",      {-3.0f,  20.0f, 12.0f,  90.0f, 100.0f,  -8.0f,  0.0f } },
    { "Scream Lead",      { 6.0f,  62.0f, 82.0f,  82.0f, 100.0f,  -9.0f,  0.0f } },
    { "Metallic Edge",    { 0.0f,  90.0f, 88.0f,  72.0f, 100.0f,  -7.0f,  0.0f } },
    { "Vowel Formant",    {-6.0f,  45.0f, 22.0f, 100.0f, 100.0f, -10.0f, 20.0f } },
    { "Tight Bite",       {10.0f,  76.0f, 50.0f,  42.0f, 100.0f, -12.0f,  0.0f } },
    { "Squelch",          { 0.0f,  94.0f, 86.0f,  65.0f, 100.0f,  -6.0f,  0.0f } },
    { "Dubstep Classic",  { 3.0f,  70.0f, 58.0f,  94.0f, 100.0f,  -8.0f, 10.0f } },
    { "Subtle Saturation",{-8.0f,  78.0f,  5.0f,  32.0f,  72.0f,  -3.0f,  0.0f } },
    { "Feedback Drone",   {-3.0f,  48.0f, 44.0f, 100.0f, 100.0f, -14.0f, 35.0f } },
};

inline constexpr int kPresetCount = 10;

}  // namespace downspout::skream
