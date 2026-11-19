#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::splay {

// The original nine controls keep their indices so existing host automation
// still maps correctly. The new controls, CC Channel and six read-only outputs
// are appended.
enum class ParamId : std::uint32_t {
    matrixLL = 0,
    matrixLR,
    matrixRL,
    matrixRR,
    width,
    imageDelay,
    invertR,
    level,
    bypass,

    rotation,
    panLaw,
    midGain,
    msSolo,
    spectralWidth,
    crossover,
    lowWidth,
    highWidth,

    ccChannel,

    outCorrelation,
    outMonoCompat,
    outCcRotation,
    outCcWidth,
    outCcMid,
    outCcDelay,
    count
};

// The audio controls stay ahead of CC Channel and the read-only outputs.
// Existing writable indices are unchanged; CC Channel and the output mirrors
// were appended after them.
inline constexpr std::size_t kDspParameterCount = static_cast<std::size_t>(ParamId::ccChannel);
inline constexpr std::size_t kInputParameterCount = static_cast<std::size_t>(ParamId::outCorrelation);
inline constexpr std::size_t kParameterCount = static_cast<std::size_t>(ParamId::count);

inline constexpr std::array<const char*, 2> kOffOnNames = {{"Off", "On"}};
inline constexpr std::array<const char*, 2> kPanLawNames = {{"Linear", "-3 dB"}};
inline constexpr std::array<const char*, 3> kSoloNames = {{"Off", "Mid", "Side"}};
inline constexpr std::array<const char*, 17> kMidiChannelNames = {{
    "Off", "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16",
}};

struct ParamSpec {
    const char* name;
    const char* symbol;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
    bool output;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"L to L", "ll", "x", 0.0f, 2.0f, 1.0f, false, false},
    {"L to R", "lr", "x", 0.0f, 2.0f, 0.0f, false, false},
    {"R to L", "rl", "x", 0.0f, 2.0f, 0.0f, false, false},
    {"R to R", "rr", "x", 0.0f, 2.0f, 1.0f, false, false},
    {"Width", "width", "x", 0.0f, 2.0f, 1.0f, false, false},
    {"Image Delay", "image_delay", "ms", 0.0f, 20.0f, 0.0f, false, false},
    {"Invert R", "invert_r", "", 0.0f, 1.0f, 0.0f, true, false},
    {"Output", "level", "x", 0.0f, 2.0f, 1.0f, false, false},
    {"Bypass", "bypass", "", 0.0f, 1.0f, 0.0f, true, false},

    {"Rotation", "rotation", "deg", -90.0f, 90.0f, 0.0f, false, false},
    {"Pan Law", "pan_law", "", 0.0f, 1.0f, 0.0f, true, false},
    {"Mid", "mid_gain", "x", 0.0f, 2.0f, 1.0f, false, false},
    {"M/S Solo", "ms_solo", "", 0.0f, 2.0f, 0.0f, true, false},
    {"Spectral Width", "spectral_width", "", 0.0f, 1.0f, 0.0f, true, false},
    {"Crossover", "crossover", "Hz", 100.0f, 2000.0f, 500.0f, false, false},
    {"Low Width", "low_width", "x", 0.0f, 2.0f, 1.0f, false, false},
    {"High Width", "high_width", "x", 0.0f, 2.0f, 1.0f, false, false},

    {"CC Channel", "cc_channel", "", 0.0f, 16.0f, 1.0f, true, false},

    {"Correlation", "out_correlation", "", -1.0f, 1.0f, 0.0f, false, true},
    {"Mono Fit", "out_mono", "", 0.0f, 1.0f, 0.0f, false, true},
    // Read-only mirrors of the effective values after MIDI CC override.
    {"Rotation (CC1)", "out_cc_rotation", "deg", -90.0f, 90.0f, 0.0f, false, true},
    {"Width (CC2)", "out_cc_width", "x", 0.0f, 2.0f, 1.0f, false, true},
    {"Mid (CC3)", "out_cc_mid", "x", 0.0f, 2.0f, 1.0f, false, true},
    {"Image Delay (CC4)", "out_cc_delay", "ms", 0.0f, 20.0f, 0.0f, false, true},
}};

struct Parameters {
    float matrixLL = 1.0f;
    float matrixLR = 0.0f;
    float matrixRL = 0.0f;
    float matrixRR = 1.0f;
    float width = 1.0f;
    float imageDelay = 0.0f;
    float invertR = 0.0f;
    float level = 1.0f;
    float bypass = 0.0f;

    float rotation = 0.0f;
    float panLaw = 0.0f;
    float midGain = 1.0f;
    float msSolo = 0.0f;
    float spectralWidth = 0.0f;
    float crossover = 500.0f;
    float lowWidth = 1.0f;
    float highWidth = 1.0f;

    float ccChannel = 1.0f;  // 0 ignores MIDI; 1-16 selects a channel
};

inline constexpr std::array<float Parameters::*, kInputParameterCount> kParameterFields {{
    &Parameters::matrixLL, &Parameters::matrixLR, &Parameters::matrixRL, &Parameters::matrixRR,
    &Parameters::width, &Parameters::imageDelay, &Parameters::invertR, &Parameters::level,
    &Parameters::bypass, &Parameters::rotation, &Parameters::panLaw, &Parameters::midGain,
    &Parameters::msSolo, &Parameters::spectralWidth, &Parameters::crossover, &Parameters::lowWidth,
    &Parameters::highWidth, &Parameters::ccChannel,
}};

[[nodiscard]] inline int clampInt(const float value, const int low, const int high) noexcept
{
    return std::clamp(static_cast<int>(value), low, high);
}

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        const ParamSpec& spec = kParameterSpecs[i];
        float& value = p.*kParameterFields[i];
        if (!std::isfinite(value))
            value = spec.defaultValue;
        value = std::clamp(value, spec.minimum, spec.maximum);
        if (spec.integer)
            value = static_cast<float>(clampInt(value, static_cast<int>(spec.minimum),
                                                 static_cast<int>(spec.maximum)));
    }
    return p;
}

// The CC-controlled destination for a host/panel parameter, or -1. Moving one
// of these controls releases the matching controller override.
[[nodiscard]] constexpr int liveSlot(const std::uint32_t index) noexcept
{
    return index == static_cast<std::uint32_t>(ParamId::rotation) ? 0
         : index == static_cast<std::uint32_t>(ParamId::width) ? 1
         : index == static_cast<std::uint32_t>(ParamId::midGain) ? 2
         : index == static_cast<std::uint32_t>(ParamId::imageDelay) ? 3
         : -1;
}

}  // namespace downspout::splay
