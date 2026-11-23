#pragma once

#include <array>
#include <cstdint>

namespace downspout::spliff {

// Spectrum analyser for the panel: a Hann-windowed 4096-point FFT of the mono sum, refreshed about 30 times a second,
// folded into 64 log-spaced bands from 20 Hz to 20 kHz. Each band is a level in dB (a full-scale sine reads 0 dB at its
// peak), with an instant attack and a slow fall so the display is readable. The wrapper chooses which signal to feed it
// (input, output or wet-minus-dry) and publishes the levels as read-only output parameters.
inline constexpr int kSpectrumBins = 64;
inline constexpr int kFftSize = 4096;
inline constexpr float kSpectrumFloorDb = -90.0f;
inline constexpr float kSpectrumFallDbPerSecond = 40.0f;

// Which signal the analyser shows. The wrapper persists it as text state but exposes it as a real
// appended host parameter so UI switches reach the processor in every VST3 host.
enum class SpectrumSource : int { Input = 0, Output, Delta };

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
void analyseBlock(SpectrumState& state, float sampleRate, std::uint32_t frames,
                  const float* left, const float* right) noexcept;

}  // namespace downspout::spliff
