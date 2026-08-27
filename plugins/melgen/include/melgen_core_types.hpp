#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::melgen {

inline constexpr int kMinLengthBeats = 4;
inline constexpr int kMaxLengthBeats = 64;
inline constexpr int kMaxPatternSteps = 384;
inline constexpr int kMaxEvents = 256;
inline constexpr int kMaxPhrases = 16;
inline constexpr int kMaxMidiMessageData = 4;
inline constexpr int kMaxInputMidiEvents = 512;
inline constexpr int kSafetyGapSamples = 1;
inline constexpr int kMaxScheduledMidiEvents = (kMaxEvents * 2) + 16;
inline constexpr std::int32_t kPatternStateVersion = 1;
inline constexpr std::int32_t kVariationStateVersion = 1;

enum class ScaleId : std::int32_t {
    major = 0,
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
    count
};

enum class SubdivisionId : std::int32_t {
    eighth = 0,
    sixteenth,
    sixteenthTriplet,
    quarter,
    count
};

enum class PeriodId : std::int32_t {
    free = 0,
    aa,
    ab,
    aaPrime,
    callAnswer,
    aba,
    count
};

enum class ContourId : std::int32_t {
    wandering = 0,
    flat,
    rising,
    falling,
    arch,
    invertedArch,
    count
};

enum class AnswerId : std::int32_t {
    related = 0,
    same,
    transpose,
    invert,
    compress,
    expand,
    count
};

enum class PhraseRole : std::int32_t {
    free = 0,
    call,
    answer,
    repeat,
    variation,
    contrast,
    cadence
};

struct Controls {
    int rootNote = 60;
    ScaleId scale = ScaleId::major;
    int channel = 1;
    int lengthBeats = 16;
    int phraseLengthBars = 2;
    SubdivisionId subdivision = SubdivisionId::sixteenth;
    PeriodId period = PeriodId::callAnswer;
    ContourId contour = ContourId::arch;
    AnswerId answer = AnswerId::related;
    float density = 0.48f;
    int reg = 1;
    float hold = 0.42f;
    float accent = 0.45f;
    float structure = 0.62f;
    float range = 0.45f;
    float leap = 0.28f;
    float rest = 0.24f;
    float cadence = 0.55f;
    float color = 0.5f;
    float vary = 0.0f;
    float follow = 0.0f;
    std::uint32_t seed = 1u;
    int actionNew = 0;
    int actionNotes = 0;
    int actionRhythm = 0;
};

struct NoteEvent {
    std::int32_t startStep = 0;
    std::int32_t durationSteps = 0;
    std::int32_t note = 0;
    std::int32_t velocity = 0;
};

struct PhraseInfo {
    std::int32_t startStep = 0;
    std::int32_t lengthSteps = 0;
    PhraseRole role = PhraseRole::free;
    std::int32_t sourcePhrase = -1;
};

struct PatternState {
    std::int32_t version = kPatternStateVersion;
    std::int32_t patternSteps = 0;
    std::int32_t stepsPerBeat = 4;
    std::int32_t stepsPerBar = 16;
    std::int32_t eventCount = 0;
    std::int32_t phraseCount = 0;
    std::int32_t generationSerial = 0;
    ::downspout::Meter meter {};
    std::array<NoteEvent, kMaxEvents> events {};
    std::array<PhraseInfo, kMaxPhrases> phrases {};
};

struct VariationState {
    std::int32_t version = kVariationStateVersion;
    std::int64_t completedLoops = 0;
    std::int64_t lastMutationLoop = 0;
};

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double beatType = 4.0;
    double bpm = 120.0;
    ::downspout::Meter meter {};
};

struct InputMidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, kMaxMidiMessageData> data {};
};

enum class MidiEventType : std::uint8_t {
    noteOff = 0,
    noteOn
};

struct ScheduledMidiEvent {
    MidiEventType type = MidiEventType::noteOn;
    std::uint32_t frame = 0;
    std::uint8_t channel = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

}  // namespace downspout::melgen
