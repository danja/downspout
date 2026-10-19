#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::qune {

// ── Scales ─────────────────────────────────────────────────────────────────
//
// Canonical 24-scale ordering shared with plank, sprout and markov (see
// docs/scales.md). Append-only: the integer is saved in host projects.

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

inline constexpr std::size_t kScaleCount = static_cast<std::size_t>(ScaleId::count);

inline constexpr std::array<const char*, kScaleCount> kScaleNames = {{
    "Chromatic",
    "Major",
    "Ionian",
    "Minor",
    "Harmonic Minor",
    "Melodic Minor",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Locrian",
    "Phrygian Dominant",
    "Neapolitan Major",
    "Neapolitan Minor",
    "Pentatonic Major",
    "Pentatonic Minor",
    "Blues",
    "Whole Tone",
    "Altered",
    "Half-Whole Dim",
    "Whole-Half Dim",
    "Bebop Dominant",
    "Bebop Major",
    "Bebop Minor",
}};

struct ScaleDef {
    std::uint8_t degrees;
    std::array<std::uint8_t, 12> semitones;
};

// Semitones above the root. Chromatic lists all twelve.
inline constexpr std::array<ScaleDef, kScaleCount> kScaleDefs = {{
    {12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},  // chromatic
    {7, {0, 2, 4, 5, 7, 9, 11}},                   // major
    {7, {0, 2, 4, 5, 7, 9, 11}},                   // ionian
    {7, {0, 2, 3, 5, 7, 8, 10}},                   // minor
    {7, {0, 2, 3, 5, 7, 8, 11}},                   // harmonic minor
    {7, {0, 2, 3, 5, 7, 9, 11}},                   // melodic minor
    {7, {0, 2, 3, 5, 7, 9, 10}},                   // dorian
    {7, {0, 1, 3, 5, 7, 8, 10}},                   // phrygian
    {7, {0, 2, 4, 6, 7, 9, 11}},                   // lydian
    {7, {0, 2, 4, 5, 7, 9, 10}},                   // mixolydian
    {7, {0, 1, 3, 5, 6, 8, 10}},                   // locrian
    {7, {0, 1, 4, 5, 7, 8, 10}},                   // phrygian dominant
    {7, {0, 1, 3, 5, 7, 9, 11}},                   // neapolitan major
    {7, {0, 1, 3, 5, 7, 8, 11}},                   // neapolitan minor
    {5, {0, 2, 4, 7, 9}},                          // pentatonic major
    {5, {0, 3, 5, 7, 10}},                         // pentatonic minor
    {6, {0, 3, 5, 6, 7, 10}},                      // blues
    {6, {0, 2, 4, 6, 8, 10}},                      // whole tone
    {7, {0, 1, 3, 4, 6, 8, 10}},                   // altered
    {8, {0, 1, 3, 4, 6, 7, 9, 10}},                // half-whole diminished
    {8, {0, 2, 3, 5, 6, 8, 9, 11}},                // whole-half diminished
    {8, {0, 2, 4, 5, 7, 9, 10, 11}},               // bebop dominant
    {8, {0, 2, 4, 5, 7, 8, 9, 11}},                // bebop major
    {8, {0, 2, 3, 4, 5, 7, 9, 10}},                // bebop minor
}};

// Bit i set = semitone i above the root is in the scale.
[[nodiscard]] constexpr std::uint16_t scaleMask(const std::size_t scale) noexcept
{
    const ScaleDef& def = kScaleDefs[scale < kScaleCount ? scale : 0u];
    std::uint16_t mask = 0;
    for (std::size_t i = 0; i < def.degrees; ++i)
        mask = static_cast<std::uint16_t>(mask | (1u << def.semitones[i]));
    return mask;
}

// Mask in absolute pitch classes (bit 0 = C), rotated by the root.
[[nodiscard]] constexpr std::uint16_t rotatedScaleMask(const std::size_t scale, const int root) noexcept
{
    const std::uint32_t mask = scaleMask(scale);
    const int r = ((root % 12) + 12) % 12;
    const std::uint32_t rotated = ((mask << r) | (mask >> (12 - r))) & 0xFFFu;
    return static_cast<std::uint16_t>(rotated);
}

