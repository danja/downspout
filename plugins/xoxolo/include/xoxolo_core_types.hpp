#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::xoxolo {

inline constexpr int kDownspoutLaneCount = 11;
inline constexpr int kAvlDrumkitsLaneCount = 29;
inline constexpr int kLaneCount = kAvlDrumkitsLaneCount;
inline constexpr int kMinSteps = 8;
inline constexpr int kDefaultSteps = 16;
inline constexpr int kMaxSteps = 32;
inline constexpr int kMinBars = 1;
inline constexpr int kMaxBars = 4;
inline constexpr int kMaxScheduledMidiEvents = 96;
inline constexpr int kMaxPendingNoteOffs = 64;
inline constexpr int kPatternStateVersion = 3;

enum class ResolutionId : std::int32_t {
    quarter = 0,
    eighth,
    sixteenth,
    count
};

enum class NotePresetId : std::int32_t {
    downspout = 0,
    avlDrumkits,
    count
};

struct LaneSpec {
    const char* name;
    int note;
};

inline constexpr std::array<LaneSpec, kLaneCount> kDownspoutLanes {{
    {"Kick", 36},
    {"Clap", 39},
    {"Snare", 40},
    {"Crash", 41},
    {"Closed HH", 42},
    {"Low Tom", 45},
    {"Open HH", 46},
    {"High Tom", 50},
    {"Bash", 51},
    {"Cowbell", 52},
    {"Clave", 53},
    {"SnareSidestk", 37},
    {"SnareCtr", 38},
    {"SnareEdge", 40},
    {"FloorTomCtr", 41},
    {"FloorTomEdge", 43},
    {"PedalHat", 44},
    {"TomCtr", 45},
    {"SemiHat", 46},
    {"TomEdge", 47},
    {"SwishHat", 48},
    {"Crash1", 49},
    {"Crash1Chk", 50},
    {"RideTip", 51},
    {"RideChk", 52},
    {"RideBell", 53},
    {"Tambourine", 54},
    {"Splash", 55},
    {"Crash2", 57},
}};

inline constexpr std::array<LaneSpec, kLaneCount> kAvlDrumkitsLanes {{
    {"KickDrum", 36},
    {"SnareSidestk", 37},
    {"SnareCtr", 38},
    {"HandClap", 39},
    {"SnareEdge", 40},
    {"FloorTomCtr", 41},
    {"ClosedHat", 42},
    {"FloorTomEdge", 43},
    {"PedalHat", 44},
    {"TomCtr", 45},
    {"SemiHat", 46},
    {"TomEdge", 47},
    {"SwishHat", 48},
    {"Crash1", 49},
    {"Crash1Chk", 50},
    {"RideTip", 51},
    {"RideChk", 52},
    {"RideBell", 53},
    {"Tambourine", 54},
    {"Splash", 55},
    {"Cowbell", 56},
    {"Crash2", 57},
    {"Crash2Chk", 58},
    {"RideShank", 59},
    {"Crash3", 60},
    {"HiRoto", 61},
    {"MidRoto", 62},
    {"LoRoto", 63},
    {"Maracas", 64},
}};

inline constexpr const std::array<LaneSpec, kLaneCount>& kDefaultLanes = kDownspoutLanes;

struct Controls {
    int steps = kDefaultSteps;
    ResolutionId resolution = ResolutionId::sixteenth;
    int channel = 10;
    NotePresetId notePreset = NotePresetId::downspout;
    int clearSerial = 0;
    int previewLane = 0;
    int previewSerial = 0;
};

struct LaneState {
    std::int32_t midiNote = 0;
    std::array<std::uint8_t, kMaxSteps> steps {};
};

struct PatternState {
    std::int32_t version = kPatternStateVersion;
    std::int32_t bars = 1;
    ResolutionId resolution = ResolutionId::sixteenth;
    std::int32_t channel = 10;
    NotePresetId notePreset = NotePresetId::downspout;
    std::int32_t stepsPerBeat = 4;
    std::int32_t stepsPerBar = 16;
    std::int32_t totalSteps = kDefaultSteps;
    ::downspout::Meter meter {};
    std::array<LaneState, kLaneCount> lanes {};
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

}  // namespace downspout::xoxolo
