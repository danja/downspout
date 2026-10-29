#pragma once

#include "trio_core.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace downspout::trio {

// Parameter indices are stable: never reorder, only append.
// 0-6 global, then eight per band (on, down threshold, down ratio, up threshold, up ratio,
// attack, release, gain), then six output meters.
enum GlobalParameter : std::uint32_t { kInput = 0, kOutput, kAmount, kTime, kUpLimit, kCrossLow, kCrossHigh, kGlobalCount };
enum BandParameter : std::uint32_t { kOn = 0, kDownThresh, kDownRatio, kUpThresh, kUpRatio, kAttack, kRelease, kGain, kPerBand };

inline constexpr std::uint32_t kFirstBand = kGlobalCount;
inline constexpr std::uint32_t kControlCount = kFirstBand + kBands * kPerBand;
inline constexpr std::uint32_t kFirstMeter = kControlCount;       // three band gains, then three band levels
inline constexpr std::uint32_t kMeterCount = 2 * kBands;
inline constexpr std::uint32_t kCcChannel = kControlCount + kMeterCount;   // appended after the meters: parameter indices never move
inline constexpr std::uint32_t kKnee = kCcChannel + 1;
inline constexpr std::uint32_t kSolo = kCcChannel + 2;
inline constexpr std::uint32_t kSidechain = kCcChannel + 3;
inline constexpr std::uint32_t kParameterCount = kCcChannel + 4;

[[nodiscard]] constexpr std::uint32_t bandParameter(const int band, const BandParameter p) noexcept
{
    return kFirstBand + static_cast<std::uint32_t>(band) * kPerBand + p;
}
[[nodiscard]] constexpr std::uint32_t gainMeter(const int band) noexcept { return kFirstMeter + static_cast<std::uint32_t>(band); }
[[nodiscard]] constexpr std::uint32_t levelMeter(const int band) noexcept { return kFirstMeter + kBands + static_cast<std::uint32_t>(band); }

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
    bool output;
};

