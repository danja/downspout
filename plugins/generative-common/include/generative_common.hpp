#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::generative {

inline constexpr std::size_t kMaxMidiEvents = 512;

struct Transport {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double beatType = 4.0;
    double bpm = 120.0;
};

struct MidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 3;
    std::array<std::uint8_t, 4> data {};
};

struct MidiBlock {
    std::array<MidiEvent, kMaxMidiEvents> events {};
    std::uint32_t count = 0;

    void push(const std::uint32_t frame,
              const std::uint8_t status,
              const std::uint8_t data1,
              const std::uint8_t data2) noexcept
    {
        if (count >= events.size())
            return;
        events[count++] = {frame, 3, {status, data1, data2, 0}};
    }
};

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline float clampParam(const float value, const ParamSpec& spec) noexcept
{
    float result = std::isfinite(value) ? value : spec.defaultValue;
    result = std::clamp(result, spec.minimum, spec.maximum);
    return spec.integer ? std::round(result) : result;
}

inline double barLengthQuarters(const Transport& transport) noexcept
{
    return transport.beatType > 0.0
        ? std::max(0.25, transport.beatsPerBar * 4.0 / transport.beatType)
        : 4.0;
}

inline double absoluteQuarter(const Transport& transport) noexcept
{
    const double beatUnit = transport.beatType > 0.0 ? 4.0 / transport.beatType : 1.0;
    return transport.bar * barLengthQuarters(transport) + transport.barBeat * beatUnit;
}

inline std::uint32_t frameAt(const double quarter,
                             const double blockStart,
                             const double quartersPerFrame,
                             const std::uint32_t nframes) noexcept
{
    if (quarter <= blockStart || quartersPerFrame <= 0.0 || nframes == 0)
        return 0;
    const double raw = (quarter - blockStart) / quartersPerFrame;
    return static_cast<std::uint32_t>(
        std::clamp(std::llround(raw), 0LL, static_cast<long long>(nframes - 1)));
}

inline std::uint64_t mix64(std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

inline float randomUnit(const std::uint64_t seed, const std::uint64_t index) noexcept
{
    const std::uint64_t value = mix64(seed ^ mix64(index));
    return static_cast<float>((value >> 40U) * (1.0 / 16777216.0));
}

inline int randomInt(const std::uint64_t seed,
                     const std::uint64_t index,
                     const int minimum,
                     const int maximum) noexcept
{
    if (maximum <= minimum)
        return minimum;
    const int span = maximum - minimum + 1;
    return minimum + static_cast<int>(mix64(seed ^ mix64(index)) % static_cast<std::uint64_t>(span));
}

inline bool isDiscontinuity(const bool havePosition,
                            const double previousEnd,
                            const double blockStart,
                            const double tolerance = 0.05) noexcept
{
    return havePosition
        && (blockStart + 1.0e-5 < previousEnd || std::fabs(blockStart - previousEnd) > tolerance);
}

inline std::uint8_t status(const bool noteOn, const int channel) noexcept
{
    return static_cast<std::uint8_t>((noteOn ? 0x90 : 0x80)
        | (std::clamp(channel, 1, 16) - 1));
}

inline std::uint8_t ccStatus(const int channel) noexcept
{
    return static_cast<std::uint8_t>(0xb0 | (std::clamp(channel, 1, 16) - 1));
}

} // namespace downspout::generative
