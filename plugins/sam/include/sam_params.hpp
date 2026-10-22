#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::sam {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// Seventeen host-writable parameters and one read-only status parameter. The
// text itself is not a parameter: it travels as the "text" state.

enum class ParamId : std::uint32_t {
    // Source
    mode = 0,
    // Voice (the four classic SAM knobs, plus how the text is read)
    speed,
    pitch,
    mouth,
    throat,
    monotone,
    phonetic,
    // Keys mode
    midiCh,
    advance,
    transpose,
    release,
    // Transport mode
    step,
    startBar,
    loop,
    speak,
    // Output
    tone,
    level,
    // Read-only status
    outWord,
};

inline constexpr std::size_t kParameterCount = 18;
inline constexpr std::size_t kInputParameterCount = 17;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
    bool trigger = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"mode", "Mode", "", 0.0f, 1.0f, 0.0f, true, false, false},

    {"speed", "Speed", "", 20.0f, 200.0f, 72.0f, true, false, false},
    {"pitch", "Pitch", "", 16.0f, 255.0f, 64.0f, true, false, false},
    {"mouth", "Mouth", "", 0.0f, 255.0f, 128.0f, true, false, false},
    {"throat", "Throat", "", 0.0f, 255.0f, 128.0f, true, false, false},
    {"monotone", "Monotone", "", 0.0f, 1.0f, 0.0f, true, false, false},
    {"phonetic", "Phonetic", "", 0.0f, 1.0f, 0.0f, true, false, false},

    {"midi_ch", "MIDI In", "", 0.0f, 17.0f, 17.0f, true, false, false},
    {"advance", "Advance", "", 0.0f, 1.0f, 0.0f, true, false, false},
    {"transpose", "Transpose", "st", -24.0f, 24.0f, 0.0f, true, false, false},
    {"release", "Release", "ms", 5.0f, 1000.0f, 60.0f, false, false, false},

    {"step", "Step", "", 0.0f, 7.0f, 2.0f, true, false, false},
    {"start_bar", "Start Bar", "", 1.0f, 256.0f, 1.0f, true, false, false},
    {"loop", "Loop", "", 0.0f, 1.0f, 1.0f, true, false, false},
    {"speak", "Speak", "", 0.0f, 1.0f, 0.0f, true, false, true},

    {"tone", "Tone", "Hz", 1000.0f, 12000.0f, 8000.0f, false, false, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.70f, false, false, false},

    {"out_word", "Current Word", "", 0.0f, 4096.0f, 0.0f, true, true, false},
}};

[[nodiscard]] constexpr std::uint32_t paramIndex(const ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

// ── Enumerated value tables ────────────────────────────────────────────────

inline constexpr std::array<const char*, 2> kModeNames = {{"Keys", "Transport"}};

inline constexpr std::array<const char*, 2> kAdvanceNames = {{"Next word", "Key = word"}};

// 0 = Off, 1-16 = that MIDI channel only, 17 = all channels.
inline constexpr std::array<const char*, 18> kMidiChannelNames = {{
    "Off", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8",
    "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16", "All",
}};

// Length of one word slot. Values <= 0 mean "bars" and are resolved against the
// host time signature: -1 = 1 bar, -2 = 2 bars, -4 = 4 bars.
inline constexpr std::array<float, 8> kStepBeats = {{0.25f, 0.5f, 1.0f, 2.0f, 4.0f, -1.0f, -2.0f, -4.0f}};

inline constexpr std::array<const char*, 8> kStepNames = {{
    "1/16", "1/8", "1/4", "1/2", "1/1", "1 bar", "2 bars", "4 bars",
}};

inline constexpr std::array<const char*, 2> kOffOnNames = {{"Off", "On"}};

// The six voices from the SAM manual, as speed / pitch / throat / mouth.
struct VoicePreset {
    const char* name;
    int speed;
    int pitch;
    int throat;
    int mouth;
};

inline constexpr std::array<VoicePreset, 6> kVoicePresets = {{
    {"SAM", 72, 64, 128, 128},
    {"Elf", 72, 64, 110, 160},
    {"Robot", 92, 60, 190, 190},
    {"Stuffy", 82, 72, 110, 105},
    {"Old Lady", 82, 32, 145, 145},
    {"Alien", 100, 64, 150, 200},
}};

inline constexpr const char* kDefaultText = "Hello world. This is Sam, speaking in time.";

// ── MIDI note to SAM pitch ─────────────────────────────────────────────────
//
// In singing mode SAM's pitch byte is a glottal period counted in ticks of
// 3.24 output samples at 22.05 kHz, so f0 = 6805.56 / pitch. Measured, not
// assumed: see docs/design.md.

inline constexpr double kSamRate = 22050.0;
inline constexpr double kPitchConstant = 22050.0 * 50.0 / 162.0;

}  // namespace downspout::sam
