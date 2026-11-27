#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::padseq {

// ── Layout ──────────────────────────────────────────────────────────────────
//
// PadSeq is a paged 8x8 step editor. X is time and Y is note, so a page shows
// eight consecutive steps scanning left to right. A pattern is 1-32 steps
// (default 16), stored as step-major cells: cell(step, note) = step * 8 + note.
//
// Note row 0 is the bottom hardware row, matching docs/launchpad.md.

inline constexpr std::size_t kGridWidth = 8;
inline constexpr std::size_t kGridHeight = 8;
inline constexpr std::size_t kGridCellCount = kGridWidth * kGridHeight;
inline constexpr int kStepsPerPage = 8;
inline constexpr int kMaxPages = 4;
inline constexpr int kMinSteps = 1;
inline constexpr int kMaxSteps = 32;
inline constexpr int kDefaultSteps = 16;
// One boolean parameter per pattern step/column, not merely per visible page.
inline constexpr std::size_t kCellCount = static_cast<std::size_t>(kMaxSteps) * kGridWidth;

// ── Canonical scale list ────────────────────────────────────────────────────
//
// Ordinals are the canonical rows in docs/scales.md, including the generic
// pentatonic row 16. New plugins must not reorder or insert into this list;
// host projects save the integer ordinal.

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
    pentatonic,
    blues,
    wholeTone,
    altered,
    halfWholeDiminished,
    wholeHalfDiminished,
    bebopDominant,
    bebopMajor,
    bebopMinor,
    hirajoshi,
    insen,
    iwato,
    minyo,
    triadMajor,
    triadMinor,
    fifths,
    romanian,
    count,
};

enum class NoteMap : std::uint32_t {
    scale = 0,
    gmDrums,
    count,
};

enum class ClockMode : std::uint32_t {
    autoClock = 0,
    host,
    free,
    count,
};

