#pragma once

#include <array>
#include <cstdint>

namespace downspout::flues_synth_driver {

// ── Parameter indices ─────────────────────────────────────────────────────────

enum Param : std::uint32_t {
    // Disyn oscillator
    kParamAlgorithm = 0,   // CC16  0-17
    kParamDisynP1,         // CC17  0-1
    kParamDisynP2,         // CC18  0-1
    kParamDisynP3,         // CC19  0-1

    // Source mix
    kParamNoiseLevel,      // CC20  0-1
    kParamDcLevel,         // CC21  0-1

    // Physical model
    kParamInterfaceType,   // CC24  0-11
    kParamIntensity,       // CC1   0-1

    // Formants
    kParamF1,              // CC71  200-1000 Hz
    kParamF2,              // CC10  500-3000 Hz
    kParamF3,              // CC74  1500-4000 Hz
    kParamF4,              // CC75  2500-4500 Hz

    // Vocal modes (boolean)
    kParamNasal,           // CC80
    kParamSing,            // CC81
    kParamShout,           // CC82
    kParamFry,             // CC83

    // Envelope
    kParamAttack,          // CC73  0.001-1.0 s
    kParamRelease,         // CC72  0.01-3.0 s

    // Pitch
    kParamTuning,          // CC26  -12 to +12 st

    // Effects
    kParamDelay1Fb,        // CC28  0-1
    kParamDelay2Fb,        // CC29  0-1
    kParamDelayRatio,      // CC27  0.5-2.0
    kParamFilterFb,        // CC30  0-1

    // Filter
    kParamFilterFreq,      // CC32  20-20000 Hz
    kParamFilterQ,         // CC33  0.1-10.0
    kParamFilterShape,     // CC34  0-1

    // LFO
    kParamLfoFreq,         // CC36  0.1-20 Hz
    kParamAmFmDepth,       // CC37  -1 to +1

    // Master
    kParamGain,            // CC7   0-1

    // Trajectory (Program 2 only; share CCs with envelope/formant/intensity above)
    kParamTrajSides,       // CC73  3-24  (shares CC with Attack)
    kParamTrajStartPos,    // CC72  0-360 (shares CC with Release)
    kParamTrajStartAngle,  // CC28  0-360 (shares CC with Delay1 FB)
    kParamTrajJitter,      // CC30  0-10  (shares CC with Filter FB)
    kParamTrajClip,        // CC74  0-1   (shares CC with F3)
    kParamTrajMixX,        // CC71  0-1   (shares CC with F1)
    kParamTrajMixY,        // CC1   0-1   (shares CC with Intensity)

    // Driver routing
    kParamProgram,         // MIDI Program Change 0-30
    kParamOutputChannel,   // 1-16
    kParamConductorCh,     // 0=off, 1-16
    kParamPassInput,       // boolean
    kParamPanic,           // trigger
    kParamRandomize,       // trigger

    // Status output
    kParamMidiActivity,    // output: 0-1 flash

