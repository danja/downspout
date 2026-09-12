#pragma once

#include "modules/Waveguide.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace downspout::magneto {

inline constexpr int kMufflerElements = 4;

// One-way delay bounds at 192 kHz:
//   pipe     4.00 m          -> 2239 samples
//   muffler  1.50 m x 1.40   -> 1176 samples
//   outlet   1.00 m          ->  560 samples
inline constexpr std::size_t kPipeLineLen = 4096;
inline constexpr std::size_t kMufflerLineLen = 2048;
inline constexpr std::size_t kOutletLineLen = 1024;

// Muffler element length ratios around the average-length parameter. A
// waveguide of one-way delay k resonates at multiples of fs/(2k), so four
// elements share a peak only where their delays share a factor. Snapping each
// delay to a prime makes them pairwise coprime, which is the paper's
// "every frequency has a peak in at most one element".
inline constexpr std::array<float, kMufflerElements> kMufflerRatio = {{
    0.80f, 0.95f, 1.15f, 1.40f,
}};

[[nodiscard]] inline bool isPrime(const int n) noexcept
{
    if (n < 2)
        return false;
    if (n % 2 == 0)
        return n == 2;
    for (int d = 3; d * d <= n; d += 2)
    {
        if (n % d == 0)
            return false;
    }
    return true;
}

// Nearest prime at or below the requested delay, floored at 5. Called at
// control rate only.
[[nodiscard]] inline float nearestPrimeDelay(const float delaySamples) noexcept
{
    int n = static_cast<int>(delaySamples);
    if (n < 5)
        return 5.0f;
    while (n > 5 && !isPrime(n))
        --n;
    return static_cast<float>(n);
}

// Junction reflection coefficients where one element meets the next.
inline constexpr float kManifoldJunction = 0.10f;
inline constexpr float kPipeJunction = 0.20f;
inline constexpr float kMufflerJunction = 0.20f;
inline constexpr float kOutletFreeEnd = -0.35f;  // open tailpipe

struct ExhaustState {
    Waveguide<kPipeLineLen> pipe;
    std::array<Waveguide<kMufflerLineLen>, kMufflerElements> muffler;
    Waveguide<kOutletLineLen> outlet;

    void reset() noexcept
    {
        pipe.reset();
        for (auto& element : muffler)
            element.reset();
        outlet.reset();
    }
};

}  // namespace downspout::magneto
