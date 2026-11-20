#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace downspout::gate {

// Ten host-writable controls, then three read-only status outputs. The detector,
// reduction and gate-state outputs let the panel show what the processor is doing
// rather than only the last host automation value.
enum class ParamId : std::uint32_t {
    threshold = 0,
    hysteresis,
    range,
    attack,
    hold,
    release,
    scSource,
    scHpf,
    outputDb,
    bypass,
    outLevel,
    outReduction,
    outOpen,
};

inline constexpr std::uint32_t kInputParameterCount = 10;
inline constexpr std::uint32_t kOutLevel = static_cast<std::uint32_t>(ParamId::outLevel);
inline constexpr std::uint32_t kOutReduction = static_cast<std::uint32_t>(ParamId::outReduction);
inline constexpr std::uint32_t kOutOpen = static_cast<std::uint32_t>(ParamId::outOpen);
inline constexpr std::uint32_t kParameterCount = static_cast<std::uint32_t>(ParamId::outOpen) + 1;

inline constexpr std::array<const char*, 2> kSourceNames = {{"Internal", "External"}};
inline constexpr std::array<const char*, 2> kOffOnNames = {{"Off", "On"}};

inline constexpr float kMinThresholdDb = -80.0f;
inline constexpr float kMaxThresholdDb = 0.0f;
inline constexpr float kMaxHysteresisDb = 20.0f;
inline constexpr float kMaxRangeDb = 80.0f;
inline constexpr float kMinScHpfHz = 20.0f;
inline constexpr float kMaxScHpfHz = 500.0f;
inline constexpr float kMinOutputDb = -12.0f;
inline constexpr float kMaxOutputDb = 12.0f;

struct ParamSpec {
    char symbol[24];
    char name[32];
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
    bool output;
    bool logarithmic;
};

namespace detail {

inline ParamSpec make(const char* symbol, const char* name, const char* unit, const float lo, const float hi,
                      const float def, const bool integer, const bool output = false, const bool log = false)
{
    ParamSpec s {};
    std::snprintf(s.symbol, sizeof(s.symbol), "%s", symbol);
    std::snprintf(s.name, sizeof(s.name), "%s", name);
    s.unit = unit;
    s.minimum = lo;
    s.maximum = hi;
    s.defaultValue = def;
    s.integer = integer;
    s.output = output;
    s.logarithmic = log;
    return s;
}

}  // namespace detail

[[nodiscard]] constexpr std::uint32_t kId(const ParamId id) noexcept { return static_cast<std::uint32_t>(id); }

[[nodiscard]] inline const std::array<ParamSpec, kParameterCount>& specs()
{
    static const std::array<ParamSpec, kParameterCount> s = [] {
        std::array<ParamSpec, kParameterCount> out {};
        out[kId(ParamId::threshold)] = detail::make("threshold", "Threshold", "dB", kMinThresholdDb, kMaxThresholdDb, -40.0f, false);
        out[kId(ParamId::hysteresis)] = detail::make("hysteresis", "Hysteresis", "dB", 0.0f, kMaxHysteresisDb, 6.0f, false);
        out[kId(ParamId::range)] = detail::make("range", "Range", "dB", 0.0f, kMaxRangeDb, 60.0f, false);
        out[kId(ParamId::attack)] = detail::make("attack", "Attack", "ms", 0.0f, 100.0f, 5.0f, false);
        out[kId(ParamId::hold)] = detail::make("hold", "Hold", "ms", 0.0f, 500.0f, 50.0f, false);
        out[kId(ParamId::release)] = detail::make("release", "Release", "ms", 10.0f, 2000.0f, 200.0f, false);
        out[kId(ParamId::scSource)] = detail::make("sc_source", "SC Source", "", 0.0f, 1.0f, 0.0f, true);
        out[kId(ParamId::scHpf)] = detail::make("sc_hpf", "SC HPF", "Hz", kMinScHpfHz, kMaxScHpfHz, kMinScHpfHz, false, false, true);
        out[kId(ParamId::outputDb)] = detail::make("output", "Output", "dB", kMinOutputDb, kMaxOutputDb, 0.0f, false);
        out[kId(ParamId::bypass)] = detail::make("bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, true);
        out[kOutLevel] = detail::make("out_level", "Detector", "dB", -100.0f, 6.0f, -100.0f, false, true);
        out[kOutReduction] = detail::make("out_reduction", "Reduction", "dB", -80.0f, 0.0f, 0.0f, false, true);
        out[kOutOpen] = detail::make("out_open", "Gate", "", 0.0f, 1.0f, 0.0f, false, true);
        return out;
    }();
    return s;
}

[[nodiscard]] inline bool isOutput(const std::uint32_t index) noexcept { return index >= kInputParameterCount; }

struct Parameters {
    float threshold = -40.0f;
    float hysteresis = 6.0f;
    float range = 60.0f;
    float attack = 5.0f;
    float hold = 50.0f;
    float release = 200.0f;
    float scSource = 0.0f;
    float scHpf = kMinScHpfHz;
    float outputDb = 0.0f;
    float bypass = 0.0f;
};

[[nodiscard]] inline float& field(Parameters& p, const std::uint32_t index)
{
    switch (static_cast<ParamId>(index)) {
    case ParamId::threshold: return p.threshold;
    case ParamId::hysteresis: return p.hysteresis;
    case ParamId::range: return p.range;
    case ParamId::attack: return p.attack;
    case ParamId::hold: return p.hold;
    case ParamId::release: return p.release;
    case ParamId::scSource: return p.scSource;
    case ParamId::scHpf: return p.scHpf;
    case ParamId::outputDb: return p.outputDb;
    case ParamId::bypass: return p.bypass;
    default: return p.threshold;  // callers never write output parameters
    }
}

[[nodiscard]] inline float field(const Parameters& p, const std::uint32_t index)
{
    switch (static_cast<ParamId>(index)) {
    case ParamId::threshold: return p.threshold;
    case ParamId::hysteresis: return p.hysteresis;
    case ParamId::range: return p.range;
    case ParamId::attack: return p.attack;
    case ParamId::hold: return p.hold;
    case ParamId::release: return p.release;
    case ParamId::scSource: return p.scSource;
    case ParamId::scHpf: return p.scHpf;
    case ParamId::outputDb: return p.outputDb;
    case ParamId::bypass: return p.bypass;
    default: return 0.0f;
    }
}

[[nodiscard]] inline bool externalSidechain(const Parameters& p) noexcept { return p.scSource >= 0.5f; }

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    const auto& s = specs();
    float* fields[] = {&p.threshold, &p.hysteresis, &p.range, &p.attack, &p.hold,
                       &p.release, &p.scSource, &p.scHpf, &p.outputDb, &p.bypass};
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        float& v = *fields[i];
        if (!std::isfinite(v))
            v = s[i].defaultValue;
        v = std::clamp(v, s[i].minimum, s[i].maximum);
        if (s[i].integer)
            v = std::round(v);
    }
    return p;
}

}  // namespace downspout::gate