// Built once: names and symbols need storage that outlives the host's calls.
class Specs {
public:
    Specs()
    {
        const Parameters d;
        global(kInput, "input", "Input", "dB", -12.0f, 24.0f, d.inputDb, false);
        global(kOutput, "output", "Output", "dB", -24.0f, 12.0f, d.outputDb, false);
        global(kAmount, "amount", "Amount", "%", 0.0f, 100.0f, d.amount, false);
        global(kTime, "time", "Time", "x", 0.1f, 10.0f, d.time, true);
        global(kUpLimit, "up_limit", "Upward Limit", "dB", 0.0f, 24.0f, d.upLimitDb, false);
        global(kCrossLow, "cross_low", "Low Crossover", "Hz", 40.0f, 1000.0f, d.crossLowHz, true);
        global(kCrossHigh, "cross_high", "High Crossover", "Hz", 1000.0f, 12000.0f, d.crossHighHz, true);
        global(kCcChannel, "cc_channel", "CC Channel", "", 0.0f, 16.0f, d.ccChannel, false, true);
        global(kKnee, "knee", "Knee", "dB", 0.0f, 24.0f, d.kneeDb, false);
        global(kSolo, "solo", "Solo", "", 0.0f, 3.0f, d.solo, false, true);
        global(kSidechain, "sidechain", "Sidechain", "", 0.0f, 1.0f, d.sidechain, false, true);
        static constexpr const char* kBandNames[kBands] = {"Low", "Mid", "High"};
        for (int b = 0; b < kBands; ++b) {
            const Band& x = d.bands[static_cast<size_t>(b)];
            band(b, kOn, "on", "On", "", 0.0f, 1.0f, x.on, false, true, kBandNames[b]);
            band(b, kDownThresh, "down_thresh", "Down Threshold", "dB", -60.0f, 0.0f, x.downThreshDb, false, false, kBandNames[b]);
            band(b, kDownRatio, "down_ratio", "Down Ratio", ":1", 1.0f, 20.0f, x.downRatio, true, false, kBandNames[b]);
            band(b, kUpThresh, "up_thresh", "Up Threshold", "dB", -80.0f, 0.0f, x.upThreshDb, false, false, kBandNames[b]);
            band(b, kUpRatio, "up_ratio", "Up Ratio", ":1", 0.2f, 10.0f, x.upRatio, true, false, kBandNames[b]);
            band(b, kAttack, "attack", "Attack", "ms", 0.5f, 200.0f, x.attackMs, true, false, kBandNames[b]);
            band(b, kRelease, "release", "Release", "ms", 5.0f, 2000.0f, x.releaseMs, true, false, kBandNames[b]);
            band(b, kGain, "gain", "Gain", "dB", -18.0f, 18.0f, x.gainDb, false, false, kBandNames[b]);
            meter(gainMeter(b), "status_gain_", "Gain", "dB", -40.0f, 24.0f, b, kBandNames[b]);
            meter(levelMeter(b), "status_level_", "Level", "dB", -120.0f, 12.0f, b, kBandNames[b]);
        }
    }
    [[nodiscard]] const ParameterSpec& operator[](const std::uint32_t i) const { return specs_[i]; }

private:
    void put(const std::uint32_t i, const ParameterSpec& s) { specs_[i] = s; specs_[i].symbol = symbols_[i]; specs_[i].name = names_[i]; }
    void global(const std::uint32_t i, const char* sym, const char* name, const char* unit, const float lo, const float hi,
                const float def, const bool log, const bool integer = false)
    {
        std::snprintf(symbols_[i], sizeof(symbols_[i]), "%s", sym);
        std::snprintf(names_[i], sizeof(names_[i]), "%s", name);
        put(i, {nullptr, nullptr, unit, lo, hi, def, log, integer, false});
    }
    void band(const int b, const BandParameter p, const char* sym, const char* name, const char* unit, const float lo,
              const float hi, const float def, const bool log, const bool integer, const char* bandName)
    {
        const std::uint32_t i = bandParameter(b, p);
        std::snprintf(symbols_[i], sizeof(symbols_[i]), "band%d_%s", b + 1, sym);
        std::snprintf(names_[i], sizeof(names_[i]), "%s %s", bandName, name);
        put(i, {nullptr, nullptr, unit, lo, hi, def, log, integer, false});
    }
    void meter(const std::uint32_t i, const char* sym, const char* name, const char* unit, const float lo, const float hi,
               const int b, const char* bandName)
    {
        std::snprintf(symbols_[i], sizeof(symbols_[i]), "%s%d", sym, b + 1);
        std::snprintf(names_[i], sizeof(names_[i]), "%s %s", bandName, name);
        put(i, {nullptr, nullptr, unit, lo, hi, lo, false, false, true});
    }

    std::array<ParameterSpec, kParameterCount> specs_ {};
    char symbols_[kParameterCount][28] {};
    char names_[kParameterCount][32] {};
};

[[nodiscard]] inline const Specs& specs()
{
    static const Specs s;
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kInput:     return p.inputDb;
    case kOutput:    return p.outputDb;
    case kAmount:    return p.amount;
    case kTime:      return p.time;
    case kUpLimit:   return p.upLimitDb;
    case kCrossLow:  return p.crossLowHz;
    case kCrossHigh: return p.crossHighHz;
    default: break;
    }
    if (index == kCcChannel) return p.ccChannel;
    if (index == kKnee) return p.kneeDb;
    if (index == kSolo) return p.solo;
    if (index == kSidechain) return p.sidechain;
    if (index < kFirstBand || index >= kControlCount) return unused;
    Band& b = p.bands[(index - kFirstBand) / kPerBand];
    switch ((index - kFirstBand) % kPerBand) {
    case kOn:         return b.on;
    case kDownThresh: return b.downThreshDb;
    case kDownRatio:  return b.downRatio;
    case kUpThresh:   return b.upThreshDb;
    case kUpRatio:    return b.upRatio;
    case kAttack:     return b.attackMs;
    case kRelease:    return b.releaseMs;
    default:          return b.gainDb;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

// The control a Drift CC overrides, or -1: the panel moving this control releases its override.
[[nodiscard]] constexpr int liveSlot(const std::uint32_t index) noexcept
{
    return index == kAmount ? 0 : index == kTime ? 1 : index == kInput ? 2 : index == kUpLimit ? 3 : -1;
}

}  // namespace downspout::trio
