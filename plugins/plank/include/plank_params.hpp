#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::plank {

// ── Layout ──────────────────────────────────────────────────────────────────
//
// Plinky is an eight-voice touch synth: eight fingers, eight strings, eight
// voices. The Launchpad grid is eight columns wide, so each column becomes one
// string and each row picks a scale degree within that string's octave. That
// makes Plinky's polyphony directly playable rather than hidden behind a
// round-robin allocator.
//
//   grid note (row, col) -> string `col`, scale degree `row`
//
// Row 0 is the bottom hardware row and row 7 the top, matching the `padseq`
// reference described in docs/launchpad.md.

// Comb lines in the reverb tank. Also used at namespace scope to size the
// per-line length table, so it lives here rather than inside Processor.
inline constexpr std::size_t kReverbLineCount = 4;

inline constexpr std::size_t kStringCount = 8;
inline constexpr std::size_t kGridWidth = 8;
inline constexpr std::size_t kGridHeight = 8;
inline constexpr std::size_t kCellCount = kGridWidth * kGridHeight;

// ── Scales ──────────────────────────────────────────────────────────────────
//
// Ordinals match the canonical table in docs/scales.md (chromatic 0 through
// bebopMinor 23) so host state stays interoperable and the reference document
// can track coverage. Append-only: never insert ahead of an existing row.
//
// Eight intervals per scale, index 7 always the octave, so grid row 7 is one
// octave above row 0.

