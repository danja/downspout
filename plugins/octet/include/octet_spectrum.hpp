#pragma once

#include <array>
#include <cstdint>

namespace downspout::octet {

// Spectrum analyser for the panel: a Hann-windowed 4096-point FFT of the mono sum, refreshed about 30 times a second,
// folded into 64 log-spaced bands from 20 Hz to 20 kHz. Each band is a level in dB (a full-scale sine reads 0 dB at its
// peak), with an instant attack and a slow fall so the display is readable. Portable: the wrapper only chooses which
// signal to feed it (see SpectrumSource) and publishes the 64 values as read-only output parameters.

inline constexpr int kSpectrumBins = 64;
inline constexpr int kFftSize = 4096;
inline constexpr float kSpectrumFloorDb = -90.0f;
inline constexpr float kSpectrumFallDbPerSecond = 40.0f;

// Which signal the analyser shows. Off costs nothing.
enum class SpectrumSource : int { Off = 0, Pre, Post };

struct SpectrumState {
    std::array<float, kFftSize> ring {};
    int writePos = 0;
    int sinceUpdate = 0;
    std::array<double, kFftSize> re {}, im {};        // FFT scratch (64 KB, in the state so nothing is thread-local or on the stack)
    std::array<float, kSpectrumBins> levelDb {};   // starts at the floor after resetSpectrum
    bool primed = false;
};

// Edge frequencies of band `k` (0..kSpectrumBins): geometric from 20 Hz to 20 kHz.
[[nodiscard]] double spectrumBandEdgeHz(int k) noexcept;

// Clears the analyser memory only.
void resetSpectrum(SpectrumState& state) noexcept;

// Feeds `frames` stereo frames (right may equal left for a mono source) and refreshes `state.levelDb` when a hop has passed.
void analyseBlock(SpectrumState& state, float sampleRate, std::uint32_t frames, const float* left, const float* right) noexcept;

// Output level meter for the panel: the peak of each output channel in dB, instant attack and a steady fall.
inline constexpr int kMeterChannels = 2;
inline constexpr float kMeterFloorDb = -60.0f;
inline constexpr float kMeterFallDbPerSecond = 30.0f;

struct MeterState {
    std::array<float, kMeterChannels> peakDb {kMeterFloorDb, kMeterFloorDb};
};

void resetMeter(MeterState& state) noexcept;

// Folds one block of the finished output into the meter.
void updateMeter(MeterState& state, float sampleRate, std::uint32_t frames, const float* left, const float* right) noexcept;

}  // namespace downspout::octet
