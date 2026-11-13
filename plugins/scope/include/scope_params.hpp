#pragma once

#include <array>
#include <cstdio>
#include <cstdint>

namespace downspout::scope {

inline constexpr int kFftSize = 2048;
inline constexpr int kBins = kFftSize / 2 + 1;
inline constexpr int kBands = 64;
inline constexpr int kWaveformSize = 128;

// Stable parameter indices: never reorder, only append. The first four are the
// interactive controls; everything from kPeakL on is a read-only analysis output.
enum ParamId : int {
    kRange = 0,   // 0 = 60 dB, 1 = 90 dB, 2 = 120 dB display range
    kSpeed,       // ballistics (0 slow .. 1 fast)
    kHold,        // peak hold off/on
    kMode,        // 0 spectrum, 1 levels, 2 oscilloscope
    kPeakL,       // output: peak L in dB
    kPeakR,       // output: peak R in dB
    kRmsL,        // output: RMS L in dB
    kRmsR,        // output: RMS R in dB
    kCorr,        // output: correlation -1..1
    kLufsMomentaryL,
    kLufsMomentaryR,
    kLufsShortL,
    kLufsShortR,
    kLufsIntegratedL,
    kLufsIntegratedR,
    kWaveL0,
    kWaveR0 = kWaveL0 + kWaveformSize,
    kSpec0 = kWaveR0 + kWaveformSize,
    kSpecCount = kBands,
    kParamCount = kSpec0 + kSpecCount
};

inline constexpr int kControlCount = kPeakL;

inline constexpr float kRangeDb[3] = {60.0f, 90.0f, 120.0f};

struct Params {
    float rangeDb = 60.0f;   // display floor in dB (positive)
    float speed = 0.5f;      // 0..1 ballistics
    float hold = 1.0f;       // peak hold off/on
    float mode = 0.0f;       // 0 spectrum, 1 levels, 2 oscilloscope
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
    bool output = false;
};

// The spectrum bands need per-band symbols and names, so the table is built at
// startup rather than written out as a 73-entry literal.
struct ParameterTable {
    std::array<ParameterSpec, kParamCount> specs {};
    std::array<std::array<char, 16>, kSpecCount> symbols {};
    std::array<std::array<char, 20>, kSpecCount> names {};

    ParameterTable()
    {
        specs[kRange] = {"range", "Range", "dB", 0.0f, 2.0f, 0.0f, true, false};
        specs[kSpeed] = {"speed", "Speed", "", 0.0f, 1.0f, 0.5f, false, false};
        specs[kHold] = {"hold", "Peak hold", "", 0.0f, 1.0f, 1.0f, true, false};
        specs[kMode] = {"mode", "Mode", "", 0.0f, 2.0f, 0.0f, true, false};
        specs[kPeakL] = {"peak_l", "Peak L", "dB", -120.0f, 0.0f, -120.0f, false, true};
        specs[kPeakR] = {"peak_r", "Peak R", "dB", -120.0f, 0.0f, -120.0f, false, true};
        specs[kRmsL] = {"rms_l", "RMS L", "dB", -120.0f, 0.0f, -120.0f, false, true};
        specs[kRmsR] = {"rms_r", "RMS R", "dB", -120.0f, 0.0f, -120.0f, false, true};
        specs[kCorr] = {"corr", "Correlation", "", -1.0f, 1.0f, 0.0f, false, true};
        specs[kLufsMomentaryL] = {"lufs_m_l", "Momentary LUFS L", "LUFS", -60.0f, 0.0f, -60.0f, false, true};
        specs[kLufsMomentaryR] = {"lufs_m_r", "Momentary LUFS R", "LUFS", -60.0f, 0.0f, -60.0f, false, true};
        specs[kLufsShortL] = {"lufs_s_l", "Short LUFS L", "LUFS", -60.0f, 0.0f, -60.0f, false, true};
        specs[kLufsShortR] = {"lufs_s_r", "Short LUFS R", "LUFS", -60.0f, 0.0f, -60.0f, false, true};
        specs[kLufsIntegratedL] = {"lufs_i_l", "Integrated LUFS L", "LUFS", -60.0f, 0.0f, -60.0f, false, true};
        specs[kLufsIntegratedR] = {"lufs_i_r", "Integrated LUFS R", "LUFS", -60.0f, 0.0f, -60.0f, false, true};
        for (int i = 0; i < kWaveformSize; ++i) {
            std::array<char, 16> waveSymbol {};
            std::array<char, 24> waveName {};
            std::snprintf(waveSymbol.data(), waveSymbol.size(), "wave_l_%03d", i);
            std::snprintf(waveName.data(), waveName.size(), "Wave L %03d", i);
            specs[static_cast<std::size_t>(kWaveL0 + i)] = {waveSymbol.data(), waveName.data(), "", -1.0f, 1.0f, 0.0f, false, true};
            std::snprintf(waveSymbol.data(), waveSymbol.size(), "wave_r_%03d", i);
            std::snprintf(waveName.data(), waveName.size(), "Wave R %03d", i);
            specs[static_cast<std::size_t>(kWaveR0 + i)] = {waveSymbol.data(), waveName.data(), "", -1.0f, 1.0f, 0.0f, false, true};
        }
        for (int i = 0; i < kSpecCount; ++i) {
            std::snprintf(symbols[static_cast<std::size_t>(i)].data(),
                          symbols[static_cast<std::size_t>(i)].size(), "spec_%02d", i);
            std::snprintf(names[static_cast<std::size_t>(i)].data(),
                          names[static_cast<std::size_t>(i)].size(), "Band %02d", i);
            specs[static_cast<std::size_t>(kSpec0 + i)] = {
                symbols[static_cast<std::size_t>(i)].data(),
                names[static_cast<std::size_t>(i)].data(),
                "dB", -120.0f, 0.0f, -120.0f, false, true};
        }
    }
};

inline const ParameterTable& parameterTable()
{
    static const ParameterTable table;
    return table;
}

}  // namespace downspout::scope
