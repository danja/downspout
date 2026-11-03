#pragma once

#include "wheelhouse_core.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace downspout::wheelhouse {

// Parameter indices are stable: never reorder, only append.
enum ParameterId : std::uint32_t {
    kUpperDrawbar0 = 0,                                   // 0-8: upper 16' .. 1'
    kLowerDrawbar0 = kUpperDrawbar0 + kDrawbarCount,      // 9-17
    kPedalDrawbar0 = kLowerDrawbar0 + kDrawbarCount,      // 18-19
    kPercOn = kPedalDrawbar0 + kPedalDrawbarCount,        // 20
    kPercHarmonic, kPercDecay, kPercLevel,
    kVibrato, kVibUpper, kVibLower, kScannerRate,
    kClick, kDrive, kTone, kWidth, kVolume,
    kUpperChannel, kLowerChannel, kPedalChannel,
    kParameterCount
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool integer;
};

inline constexpr std::array<const char*, kDrawbarCount> kFootageNames = {"16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"};
inline constexpr std::array<const char*, kDrawbarCount> kFootageSymbols = {"16", "5_1_3", "8", "4", "2_2_3", "2", "1_3_5", "1_1_3", "1"};

inline float field(const Parameters& p, const std::uint32_t i) noexcept
{
    if (i < kLowerDrawbar0) return p.upper[i - kUpperDrawbar0];
    if (i < kPedalDrawbar0) return p.lower[i - kLowerDrawbar0];
    if (i < kPercOn) return p.pedal[i - kPedalDrawbar0];
    switch (i) {
    case kPercOn:        return p.percOn;
    case kPercHarmonic:  return p.percHarmonic;
    case kPercDecay:     return p.percDecay;
    case kPercLevel:     return p.percLevel;
    case kVibrato:       return p.vibrato;
    case kVibUpper:      return p.vibUpper;
    case kVibLower:      return p.vibLower;
    case kScannerRate:   return p.scannerRate;
    case kClick:         return p.click;
    case kDrive:         return p.drive;
    case kTone:          return p.tone;
    case kWidth:         return p.width;
    case kVolume:        return p.volume;
    case kUpperChannel:  return p.upperChannel;
    case kLowerChannel:  return p.lowerChannel;
    case kPedalChannel:  return p.pedalChannel;
    default:             return 0.0f;
    }
}

inline void setField(Parameters& p, const std::uint32_t i, const float v) noexcept
{
    if (i < kLowerDrawbar0) { p.upper[i - kUpperDrawbar0] = v; return; }
    if (i < kPedalDrawbar0) { p.lower[i - kLowerDrawbar0] = v; return; }
    if (i < kPercOn) { p.pedal[i - kPedalDrawbar0] = v; return; }
    switch (i) {
    case kPercOn:        p.percOn = v; break;
    case kPercHarmonic:  p.percHarmonic = v; break;
    case kPercDecay:     p.percDecay = v; break;
    case kPercLevel:     p.percLevel = v; break;
    case kVibrato:       p.vibrato = v; break;
    case kVibUpper:      p.vibUpper = v; break;
    case kVibLower:      p.vibLower = v; break;
    case kScannerRate:   p.scannerRate = v; break;
    case kClick:         p.click = v; break;
    case kDrive:         p.drive = v; break;
    case kTone:          p.tone = v; break;
    case kWidth:         p.width = v; break;
    case kVolume:        p.volume = v; break;
    case kUpperChannel:  p.upperChannel = v; break;
    case kLowerChannel:  p.lowerChannel = v; break;
    case kPedalChannel:  p.pedalChannel = v; break;
    default: break;
    }
}

// The parameter table. Built once; the drawbars are generated from the footage names so they are typed once.
struct SpecTable {
    std::array<ParameterSpec, kParameterCount> specs {};
    std::array<std::string, kParameterCount> names {};
    std::array<std::string, kParameterCount> symbols {};

    SpecTable()
    {
        const Parameters d;
        const auto put = [&](const std::uint32_t i, std::string symbol, std::string name, const char* unit, const float lo, const float hi,
                             const bool integer) {
            symbols[i] = std::move(symbol);
            names[i] = std::move(name);
            specs[i] = {symbols[i].c_str(), names[i].c_str(), unit, lo, hi, field(d, i), integer};
        };
        for (std::uint32_t f = 0; f < kDrawbarCount; ++f) {
            put(kUpperDrawbar0 + f, std::string("upper_") + kFootageSymbols[f], std::string("Upper ") + kFootageNames[f], "", 0.0f, 8.0f, true);
            put(kLowerDrawbar0 + f, std::string("lower_") + kFootageSymbols[f], std::string("Lower ") + kFootageNames[f], "", 0.0f, 8.0f, true);
        }
        put(kPedalDrawbar0, "pedal_16", "Pedal 16'", "", 0.0f, 8.0f, true);
        put(kPedalDrawbar0 + 1, "pedal_8", "Pedal 8'", "", 0.0f, 8.0f, true);
        put(kPercOn, "perc_on", "Percussion", "", 0.0f, 1.0f, true);
        put(kPercHarmonic, "perc_harmonic", "Perc Harmonic", "", 0.0f, 1.0f, true);
        put(kPercDecay, "perc_decay", "Perc Decay", "", 0.0f, 1.0f, true);
        put(kPercLevel, "perc_level", "Perc Level", "", 0.0f, 1.0f, true);
        put(kVibrato, "vibrato", "Vibrato", "", 0.0f, static_cast<float>(kVibratoModeCount - 1), true);
        put(kVibUpper, "vib_upper", "Vibrato Upper", "", 0.0f, 1.0f, true);
        put(kVibLower, "vib_lower", "Vibrato Lower", "", 0.0f, 1.0f, true);
        put(kScannerRate, "scanner_rate", "Scanner Rate", "Hz", kMinScannerRate, kMaxScannerRate, false);
        put(kClick, "click", "Key Click", "", 0.0f, 1.0f, false);
        put(kDrive, "drive", "Drive", "", 0.0f, 1.0f, false);
        put(kTone, "tone", "Tone", "", 0.0f, 1.0f, false);
        put(kWidth, "width", "Width", "", 0.0f, 1.0f, false);
        put(kVolume, "volume", "Volume", "", 0.0f, 1.0f, false);
        put(kUpperChannel, "upper_channel", "Upper Channel", "", 0.0f, 16.0f, true);
        put(kLowerChannel, "lower_channel", "Lower Channel", "", 0.0f, 16.0f, true);
        put(kPedalChannel, "pedal_channel", "Pedal Channel", "", 0.0f, 16.0f, true);
    }
    SpecTable(const SpecTable&) = delete;
    SpecTable& operator=(const SpecTable&) = delete;
};

[[nodiscard]] inline const std::array<ParameterSpec, kParameterCount>& specs()
{
    static const SpecTable t;
    return t.specs;
}

}  // namespace downspout::wheelhouse
