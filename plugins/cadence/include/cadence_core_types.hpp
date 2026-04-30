#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::cadence {

inline constexpr int kMaxSegments = 32;
inline constexpr int kMaxChordNotes = 4;
inline constexpr int kMaxCandidates = 108;
inline constexpr int kTimingBins = 8;
inline constexpr int kMaxCompHits = 4;
inline constexpr int kProgressionStateVersion = 1;
inline constexpr int kVariationStateVersion = 1;
inline constexpr int kMaxMidiMessageData = 4;
inline constexpr int kMaxScheduledMidiEvents = 2048;
inline constexpr double kBeatEpsilon = 1e-6;

inline constexpr int CADENCE_MAX_SEGMENTS = kMaxSegments;
inline constexpr int CADENCE_MAX_CHORD_NOTES = kMaxChordNotes;
inline constexpr int CADENCE_MAX_CANDIDATES = kMaxCandidates;
inline constexpr int CADENCE_TIMING_BINS = kTimingBins;
inline constexpr int CADENCE_MAX_COMP_HITS = kMaxCompHits;
inline constexpr int CADENCE_PROGRESSION_STATE_VERSION = kProgressionStateVersion;
inline constexpr int CADENCE_VARIATION_STATE_VERSION = kVariationStateVersion;
inline constexpr double CADENCE_BEAT_EPSILON = kBeatEpsilon;

enum ScaleId {
    SCALE_CHROMATIC = 0,
    SCALE_MAJOR,
    SCALE_NAT_MINOR,
    SCALE_HARM_MINOR,
    SCALE_PENT_MAJOR,
    SCALE_PENT_MINOR,
    SCALE_BLUES,
    SCALE_DORIAN,
    SCALE_MIXOLYDIAN,
    SCALE_PHRYGIAN,
    SCALE_LOCRIAN,
    SCALE_PHRYGIAN_DOMINANT,
    SCALE_COUNT
};

enum GranularityMode {
    GRANULARITY_BEAT = 0,
    GRANULARITY_HALF_BAR,
    GRANULARITY_BAR
};

enum ChordSizeMode {
    CHORD_SIZE_TRIADS = 0,
    CHORD_SIZE_SEVENTHS
};

enum RegisterMode {
    REGISTER_LOW = 0,
    REGISTER_MID,
    REGISTER_HIGH
};

enum SpreadMode {
    SPREAD_CLOSE = 0,
    SPREAD_OPEN,
    SPREAD_DROP2
};

enum QualityId {
    QUALITY_POWER = 0,
    QUALITY_MAJOR,
    QUALITY_MINOR,
    QUALITY_SUS2,
    QUALITY_SUS4,
    QUALITY_DIM,
    QUALITY_DOM7,
    QUALITY_MAJ7,
    QUALITY_MIN7
};

struct Controls {
    int key = 0;
    int scale = SCALE_NAT_MINOR;
    int cycle_bars = 2;
    int granularity = GRANULARITY_HALF_BAR;
    float complexity = 0.45f;
    float movement = 0.65f;
    int chord_size = CHORD_SIZE_TRIADS;
    float note_length = 1.0f;
    int reg = REGISTER_MID;
    int spread = SPREAD_CLOSE;
    bool pass_input = true;
    int output_channel = 0;
    int action_learn = 0;
    float vary = 0.0f;
    float comp = 0.0f;
};

struct SegmentCapture {
    std::array<double, 12> duration {};
    std::array<double, 12> onset {};
    std::array<double, kTimingBins> timing_bins {};
    double onset_total = 0.0;
};

struct ChordSlot {
    bool valid = false;
    std::uint8_t root_pc = 0;
    std::uint8_t quality = 0;
    std::uint8_t note_count = 0;
    std::uint8_t velocity = 96;
    std::array<std::uint8_t, kMaxChordNotes> notes {};
};

struct ProgressionState {
    int version = kProgressionStateVersion;
    int segmentCount = 0;
    bool ready = false;
    std::array<ChordSlot, kMaxSegments> slots {};
};

struct VariationState {
    int version = kVariationStateVersion;
    std::int64_t completed_cycles = 0;
    std::int64_t last_mutation_cycle = 0;
    std::int32_t mutation_serial = 0;
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

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, kMaxMidiMessageData> data {};
};

using InputMidiEvent = MidiMessage;
using ScheduledMidiEvent = MidiMessage;

[[nodiscard]] inline constexpr Controls defaultControls() noexcept
{
    return Controls {};
}

[[nodiscard]] inline constexpr VariationState defaultVariationState() noexcept
{
    return VariationState {};
}

[[nodiscard]] inline int clampi(const int value, const int minValue, const int maxValue) noexcept
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] inline float clampf(const float value, const float minValue, const float maxValue) noexcept
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] inline double clampd(const double value, const double minValue, const double maxValue) noexcept
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] inline Controls clampControls(const Controls& raw) noexcept
{
    Controls controls = raw;
    controls.key = clampi(controls.key, 0, 11);
    controls.scale = clampi(controls.scale, 0, SCALE_COUNT - 1);
    controls.cycle_bars = clampi(controls.cycle_bars, 1, 8);
    controls.granularity = clampi(controls.granularity, 0, 2);
    controls.complexity = clampf(controls.complexity, 0.0f, 1.0f);
    controls.movement = clampf(controls.movement, 0.0f, 1.0f);
    controls.chord_size = clampi(controls.chord_size, 0, 1);
    controls.note_length = clampf(controls.note_length, 0.10f, 1.0f);
    controls.reg = clampi(controls.reg, 0, 2);
    controls.spread = clampi(controls.spread, 0, 2);
    controls.output_channel = clampi(controls.output_channel, 0, 16);
    controls.action_learn = clampi(controls.action_learn, 0, 1048576);
    controls.vary = clampf(controls.vary, 0.0f, 1.0f);
    controls.comp = clampf(controls.comp, 0.0f, 1.0f);
    return controls;
}

[[nodiscard]] inline bool harmonyControlsMatch(const Controls& a, const Controls& b) noexcept
{
    return a.key == b.key &&
           a.scale == b.scale &&
           a.cycle_bars == b.cycle_bars &&
           a.granularity == b.granularity &&
           (a.complexity - b.complexity < 0.0001f && a.complexity - b.complexity > -0.0001f) &&
           (a.movement - b.movement < 0.0001f && a.movement - b.movement > -0.0001f) &&
           a.chord_size == b.chord_size &&
           a.reg == b.reg &&
           a.spread == b.spread;
}

}  // namespace downspout::cadence
