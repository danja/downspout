#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::xoxolo {

inline constexpr int kLaneCount = 11;
inline constexpr int kMaxSteps = 32;
inline constexpr int kMinBars = 1;
inline constexpr int kMaxBars = 4;
inline constexpr int kMaxScheduledMidiEvents = 96;
inline constexpr int kMaxPendingNoteOffs = 64;
inline constexpr int kPatternStateVersion = 1;

enum class ResolutionId : std::int32_t {
    quarter = 0,
    eighth,
    sixteenth,
    count
};

struct LaneSpec {
    const char* name;
    int note;
};

inline constexpr std::array<LaneSpec, kLaneCount> kDefaultLanes {{
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
}};

struct Controls {
    int bars = 1;
    ResolutionId resolution = ResolutionId::sixteenth;
    int channel = 10;
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
    std::int32_t stepsPerBeat = 4;
    std::int32_t stepsPerBar = 16;
    std::int32_t totalSteps = 16;
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
