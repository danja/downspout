#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::bassgen {

inline constexpr int kMinLengthBeats = 8;
inline constexpr int kMaxLengthBeats = 32;
inline constexpr int kMaxPatternSteps = 192;
inline constexpr int kMaxEvents = 192;
inline constexpr int kSafetyGapSamples = 1;
inline constexpr int kMaxScheduledMidiEvents = (kMaxEvents * 2) + 16;
inline constexpr std::int32_t kPatternStateVersion = 2;
inline constexpr std::int32_t kVariationStateVersion = 1;

enum class ScaleId : std::int32_t {
    minor = 0,
    major,
    dorian,
    phrygian,
    pentMinor,
    blues,
    mixolydian,
    harmonicMinor,
    pentMajor,
    locrian,
    phrygianDominant,
    count
};

enum class GenreId : std::int32_t {
    techno = 0,
    acid,
    house,
    electro,
    dub,
    ambient,
    funk,
    sabbath,
    count
};

enum class SubdivisionId : std::int32_t {
    eighth = 0,
    sixteenth,
    sixteenthTriplet,
    count
};

enum class StyleModeId : std::int32_t {
    autoMode = 0,
    straight,
    reel,
    waltz,
    jig,
    slipJig,
    count
};

struct Controls {
    int rootNote = 36;
    ScaleId scale = ScaleId::minor;
    GenreId genre = GenreId::techno;
    StyleModeId styleMode = StyleModeId::autoMode;
    int channel = 1;
    int lengthBeats = 16;
    SubdivisionId subdivision = SubdivisionId::sixteenth;
    float density = 0.45f;
    int reg = 1;
    float hold = 0.35f;
    float accent = 0.45f;
    float vary = 0.0f;
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

struct PatternState {
    std::int32_t version = kPatternStateVersion;
    std::int32_t patternSteps = 0;
    std::int32_t stepsPerBeat = 4;
    std::int32_t stepsPerBar = 16;
    std::int32_t eventCount = 0;
    std::int32_t generationSerial = 0;
    ::downspout::Meter meter {};
    std::array<NoteEvent, kMaxEvents> events {};
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

}  // namespace downspout::bassgen
