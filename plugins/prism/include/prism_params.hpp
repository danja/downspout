#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace downspout::prism {

// ── Layout ─────────────────────────────────────────────────────────────────
//
// Six global parameters, then four per band (L delay, R delay, feedback, solo)
// for up to eight bands, then one read-only output. Band parameters of bands
// beyond "Bands" are kept (and saved) but have no effect.

inline constexpr std::size_t kMaxBands = 8;
inline constexpr std::size_t kFftSizeCount = 4;
// Frames overlap by this factor (hop = N / kOverlap). The fractional part of a delay is a
// phase rotation within a frame, and each trip round the feedback loop costs cos(pi r / N)
// of level for a residual r of up to half a hop, so a smaller hop keeps repeats at level.
inline constexpr int kOverlap = 8;

inline constexpr std::array<int, kFftSizeCount> kFftSizes = {{512, 1024, 2048, 4096}};
inline constexpr std::array<const char*, kFftSizeCount> kFftSizeNames = {{"512", "1024", "2048", "4096"}};
inline constexpr std::array<const char*, 2> kLinkNames = {{"Off", "On"}};

enum GlobalParam : std::uint32_t {
    kBands = 0,
    kSize,
    kLowCut,
    kHighCut,
    kMix,
    kLink,
    kSyncL,  // left delays are note divisions of the host tempo instead of milliseconds
    kSyncR,
    kGlobalCount,
};

enum BandField : std::uint32_t {
    kDelayL = 0,
    kDelayR,
    kFeedback,
    kSolo,
    kDivL,  // note division, used while that side is synced
    kDivR,
    kFieldsPerBand,
};

inline constexpr std::uint32_t kInputParameterCount = kGlobalCount + kMaxBands * kFieldsPerBand;
inline constexpr std::uint32_t kOutLatency = kInputParameterCount;
inline constexpr std::uint32_t kOutTempo = kInputParameterCount + 1;  // the tempo in use, for the panel
inline constexpr std::uint32_t kParameterCount = kInputParameterCount + 2;

[[nodiscard]] constexpr std::uint32_t bandParam(const std::size_t band, const BandField field) noexcept
{
    return kGlobalCount + static_cast<std::uint32_t>(band) * kFieldsPerBand + field;
}

// Note divisions for tempo sync, in beats (a beat is a quarter note, the host's BBT beat).
inline constexpr std::size_t kDivisionCount = 13;
inline constexpr std::array<const char*, kDivisionCount> kDivisionNames = {{
    "1/64", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1",
}};
inline constexpr std::array<double, kDivisionCount> kDivisionBeats = {{
    0.0625, 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0,
}};
inline constexpr std::array<const char*, 2> kSyncNames = {{"ms", "RATE"}};
inline constexpr float kDefaultTempo = 120.0f;

// Length of a division at a tempo. Not limited to the maximum delay: the caller clamps.
[[nodiscard]] inline float divisionMs(const int division, const double bpm) noexcept
{
    const auto i = static_cast<std::size_t>(std::clamp(division, 0, static_cast<int>(kDivisionCount) - 1));
    const double tempo = bpm > 1.0 ? bpm : static_cast<double>(kDefaultTempo);
    return static_cast<float>(kDivisionBeats[i] * 60000.0 / tempo);
}

inline constexpr float kMaxDelayMs = 2000.0f;
inline constexpr float kMaxFeedback = 0.95f;
inline constexpr float kMinLowCutHz = 20.0f;
inline constexpr float kMaxHighCutHz = 20000.0f;
inline constexpr float kLowestCentreHz = 31.25f;
inline constexpr float kHighestCentreHz = 16000.0f;
inline constexpr float kCutRampOctaves = 1.0f / 3.0f;