    kParamCount
};

// ── CC numbers ────────────────────────────────────────────────────────────────

struct SynthCC {
    std::uint8_t cc;
    float minimum;
    float maximum;
    float defaultValue;
    bool exponential;
    bool boolean;
    bool integer;
};

// Indexed by (Param - kParamAlgorithm); covers kParamAlgorithm..kParamTrajMixY
inline constexpr std::array<SynthCC, static_cast<std::size_t>(kParamTrajMixY) + 1> kSynthCCs {{
    // algorithm
    {16,  0.0f,    17.0f,   0.0f,   false, false, true},
    // disyn p1/p2/p3
    {17,  0.0f,    1.0f,    0.5f,   false, false, false},
    {18,  0.0f,    1.0f,    0.5f,   false, false, false},
    {19,  0.0f,    1.0f,    0.5f,   false, false, false},
    // noise, dc
    {20,  0.0f,    1.0f,    0.15f,  false, false, false},
    {21,  0.0f,    1.0f,    0.0f,   false, false, false},
    // interface type
    {24,  0.0f,    11.0f,   2.0f,   false, false, true},
    // intensity
    {1,   0.0f,    1.0f,    0.5f,   false, false, false},
    // F1 F2 F3 F4
    {71,  200.0f,  1000.0f, 500.0f, true,  false, false},
    {10,  500.0f,  3000.0f, 1500.0f,true,  false, false},
    {74,  1500.0f, 4000.0f, 2500.0f,true,  false, false},
    {75,  2500.0f, 4500.0f, 3500.0f,true,  false, false},
    // vocal modes
    {80,  0.0f,    1.0f,    0.0f,   false, true,  false},
    {81,  0.0f,    1.0f,    0.0f,   false, true,  false},
    {82,  0.0f,    1.0f,    0.0f,   false, true,  false},
    {83,  0.0f,    1.0f,    0.0f,   false, true,  false},
    // attack, release
    {73,  0.001f,  1.0f,    0.01f,  true,  false, false},
    {72,  0.01f,   3.0f,    0.05f,  true,  false, false},
    // tuning  (stored as semitones -12..+12, linear mapped to 0-127)
    {26,  -12.0f,  12.0f,   0.0f,   false, false, false},
    // delay1 fb, delay2 fb, delay ratio, filter fb
    {28,  0.0f,    1.0f,    0.2f,   false, false, false},
    {29,  0.0f,    1.0f,    0.2f,   false, false, false},
    {27,  0.5f,    2.0f,    1.0f,   true,  false, false},
    {30,  0.0f,    1.0f,    0.1f,   false, false, false},
    // filter freq, q, shape
    {32,  20.0f,   20000.0f,2000.0f,true,  false, false},
    {33,  0.1f,    10.0f,   1.0f,   true,  false, false},
    {34,  0.0f,    1.0f,    0.0f,   false, false, false},
    // lfo freq, am/fm depth
    {36,  0.1f,    20.0f,   5.0f,   true,  false, false},
    {37,  -1.0f,   1.0f,    0.0f,   false, false, false},
    // gain
    {7,   0.0f,    1.0f,    0.5f,   false, false, false},
    // trajectory (shares CCs with above)
    {73,  3.0f,    24.0f,   8.0f,   false, false, true},  // traj sides
    {72,  0.0f,    360.0f,  0.0f,   false, false, false}, // traj start pos
    {28,  0.0f,    360.0f,  0.0f,   false, false, false}, // traj start angle
    {30,  0.0f,    10.0f,   0.0f,   false, false, false}, // traj jitter
    {74,  0.0f,    1.0f,    0.5f,   false, false, false}, // traj clip
    {71,  0.0f,    1.0f,    0.5f,   false, false, false}, // traj mix x
    {1,   0.0f,    1.0f,    0.5f,   false, false, false}, // traj mix y
}};

inline constexpr std::size_t kSynthParamCount = kParamTrajMixY - kParamAlgorithm + 1;

// Conductor default CCs (matching Conductor plugin defaults)
inline constexpr std::uint8_t kConductorCcScene    = 20;
inline constexpr std::uint8_t kConductorCcDensity  = 21;
inline constexpr std::uint8_t kConductorCcEnergy   = 22;
inline constexpr std::uint8_t kConductorCcMutation = 23;
inline constexpr std::uint8_t kConductorCcReset    = 24;

// ── Program names ────────────────────────────────────────────────────────────

inline constexpr std::array<const char*, 31> kProgramNames {{
    "Disyn Echo",       "Disyn+Filter",      "Traj Polygon",
    "Formant Voice",    "Hybrid Speech",     "Physical Model",
    "Full Hybrid",      "Disyn Direct",      "ModFM Formant",
    "DSF Inharmonic",   "PAF Direct",        "Cascaded DSF+PAF",
    "Tanh Spectral",    "Hybrid DSF>Fmnt",   "Feedback ModFM",
    "Dirichlet",        "Multi-Algo Demo",   "Spectral Sculptor",
    "Hybrid Fmnt Eng",  "Cascaded Spectral", "Parallel Dist",
    "Feedback Dist Net","Morph Spectral",    "Inharmonic Res",
    "Adaptive Filter",  "Multi-Stage Wave",  "Freq-Dep Asym",
    "Cross-Algo Mod",   "Vocal Morph",       "Taylor Series",
    "Disyn+Delays"
}};

// ── Per-program slider availability ──────────────────────────────────────────

// Mirrors flues-synth SynthParameter enum (midi_mapping.h)
enum FlueSynthParam : std::uint8_t {
    FSP_ALGORITHM = 0, FSP_P1, FSP_P2, FSP_P3, FSP_LEVEL,
    FSP_NOISE, FSP_DC,
    FSP_TSIDES, FSP_TPOS, FSP_TANGLE, FSP_TCLIP, FSP_TMIXX, FSP_TMIXY,
    FSP_INTENSITY, FSP_TUNING, FSP_RATIO,
    FSP_D1FB, FSP_D2FB, FSP_FFB,
    FSP_FFREQ, FSP_FQ, FSP_FSHAPE,
    FSP_F1, FSP_F2, FSP_F3, FSP_F4,
    FSP_NASAL, FSP_SING, FSP_SHOUT, FSP_FRY,
    FSP_ATTACK, FSP_RELEASE,
    FSP_LFREQ, FSP_AMDEPTH, FSP_ITYPE, FSP_TJITTER,
    FSP_NONE = 255
};

// 31 programs × 9 sliders (sourced from flues-synth midi_mapping.c)
inline constexpr FlueSynthParam kProgramMap[31][9] = {
    // 0: Disyn Echo
    {FSP_ALGORITHM,FSP_P1,    FSP_P2,     FSP_ITYPE,    FSP_INTENSITY,FSP_TUNING,FSP_D1FB,  FSP_ATTACK,FSP_RELEASE},
    // 1: Disyn+Filter
    {FSP_FFREQ,   FSP_FQ,    FSP_FSHAPE,  FSP_LEVEL,    FSP_INTENSITY,FSP_TUNING,FSP_RATIO, FSP_ATTACK,FSP_RELEASE},
    // 2: Trajectory Polygon
    {FSP_TSIDES,  FSP_TPOS,  FSP_TANGLE,  FSP_TJITTER,  FSP_TCLIP,   FSP_TMIXX, FSP_TMIXY, FSP_ATTACK,FSP_RELEASE},
    // 3: Formant Voice
    {FSP_F1,      FSP_F2,    FSP_F3,      FSP_F4,       FSP_NOISE,   FSP_NASAL, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 4: Hybrid Speech
    {FSP_F1,      FSP_F2,    FSP_F3,      FSP_F4,       FSP_LEVEL,   FSP_NOISE, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 5: Physical Model
    {FSP_D1FB,    FSP_D2FB,  FSP_FFB,     FSP_ITYPE,    FSP_INTENSITY,FSP_TUNING,FSP_RATIO, FSP_ATTACK,FSP_RELEASE},
    // 6: Full Hybrid
    {FSP_D1FB,    FSP_D2FB,  FSP_FFB,     FSP_ITYPE,    FSP_INTENSITY,FSP_TUNING,FSP_RATIO, FSP_ATTACK,FSP_RELEASE},
    // 7: Disyn Direct
    {FSP_ALGORITHM,FSP_P1,   FSP_P2,      FSP_LEVEL,    FSP_INTENSITY,FSP_TUNING,FSP_RATIO, FSP_ATTACK,FSP_RELEASE},
    // 8: ModFM Formant
    {FSP_P1,      FSP_P2,    FSP_F1,      FSP_F2,       FSP_F3,      FSP_F4,    FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 9: DSF Inharmonic
    {FSP_P1,      FSP_P2,    FSP_D1FB,    FSP_D2FB,     FSP_INTENSITY,FSP_TUNING,FSP_RATIO, FSP_ATTACK,FSP_RELEASE},
    // 10: PAF Direct
    {FSP_P1,      FSP_P2,    FSP_FFREQ,   FSP_FQ,       FSP_FSHAPE,  FSP_TUNING,FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 11: Cascaded DSF+PAF
    {FSP_P1,      FSP_P2,    FSP_F1,      FSP_F2,       FSP_F3,      FSP_TUNING,FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 12: Tanh Spectral
    {FSP_P1,      FSP_P2,    FSP_FFREQ,   FSP_FQ,       FSP_FSHAPE,  FSP_FFB,   FSP_INTENSITY,FSP_ATTACK,FSP_RELEASE},
    // 13: Hybrid DSF>Fmnt
    {FSP_P1,      FSP_P2,    FSP_F1,      FSP_F2,       FSP_F3,      FSP_F4,    FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 14: Feedback ModFM
    {FSP_P1,      FSP_P2,    FSP_D1FB,    FSP_D2FB,     FSP_FFB,     FSP_TUNING,FSP_RATIO,  FSP_ATTACK,FSP_RELEASE},
    // 15: Dirichlet
    {FSP_P1,      FSP_P2,    FSP_FFREQ,   FSP_FQ,       FSP_FSHAPE,  FSP_TUNING,FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 16: Multi-Algo Demo
    {FSP_ALGORITHM,FSP_P1,   FSP_P2,      FSP_LEVEL,    FSP_INTENSITY,FSP_TUNING,FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 17: Spectral Sculptor
    {FSP_P1,      FSP_P2,    FSP_FFREQ,   FSP_FQ,       FSP_FSHAPE,  FSP_D1FB,  FSP_FFB,   FSP_ATTACK,FSP_RELEASE},
    // 18: Hybrid Fmnt Eng
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_LEVEL,    FSP_INTENSITY,FSP_TUNING,FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 19: Cascaded Spectral
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_FFREQ,    FSP_FQ,      FSP_INTENSITY,FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 20: Parallel Dist
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_INTENSITY,FSP_TUNING,  FSP_FFREQ, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 21: Feedback Dist Net
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_INTENSITY,FSP_TUNING,  FSP_FFREQ, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 22: Morph Spectral
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_INTENSITY,FSP_TUNING,  FSP_FFREQ, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 23: Inharmonic Res
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_TUNING,   FSP_INTENSITY,FSP_D1FB, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 24: Adaptive Filter
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_FFREQ,    FSP_FQ,      FSP_LFREQ, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 25: Multi-Stage Wave
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_FFREQ,    FSP_FQ,      FSP_INTENSITY,FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 26: Freq-Dep Asym
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_FFREQ,    FSP_FQ,      FSP_LFREQ, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 27: Cross-Algo Mod
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_INTENSITY,FSP_TUNING,  FSP_FFREQ, FSP_TJITTER,FSP_ATTACK,FSP_RELEASE},
    // 28: Vocal Morph
    {FSP_P1,      FSP_P2,    FSP_F1,      FSP_F2,       FSP_NASAL,   FSP_SING,  FSP_SHOUT,  FSP_ATTACK,FSP_RELEASE},
    // 29: Taylor Series
    {FSP_P1,      FSP_P2,    FSP_P3,      FSP_TJITTER,  FSP_NONE,    FSP_NONE,  FSP_NONE,   FSP_ATTACK,FSP_RELEASE},
    // 30: Disyn+Delays
    {FSP_D1FB,    FSP_D2FB,  FSP_FFB,     FSP_LEVEL,    FSP_INTENSITY,FSP_TUNING,FSP_RATIO, FSP_ATTACK,FSP_RELEASE},
};

inline FlueSynthParam paramToFlueSynthParam(Param p) noexcept
{
    switch (p) {
    case kParamAlgorithm:      return FSP_ALGORITHM;
    case kParamDisynP1:        return FSP_P1;
    case kParamDisynP2:        return FSP_P2;
    case kParamDisynP3:        return FSP_P3;
    case kParamNoiseLevel:     return FSP_NOISE;
    case kParamDcLevel:        return FSP_DC;
    case kParamInterfaceType:  return FSP_ITYPE;
    case kParamIntensity:      return FSP_INTENSITY;
    case kParamF1:             return FSP_F1;
    case kParamF2:             return FSP_F2;
    case kParamF3:             return FSP_F3;
    case kParamF4:             return FSP_F4;
    case kParamNasal:          return FSP_NASAL;
    case kParamSing:           return FSP_SING;
    case kParamShout:          return FSP_SHOUT;
    case kParamFry:            return FSP_FRY;
    case kParamAttack:         return FSP_ATTACK;
    case kParamRelease:        return FSP_RELEASE;
    case kParamTuning:         return FSP_TUNING;
    case kParamDelay1Fb:       return FSP_D1FB;
    case kParamDelay2Fb:       return FSP_D2FB;
    case kParamDelayRatio:     return FSP_RATIO;
    case kParamFilterFb:       return FSP_FFB;
    case kParamFilterFreq:     return FSP_FFREQ;
    case kParamFilterQ:        return FSP_FQ;
    case kParamFilterShape:    return FSP_FSHAPE;
    case kParamLfoFreq:        return FSP_LFREQ;
    case kParamAmFmDepth:      return FSP_AMDEPTH;
    case kParamTrajSides:      return FSP_TSIDES;
    case kParamTrajStartPos:   return FSP_TPOS;
    case kParamTrajStartAngle: return FSP_TANGLE;
    case kParamTrajJitter:     return FSP_TJITTER;
    case kParamTrajClip:       return FSP_TCLIP;
    case kParamTrajMixX:       return FSP_TMIXX;
    case kParamTrajMixY:       return FSP_TMIXY;
    default:                   return FSP_NONE;
    }
}

// Returns true if param p is active for the given program (0-30).
// Params with dedicated fixed CCs always return true.
inline bool isParamAvailableForProgram(Param p, int prog) noexcept
{
    const FlueSynthParam fsp = paramToFlueSynthParam(p);
    if (fsp == FSP_NONE) return true;  // Gain, driver params — always on
    // Always-on: fixed dedicated CCs, plus Attack/Release in every program
    if (fsp == FSP_ATTACK   || fsp == FSP_RELEASE) return true;
    if (fsp == FSP_NASAL    || fsp == FSP_SING  ||
        fsp == FSP_SHOUT    || fsp == FSP_FRY)   return true;
    if (fsp == FSP_LFREQ    || fsp == FSP_AMDEPTH) return true;
    if (fsp == FSP_DC       || fsp == FSP_NOISE)   return true;
    if (fsp == FSP_ALGORITHM) return true;
    const int pg = prog < 0 ? 0 : (prog > 30 ? 30 : prog);
    for (int s = 0; s < 9; ++s)
        if (kProgramMap[pg][s] == fsp) return true;
    return false;
}

// Interface type names (CC24, 0-11)
inline constexpr std::array<const char*, 12> kInterfaceTypeNames {{
    "Pluck", "Hit", "Reed", "Flute", "Brass", "Bow",
    "Bell", "Drum", "Crystal", "Vapor", "Quantum", "Plasma"
}};

// Algorithm names (CC16, 0-17)
inline constexpr std::array<const char*, 18> kAlgorithmNames {{
    "Sine", "Square", "Tri", "Saw", "RevSaw", "Pulse", "Noise",
    "FM", "AM", "Ring", "Phase", "Fold", "Wrap", "Clip",
    "Crush", "Shift", "Comb", "Karplus"
}};

}  // namespace downspout::flues_synth_driver
