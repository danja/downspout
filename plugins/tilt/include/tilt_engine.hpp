#pragma once

#include "tilt_fft.hpp"
#include "tilt_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace downspout::tilt {

// Linked stereo tilt engine. The two paths run together:
//
//  * IIR: up to four smoothed first-order shelf sections (one per 6 dB/oct of slope),
//    with a zero near 20 Hz and a pole near 20 kHz for a boost, swapped for a cut.
//    The cascade is normalised at the pivot, so the curve crosses 0 dB there.
//  * FFT: 4096-point sqrt-Hann STFT, hop 512, with a real gain per bin from the
//    ideal straight-line response. Linear phase, reports 4096 samples of latency.
//
// A short crossfade switches between them; the minimum-phase path is delayed by the
// STFT latency while it is fading out so the two agree in phase during the switch.
// Neither path is used when the smoothed tilt is zero: it is bit-for-bit transparent.

inline constexpr int kFftSize = 4096;
inline constexpr int kFftHop = kFftSize / 8;
// Four whole 6 dB/oct shelves plus six log-spaced pole/zero pairs for the fractional remainder.
inline constexpr int kOustaloupSections = 6;
inline constexpr int kMaxSections = 4 + kOustaloupSections;
inline constexpr double kSmoothSeconds = 0.05;

struct FirstOrder {
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
};

struct IirChannel {
    std::array<double, kMaxSections> z {};
};

struct FftChannel {
    std::vector<float> fifo;
    std::vector<float> accum;
    int position = 0;
};

struct Engine {
    double sampleRate = 48000.0;
    bool allocated = false;

    // Smoothed control values and the resulting IIR design.
    double logPivot = std::log(1000.0);
    double exponent = 0.0;
    double modeMix = 0.0;
    double bypassMix = 0.0;
    bool primed = false;
    std::array<FirstOrder, kMaxSections> sections {};
    int sectionCount = 0;
    double normalizer = 1.0;

    std::array<IirChannel, 2> iir {};
    Fft fft {};
    std::vector<float> window;
    std::vector<float> scratch;
    std::vector<float> binGain;
    std::array<FftChannel, 2> channels {};
    int hopCounter = 0;
    float cola = 1.0f;

    // Latency-alignment rings for crossfades. Written every sample whether or not
    // the corresponding path is active, so they are always coherent.
    std::array<std::vector<float>, 2> iirDelay;
    std::array<std::vector<float>, 2> dryDelay;
    std::array<int, 2> delayPos {0, 0};

    double outputGain = 1.0;
};

// Allocates for a sample rate. Not real-time safe; call from activate/sampleRateChanged.
void activate(Engine& engine, double sampleRate);

// Clears audio state only; the patch lives in the caller's Parameters.
void reset(Engine& engine);

// Host latency in samples: the STFT size in FFT mode, zero in IIR mode.
[[nodiscard]] int latencySamples(Engine& engine, const Parameters& params) noexcept;

// Exact digital magnitude response of the IIR tilt, or the ideal straight line in FFT mode.
[[nodiscard]] double responseDb(const Engine& engine, const Parameters& params, double hz) noexcept;

void processBlock(Engine& engine, const Parameters& params, std::uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept;

}  // namespace downspout::tilt