enum class ScaleId : std::uint32_t {
    chromatic = 0,
    major,
    ionian,
    minor,
    harmonicMinor,
    melodicMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    phrygianDominant,
    neapolitanMajor,
    neapolitanMinor,
    pentMajor,
    pentMinor,
    blues,
    wholeTone,
    altered,
    halfWholeDiminished,
    wholeHalfDiminished,
    bebopDominant,
    bebopMajor,
    bebopMinor,
    count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(ScaleId::count)> kScaleNames = {{
    "Chromatic",
    "Major",
    "Ionian",
    "Minor",
    "Harm Min",
    "Mel Min",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Locrian",
    "Phryg Dom",
    "Neo Maj",
    "Neo Min",
    "Penta Maj",
    "Penta Min",
    "Blues",
    "Whole Tone",
    "Altered",
    "Half-Wh Dim",
    "Wh-Half Dim",
    "Bebop Dom",
    "Bebop Major",
    "Bebop Min",
}};

// A performance-scale subset reachable from the eight side buttons. Indices are
// scale ordinals, not positions in kScaleNames.
inline constexpr std::array<ScaleId, kStringCount> kSideButtonScales = {{
    ScaleId::chromatic,
    ScaleId::major,
    ScaleId::minor,
    ScaleId::dorian,
    ScaleId::mixolydian,
    ScaleId::pentMajor,
    ScaleId::blues,
    ScaleId::wholeTone,
}};

// The notes of one octave, and how many of them the scale actually uses. The
// ladder is built by wrapping with an octave per repetition, so a five-note
// pentatonic and a seven-note diatonic both ascend correctly across all eight
// grid rows. Storing a fixed octave at index 7 instead would make pentatonic,
// blues and whole-tone *descend* at the top row, which is how they were first
// written and what the test suite failed to catch.
inline constexpr std::array<std::array<std::uint8_t, 8>, static_cast<std::size_t>(ScaleId::count)>
    kScaleIntervals = {{
        {{0, 1, 2, 3, 4, 5, 6, 7}},    // chromatic
        {{0, 2, 4, 5, 7, 9, 11, 0}},    // major
        {{0, 2, 4, 5, 7, 9, 11, 0}},    // ionian
        {{0, 2, 3, 5, 7, 8, 10, 0}},    // minor
        {{0, 2, 3, 5, 7, 8, 11, 0}},    // harmonic minor
        {{0, 2, 3, 5, 7, 9, 11, 0}},    // melodic minor
        {{0, 2, 3, 5, 7, 9, 10, 0}},    // dorian
        {{0, 1, 3, 5, 7, 8, 10, 0}},    // phrygian
        {{0, 2, 4, 6, 7, 9, 11, 0}},    // lydian
        {{0, 2, 4, 5, 7, 9, 10, 0}},    // mixolydian
        {{0, 1, 3, 5, 6, 8, 10, 0}},    // locrian
        {{0, 1, 4, 5, 7, 8, 10, 0}},    // phrygian dominant
        {{0, 1, 4, 6, 7, 9, 11, 0}},    // neapolitan major
        {{0, 1, 3, 6, 7, 8, 10, 0}},    // neapolitan minor
        {{0, 2, 4, 7, 9, 0, 0, 0}},     // pentatonic major
        {{0, 3, 5, 7, 10, 0, 0, 0}},    // pentatonic minor
        {{0, 3, 5, 6, 7, 10, 0, 0}},    // blues
        {{0, 2, 4, 6, 8, 10, 0, 0}},    // whole tone
        {{0, 1, 3, 4, 6, 8, 10, 0}},    // altered
        {{0, 1, 3, 4, 6, 7, 9, 0}},     // half-whole diminished
        {{0, 2, 3, 5, 6, 8, 9, 0}},     // whole-half diminished
        {{0, 2, 4, 5, 7, 9, 10, 0}},    // bebop dominant
        {{0, 2, 4, 5, 7, 8, 9, 0}},     // bebop major
        {{0, 2, 3, 4, 5, 7, 9, 0}},     // bebop minor
    }};

// How many entries of kScaleIntervals are real degrees. Everything past this is
// padding and must not be read.
inline constexpr std::array<std::uint8_t, static_cast<std::size_t>(ScaleId::count)> kScaleDegreeCount = {{
    8,  // chromatic
    7,  // major
    7,  // ionian
    7,  // minor
    7,  // harmonic minor
    7,  // melodic minor
    7,  // dorian
    7,  // phrygian
    7,  // lydian
    7,  // mixolydian
    7,  // locrian
    7,  // phrygian dominant
    7,  // neapolitan major
    7,  // neapolitan minor
    5,  // pentatonic major
    5,  // pentatonic minor
    6,  // blues
    6,  // whole tone
    7,  // altered
    7,  // half-whole diminished
    7,  // whole-half diminished
    7,  // bebop dominant
    7,  // bebop major
    7,  // bebop minor
}};

// ── Modulation ──────────────────────────────────────────────────────────────

enum class LfoShape : std::uint32_t {
    triangle = 0,
    sine,
    smoothNoise,
    stepNoise,
    biSquare,
    square,
    sandcastle,
    saw,
    biTrigs,
    trigs,
    envelope,
    count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(LfoShape::count)> kLfoShapeNames = {{
    "Triangle",
    "Sine",
    "Smth Noise",
    "Step Noise",
    "Bi Square",
    "Square",
    "Castle",
    "Saw",
    "Bi Trig",
    "Trig",
    "Env",
}};

enum class ModTarget : std::uint32_t {
    off = 0,
    pitch,
    cutoff,
    morph,
    noise,
    drive,
    count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(ModTarget::count)> kModTargetNames = {{
    "Off",
    "Pitch",
    "Cutoff",
    "Morph",
    "Noise",
    "Drive",
}};

// ── String spread ───────────────────────────────────────────────────────────
//
// Plinky's eight strings are tuned like guitar strings: strum across them and
// you get eight different notes, not eight copies of one. `Spread` is what
// separates a column's tuning from the others.
//
//   unison  every column plays the same ladder (the column is ignored)
//   scale   column c starts c scale degrees higher, so the grid becomes a
//           two-octave scale surface and holding a row plays a cluster
//   fourths column c is c perfect fourths higher, the classic guitar stack
//   fifths  column c is c perfect fifths higher

enum class SpreadId : std::uint32_t {
    unison = 0,
    scale,
    fourths,
    fifths,
    count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(SpreadId::count)> kSpreadNames = {{
    "Unison",
    "Scale",
    "Fourths",
    "Fifths",
}};

inline constexpr int kFourthSemitones = 5;
inline constexpr int kFifthSemitones = 7;

// ── Parameters ──────────────────────────────────────────────────────────────
//
// Host-writable parameters first, then read-only status parameters the UI uses
// to draw each string's live envelope level and the output peak.

enum class ParamId : std::uint32_t {
    // Oscillators
    morph = 0,
    detune,
    interval,
    octave,
    glide,
    drive,
    noise,
    // Filter
    cutoff,
    resonance,
    filterEnv,
    // Amplitude envelope
    envLevel,
    envAttack,
    envDecay,
    envSustain,
    envRelease,
    // Modulation envelope
    modLevel,
    modAttack,
    modDecay,
    modSustain,
    modRelease,
    // Pitch and scale
    root,
    scale,
    spread,
    rotate,
    microtune,
    stride,
    // Modulators
    lfoAFrequency,
    lfoAShape,
    lfoATarget,
    lfoBFrequency,
    lfoBShape,
    lfoBTarget,
    // Effects
    delaySend,
    delayTime,
    delayRatio,
    delayWobble,
    delayFeedback,
    reverbSend,
    reverbTime,
    reverbShimmer,
    reverbWobble,
    width,
    level,
    // Launchpad and host integration
    baseChannel,
    ledFeedback,
    passInput,
    midiThru,
    latch,
    panic,

