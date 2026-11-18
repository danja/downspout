#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace downspout::polyscribe {

// The four ways of listening to the input. Each one uses a different note
// selector; the shared range controls narrow its band further.
enum class InputType : int { Melody = 0, Bass, Chords, Drums };

inline constexpr int kInputTypeCount = 4;
inline constexpr std::array<const char*, kInputTypeCount> kInputTypeNames = {{
    "Melody", "Bass", "Chords", "Drums",
}};
inline constexpr std::array<const char*, 2> kOnOffNames = {{"Off", "On"}};

// Drums output on channel 10 by convention, but the user can still choose a
// channel below; the UI explains the convention.
inline constexpr int kDrumChannel = 10;
inline constexpr int kDrumKick = 36;
inline constexpr int kDrumSnare = 38;
inline constexpr int kDrumHat = 42;

enum class ParamId : std::uint32_t {
    inputType = 0,
    sensitivity,
    minNote,
    maxNote,
    maxNotes,
    gate,
    release,
    velocity,
    dynamics,
    transpose,
    channel,
    midiOut,
    audioThru,

    outCount,
    outLastNote,
    outLevel,
    outVelocity,
    outFlash,
    count,
};

inline constexpr std::size_t kInputParameterCount = static_cast<std::size_t>(ParamId::outCount);
inline constexpr std::size_t kParameterCount = static_cast<std::size_t>(ParamId::count);

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
    {"input_type", "Input Type", "", 0.0f, 3.0f, 0.0f, true, false},
    {"sensitivity", "Sensitivity", "", 0.0f, 1.0f, 0.65f, false, false},
    {"min_note", "Low Note", "", 24.0f, 96.0f, 36.0f, true, false},
    {"max_note", "High Note", "", 36.0f, 120.0f, 84.0f, true, false},
    {"max_notes", "Max Notes", "", 1.0f, 8.0f, 4.0f, true, false},
    {"gate", "Gate", "ms", 0.0f, 100.0f, 20.0f, true, false},
    {"release", "Release", "ms", 0.0f, 500.0f, 120.0f, true, false},
    {"velocity", "Velocity", "", 1.0f, 127.0f, 100.0f, true, false},
    {"dynamics", "Dynamics", "", 0.0f, 1.0f, 0.35f, false, false},
    {"transpose", "Transpose", "st", -24.0f, 24.0f, 0.0f, true, false},
    {"channel", "MIDI Channel", "", 1.0f, 16.0f, 1.0f, true, false},
    {"midi_out", "MIDI Out", "", 0.0f, 1.0f, 1.0f, true, false},
    {"audio_thru", "Audio Thru", "", 0.0f, 1.0f, 0.0f, true, false},

    {"out_count", "Notes", "", 0.0f, 8.0f, 0.0f, true, true},
    {"out_last_note", "Last Note", "", 0.0f, 127.0f, 0.0f, true, true},
    {"out_level", "Input Level", "", 0.0f, 1.0f, 0.0f, false, true},
    {"out_velocity", "Last Velocity", "", 0.0f, 127.0f, 0.0f, true, true},
    {"out_flash", "Detection Flash", "", 0.0f, 1.0f, 0.0f, false, true},
}};

struct Parameters {
    float inputType = 0.0f;
    float sensitivity = 0.65f;
    float minNote = 36.0f;
    float maxNote = 84.0f;
    float maxNotes = 4.0f;
    float gate = 20.0f;
    float release = 120.0f;
    float velocity = 100.0f;
    float dynamics = 0.35f;
    float transpose = 0.0f;
    float channel = 1.0f;
    float midiOut = 1.0f;
    float audioThru = 0.0f;
};

// Address of each host-writable field, in ParamId order.
inline constexpr std::array<float Parameters::*, kInputParameterCount> kParameterFields = {{
    &Parameters::inputType,
    &Parameters::sensitivity,
    &Parameters::minNote,
    &Parameters::maxNote,
    &Parameters::maxNotes,
    &Parameters::gate,
    &Parameters::release,
    &Parameters::velocity,
    &Parameters::dynamics,
    &Parameters::transpose,
    &Parameters::channel,
    &Parameters::midiOut,
    &Parameters::audioThru,
}};

[[nodiscard]] inline int clampInt(const float value, const int lo, const int hi) noexcept
{
    if (!std::isfinite(value))
        return lo;
    return std::clamp(static_cast<int>(std::lround(value)), lo, hi);
}

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        const ParamSpec& spec = kParameterSpecs[i];
        float& field = p.*kParameterFields[i];
        if (!std::isfinite(field))
            field = spec.defaultValue;
        field = std::clamp(field, spec.minimum, spec.maximum);
        if (spec.integer)
            field = std::round(field);
    }
    return p;
}

// Input type narrows the search band; the Low/High controls narrow it again.
// Returned values are inclusive MIDI notes, already sorted.
[[nodiscard]] inline std::pair<int, int> preferredRange(const int type) noexcept
{
    switch (static_cast<InputType>(type)) {
    case InputType::Melody: return {48, 84};
    case InputType::Bass:   return {36, 60};
    case InputType::Chords: return {36, 96};
    case InputType::Drums:  return {24, 120};
    default:                return {36, 96};
    }
}

}  // namespace downspout::polyscribe
