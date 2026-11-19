#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace downspout::tilt {

// ── Layout ─────────────────────────────────────────────────────────────────
//
// Seven host-writable controls, then three read-only scalars and 64 spectrum bins.
// The spectrum outputs live after the controls so their indices never move when a
// control is appended. DPF's parameter table holds all 74; the panel gives its
// MagnetoUI base only the first ten (the bars live in a separate array).
//
// Angle is -1..1. With Slope S in {6, 12, 24} dB/oct the ideal gain is
//     gainDb(f) = clamp(Angle * S * log2(f / Pivot), -24, +24).
// At Angle = +/-1 the line therefore reaches +/-S dB per octave, and the clamp
// keeps the extreme ends of a wide-range master-bus tilt within sane limits.

enum class ParamId : std::uint32_t {
    pivot = 0,
    angle,
    slope,
    mode,
    outputDb,
    bypass,
    spectrumSource,
    outLatency,
    outPeakL,
    outPeakR,
    spectrumFirst = 10,
};

inline constexpr std::uint32_t kInputParameterCount = 7;
inline constexpr std::uint32_t kOutLatency = static_cast<std::uint32_t>(ParamId::outLatency);
inline constexpr std::uint32_t kMeterFirst = static_cast<std::uint32_t>(ParamId::outPeakL);
inline constexpr std::uint32_t kSpectrumFirst = static_cast<std::uint32_t>(ParamId::spectrumFirst);
inline constexpr std::uint32_t kSpectrumBins = 64;
inline constexpr std::uint32_t kParameterCount = kSpectrumFirst + kSpectrumBins;

inline constexpr std::array<int, 3> kSlopes = {{6, 12, 24}};
inline constexpr std::array<const char*, 3> kSlopeNames = {{"6 dB", "12 dB", "24 dB"}};
inline constexpr std::array<const char*, 2> kModeNames = {{"IIR", "FFT"}};
inline constexpr std::array<const char*, 3> kSpectrumNames = {{"Off", "Pre", "Post"}};
inline constexpr std::array<const char*, 2> kOffOnNames = {{"Off", "On"}};

inline constexpr float kMinPivotHz = 20.0f;
inline constexpr float kMaxPivotHz = 20000.0f;
inline constexpr float kMaxTiltDb = 24.0f;
inline constexpr float kMinOutputDb = -12.0f;
inline constexpr float kMaxOutputDb = 12.0f;

enum class DspMode : int { Iir = 0, Fft = 1 };
enum class SpectrumSource : int { Off = 0, Pre, Post };

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
        out[kId(ParamId::pivot)] = detail::make("pivot", "Pivot Freq", "Hz", kMinPivotHz, kMaxPivotHz, 1000.0f, false, false, true);
        out[kId(ParamId::angle)] = detail::make("angle", "Angle", "", -1.0f, 1.0f, 0.0f, false);
        out[kId(ParamId::slope)] = detail::make("slope", "Slope", "", 0.0f, 2.0f, 0.0f, true);
        out[kId(ParamId::mode)] = detail::make("mode", "Mode", "", 0.0f, 1.0f, 0.0f, true);
        out[kId(ParamId::outputDb)] = detail::make("output", "Output", "dB", kMinOutputDb, kMaxOutputDb, 0.0f, false);
        out[kId(ParamId::bypass)] = detail::make("bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, true);
        out[kId(ParamId::spectrumSource)] = detail::make("spectrum_source", "Spectrum", "", 0.0f, 2.0f, 2.0f, true);
        out[kOutLatency] = detail::make("out_latency", "Latency", "ms", 0.0f, 200.0f, 0.0f, false, true);
        out[kMeterFirst] = detail::make("out_peak_l", "Peak L", "dB", -60.0f, 6.0f, -60.0f, false, true);
        out[kMeterFirst + 1] = detail::make("out_peak_r", "Peak R", "dB", -60.0f, 6.0f, -60.0f, false, true);
        for (std::uint32_t k = 0; k < kSpectrumBins; ++k) {
            char symbol[24], name[32];
            std::snprintf(symbol, sizeof(symbol), "spectrum_%02u", k + 1);
            std::snprintf(name, sizeof(name), "Spectrum %u", k + 1);
            out[kSpectrumFirst + k] = detail::make(symbol, name, "dB", -90.0f, 6.0f, -90.0f, false, true);
        }
        return out;
    }();
    return s;
}

