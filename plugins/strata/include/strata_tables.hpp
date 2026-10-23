#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace downspout::strata {

// Band-limited wavetables. Each frame is defined by a harmonic spectrum, and kMips copies of it
// are built with the harmonics above a shrinking limit removed, so a read at any pitch can pick
// the copy that stays under Nyquist. Inspired by the Wavetable device in oxide-studio's
// oxide-dsp, whose single-resolution tables alias heavily (see docs/design.md).

inline constexpr int kTableSize = 2048;           // samples per cycle
inline constexpr int kMaxHarmonics = kTableSize / 2;
inline constexpr int kFrames = 8;
inline constexpr int kCategories = 5;
inline constexpr int kMips = 21;                  // harmonic limits 1024 * 2^(-k/2), k = 0..20
inline constexpr int kPad = 4;                    // guard samples (1 before, 3 after) for cubic reads

enum class Category : int { Basic = 0, Harmonics, Formant, Digital, Vintage };

class Bank {
public:
    Bank();

    // Samples of one mip of one frame, kTableSize + kPad entries; index 1 is phase 0.
    [[nodiscard]] const float* frame(int category, int frameIndex, int mip) const noexcept
    {
        return data_[static_cast<size_t>(((category * kFrames) + frameIndex) * kMips + mip)].data();
    }

    // Harmonics kept in a mip.
    [[nodiscard]] static int harmonicLimit(int mip) noexcept;

    // The mip to read when the highest harmonic that stays under Nyquist is `allowed`: the finest
    // mip whose limit does not exceed it, so nothing is ever read that could alias.
    [[nodiscard]] static int mipForHarmonics(double allowed) noexcept;

    // One interpolated sample: cubic Hermite along the phase (0..1), linear between frames.
    [[nodiscard]] float read(int category, double position, int mip, double phase) const noexcept;

    // The first-harmonic-to-last amplitude spectrum of a stored frame, for tests: |c_h| for h = 1..N.
    [[nodiscard]] std::vector<double> spectrum(int category, int frameIndex, int mip) const;

private:
    std::vector<std::vector<float>> data_;
};

// Built once, on first use (construct the plugin on a non-audio thread so this is not paid in run()).
[[nodiscard]] const Bank& bank();

}  // namespace downspout::strata
