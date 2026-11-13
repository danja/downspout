#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::bore {

enum class ParamId : std::uint32_t {
    pressure = 0,
    embouchure,
    bell,
    breath,
    attack,
    release,
    velocity,
    level,
    midiCh,
    outVoices,
};

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

inline constexpr std::size_t kParameterCount = 10;
inline constexpr std::size_t kInputParameterCount = 9;
inline constexpr std::size_t kVoiceCount = 8;
inline constexpr std::size_t kMaxDelaySamples = 16384;

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"pressure", "Pressure", "", 0.0f, 1.0f, 0.58f, false, false},
    {"embouchure", "Embouchure", "x", 0.8f, 1.2f, 1.0f, false, false},
    {"bell", "Bell", "", 0.0f, 1.0f, 0.68f, false, false},
    {"breath", "Breath", "", 0.0f, 1.0f, 0.12f, false, false},
    {"attack", "Attack", "ms", 1.0f, 1000.0f, 12.0f, false, false},
    {"release", "Release", "ms", 20.0f, 4000.0f, 280.0f, false, false},
    {"velocity", "Velocity", "", 0.0f, 1.0f, 0.65f, false, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.65f, false, false},
    {"midi_ch", "MIDI In", "", 0.0f, 17.0f, 17.0f, true, false},
    {"out_voices", "Active Voices", "", 0.0f, 8.0f, 0.0f, true, true},
}};

inline constexpr std::array<const char*, 18> kMidiChannelNames = {{
    "Off", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8",
    "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16", "All",
}};

inline constexpr std::array<ParamId, 5> kControllerTargets = {{
    ParamId::pressure, ParamId::embouchure, ParamId::bell, ParamId::breath, ParamId::level,
}};
inline constexpr std::array<std::uint8_t, 5> kControllerNumbers = {{1, 2, 3, 4, 7}};

[[nodiscard]] inline float clampParameter(const std::uint32_t index, const float raw) noexcept
{
    if (index >= kInputParameterCount)
        return 0.0f;
    const ParamSpec& spec = kParameterSpecs[index];
    const float value = std::isfinite(raw) ? raw : spec.defaultValue;
    const float clamped = std::clamp(value, spec.minimum, spec.maximum);
    return spec.integer ? std::round(clamped) : clamped;
}

[[nodiscard]] inline double noteToHertz(const int note) noexcept
{
    return 440.0 * std::exp2((static_cast<double>(note) - 69.0) / 12.0);
}

}  // namespace downspout::bore