    // Grid cells. Setting a cell to 1 plucks that string at that scale degree,
    // setting it to 0 releases it. This is how the UI and host automation play
    // strings without a MIDI port, and it mirrors the cell parameters the
    // lifeform plugin exposes.
    cell0 = static_cast<std::uint32_t>(panic) + 1u,
    cell1,
    cell2,
    cell3,
    cell4,
    cell5,
    cell6,
    cell7,
    cell8,
    cell9,
    cell10,
    cell11,
    cell12,
    cell13,
    cell14,
    cell15,
    cell16,
    cell17,
    cell18,
    cell19,
    cell20,
    cell21,
    cell22,
    cell23,
    cell24,
    cell25,
    cell26,
    cell27,
    cell28,
    cell29,
    cell30,
    cell31,
    cell32,
    cell33,
    cell34,
    cell35,
    cell36,
    cell37,
    cell38,
    cell39,
    cell40,
    cell41,
    cell42,
    cell43,
    cell44,
    cell45,
    cell46,
    cell47,
    cell48,
    cell49,
    cell50,
    cell51,
    cell52,
    cell53,
    cell54,
    cell55,
    cell56,
    cell57,
    cell58,
    cell59,
    cell60,
    cell61,
    cell62,
    cell63,

    // Read-only status
    outString0 = static_cast<std::uint32_t>(cell63) + 1u,
    outString1,
    outString2,
    outString3,
    outString4,
    outString5,
    outString6,
    outString7,
    outActiveStrings,
    outPeak,
};

inline constexpr std::size_t kParameterCount = static_cast<std::size_t>(ParamId::outPeak) + 1u;

// Host-writable parameters: everything up to and including panic, plus the
// 64 grid cells that let the UI and automation pluck strings without MIDI.
inline constexpr std::size_t kInputParameterCount = static_cast<std::size_t>(ParamId::cell63) + 1u;

// First grid-cell parameter. Cells are ordered row-major, matching cellIndex().
inline constexpr std::size_t kCellParameterStart = static_cast<std::size_t>(ParamId::cell0);

// First read-only status parameter.
inline constexpr std::size_t kStatusParameterStart = static_cast<std::size_t>(ParamId::outString0);

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"morph", "Morph", "", 0.0f, 1.0f, 0.0f, false, false},
    {"detune", "Detune", "ct", 0.0f, 50.0f, 7.0f, false, false},
    {"interval", "Interval", "st", 0.0f, 12.0f, 12.0f, true, false},
    {"octave", "Octave", "", -2.0f, 3.0f, 0.0f, true, false},
    {"glide", "Glide", "ms", 0.0f, 2000.0f, 0.0f, false, false},
    {"drive", "Drive", "", 0.0f, 1.0f, 0.32f, false, false},
    {"noise", "Noise", "", 0.0f, 1.0f, 0.05f, false, false},

    {"cutoff", "Cutoff", "", 0.02f, 1.0f, 0.62f, false, false},
    {"resonance", "Resonance", "", 0.0f, 1.0f, 0.35f, false, false},
    {"filter_env", "Filter Env", "", 0.0f, 1.0f, 0.35f, false, false},

