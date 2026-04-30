#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::drumgen {

inline constexpr int kMinBars = 1;
inline constexpr int kMaxBars = 4;
inline constexpr int kLaneCount = 11;
inline constexpr int kMaxPatternSteps = 128;
inline constexpr int kMaxScheduledMidiEvents = 512;
inline constexpr int kMaxPendingNoteOffs = 96;
inline constexpr int kSafetyGapSamples = 1;
inline constexpr std::int32_t kPatternStateVersion = 2;
inline constexpr std::int32_t kVariationStateVersion = 1;

enum class GenreId : std::int32_t {
    rock = 0,
    disco,
    shuffle,
    electro,
    dub,
    motorik,
    bossa,
    afro,
    count
};

enum class ResolutionId : std::int32_t {
    eighth = 0,
    sixteenth,
    sixteenthTriplet,
    count
};

enum class KitMapId : std::int32_t {
    fluesDrumkit = 0,
    gm,
    count
};

enum class LaneId : std::int32_t {
    kick = 0,
    clap,
    snare,
    crash,
    closedHat,
    lowTom,
    openHat,
    highTom,
    bash,
    cowbell,
    clave
};

enum StepFlag : std::uint8_t {
    kStepFlagAccent = 1 << 0,
    kStepFlagFill = 1 << 1
};

struct Controls {
    GenreId genre = GenreId::rock;
    int channel = 10;
    KitMapId kitMap = KitMapId::fluesDrumkit;
    int bars = 2;
    ResolutionId resolution = ResolutionId::sixteenth;
    float density = 0.58f;
    float variation = 0.35f;
    float fill = 0.30f;
    float vary = 0.0f;
    std::uint32_t seed = 1u;
    float kickAmt = 0.78f;
    float backbeatAmt = 0.76f;
    float hatAmt = 0.82f;
    float auxAmt = 0.28f;
    float tomAmt = 0.30f;
    float metalAmt = 0.26f;
    int actionNew = 0;
    int actionMutate = 0;
    int actionFill = 0;
};

struct DrumStepCell {
    std::uint8_t velocity = 0;
    std::uint8_t flags = 0;
};

struct DrumLaneState {
    std::int32_t midiNote = 0;
    std::array<DrumStepCell, kMaxPatternSteps> steps {};
};

struct PatternState {
    std::int32_t version = kPatternStateVersion;
    std::int32_t bars = 2;
    std::int32_t stepsPerBeat = 4;
    std::int32_t stepsPerBar = 16;
    std::int32_t totalSteps = 0;
    std::int32_t generationSerial = 0;
    ::downspout::Meter meter {};
    std::array<DrumLaneState, kLaneCount> lanes {};
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

struct PendingNoteOff {
    bool active = false;
    int note = 0;
    int channel = 10;
    int remainingSamples = 0;
};

}  // namespace downspout::drumgen
