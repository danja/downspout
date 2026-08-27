#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::counterpointer {

inline constexpr int kMaxSegments = 32;
inline constexpr int kTimingBins = 8;
inline constexpr int kMaxMidiMessageData = 4;
inline constexpr int kMaxScheduledMidiEvents = 2048;
inline constexpr int kMaxHitsPerSegment = 3;
inline constexpr int kPhraseStateVersion = 1;
inline constexpr int kVariationStateVersion = 1;
inline constexpr double kBeatEpsilon = 1e-6;

enum ScaleId {
    SCALE_CHROMATIC = 0,
    SCALE_MAJOR,
    SCALE_IONIAN,
    SCALE_NAT_MINOR,
    SCALE_HARM_MINOR,
    SCALE_MELODIC_MINOR,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_LOCRIAN,
    SCALE_PHRYGIAN_DOMINANT,
    SCALE_NEO_MAJOR,
    SCALE_NEO_MINOR,
    SCALE_PENT_MAJOR,
    SCALE_PENT_MINOR,
    SCALE_BLUES,
    SCALE_WHOLE_TONE,
    SCALE_ALTERED,
    SCALE_HALF_WHOLE_DIMINISHED,
    SCALE_WHOLE_HALF_DIMINISHED,
    SCALE_BEBOP_DOMINANT,
    SCALE_BEBOP_MAJOR,
    SCALE_BEBOP_MINOR,
    SCALE_COUNT
};

enum GranularityMode {
    GRANULARITY_BEAT = 0,
    GRANULARITY_HALF_BAR,
    GRANULARITY_BAR
};

enum RegisterMode {
    REGISTER_LOW = 0,
    REGISTER_MID,
    REGISTER_HIGH
};

enum ResponseMode {
    RESPONSE_COUNTERPOINT = 0,
    RESPONSE_BASS_DESCEND,
    RESPONSE_COUNT
};

struct Controls {
    int key = 0;
    int scale = SCALE_NAT_MINOR;
    int cycle_bars = 2;
    int granularity = GRANULARITY_BEAT;
    float follow = 0.55f;
    float counter = 0.55f;
    float short_random = 0.15f;
    float long_random = 0.0f;
    float density = 0.78f;
    float rhythm_follow = 0.65f;
    float syncopation = 0.25f;
    float consonance = 0.75f;
    float color = 0.0f;
    float embellish = 0.25f;
    float regularity = 0.65f;
    int reg = REGISTER_MID;
    float span = 0.55f;
    float gate = 0.72f;
    float velocity_follow = 0.65f;
    bool pass_input = true;
    int output_channel = 0;
    int action_learn = 0;
    bool freeze = false;
    int response_mode = RESPONSE_COUNTERPOINT;
};

struct SegmentCapture {
    double onset_weight = 0.0;
    double note_sum = 0.0;
    double velocity_sum = 0.0;
    std::array<double, 12> duration {};
    std::array<double, kTimingBins> timing_bins {};
};

struct PhraseHit {
    bool active = false;
    std::uint8_t note = 60;
    std::uint8_t velocity = 96;
    double onset = 0.0;
    double gate = 0.7;
};

struct PhraseStep {
    bool active = false;
    std::uint8_t note = 60;
    std::uint8_t velocity = 96;
    double onset = 0.0;
    double gate = 0.7;
    int hitCount = 0;
    std::array<PhraseHit, kMaxHitsPerSegment> hits {};
};

struct PhraseState {
    int version = kPhraseStateVersion;
    int segmentCount = 0;
    bool ready = false;
    std::array<PhraseStep, kMaxSegments> steps {};
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
    controls.follow = clampf(controls.follow, 0.0f, 1.0f);
    controls.counter = clampf(controls.counter, 0.0f, 1.0f);
    controls.short_random = clampf(controls.short_random, 0.0f, 1.0f);
    controls.long_random = clampf(controls.long_random, 0.0f, 1.0f);
    controls.density = clampf(controls.density, 0.0f, 1.0f);
    controls.rhythm_follow = clampf(controls.rhythm_follow, 0.0f, 1.0f);
    controls.syncopation = clampf(controls.syncopation, 0.0f, 1.0f);
    controls.consonance = clampf(controls.consonance, 0.0f, 1.0f);
    controls.color = clampf(controls.color, 0.0f, 1.0f);
    controls.embellish = clampf(controls.embellish, 0.0f, 1.0f);
    controls.regularity = clampf(controls.regularity, 0.0f, 1.0f);
    controls.reg = clampi(controls.reg, 0, 2);
    controls.span = clampf(controls.span, 0.0f, 1.0f);
    controls.gate = clampf(controls.gate, 0.10f, 1.0f);
    controls.velocity_follow = clampf(controls.velocity_follow, 0.0f, 1.0f);
    controls.output_channel = clampi(controls.output_channel, 0, 16);
    controls.action_learn = clampi(controls.action_learn, 0, 1048576);
    controls.response_mode = clampi(controls.response_mode, 0, RESPONSE_COUNT - 1);
    return controls;
}

[[nodiscard]] inline bool phraseControlsMatch(const Controls& a, const Controls& b) noexcept
{
    return a.key == b.key &&
           a.scale == b.scale &&
           a.cycle_bars == b.cycle_bars &&
           a.granularity == b.granularity &&
           a.reg == b.reg &&
           (a.follow - b.follow < 0.0001f && a.follow - b.follow > -0.0001f) &&
           (a.counter - b.counter < 0.0001f && a.counter - b.counter > -0.0001f) &&
           (a.short_random - b.short_random < 0.0001f && a.short_random - b.short_random > -0.0001f) &&
           (a.density - b.density < 0.0001f && a.density - b.density > -0.0001f) &&
           (a.rhythm_follow - b.rhythm_follow < 0.0001f && a.rhythm_follow - b.rhythm_follow > -0.0001f) &&
           (a.syncopation - b.syncopation < 0.0001f && a.syncopation - b.syncopation > -0.0001f) &&
           (a.consonance - b.consonance < 0.0001f && a.consonance - b.consonance > -0.0001f) &&
           (a.color - b.color < 0.0001f && a.color - b.color > -0.0001f) &&
           (a.embellish - b.embellish < 0.0001f && a.embellish - b.embellish > -0.0001f) &&
           (a.regularity - b.regularity < 0.0001f && a.regularity - b.regularity > -0.0001f) &&
           (a.span - b.span < 0.0001f && a.span - b.span > -0.0001f) &&
           (a.gate - b.gate < 0.0001f && a.gate - b.gate > -0.0001f) &&
           (a.velocity_follow - b.velocity_follow < 0.0001f && a.velocity_follow - b.velocity_follow > -0.0001f) &&
           a.response_mode == b.response_mode;
}

}  // namespace downspout::counterpointer
