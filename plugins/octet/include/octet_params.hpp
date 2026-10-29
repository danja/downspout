#pragma once

#include "octet_core.hpp"
#include "octet_spectrum.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace downspout::octet {

// Parameter indices are stable: never reorder, only append.
// 0 mode, 1 output, then six per band: on, type, freq, gain, q, channel.
enum GlobalParameter : std::uint32_t { kMode = 0, kOutput = 1 };
enum BandParameter : std::uint32_t { kOn = 0, kType, kFreq, kGain, kQ, kChannel, kPerBand };

inline constexpr std::uint32_t kFirstBand = 2;
inline constexpr std::uint32_t kBandControlEnd = kFirstBand + kBands * kPerBand;
inline constexpr std::uint32_t kCcChannel = kBandControlEnd;   // appended after the band controls: parameter indices never move
inline constexpr std::uint32_t kCcBand = kBandControlEnd + 1;
inline constexpr std::uint32_t kSpectrumSource = kBandControlEnd + 2;   // appended: 0 off, 1 pre-EQ, 2 post-EQ
inline constexpr std::uint32_t kSpectrumFirst = kBandControlEnd + 3;    // 64 read-only outputs: band levels in dB, measured from the audio
inline constexpr std::uint32_t kMeterFirst = kSpectrumFirst + kSpectrumBins;   // appended: left and right output peak in dB, read-only
inline constexpr std::uint32_t kParameterCount = kMeterFirst + kMeterChannels;

[[nodiscard]] constexpr bool isSpectrumBin(const std::uint32_t index) noexcept
{
    return index >= kSpectrumFirst && index < kParameterCount;
}

[[nodiscard]] constexpr bool isMeter(const std::uint32_t index) noexcept
{
    return index >= kMeterFirst && index < kParameterCount;
}

[[nodiscard]] constexpr std::uint32_t bandParameter(const int band, const BandParameter p) noexcept
{
    return kFirstBand + static_cast<std::uint32_t>(band) * kPerBand + p;
}

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
    bool output;   // read-only: written by the audio thread, shown by the panel
};

// Built once: names and symbols need storage that outlives the host's calls.
class Specs {
public:
    Specs()
    {
        const Parameters defaults;
        set(kMode, "mode", "Mode", "", 0.0f, 2.0f, defaults.mode, false, true);
        set(kOutput, "output", "Output", "dB", -12.0f, 12.0f, defaults.outputDb, false, false);
        set(kCcChannel, "cc_channel", "CC Channel", "", 0.0f, 16.0f, defaults.ccChannel, false, true);
        set(kCcBand, "cc_band", "CC Band", "", 1.0f, static_cast<float>(kBands), defaults.ccBand, false, true);
        set(kSpectrumSource, "spectrum_source", "Spectrum", "", 0.0f, 2.0f, defaults.spectrumSource, false, true);
        for (int k = 0; k < kSpectrumBins; ++k) {
            const std::uint32_t i = kSpectrumFirst + static_cast<std::uint32_t>(k);
            std::snprintf(symbols_[i], sizeof(symbols_[i]), "spectrum_%02d", k + 1);
            std::snprintf(names_[i], sizeof(names_[i]), "Spectrum Band %02d", k + 1);
            specs_[i] = {symbols_[i], names_[i], "dB", kSpectrumFloorDb, 6.0f, kSpectrumFloorDb, false, false, true};
        }
        for (int c = 0; c < kMeterChannels; ++c) {
            const std::uint32_t i = kMeterFirst + static_cast<std::uint32_t>(c);
            std::snprintf(symbols_[i], sizeof(symbols_[i]), c == 0 ? "meter_left" : "meter_right");
            std::snprintf(names_[i], sizeof(names_[i]), c == 0 ? "Output Level Left" : "Output Level Right");
            specs_[i] = {symbols_[i], names_[i], "dB", kMeterFloorDb, 12.0f, kMeterFloorDb, false, false, true};
        }
        for (int b = 0; b < kBands; ++b) {
            const Band& d = defaults.bands[static_cast<size_t>(b)];
            add(b, kOn, "on", "On", "", 0.0f, 1.0f, d.on, false, true);
            add(b, kType, "type", "Type", "", 0.0f, static_cast<float>(kBandTypeCount - 1), d.type, false, true);
            add(b, kFreq, "freq", "Frequency", "Hz", 20.0f, 20000.0f, d.freqHz, true, false);
            add(b, kGain, "gain", "Gain", "dB", -18.0f, 18.0f, d.gainDb, false, false);
            add(b, kQ, "q", "Q", "", 0.1f, 18.0f, d.q, true, false);
            add(b, kChannel, "channel", "Channel", "", 0.0f, 2.0f, d.channel, false, true);
        }
    }
    [[nodiscard]] const ParameterSpec& operator[](const std::uint32_t i) const { return specs_[i]; }

private:
    void add(const int band, const BandParameter p, const char* sym, const char* name, const char* unit,
             const float lo, const float hi, const float def, const bool log, const bool integer)
    {
        const std::uint32_t i = bandParameter(band, p);
        std::snprintf(symbols_[i], sizeof(symbols_[i]), "band%d_%s", band + 1, sym);
        std::snprintf(names_[i], sizeof(names_[i]), "Band %d %s", band + 1, name);
        specs_[i] = {symbols_[i], names_[i], unit, lo, hi, def, log, integer, false};
    }
    void set(const std::uint32_t i, const char* sym, const char* name, const char* unit, const float lo, const float hi,
             const float def, const bool log, const bool integer)
    {
        std::snprintf(symbols_[i], sizeof(symbols_[i]), "%s", sym);
        std::snprintf(names_[i], sizeof(names_[i]), "%s", name);
        specs_[i] = {symbols_[i], names_[i], unit, lo, hi, def, log, integer, false};
    }

    std::array<ParameterSpec, kParameterCount> specs_ {};
    char symbols_[kParameterCount][24] {};
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
    if (index == kMode) return p.mode;
    if (index == kOutput) return p.outputDb;
    if (index == kCcChannel) return p.ccChannel;
    if (index == kCcBand) return p.ccBand;
    if (index == kSpectrumSource) return p.spectrumSource;
    if (index < kFirstBand || index >= kBandControlEnd) return unused;   // including the spectrum and meter outputs: not part of the patch
    Band& b = p.bands[(index - kFirstBand) / kPerBand];
    switch ((index - kFirstBand) % kPerBand) {
    case kOn:      return b.on;
    case kType:    return b.type;
    case kFreq:    return b.freqHz;
    case kGain:    return b.gainDb;
    case kQ:       return b.q;
    default:       return b.channel;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

// The control a Drift CC overrides, or -1: the panel moving this control releases its override. CC 1-3 act on the band
// chosen by `ccBand` (1-8), so which indices count depends on it.
[[nodiscard]] constexpr int liveSlot(const std::uint32_t index, const int ccBand) noexcept
{
    const int band = ccBand < 1 ? 0 : ccBand > kBands ? kBands - 1 : ccBand - 1;
    return index == bandParameter(band, kFreq) ? 0 : index == bandParameter(band, kGain) ? 1 : index == bandParameter(band, kQ) ? 2
         : index == kOutput ? 3 : -1;
}

}  // namespace downspout::octet
