#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>

namespace downspout::worms {

// DPF parameter indices — order is stable; append only
enum ParamIndex : std::uint32_t {
    kParamRoot = 0,
    kParamReg,
    kParamStepSize,
    kParamPatLen,
    kParamDensity,
    kParamVelocity,
    kParamVary,
    kParamSeed,
    kParamCondCh,
    kParamRule0,
    kParamRule1,
    kParamRule2,
    kParamRule3,
    kParamRule4,
    kParamRule5,
    kParamQuantize,
    kParamScale,
    kParamMidiCh,
    kParamActionRandomize,
    kParamActionMutate,
    kParameterCount
};

using downspout::generative::ParamSpec;

inline constexpr std::array<ParamSpec, kParameterCount> kParamSpecs {{
    {"root",       "Root",       0.0f,  11.0f,  0.0f, true},
    {"reg",        "Register",   0.0f,   4.0f,  2.0f, true},
    {"step_size",  "Step",       0.0f,   3.0f,  1.0f, true},
    {"pat_len",    "Length",     0.0f,   3.0f,  1.0f, true},
    {"density",    "Density",    0.0f,   1.0f,  0.8f},
    {"velocity",   "Velocity",   0.0f,   1.0f,  0.75f},
    {"vary",       "Vary",       0.0f,   1.0f,  0.2f},
    {"seed",       "Seed",       0.0f,   1.0f,  0.0f},
    {"cond_ch",    "Cond. Ch",   0.0f,  16.0f,  0.0f, true},
    {"rule0",      "Rule 0",     0.0f,   4.0f,  1.0f, true},
    {"rule1",      "Rule 1",     0.0f,   4.0f,  2.0f, true},
    {"rule2",      "Rule 2",     0.0f,   4.0f,  3.0f, true},
    {"rule3",      "Rule 3",     0.0f,   4.0f,  0.0f, true},
    {"rule4",      "Rule 4",     0.0f,   4.0f,  1.0f, true},
    {"rule5",      "Rule 5",     0.0f,   4.0f,  2.0f, true},
    {"quantize",   "Quantize",   0.0f,   1.0f,  0.0f, true},
    {"scale",      "Scale",      0.0f,  22.0f,  0.0f, true},
    {"midi_ch",    "MIDI Ch",    1.0f,  16.0f,  1.0f, true},
    {"randomize",  "Randomize",  0.0f, 65535.0f, 0.0f, true},
    {"mutate",     "Mutate",     0.0f, 65535.0f, 0.0f, true},
}};

}  // namespace downspout::worms
