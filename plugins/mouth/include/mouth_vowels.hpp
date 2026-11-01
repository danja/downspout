#pragma once

#include <array>
#include <cstddef>

namespace downspout::mouth {

// Five formants per vowel: frequency (Hz), level relative to the first formant (dB) and
// bandwidth (Hz). This is the widely circulated Csound formant table for bass, tenor,
// alto and soprano voices. The tests hold an independent copy of the first two formants.
inline constexpr std::size_t kFormants = 5;
inline constexpr std::size_t kVowels = 5;  // a, e, i, o, u
inline constexpr std::size_t kVoices = 4;  // bass, tenor, alto, soprano

inline constexpr std::array<const char*, kVowels> kVowelNames = {{"A", "E", "I", "O", "U"}};
inline constexpr std::array<const char*, kVoices> kVoiceNames = {{"Bass", "Tenor", "Alto", "Soprano"}};

struct VowelData {
    float freq[kFormants];
    float db[kFormants];
    float bw[kFormants];
};

inline constexpr std::array<std::array<VowelData, kVowels>, kVoices> kVowelTable = {{
    // Bass
    {{
        {{600, 1040, 2250, 2450, 2750}, {0, -7, -9, -9, -20}, {60, 70, 110, 120, 130}},
        {{400, 1620, 2400, 2800, 3100}, {0, -12, -9, -12, -18}, {40, 80, 100, 120, 120}},
        {{250, 1750, 2600, 3050, 3340}, {0, -30, -16, -22, -28}, {60, 90, 100, 120, 120}},
        {{400, 750, 2400, 2600, 2900}, {0, -11, -21, -20, -40}, {40, 80, 100, 120, 120}},
        {{350, 600, 2400, 2675, 2950}, {0, -20, -32, -28, -36}, {40, 80, 100, 120, 120}},
    }},
    // Tenor
    {{
        {{650, 1080, 2650, 2900, 3250}, {0, -6, -7, -8, -22}, {80, 90, 120, 130, 140}},
        {{400, 1700, 2600, 3200, 3580}, {0, -14, -12, -14, -20}, {70, 80, 100, 120, 120}},
        {{290, 1870, 2800, 3250, 3540}, {0, -15, -18, -20, -30}, {40, 90, 100, 120, 120}},
        {{400, 800, 2600, 2800, 3000}, {0, -10, -12, -12, -26}, {40, 80, 100, 120, 120}},
        {{350, 600, 2700, 2900, 3300}, {0, -20, -17, -14, -26}, {40, 80, 100, 120, 120}},
    }},
    // Alto
    {{
        {{800, 1150, 2800, 3500, 4950}, {0, -4, -20, -36, -60}, {80, 90, 120, 130, 140}},
        {{400, 1600, 2700, 3300, 4950}, {0, -24, -30, -35, -60}, {60, 80, 120, 150, 200}},
        {{350, 1700, 2700, 3700, 4950}, {0, -20, -30, -36, -60}, {50, 100, 120, 150, 200}},
        {{450, 800, 2830, 3500, 4950}, {0, -9, -16, -28, -55}, {70, 80, 100, 130, 135}},
        {{325, 700, 2530, 3500, 4950}, {0, -12, -30, -40, -64}, {50, 60, 170, 180, 200}},
    }},
    // Soprano
    {{
        {{800, 1150, 2900, 3900, 4950}, {0, -6, -32, -20, -50}, {80, 90, 120, 130, 140}},
        {{350, 2000, 2800, 3600, 4950}, {0, -20, -15, -40, -56}, {60, 100, 120, 150, 200}},
        {{270, 2140, 2950, 3900, 4950}, {0, -12, -26, -26, -44}, {60, 90, 100, 120, 120}},
        {{450, 800, 2830, 3800, 4950}, {0, -11, -22, -22, -50}, {40, 80, 100, 120, 120}},
        {{325, 700, 2700, 3800, 4950}, {0, -16, -35, -40, -60}, {50, 60, 170, 180, 200}},
    }},
}};

// Where each vowel sits on the pad (0..100 both ways), laid out like a phonetics chart:
// across = front to back (second formant falls), down = closed to open (first formant rises).
struct PadPoint {
    float x;
    float y;
};

inline constexpr std::array<PadPoint, kVowels> kPadPoints = {{
    {50.0f, 95.0f},  // A
    {10.0f, 60.0f},  // E
    {10.0f, 10.0f},  // I
    {90.0f, 60.0f},  // O
    {90.0f, 10.0f},  // U
}};

}  // namespace downspout::mouth