    {"env_level", "Env Level", "", 0.0f, 1.0f, 0.80f, false, false},
    {"env_attack", "Attack", "ms", 0.5f, 4000.0f, 8.0f, false, false},
    {"env_decay", "Decay", "ms", 5.0f, 4000.0f, 400.0f, false, false},
    {"env_sustain", "Sustain", "", 0.0f, 1.0f, 0.70f, false, false},
    {"env_release", "Release", "ms", 5.0f, 8000.0f, 300.0f, false, false},

    {"mod_level", "Mod Level", "", 0.0f, 1.0f, 0.50f, false, false},
    {"mod_attack", "Mod Atk", "ms", 0.5f, 4000.0f, 120.0f, false, false},
    {"mod_decay", "Mod Dec", "ms", 5.0f, 4000.0f, 600.0f, false, false},
    {"mod_sustain", "Mod Sus", "", 0.0f, 1.0f, 0.30f, false, false},
    {"mod_release", "Mod Rel", "ms", 5.0f, 8000.0f, 300.0f, false, false},

    {"root", "Root", "", 21.0f, 108.0f, 45.0f, true, false},
    {"scale", "Scale", "", 0.0f, static_cast<float>(static_cast<std::size_t>(ScaleId::count) - 1u), 1.0f, true, false},
    {"spread", "Spread", "", 0.0f, static_cast<float>(static_cast<std::size_t>(SpreadId::count) - 1u), 1.0f, true, false},
    {"rotate", "Rotate", "", 0.0f, 7.0f, 0.0f, true, false},
    {"microtune", "Microtune", "ct", 0.0f, 50.0f, 8.0f, false, false},
    {"stride", "Stride", "st", -24.0f, 24.0f, 0.0f, true, false},

    {"lfo_a_freq", "LFO A Rate", "Hz", 0.01f, 40.0f, 1.20f, false, false},
    {"lfo_a_shape", "LFO A Shape", "", 0.0f, static_cast<float>(static_cast<std::size_t>(LfoShape::count) - 1u), 0.0f, true, false},
    {"lfo_a_target", "LFO A Dest", "", 0.0f, static_cast<float>(static_cast<std::size_t>(ModTarget::count) - 1u), 1.0f, true, false},
    {"lfo_b_freq", "LFO B Rate", "Hz", 0.01f, 40.0f, 0.35f, false, false},
    {"lfo_b_shape", "LFO B Shape", "", 0.0f, static_cast<float>(static_cast<std::size_t>(LfoShape::count) - 1u), 0.0f, true, false},
    {"lfo_b_target", "LFO B Dest", "", 0.0f, static_cast<float>(static_cast<std::size_t>(ModTarget::count) - 1u), 2.0f, true, false},

    {"delay_send", "Delay Send", "", 0.0f, 1.0f, 0.18f, false, false},
    {"delay_time", "Delay Time", "", 0.02f, 1.0f, 0.34f, false, false},
    {"delay_ratio", "Delay Ratio", "", 0.25f, 2.0f, 0.50f, false, false},
    {"delay_wobble", "Delay Wobble", "", 0.0f, 1.0f, 0.12f, false, false},
    {"delay_feedback", "Delay Fdbk", "", 0.0f, 0.95f, 0.38f, false, false},
    {"reverb_send", "Reverb Send", "", 0.0f, 1.0f, 0.22f, false, false},
    {"reverb_time", "Reverb Time", "", 0.05f, 1.0f, 0.45f, false, false},
    {"reverb_shimmer", "Shimmer", "", 0.0f, 1.0f, 0.35f, false, false},
    {"reverb_wobble", "Reverb Wobble", "", 0.0f, 1.0f, 0.20f, false, false},
    {"width", "Width", "", 0.0f, 1.0f, 0.55f, false, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.75f, false, false},