// LED transport. Note+CC matches Plank but broadcasts channel-1 notes/CCs to
// every MIDI destination; SysEx keeps those colours away from a shared synth.
enum class LedTransport : std::uint32_t {
    off = 0,
    sysex,
    noteCc,
    count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(ScaleId::count)> kScaleNames = {{
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
    "Phryg Dom",
    "Neapolitan Maj",
    "Neapolitan Min",
    "Pentatonic Maj",
    "Pentatonic Min",
    "Pentatonic",
    "Blues",
    "Whole Tone",
    "Altered",
    "Half-Whole Dim",
    "Whole-Half Dim",
    "Bebop Dom",
    "Bebop Major",
    "Bebop Minor",
    "Hirajoshi",
    "Insen",
    "Iwato",
    "Minyo",
    "Triad Maj",
    "Triad Min",
    "Fifths",
    "Romanian",
}};

inline constexpr std::array<const char*, static_cast<std::size_t>(NoteMap::count)> kNoteMapNames = {{
    "Scale",
    "GM Drums",
}};

inline constexpr std::array<const char*, static_cast<std::size_t>(ClockMode::count)> kClockModeNames = {{
    "Auto",
    "Host",
    "Free",
}};

inline constexpr std::array<const char*, static_cast<std::size_t>(LedTransport::count)> kLedTransportNames = {{
    "Off",
    "SysEx",
    "Note+CC",
}};

// GM drum notes (General MIDI level 1): kick, snare, closed/open hat, low/mid/high
// tom, crash. These are MIDI note numbers, not scale degrees.
inline constexpr std::array<std::uint8_t, kGridWidth> kDrumNotes = {{
    36, 38, 42, 46, 45, 48, 50, 49,
}};

inline constexpr std::array<const char*, kGridWidth> kDrumNames = {{
    "Kick", "Snare", "Cl Hat", "Op Hat", "Lo Tom", "Mid Tom", "Hi Tom", "Crash",
}};

// Degrees in each scale. Chromatic is implicit (degree == semitone), so its
// interval row is only a readable placeholder.
inline constexpr std::array<std::uint8_t, static_cast<std::size_t>(ScaleId::count)> kScaleDegreeCount = {{
    12, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 6, 6, 7, 8, 8, 8, 8, 8, 5, 5, 5, 5, 3, 3, 2, 7,
}};

inline constexpr std::array<std::array<std::uint8_t, 12>, static_cast<std::size_t>(ScaleId::count)> kScaleIntervals = {{
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},  // chromatic
    {{0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}},    // major
    {{0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}},    // ionian
    {{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},    // minor
    {{0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}},    // harmonic minor
    {{0, 2, 3, 5, 7, 9, 11, 0, 0, 0, 0, 0}},    // melodic minor
    {{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}},    // dorian
    {{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},    // phrygian
    {{0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0}},    // lydian
    {{0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0}},    // mixolydian
    {{0, 1, 3, 5, 6, 8, 10, 0, 0, 0, 0, 0}},    // locrian
    {{0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0}},    // phrygian dominant
    {{0, 1, 3, 5, 7, 9, 11, 0, 0, 0, 0, 0}},    // neapolitan major
    {{0, 1, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}},    // neapolitan minor
    {{0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0}},     // pentatonic major
    {{0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}},    // pentatonic minor
    {{0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0}},     // pentatonic (generic)
    {{0, 3, 5, 6, 7, 10, 0, 0, 0, 0, 0, 0}},    // blues
    {{0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0}},    // whole tone
    {{0, 1, 3, 4, 6, 8, 10, 0, 0, 0, 0, 0}},    // altered
    {{0, 1, 3, 4, 6, 7, 9, 10, 0, 0, 0, 0}},    // half-whole diminished
    {{0, 2, 3, 5, 6, 8, 9, 11, 0, 0, 0, 0}},    // whole-half diminished
    {{0, 2, 4, 5, 7, 9, 10, 11, 0, 0, 0, 0}},   // bebop dominant
    {{0, 2, 4, 5, 7, 8, 9, 11, 0, 0, 0, 0}},    // bebop major
    {{0, 2, 3, 4, 5, 7, 9, 10, 0, 0, 0, 0}},    // bebop minor
    {{0, 2, 3, 7, 8, 0, 0, 0, 0, 0, 0, 0}},     // hirajoshi
    {{0, 1, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}},    // insen
    {{0, 1, 5, 6, 10, 0, 0, 0, 0, 0, 0, 0}},    // iwato
    {{0, 2, 5, 7, 9, 0, 0, 0, 0, 0, 0, 0}},     // minyo
    {{0, 4, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0}},     // major triad
    {{0, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0}},     // minor triad
    {{0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},     // fifths
    {{0, 2, 3, 6, 7, 9, 10, 0, 0, 0, 0, 0}},    // romanian
}};

[[nodiscard]] constexpr int scaleDegreeCount(const std::size_t scale) noexcept
{
    return scale < kScaleDegreeCount.size() ? static_cast<int>(kScaleDegreeCount[scale]) : 7;
}

// Semitones above the root for a scale degree. Negative degrees fall below the
// root; positive degrees wrap per octave.
[[nodiscard]] constexpr int scaleStepAt(const std::size_t scale, const int degree) noexcept
{
    if (scale >= kScaleIntervals.size() || scale == static_cast<std::size_t>(ScaleId::chromatic))
        return degree;

    const int perOctave = scaleDegreeCount(scale);
    int octave = degree / perOctave;
    int step = degree % perOctave;
    if (step < 0)
    {
        step += perOctave;
        --octave;
    }
    return static_cast<int>(kScaleIntervals[scale][static_cast<std::size_t>(step)]) + octave * 12;
}

// ── Parameters ──────────────────────────────────────────────────────────────

enum class ParamId : std::uint32_t {
    steps = 0,
    scale,
    root,
    noteView,
    page,
    channel,
    noteMap,
    clockMode,
    tempo,
    velocity,
    seed,
    randomise,
    clear,
    ledFeedback,
    passInput,
    cellStart,

    // UI equivalents of the eight Launchpad side triggers. Hardware CCs remain
    // the canonical controls; these parameters let the UI play the same notes.
    triggerStart = cellStart + kCellCount,

    statusStep = triggerStart + kGridWidth,
    statusPage,
    statusScale,
    statusRunning,
    statusNoteView,
    // Read-only mirror of the 8x8 cells on the currently visible page.
    statusCellStart,

    // Appended after all 64 status cells so every existing host index is stable.
    ledTransport = statusCellStart + kGridCellCount,

    count = ledTransport + 1,
};

inline constexpr std::uint32_t kInputParameterCount = static_cast<std::uint32_t>(ParamId::statusStep);
inline constexpr std::uint32_t kParameterCount = static_cast<std::uint32_t>(ParamId::count);

struct ParamRange {
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
    bool boolean;
    bool trigger;
    bool output;
};

[[nodiscard]] constexpr ParamRange rangeFor(const ParamId id) noexcept
{
    const auto triggerBase = static_cast<std::uint32_t>(ParamId::triggerStart);
    const auto rawId = static_cast<std::uint32_t>(id);
    if (rawId >= triggerBase && rawId < triggerBase + static_cast<std::uint32_t>(kGridWidth))
        return {0.0f, 1.0f, 0.0f, true, true, true, false};

    switch (id)
    {
    case ParamId::steps:        return {kMinSteps, kMaxSteps, kDefaultSteps, true, false, false, false};
    case ParamId::scale:        return {0.0f, static_cast<float>(static_cast<int>(ScaleId::count) - 1), 1.0f, true, false, false, false};
    case ParamId::root:         return {0.0f, 127.0f, 60.0f, true, false, false, false};
    case ParamId::noteView:     return {-32.0f, 32.0f, 0.0f, true, false, false, false};
    case ParamId::page:         return {0.0f, kMaxPages - 1.0f, 0.0f, true, false, false, false};
    case ParamId::channel:      return {1.0f, 16.0f, 4.0f, true, false, false, false};
    case ParamId::noteMap:      return {0.0f, static_cast<float>(static_cast<int>(NoteMap::count) - 1), 0.0f, true, false, false, false};
    case ParamId::clockMode:    return {0.0f, static_cast<float>(static_cast<int>(ClockMode::count) - 1), 0.0f, true, false, false, false};
    case ParamId::tempo:        return {40.0f, 240.0f, 120.0f, true, false, false, false};
    case ParamId::velocity:     return {1.0f, 127.0f, 100.0f, true, false, false, false};
    case ParamId::seed:         return {1.0f, 65535.0f, 12345.0f, true, false, false, false};
    case ParamId::randomise:    return {0.0f, 1.0f, 0.0f, true, true, true, false};
    case ParamId::clear:        return {0.0f, 1.0f, 0.0f, true, true, true, false};
    // Master LED switch. `ledTransport` chooses how colours are sent; On by
    // default so a connected Launchpad responds.
    case ParamId::ledFeedback:  return {0.0f, 1.0f, 1.0f, true, true, false, false};
    // Note+CC matches Plank; SysEx is the workaround for a shared MIDI output
    // where a synth must not hear channel-1 grid notes or button CCs.
    case ParamId::ledTransport: return {0.0f, static_cast<float>(static_cast<int>(LedTransport::count) - 1),
                                        static_cast<float>(static_cast<int>(LedTransport::noteCc)),
                                        true, false, false, false};
    case ParamId::passInput:    return {0.0f, 1.0f, 0.0f, true, true, false, false};
    case ParamId::cellStart:    return {0.0f, 1.0f, 0.0f, true, true, false, false};
    case ParamId::statusStep:   return {-1.0f, kMaxSteps - 1.0f, -1.0f, true, false, false, true};
    case ParamId::statusPage:   return {0.0f, kMaxPages - 1.0f, 0.0f, true, false, false, true};
    case ParamId::statusScale:  return {0.0f, static_cast<float>(static_cast<int>(ScaleId::count) - 1), 1.0f, true, false, false, true};
    case ParamId::statusRunning:return {0.0f, 1.0f, 0.0f, true, true, false, true};
    case ParamId::statusNoteView:return {-32.0f, 32.0f, 0.0f, true, false, false, true};
    case ParamId::statusCellStart: return {0.0f, 1.0f, 0.0f, true, true, false, true};
    case ParamId::triggerStart:
    case ParamId::count:        break;
    }
    return {0.0f, 1.0f, 0.0f, false, false, false, false};
}

// ── Launchpad contract ──────────────────────────────────────────────────────

inline constexpr std::array<std::uint8_t, 9> kTopButtonCCs = {{
    91, 92, 93, 94, 95, 96, 97, 98, 99,
}};

// Left to right: note view down/up, page back/forward, scale back/forward,
// randomise, clear/panic, logo (unused).
enum class TopButton : std::uint32_t {
    viewDown = 0,
    viewUp,
    pageBack,
    pageForward,
    scaleBack,
    scaleForward,
    randomise,
    clear,
    logo,
    count,
};

inline constexpr std::array<std::uint8_t, kGridWidth> kSideButtonCCs = {{
    19, 29, 39, 49, 59, 69, 79, 89,
}};

inline constexpr std::uint8_t kLedOff = 0;
inline constexpr std::uint8_t kLedDim = 1;
inline constexpr std::uint8_t kLedWhite = 3;
inline constexpr std::uint8_t kLedRed = 5;
inline constexpr std::uint8_t kLedAmber = 9;
inline constexpr std::uint8_t kLedGreen = 21;
inline constexpr std::uint8_t kLedCyan = 37;
inline constexpr std::uint8_t kLedBlue = 45;
inline constexpr std::uint8_t kLedPurple = 53;
inline constexpr std::uint8_t kLedPink = 57;

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

    const std::uint8_t tens = static_cast<std::uint8_t>(note / 10u);
    const std::uint8_t ones = static_cast<std::uint8_t>(note % 10u);
    if (tens < 1u || tens > 8u || ones < 1u || ones > 8u)
        return false;

    row = static_cast<std::size_t>(tens - 1u);
    col = static_cast<std::size_t>(ones - 1u);
    return true;
}

[[nodiscard]] constexpr std::size_t cellIndex(const std::size_t step, const std::size_t note) noexcept
{
    return step * kGridWidth + note;
}

[[nodiscard]] constexpr int pageCount(const int steps) noexcept
{
    return (steps + kStepsPerPage - 1) / kStepsPerPage;
}

// Keep emitted MIDI in range by whole octaves, preserving pitch class. This
// mirrors Plank's grid behaviour: a high Root or a wide note view wraps down
// rather than clamping every pad to MIDI 127.
[[nodiscard]] constexpr int foldMidi(const int note) noexcept
{
    int folded = note;
    while (folded > 127)
        folded -= 12;
    while (folded < 0)
        folded += 12;
    return folded;
}

} // namespace downspout::padseq