struct ParamSpec {
    char symbol[20];
    char name[28];
    const char* unit;  // empty string when unitless
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

// Built once. Defaults stagger the delay up the spectrum (lows early, highs late) with a
// little more on the right, so a fresh instance already sounds like what it is.
[[nodiscard]] inline const std::array<ParamSpec, kParameterCount>& parameterSpecs()
{
    static const std::array<ParamSpec, kParameterCount> specs = [] {
        std::array<ParamSpec, kParameterCount> s {};
        s[kBands] = detail::make("bands", "Bands", "", 1.0f, 8.0f, 8.0f, true);
        s[kSize] = detail::make("size", "Resolution", "", 0.0f, 3.0f, 2.0f, true);
        s[kLowCut] = detail::make("low_cut", "Low Cut", "Hz", kMinLowCutHz, 2000.0f, kMinLowCutHz, false, false, true);
        s[kHighCut] = detail::make("high_cut", "High Cut", "Hz", 200.0f, kMaxHighCutHz, kMaxHighCutHz, false, false, true);
        s[kMix] = detail::make("mix", "Mix", "", 0.0f, 1.0f, 0.5f, false);
        s[kLink] = detail::make("link", "Link L/R", "", 0.0f, 1.0f, 0.0f, true);
        s[kSyncL] = detail::make("sync_l", "Sync L", "", 0.0f, 1.0f, 0.0f, true);
        s[kSyncR] = detail::make("sync_r", "Sync R", "", 0.0f, 1.0f, 0.0f, true);
        for (std::size_t b = 0; b < kMaxBands; ++b)
        {
            char symbol[20];
            char name[28];
            const float stagger = 60.0f * static_cast<float>(b);
            std::snprintf(symbol, sizeof(symbol), "b%zu_delay_l", b + 1);
            std::snprintf(name, sizeof(name), "Band %zu L Delay", b + 1);
            s[bandParam(b, kDelayL)] = detail::make(symbol, name, "ms", 0.0f, kMaxDelayMs, stagger, false);
            std::snprintf(symbol, sizeof(symbol), "b%zu_delay_r", b + 1);
            std::snprintf(name, sizeof(name), "Band %zu R Delay", b + 1);
            s[bandParam(b, kDelayR)] = detail::make(symbol, name, "ms", 0.0f, kMaxDelayMs, stagger + 30.0f, false);
            std::snprintf(symbol, sizeof(symbol), "b%zu_feedback", b + 1);
            std::snprintf(name, sizeof(name), "Band %zu Feedback", b + 1);
            s[bandParam(b, kFeedback)] = detail::make(symbol, name, "", 0.0f, 95.0f, 30.0f, false);
            std::snprintf(symbol, sizeof(symbol), "b%zu_solo", b + 1);
            std::snprintf(name, sizeof(name), "Band %zu Solo", b + 1);
            s[bandParam(b, kSolo)] = detail::make(symbol, name, "", 0.0f, 1.0f, 0.0f, true);
            std::snprintf(symbol, sizeof(symbol), "b%zu_div_l", b + 1);
            std::snprintf(name, sizeof(name), "Band %zu L Division", b + 1);
            s[bandParam(b, kDivL)] = detail::make(symbol, name, "", 0.0f, static_cast<float>(kDivisionCount - 1), 6.0f, true);
            std::snprintf(symbol, sizeof(symbol), "b%zu_div_r", b + 1);
            std::snprintf(name, sizeof(name), "Band %zu R Division", b + 1);
            s[bandParam(b, kDivR)] = detail::make(symbol, name, "", 0.0f, static_cast<float>(kDivisionCount - 1), 7.0f, true);
        }
        s[kOutLatency] = detail::make("out_latency", "Latency", "ms", 0.0f, 400.0f, 0.0f, false, true);
        s[kOutTempo] = detail::make("out_tempo", "Tempo", "BPM", 20.0f, 300.0f, kDefaultTempo, false, true);
        return s;
    }();
    return specs;
}

// The host-writable values, in parameter order.
struct Parameters {
    std::array<float, kInputParameterCount> v {};

    Parameters()
    {
        const auto& specs = parameterSpecs();
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = specs[i].defaultValue;
    }

