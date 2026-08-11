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
    kParamOutputChannel,   // 1-16
    kParamConductorCh,     // 0=off, 1-16
    kParamPassInput,       // boolean
    kParamPanic,           // trigger

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
