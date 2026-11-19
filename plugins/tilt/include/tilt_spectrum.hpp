#pragma once

#include "tilt_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::tilt {

// Spectrum analyser for the panel: a Hann-windowed 4096-point FFT of the mono sum,
// refreshed about 30 times a second and folded into 64 log-spaced bands from 20 Hz
// to 20 kHz. A full-scale sine reads 0 dB at its peak. The audio core never uses
// this; it is measurement state for the read-only output parameters.

inline constexpr int kSpectrumFftSize = 4096;
inline constexpr float kSpectrumFloorDb = -90.0f;
inline constexpr float kSpectrumFallDbPerSecond = 40.0f;

struct SpectrumState {
    std::array<float, kSpectrumFftSize> ring {};
    int writePos = 0;
    int sinceUpdate = 0;
    std::array<double, kSpectrumFftSize> re {}, im {};
    std::array<float, kSpectrumBins> levelDb {};
    bool primed = false;
};

void resetSpectrum(SpectrumState& state) noexcept;
void analyseBlock(SpectrumState& state, float sampleRate, std::uint32_t frames,
                  const float* left, const float* right) noexcept;

inline constexpr float kMeterFloorDb = -60.0f;
inline constexpr float kMeterFallDbPerSecond = 30.0f;

struct MeterState {
    std::array<float, 2> peakDb {kMeterFloorDb, kMeterFloorDb};
};

void resetMeter(MeterState& state) noexcept;
void updateMeter(MeterState& state, float sampleRate, std::uint32_t frames,
                 const float* left, const float* right) noexcept;

}  // namespace downspout::tilt