// ── Detector ranges ────────────────────────────────────────────────────────

struct PitchRange {
    const char* name;
    float minHz;
    float maxHz;
};

inline constexpr std::size_t kRangeCount = 4;

inline constexpr std::array<PitchRange, kRangeCount> kRanges = {{
    {"Bass", 40.0f, 400.0f},
    {"Voice", 90.0f, 900.0f},
    {"Lead", 160.0f, 1600.0f},
    {"Wide", 40.0f, 1600.0f},
}};

// Absolute limits every range sits inside; buffers are sized for these once.
inline constexpr float kAbsoluteMinHz = 40.0f;
inline constexpr float kAbsoluteMaxHz = 1600.0f;

// ── Enumerations ───────────────────────────────────────────────────────────

enum class Quality : int { fast = 0, good = 1, best = 2 };
inline constexpr std::array<const char*, 3> kQualityNames = {{"Fast", "Good", "Best"}};

enum class FormantMode : int { follow = 0, preserve = 1 };
inline constexpr std::array<const char*, 2> kFormantNames = {{"Follow pitch", "Preserve"}};

enum class Source : int { scale = 0, midiNearest = 1, midiExact = 2 };
inline constexpr std::array<const char*, 3> kSourceNames = {{"Scale", "MIDI chord", "MIDI note"}};

inline constexpr std::array<const char*, 2> kMidiOutNames = {{"Off", "On"}};

inline constexpr std::array<const char*, 2> kNoNotesNames = {{"Bypass", "Use scale"}};

inline constexpr std::array<const char*, 12> kNoteNames = {{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
}};

// 0 = all channels, 1-16 = that channel only.
inline constexpr std::array<const char*, 17> kMidiChannelNames = {{
    "All", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8",
    "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16",
}};

// ── Parameters ─────────────────────────────────────────────────────────────

enum class ParamId : std::uint32_t {
    // Pitch
    root = 0,
    scale,
    source,
    noNotes,
    midiCh,
    bendRange,
    // Correction
    amount,
    glide,
    stickiness,
    transpose,
    // Engine
    quality,
    range,
    voicing,
    formantMode,
    formantShift,
    // Output
    mix,
    level,
    // MIDI output
    midiOut,
    outChannel,
    outVelocity,
    // Read-only status
    outPitch,
    outTarget,
    outShift,
    outLatency,
};

inline constexpr std::size_t kParameterCount = 24;
inline constexpr std::size_t kInputParameterCount = 20;

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
    {"root", "Root", "", 0.0f, 11.0f, 0.0f, true, false},
    {"scale", "Scale", "", 0.0f, static_cast<float>(kScaleCount - 1), 1.0f, true, false},
    {"source", "Target", "", 0.0f, 2.0f, 0.0f, true, false},
    {"no_notes", "No Notes", "", 0.0f, 1.0f, 1.0f, true, false},
    {"midi_ch", "MIDI In", "", 0.0f, 16.0f, 0.0f, true, false},
    {"bend_range", "Bend Range", "st", 0.0f, 24.0f, 2.0f, true, false},

    {"amount", "Amount", "", 0.0f, 1.0f, 1.0f, false, false},
    {"glide", "Glide", "ms", 0.0f, 1000.0f, 20.0f, false, false},
    {"stickiness", "Stickiness", "cents", 0.0f, 50.0f, 15.0f, false, false},
    {"transpose", "Transpose", "st", -24.0f, 24.0f, 0.0f, true, false},

    {"quality", "Quality", "", 0.0f, 2.0f, 1.0f, true, false},
    {"range", "Range", "", 0.0f, 3.0f, 1.0f, true, false},
    {"voicing", "Voicing", "", 0.0f, 1.0f, 0.5f, false, false},
    {"formant_mode", "Formant", "", 0.0f, 1.0f, 1.0f, true, false},
    {"formant_shift", "Formant Shift", "st", -12.0f, 12.0f, 0.0f, false, false},

    {"mix", "Mix", "", 0.0f, 1.0f, 1.0f, false, false},
    {"level", "Level", "dB", -24.0f, 12.0f, 0.0f, false, false},

    {"midi_out", "MIDI Out", "", 0.0f, 1.0f, 1.0f, true, false},
    {"out_channel", "Out Channel", "", 1.0f, 16.0f, 1.0f, true, false},
    {"out_velocity", "Out Velocity", "", 1.0f, 127.0f, 100.0f, true, false},

    {"out_pitch", "Detected", "", 0.0f, 127.0f, 0.0f, false, true},
    {"out_target", "Target Note", "", 0.0f, 127.0f, 0.0f, false, true},
    {"out_shift", "Shift", "st", -48.0f, 48.0f, 0.0f, false, true},
    {"out_latency", "Latency", "ms", 0.0f, 200.0f, 0.0f, false, true},
}};

