#pragma once

// Instrument families from `PRESETS` / `preset_name()` in pratt_midi_synth.py.
// Values are copied verbatim so behaviour stays traceable to the source.

#include <array>
#include <cstddef>

namespace downspout::pratt {

struct Preset {
    const char* name;
    int base;            // Pratt base: index n = (pitch + 1) * base
    double roll;         // excitation k^-roll
    double xi;           // filter frequency scale
    double attack;       // s
    double release;      // s (exponential time constant)
    double decay;        // s (exponential time constant, 0 = sustained)
    double brightDecay;  // s (bright -> dark table crossfade, 0 = none)
    double level;
    double vibrato;      // fractional frequency depth
    double evenGain;     // reed: 0.32
    int upperStart;      // flute: 3
    double upperGain;    // flute: 0.35
};

inline constexpr std::size_t kPresetCount = 11;

inline constexpr std::array<Preset, kPresetCount> kPresets = {{
    {"Piano",          5, 0.92, 0.150, 0.0035, 0.46, 1.35, 0.38, 0.78, 0.0,    1.0,  0, 1.0},
    {"Electric Piano", 11, 0.90, 0.135, 0.0045, 0.38, 1.60, 0.65, 0.76, 0.0,    1.0,  0, 1.0},
    {"Organ",          2, 1.05, 0.130, 0.0090, 0.12, 0.00, 0.00, 0.66, 0.0007, 1.0,  0, 1.0},
    {"Pluck",          17, 0.78, 0.120, 0.0025, 0.23, 0.75, 0.25, 0.78, 0.0,    1.0,  0, 1.0},
    {"Bass",           3, 1.02, 0.160, 0.0080, 0.24, 1.25, 0.28, 0.86, 0.0,    1.0,  0, 1.0},
    {"Strings",        7, 0.72, 0.115, 0.0180, 0.16, 0.00, 0.00, 0.65, 0.0014, 1.0,  0, 1.0},
    {"Brass",          11, 0.66, 0.110, 0.0170, 0.14, 0.00, 0.00, 0.68, 0.0009, 1.0,  0, 1.0},
    {"Reed",           5, 0.80, 0.110, 0.0120, 0.11, 0.00, 0.00, 0.67, 0.0008, 0.32, 0, 1.0},
    {"Flute",          3, 1.75, 0.150, 0.0180, 0.13, 0.00, 0.00, 0.75, 0.0010, 1.0,  3, 0.35},
    {"Pad",            37, 0.82, 0.100, 0.0340, 0.42, 0.00, 0.00, 0.58, 0.0013, 1.0,  0, 1.0},
    {"Timpani",        7, 1.35, 0.190, 0.0015, 0.32, 0.62, 0.14, 0.93, 0.0,    1.0,  0, 1.0},
}};

// General MIDI program (0-based) -> preset index, as in `preset_name()`.
constexpr int presetForProgram(int program) {
    if (program == 47) return 10;
    if (program < 4) return 0;
    if (program <= 5) return 1;
    if (program < 16) return 3;
    if (program < 24) return 2;
    if (program < 32) return 3;
    if (program < 40) return 4;
    if (program == 45 || program == 46) return 3;
    if (program < 50) return 5;
    if (program < 56) return 9;
    if (program < 64) return 6;
    if (program < 72) return 7;
    if (program < 80) return 8;
    return 9;
}

}  // namespace downspout::pratt