[[nodiscard]] inline bool isMeter(const std::uint32_t index) noexcept
{
    return index >= kMeterFirst && index < kSpectrumFirst;
}
[[nodiscard]] inline bool isSpectrumBin(const std::uint32_t index) noexcept
{
    return index >= kSpectrumFirst && index < kParameterCount;
}

struct Parameters {
    float pivot = 1000.0f;
    float angle = 0.0f;
    float slope = 0.0f;
    float mode = 0.0f;
    float outputDb = 0.0f;
    float bypass = 0.0f;
    float spectrumSource = 2.0f;
};

[[nodiscard]] inline float& field(Parameters& p, const std::uint32_t index)
{
    switch (static_cast<ParamId>(index)) {
    case ParamId::pivot: return p.pivot;
    case ParamId::angle: return p.angle;
    case ParamId::slope: return p.slope;
    case ParamId::mode: return p.mode;
    case ParamId::outputDb: return p.outputDb;
    case ParamId::bypass: return p.bypass;
    case ParamId::spectrumSource: return p.spectrumSource;
    default: return p.angle;  // callers never write output parameters
    }
}

[[nodiscard]] inline float field(const Parameters& p, const std::uint32_t index)
{
    switch (static_cast<ParamId>(index)) {
    case ParamId::pivot: return p.pivot;
    case ParamId::angle: return p.angle;
    case ParamId::slope: return p.slope;
    case ParamId::mode: return p.mode;
    case ParamId::outputDb: return p.outputDb;
    case ParamId::bypass: return p.bypass;
    case ParamId::spectrumSource: return p.spectrumSource;
    default: return 0.0f;
    }
}

[[nodiscard]] inline int slopeIndex(const Parameters& p) noexcept
{
    return std::clamp(static_cast<int>(std::lround(p.slope)), 0, static_cast<int>(kSlopes.size()) - 1);
}
[[nodiscard]] inline int slopeDb(const Parameters& p) noexcept { return kSlopes[static_cast<std::size_t>(slopeIndex(p))]; }
[[nodiscard]] inline DspMode dspMode(const Parameters& p) noexcept
{
    return p.mode >= 0.5f ? DspMode::Fft : DspMode::Iir;
}

// Exponent of the underlying f^p tilt; 6 dB/oct is p = 1.
[[nodiscard]] inline double exponent(const Parameters& p) noexcept
{
    const double a = std::clamp(static_cast<double>(p.angle), -1.0, 1.0);
    return a * static_cast<double>(slopeDb(p)) / 6.0;
}

// The straight line the panel draws. Clamped at the extremes for master-bus use.
[[nodiscard]] inline double idealDb(const Parameters& p, const double hz) noexcept
{
    if (!(hz > 0.0) || !(p.pivot > 0.0))
        return 0.0;
    // A first-order tilt (p = 1) is 6 dB/octave: 20 log10(2) = 6.02 dB.
    const double raw = 6.0 * exponent(p) * std::log2(hz / static_cast<double>(p.pivot));
    return std::clamp(raw, -static_cast<double>(kMaxTiltDb), static_cast<double>(kMaxTiltDb));
}

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    const auto& s = specs();
    float* fields[] = {&p.pivot, &p.angle, &p.slope, &p.mode, &p.outputDb, &p.bypass, &p.spectrumSource};
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

[[nodiscard]] inline double spectrumBandEdgeHz(const int k) noexcept
{
    return static_cast<double>(kMinPivotHz) *
           std::pow(static_cast<double>(kMaxPivotHz) / kMinPivotHz, static_cast<double>(k) / kSpectrumBins);
}

}  // namespace downspout::tilt