    [[nodiscard]] float& operator[](const std::size_t i) noexcept { return v[i]; }
    [[nodiscard]] const float& operator[](const std::size_t i) const noexcept { return v[i]; }

    [[nodiscard]] int bands() const noexcept { return std::clamp(static_cast<int>(std::lround(v[kBands])), 1, 8); }
    [[nodiscard]] int sizeIndex() const noexcept { return std::clamp(static_cast<int>(std::lround(v[kSize])), 0, 3); }
    [[nodiscard]] bool linked() const noexcept { return v[kLink] >= 0.5f; }
    // The side a channel actually uses: linked, the right follows the left (sync, delay, division).
    [[nodiscard]] bool synced(const std::size_t channel) const noexcept
    {
        return v[channel == 0 || linked() ? kSyncL : kSyncR] >= 0.5f;
    }
    [[nodiscard]] float band(const std::size_t b, const BandField f) const noexcept { return v[bandParam(b, f)]; }
    [[nodiscard]] float& band(const std::size_t b, const BandField f) noexcept { return v[bandParam(b, f)]; }
};

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    const auto& specs = parameterSpecs();
    for (std::size_t i = 0; i < p.v.size(); ++i)
    {
        float& field = p.v[i];
        const ParamSpec& spec = specs[i];
        if (!std::isfinite(field))
            field = spec.defaultValue;
        field = std::clamp(field, spec.minimum, spec.maximum);
        if (spec.integer)
            field = std::round(field);
    }
    return p;
}

// ── Band geometry ──────────────────────────────────────────────────────────

// Band centres are spaced evenly in log frequency from 31.25 Hz to 16 kHz. With a single
// band there is no span, so it covers everything (the centre is only a label).
[[nodiscard]] inline float bandCentreHz(const int band, const int bands) noexcept
{
    if (bands <= 1)
        return 1000.0f;
    const float octaves = std::log2(kHighestCentreHz / kLowestCentreHz);
    return kLowestCentreHz * std::exp2(octaves * static_cast<float>(band) / static_cast<float>(bands - 1));
}

// Where a frequency sits between band centres: the band below and the fraction towards the
// band above. Linear in log frequency, so there is no step at a band edge; outside the
// outermost centres the end band holds.
struct BandPosition {
    int band = 0;
    float frac = 0.0f;
};

[[nodiscard]] inline BandPosition bandPosition(const double hz, const int bands) noexcept
{
    BandPosition out;
    if (bands <= 1 || !(hz > 0.0))
        return out;
    const double octaves = std::log2(static_cast<double>(kHighestCentreHz) / kLowestCentreHz);
    const double perBand = octaves / static_cast<double>(bands - 1);
    const double pos = std::clamp(std::log2(hz / kLowestCentreHz) / perBand, 0.0, static_cast<double>(bands - 1));
    out.band = std::min(static_cast<int>(std::floor(pos)), bands - 2);
    out.frac = static_cast<float>(pos - out.band);
    return out;
}

// Cut weights: 1 inside the passband, falling linearly over a third of an octave to 0 in
// the cut region. A cut parked at the end of its range is off.
[[nodiscard]] inline float lowCutWeight(const double hz, const float cutHz) noexcept
{
    if (cutHz <= kMinLowCutHz)
        return 1.0f;
    if (!(hz > 0.0))
        return 0.0f;
    const double octaves = std::log2(hz / cutHz);
    return static_cast<float>(std::clamp((octaves + kCutRampOctaves) / kCutRampOctaves, 0.0, 1.0));
}

[[nodiscard]] inline float highCutWeight(const double hz, const float cutHz) noexcept
{
    if (cutHz >= kMaxHighCutHz || !(hz > 0.0))
        return 1.0f;
    const double octaves = std::log2(hz / cutHz);
    return static_cast<float>(std::clamp((kCutRampOctaves - octaves) / kCutRampOctaves, 0.0, 1.0));
}

}  // namespace downspout::prism