struct Parameters {
    float root = 0.0f;
    float scale = 1.0f;
    float source = 0.0f;
    float noNotes = 1.0f;
    float midiCh = 0.0f;
    float bendRange = 2.0f;

    float amount = 1.0f;
    float glide = 20.0f;
    float stickiness = 15.0f;
    float transpose = 0.0f;

    float quality = 1.0f;
    float range = 1.0f;
    float voicing = 0.5f;
    float formantMode = 1.0f;
    float formantShift = 0.0f;

    float mix = 1.0f;
    float level = 0.0f;

    float midiOut = 1.0f;
    float outChannel = 1.0f;
    float outVelocity = 100.0f;
};

// Address of each host-writable field, in ParamId order.
inline constexpr std::array<float Parameters::*, kInputParameterCount> kParameterFields = {{
    &Parameters::root,
    &Parameters::scale,
    &Parameters::source,
    &Parameters::noNotes,
    &Parameters::midiCh,
    &Parameters::bendRange,
    &Parameters::amount,
    &Parameters::glide,
    &Parameters::stickiness,
    &Parameters::transpose,
    &Parameters::quality,
    &Parameters::range,
    &Parameters::voicing,
    &Parameters::formantMode,
    &Parameters::formantShift,
    &Parameters::mix,
    &Parameters::level,
    &Parameters::midiOut,
    &Parameters::outChannel,
    &Parameters::outVelocity,
}};

// ── Helpers ────────────────────────────────────────────────────────────────

[[nodiscard]] inline float midiToHz(const float note) noexcept
{
    return 440.0f * std::exp2((note - 69.0f) / 12.0f);
}

[[nodiscard]] inline float hzToMidi(const float hz) noexcept
{
    return 69.0f + 12.0f * std::log2(std::max(hz, 1.0e-3f) / 440.0f);
}

[[nodiscard]] inline int clampInt(const float value, const int lo, const int hi) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), lo, hi);
}

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    const auto clampField = [](float& field, const ParamId id) {
        const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(id)];
        if (!std::isfinite(field))
            field = spec.defaultValue;
        field = std::clamp(field, spec.minimum, spec.maximum);
        if (spec.integer)
            field = std::round(field);
    };
    clampField(p.root, ParamId::root);
    clampField(p.scale, ParamId::scale);
    clampField(p.source, ParamId::source);
    clampField(p.noNotes, ParamId::noNotes);
    clampField(p.midiCh, ParamId::midiCh);
    clampField(p.bendRange, ParamId::bendRange);
    clampField(p.amount, ParamId::amount);
    clampField(p.glide, ParamId::glide);
    clampField(p.stickiness, ParamId::stickiness);
    clampField(p.transpose, ParamId::transpose);
    clampField(p.quality, ParamId::quality);
    clampField(p.range, ParamId::range);
    clampField(p.voicing, ParamId::voicing);
    clampField(p.formantMode, ParamId::formantMode);
    clampField(p.formantShift, ParamId::formantShift);
    clampField(p.mix, ParamId::mix);
    clampField(p.level, ParamId::level);
    clampField(p.midiOut, ParamId::midiOut);
    clampField(p.outChannel, ParamId::outChannel);
    clampField(p.outVelocity, ParamId::outVelocity);
    return p;
}

}  // namespace downspout::qune