    {"base_channel", "Base Ch", "", 1.0f, 16.0f, 4.0f, true, false},
    {"led_feedback", "LED", "", 0.0f, 1.0f, 1.0f, true, false},
    {"pass_input", "Pass", "", 0.0f, 1.0f, 0.0f, true, false},
    {"midi_thru", "MIDI Out", "", 0.0f, 1.0f, 0.0f, true, false},
    {"latch", "Latch", "", 0.0f, 1.0f, 0.0f, true, false},
    {"panic", "Panic", "", 0.0f, 1.0f, 0.0f, true, false},

    {"cell_1", "Cell 1", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 0
    {"cell_2", "Cell 2", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 1
    {"cell_3", "Cell 3", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 2
    {"cell_4", "Cell 4", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 3
    {"cell_5", "Cell 5", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 4
    {"cell_6", "Cell 6", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 5
    {"cell_7", "Cell 7", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 6
    {"cell_8", "Cell 8", "", 0.0f, 1.0f, 0.0f, true, false},  // row 0 col 7
    {"cell_9", "Cell 9", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 0
    {"cell_10", "Cell 10", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 1
    {"cell_11", "Cell 11", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 2
    {"cell_12", "Cell 12", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 3
    {"cell_13", "Cell 13", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 4
    {"cell_14", "Cell 14", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 5
    {"cell_15", "Cell 15", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 6
    {"cell_16", "Cell 16", "", 0.0f, 1.0f, 0.0f, true, false},  // row 1 col 7
    {"cell_17", "Cell 17", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 0
    {"cell_18", "Cell 18", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 1
    {"cell_19", "Cell 19", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 2
    {"cell_20", "Cell 20", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 3
    {"cell_21", "Cell 21", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 4
    {"cell_22", "Cell 22", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 5
    {"cell_23", "Cell 23", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 6
    {"cell_24", "Cell 24", "", 0.0f, 1.0f, 0.0f, true, false},  // row 2 col 7
    {"cell_25", "Cell 25", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 0
    {"cell_26", "Cell 26", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 1
    {"cell_27", "Cell 27", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 2
    {"cell_28", "Cell 28", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 3
    {"cell_29", "Cell 29", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 4
    {"cell_30", "Cell 30", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 5
    {"cell_31", "Cell 31", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 6
    {"cell_32", "Cell 32", "", 0.0f, 1.0f, 0.0f, true, false},  // row 3 col 7
    {"cell_33", "Cell 33", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 0
    {"cell_34", "Cell 34", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 1
    {"cell_35", "Cell 35", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 2
    {"cell_36", "Cell 36", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 3
    {"cell_37", "Cell 37", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 4
    {"cell_38", "Cell 38", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 5
    {"cell_39", "Cell 39", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 6
    {"cell_40", "Cell 40", "", 0.0f, 1.0f, 0.0f, true, false},  // row 4 col 7
    {"cell_41", "Cell 41", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 0
    {"cell_42", "Cell 42", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 1
    {"cell_43", "Cell 43", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 2
    {"cell_44", "Cell 44", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 3
    {"cell_45", "Cell 45", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 4
    {"cell_46", "Cell 46", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 5
    {"cell_47", "Cell 47", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 6
    {"cell_48", "Cell 48", "", 0.0f, 1.0f, 0.0f, true, false},  // row 5 col 7
    {"cell_49", "Cell 49", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 0
    {"cell_50", "Cell 50", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 1
    {"cell_51", "Cell 51", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 2
    {"cell_52", "Cell 52", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 3
    {"cell_53", "Cell 53", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 4
    {"cell_54", "Cell 54", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 5
    {"cell_55", "Cell 55", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 6
    {"cell_56", "Cell 56", "", 0.0f, 1.0f, 0.0f, true, false},  // row 6 col 7
    {"cell_57", "Cell 57", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 0
    {"cell_58", "Cell 58", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 1
    {"cell_59", "Cell 59", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 2
    {"cell_60", "Cell 60", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 3
    {"cell_61", "Cell 61", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 4
    {"cell_62", "Cell 62", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 5
    {"cell_63", "Cell 63", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 6
    {"cell_64", "Cell 64", "", 0.0f, 1.0f, 0.0f, true, false},  // row 7 col 7

    {"out_string_1", "String 1", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_string_2", "String 2", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_string_3", "String 3", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_string_4", "String 4", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_string_5", "String 5", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_string_6", "String 6", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_string_7", "String 7", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_string_8", "String 8", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_active_strings", "Active Strings", "", 0.0f, 8.0f, 0.0f, false, true},
    {"out_peak", "Peak", "", 0.0f, 1.0f, 0.0f, false, true},
}};

[[nodiscard]] constexpr const ParamSpec& getParameterSpec(const std::size_t index) noexcept
{
    return kParameterSpecs[index < kParameterCount ? index : 0u];
}

// ── Launchpad mapping ───────────────────────────────────────────────────────
//
// Novation programmer mode, as documented in docs/launchpad.md and used by the
// lifeform plugin.

inline constexpr std::array<std::uint8_t, 9> kTopButtonCCs = {{
    91, 92, 93, 94, 95, 96, 97, 98, 99,
}};

inline constexpr std::array<std::uint8_t, 8> kSideButtonCCs = {{
    19, 29, 39, 49, 59, 69, 79, 89,
}};

inline constexpr std::uint8_t kLogoCC = 99;

// Top button order, matching kTopButtonCCs.
enum class TopButton : std::size_t {
    octaveDown = 0,
    octaveUp,
    morphDown,
    morphUp,
    latch,
    driveUp,
    resonanceUp,
    ledFeedback,
    panic,
};

// Programmer-mode LED colours, matching lifeform's palette.
inline constexpr std::uint8_t kLedOff = 0;
inline constexpr std::uint8_t kLedDim = 2;
inline constexpr std::uint8_t kLedWhite = 3;
inline constexpr std::uint8_t kLedRed = 5;
inline constexpr std::uint8_t kLedOrange = 9;
inline constexpr std::uint8_t kLedYellow = 14;
inline constexpr std::uint8_t kLedGreen = 21;
inline constexpr std::uint8_t kLedCyan = 37;
inline constexpr std::uint8_t kLedBlue = 45;
inline constexpr std::uint8_t kLedPurple = 53;
inline constexpr std::uint8_t kLedPink = 57;

// ── Ladder maths ────────────────────────────────────────────────────────────
//
// Shared by the core and the UI so the panel's note readout cannot drift from
// what the engine actually plays.

[[nodiscard]] constexpr std::size_t scaleDegreeCount(const std::size_t scale) noexcept
{
    return scale < kScaleDegreeCount.size() ? kScaleDegreeCount[scale] : 7u;
}

// Semitones above the root for a scale degree, wrapping per octave. Negative
// degrees fall below the root rather than wrapping to the top.
[[nodiscard]] constexpr int scaleStepAt(const std::size_t scale, const int degree) noexcept
{
    if (scale >= kScaleIntervals.size())
        return degree;

    const int perOctave = static_cast<int>(kScaleDegreeCount[scale]);
    int octave = degree / perOctave;
    int step = degree % perOctave;
    if (step < 0)
    {
        step += perOctave;
        --octave;
    }

    return static_cast<int>(kScaleIntervals[scale][static_cast<std::size_t>(step)]) + octave * 12;
}

// ── Grid helpers ────────────────────────────────────────────────────────────

[[nodiscard]] constexpr std::uint8_t gridToNote(const std::size_t row, const std::size_t col) noexcept
{
    return row < kGridHeight && col < kGridWidth
        ? static_cast<std::uint8_t>((row + 1u) * 10u + (col + 1u))
        : 0u;
}

[[nodiscard]] constexpr bool noteToGrid(const std::uint8_t note, std::size_t& row, std::size_t& col) noexcept
{
    if (note < 11u || note > 88u)
        return false;

    const auto tens = static_cast<std::uint8_t>(note / 10u);
    const auto ones = static_cast<std::uint8_t>(note % 10u);
    if (tens < 1u || tens > 8u || ones < 1u || ones > 8u)
        return false;

    row = static_cast<std::size_t>(tens - 1u);
    col = static_cast<std::size_t>(ones - 1u);
    return true;
}

[[nodiscard]] constexpr std::size_t cellIndex(const std::size_t row, const std::size_t col) noexcept
{
    return row * kGridWidth + col;
}

}  // namespace downspout::plank
