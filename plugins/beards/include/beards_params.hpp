#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::beards {

// Beards is a port of the Mutable Instruments Beads granular processor
// (MIT, Emilie Gillet). Parameter meanings follow the module; see docs/design.md
// for what was and was not carried over.

enum class ParamId : std::uint32_t {
    position = 0,   // Time: where in the buffer grains are read from
    size,           // Size: grain duration (below 1/3 reads backwards)
    shape,          // Shape: grain envelope
    pitch,          // Pitch: grain transposition, semitones
    density,        // Density: grain rate / randomness
    feedback,
    dryWet,
    reverb,
    freeze,
    timeRand,       // Position scatter (Beads: Time attenuverter, no CV)
    sizeRand,
    shapeRand,
    pitchRand,
    quality,        // 0 Digital, 1 Vintage (12 bit), 2 Tape (8 bit mu-law)
    grain,          // momentary: fire one grain now
    midiCh,         // Drift / controller CC channel (0 off, 1-16, 17 all)
    conductorCh,    // Conductor CC channel (0 off, 1-16)
    // Read-only status
    outGrains,
};

inline constexpr std::size_t kParameterCount = 18;
inline constexpr std::size_t kInputParameterCount = 17;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"position", "Position", "", 0.0f, 1.0f, 0.5f, false, false},
    {"size", "Size", "", 0.0f, 1.0f, 0.5f, false, false},
    {"shape", "Shape", "", 0.0f, 1.0f, 0.5f, false, false},
    {"pitch", "Pitch", "st", -24.0f, 24.0f, 0.0f, false, false},
    // 0.48-0.52 is a dead zone with no automatic grains (use the Grain button).
    {"density", "Density", "", 0.0f, 1.0f, 0.75f, false, false},
    {"feedback", "Feedback", "", 0.0f, 1.0f, 0.0f, false, false},
    {"dry_wet", "Dry/Wet", "", 0.0f, 1.0f, 0.5f, false, false},
    {"reverb", "Reverb", "", 0.0f, 1.0f, 0.0f, false, false},
    {"freeze", "Freeze", "", 0.0f, 1.0f, 0.0f, true, false},
    {"time_rand", "Position scatter", "", 0.0f, 1.0f, 0.0f, false, false},
    {"size_rand", "Size scatter", "", 0.0f, 1.0f, 0.0f, false, false},
    {"shape_rand", "Shape scatter", "", 0.0f, 1.0f, 0.0f, false, false},
    {"pitch_rand", "Pitch scatter", "", 0.0f, 1.0f, 0.0f, false, false},
    {"quality", "Quality", "", 0.0f, 2.0f, 0.0f, true, false},
    {"grain", "Grain", "", 0.0f, 1.0f, 0.0f, true, false},
    {"midi_ch", "MIDI in", "", 0.0f, 17.0f, 0.0f, true, false},
    {"conductor_ch", "Conductor", "", 0.0f, 16.0f, 0.0f, true, false},
    {"out_grains", "Active grains", "", 0.0f, 32.0f, 0.0f, true, true},
}};

inline constexpr std::array<const char*, 3> kQualityNames = {"Digital", "Vintage", "Tape"};

inline constexpr std::array<const char*, 18> kMidiChannelNames = {
    "Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "All"};

struct Parameters {
    std::array<float, kInputParameterCount> value {};

    Parameters()
    {
        for (std::size_t i = 0; i < kInputParameterCount; ++i)
            value[i] = kParameterSpecs[i].defaultValue;
    }

    float& operator[](const ParamId id) { return value[static_cast<std::size_t>(id)]; }
    float operator[](const ParamId id) const { return value[static_cast<std::size_t>(id)]; }
};

[[nodiscard]] inline float clampParameter(const ParamId id, const float v)
{
    const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(id)];
    float out = std::isfinite(v) ? v : spec.defaultValue;
    out = std::clamp(out, spec.minimum, spec.maximum);
    return spec.integer ? std::round(out) : out;
}

inline void setParameter(Parameters& p, const ParamId id, const float v)
{
    p[id] = clampParameter(id, v);
}

[[nodiscard]] inline Parameters clampParameters(Parameters p)
{
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        p.value[i] = clampParameter(static_cast<ParamId>(i), p.value[i]);
    return p;
}

// ── MIDI control ───────────────────────────────────────────────────────────
//
// Controller map on the MIDI-in channel. CC 1-4 are the four most useful
// controls because Drift emits those numbers from its lanes by default, so
// routing Drift into Beards works with no configuration.
//
// Conductor's CCs (21-24) arrive on the separate Conductor channel.

inline constexpr int kFreezeCc = 64;
inline constexpr int kConductorDensityCc = 21;
inline constexpr int kConductorEnergyCc = 22;
inline constexpr int kConductorMutationCc = 23;
inline constexpr int kConductorResetCc = 24;

struct CcTarget {
    int controller;
    ParamId param;
};

inline constexpr std::array<CcTarget, 12> kControllerMap = {{
    {1, ParamId::position},
    {2, ParamId::size},
    {3, ParamId::density},
    {4, ParamId::dryWet},
    {5, ParamId::shape},
    {6, ParamId::pitch},
    {7, ParamId::feedback},
    {8, ParamId::reverb},
    {9, ParamId::pitchRand},
    {10, ParamId::timeRand},
    {11, ParamId::sizeRand},
    {12, ParamId::shapeRand},
}};

[[nodiscard]] inline float controllerToParameter(const ParamId id, const int ccValue)
{
    const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(id)];
    const float unit = static_cast<float>(std::clamp(ccValue, 0, 127)) / 127.0f;
    return clampParameter(id, spec.minimum + unit * (spec.maximum - spec.minimum));
}

}  // namespace downspout::beards
