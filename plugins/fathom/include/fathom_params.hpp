#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::fathom {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// Twenty-one host-writable parameters followed by one read-only status
// parameter. The signal path is generator -> filter -> envelope -> processor, the
// shape of the Vhikk X algorithm reference this plugin is modelled on: a
// generator of clustered oscillators, a notch filter, then a delay structure with
// a frequency shifter inside its feedback loop.

enum class ParamId : std::uint32_t {
    // Generator
    algorithm = 0,
    warp,
    span,
    morph,
    seed,
    fuse,
    // Filter
    field,
    notch,
    // Processor
    processor,
    time,
    form,
    feed,
    shift,
    mix,
    // Voices and output
    basis,
    glide,
    attack,
    release,
    hold,
    level,
    midiCh,
    // Read-only status
    outVoices,
};

inline constexpr std::size_t kParameterCount = 22;
inline constexpr std::size_t kInputParameterCount = 21;
inline constexpr std::size_t kVoiceCount = 4;
inline constexpr int kAlgorithmCount = 5;

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
    {"algorithm", "Algorithm", "", 0.0f, 4.0f, 2.0f, true, false},
    {"warp", "Warp", "", 0.0f, 1.0f, 0.3f, false, false},
    {"span", "Span", "", 0.0f, 1.0f, 0.25f, false, false},
    {"morph", "Morph", "", 0.0f, 1.0f, 0.3f, false, false},
    {"seed", "Seed", "", 0.0f, 1.0f, 0.5f, false, false},
    {"fuse", "Fuse", "", 0.0f, 1.0f, 0.0f, false, false},

    {"field", "Field", "", 0.0f, 1.0f, 1.0f, false, false},
    {"notch", "Notch", "", 0.0f, 1.0f, 0.0f, false, false},

    {"processor", "Processor", "", 0.0f, 1.0f, 0.0f, true, false},
    {"time", "Time", "", 0.0f, 1.0f, 0.4f, false, false},
    {"form", "Form", "", 0.0f, 1.0f, 0.6f, false, false},
    {"feed", "Feed", "", 0.0f, 1.0f, 0.5f, false, false},
    {"shift", "Shift", "", -1.0f, 1.0f, 0.0f, false, false},
    {"mix", "Mix", "", 0.0f, 1.0f, 0.35f, false, false},

    {"basis", "Basis", "st", -24.0f, 24.0f, 0.0f, true, false},
    {"glide", "Glide", "ms", 0.0f, 2000.0f, 0.0f, false, false},
    {"attack", "Attack", "ms", 5.0f, 5000.0f, 800.0f, false, false},
    {"release", "Release", "ms", 10.0f, 10000.0f, 3000.0f, false, false},
    {"hold", "Hold", "", 0.0f, 1.0f, 0.0f, true, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.6f, false, false},
    {"midi_ch", "MIDI In", "", 0.0f, 17.0f, 17.0f, true, false},

    {"out_voices", "Active Voices", "", 0.0f, 4.0f, 0.0f, true, true},
}};

inline constexpr std::array<const char*, kAlgorithmCount> kAlgorithmNames = {{
    "Analog", "Shaper", "Sines", "Table", "Cluster",
}};

inline constexpr std::array<const char*, 2> kProcessorNames = {{"Metallic", "Diffuse"}};

// 0 = Off, 1-16 = that MIDI channel only, 17 = all channels.
inline constexpr std::array<const char*, 18> kMidiChannelNames = {{
    "Off", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8",
    "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16", "All",
}};

[[nodiscard]] inline float clampParameter(const std::uint32_t index, const float raw) noexcept
{
    if (index >= kParameterCount)
        return 0.0f;
    const ParamSpec& spec = kParameterSpecs[index];
    const float value = std::isfinite(raw) ? raw : spec.defaultValue;
    const float clamped = std::min(std::max(value, spec.minimum), spec.maximum);
    return spec.integer ? std::round(clamped) : clamped;
}

// ── Scalings shared by the engine, the panel and the tests ─────────────────

[[nodiscard]] inline double noteToHertz(const double note) noexcept
{
    return 440.0 * std::exp2((note - 69.0) / 12.0);
}

// Filter centre: 40 Hz at 0 to 20 kHz at 1.
[[nodiscard]] inline double fieldToHertz(const float field) noexcept
{
    return 40.0 * std::pow(500.0, static_cast<double>(field));
}

// Base delay length: 10 ms at 0 to 0.8 s at 1.
[[nodiscard]] inline double timeToSeconds(const float time) noexcept
{
    return 0.01 * std::pow(80.0, static_cast<double>(time));
}

// Frequency shift: 0 at the centre, up to 500 Hz either way, finer near zero.
[[nodiscard]] inline double shiftToHertz(const float shift) noexcept
{
    const double s = std::min(std::max(static_cast<double>(shift), -1.0), 1.0);
    return s * std::fabs(s) * 500.0;
}

// ── Fixed MIDI controller map ──────────────────────────────────────────────
//
// Downspout convention: incoming CC writes through to the parameter store. CC 1-4
// are the four generator controls Drift's default lanes emit, so Drift drives
// Warp, Span, Morph and Seed with no setup; CC 5 and 6 continue the block
// (Fuse, Field) and CC 7 is channel volume. The processor controls follow.
// CC 64 (sustain pedal) is Hold.

inline constexpr std::uint8_t kCcHold = 64;
inline constexpr std::uint8_t kCcAllSoundOff = 120;
inline constexpr std::uint8_t kCcAllNotesOff = 123;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 14> kControllerMap = {{
    {1, ParamId::warp, "Warp"},
    {2, ParamId::span, "Span"},
    {3, ParamId::morph, "Morph"},
    {4, ParamId::seed, "Seed"},
    {5, ParamId::fuse, "Fuse"},
    {6, ParamId::field, "Field"},
    {7, ParamId::level, "Level"},
    {8, ParamId::notch, "Notch"},
    {9, ParamId::mix, "Mix"},
    {10, ParamId::time, "Time"},
    {11, ParamId::form, "Form"},
    {12, ParamId::feed, "Feed"},
    {13, ParamId::shift, "Shift"},
    {14, ParamId::basis, "Basis"},
}};

[[nodiscard]] inline bool controllerTarget(const std::uint8_t controller, ParamId& target) noexcept
{
    for (const ControllerMapping& mapping : kControllerMap)
    {
        if (mapping.controller == controller)
        {
            target = mapping.target;
            return true;
        }
    }
    return false;
}

// Spread a 0-127 controller value across the target parameter's full range.
[[nodiscard]] inline float controllerToParameter(const ParamId target, const std::uint8_t value) noexcept
{
    const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(target)];
    const float normalized = static_cast<float>(std::min<int>(value, 127)) / 127.0f;
    return clampParameter(static_cast<std::uint32_t>(target),
                          spec.minimum + normalized * (spec.maximum - spec.minimum));
}

}  // namespace downspout::fathom
