#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace downspout::magneto {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

// ── Sine table ─────────────────────────────────────────────────────────────
//
// Twelve cylinders need four cycle functions each, so a naive implementation
// evaluates 48 transcendentals per sample. A 4096-entry table with linear
// interpolation has a maximum error near 1e-7 and costs an array read.

struct SineTable {
    static constexpr std::size_t kSize = 4096;

    std::array<float, kSize + 1> data {};

    SineTable()
    {
        for (std::size_t i = 0; i <= kSize; ++i)
            data[i] = std::sin(kTwoPi * static_cast<float>(i) / static_cast<float>(kSize));
    }
};

inline const SineTable kSineTable {};

// sin(2*pi*u) with u expressed in turns; wraps for any input.
[[nodiscard]] inline float sinTurns(float u) noexcept
{
    u -= std::floor(u);
    float pos = u * static_cast<float>(SineTable::kSize);
    if (!(pos >= 0.0f))
        pos = 0.0f;
    auto index = static_cast<std::size_t>(pos);
    if (index >= SineTable::kSize)
        index = SineTable::kSize - 1;
    const float frac = pos - static_cast<float>(index);
    const float a = kSineTable.data[index];
    const float b = kSineTable.data[index + 1];
    return a + frac * (b - a);
}

// ── Four-stroke cycle ──────────────────────────────────────────────────────
//
// One phasor ramp x in [0,1) is one full engine cycle, i.e. two crankshaft
// revolutions. Phase convention:
//
//   0.00 – 0.25  intake
//   0.25 – 0.50  compression
//   0.50 – 0.75  expansion (power)
//   0.75 – 1.00  exhaust
//
// The valve and piston functions are the SIVE'15 equations verbatim. The
// ignition function follows the paper's prose ("the positive half of a sine
// wave, shifted at the beginning of the expansion phase and rescaled by t")
// rather than its printed formula, which places the pulse at x = 0 — where the
// intake valve is open, so the combustion impulse would vent through the
// intake. See docs/design.md.

inline constexpr float kIntakeWindowStart = 0.0f;
inline constexpr float kExhaustWindowStart = 0.75f;
inline constexpr float kIgnitionWindowStart = 0.5f;

// Exhaust valve: positive half sine over the last quarter of the cycle.
[[nodiscard]] inline float exhaustValve(const float x) noexcept
{
    return (x > kExhaustWindowStart && x < 1.0f) ? -sinTurns(2.0f * x) : 0.0f;
}

// Intake valve: the same shape over the first quarter of the cycle.
[[nodiscard]] inline float intakeValve(const float x) noexcept
{
    return (x > kIntakeWindowStart && x < kIntakeWindowStart + 0.25f) ? sinTurns(2.0f * x) : 0.0f;
}

// Piston motion: harmonic, at maximum height at the start of the cycle, two
// full excursions per engine cycle. cos(4*pi*x) == sin(2*pi*(2x + 0.25)).
[[nodiscard]] inline float pistonMotion(const float x) noexcept
{
    return sinTurns(2.0f * x + 0.25f);
}

// Fuel ignition: positive half sine starting at the expansion phase. `t` scales
// the pulse across the second half of the cycle, so t = 1 spans expansion plus
// exhaust and t -> 0 approaches an instantaneous explosion.
[[nodiscard]] inline float fuelIgnition(const float x, const float t) noexcept
{
    if (t <= 0.0f)
        return 0.0f;
    const float width = 0.5f * t;
    const float u = (x - kIgnitionWindowStart) / width;
    if (u <= 0.0f || u >= 1.0f)
        return 0.0f;
    return sinTurns(0.5f * u);  // sin(pi * u)
}

// ── Phasor ─────────────────────────────────────────────────────────────────

struct Phasor {
    float phase = 0.0f;
    bool wrapped = false;

    void reset() noexcept
    {
        phase = 0.0f;
        wrapped = false;
    }

    // Advance by one sample and report whether the cycle restarted.
    float advance(const float increment) noexcept
    {
        phase += increment;
        wrapped = phase >= 1.0f;
        if (wrapped)
            phase -= std::floor(phase);
        if (!(phase >= 0.0f) || phase >= 1.0f)
            phase = 0.0f;
        return phase;
    }
};

// One engine cycle is two crankshaft revolutions, so f = (RPM/60)/2 = RPM/120.
[[nodiscard]] inline float cycleFrequencyHz(const float rpm) noexcept
{
    return rpm / 120.0f;
}

[[nodiscard]] inline float wrapPhase(const float x) noexcept
{
    const float f = x - std::floor(x);
    return (f >= 0.0f && f < 1.0f) ? f : 0.0f;
}

// ── Cylinder phase offsets ─────────────────────────────────────────────────
//
// Symmetric firing spaces the cylinders evenly over the cycle. "Growl" skews
// alternate firing intervals, which is the mechanism behind the cross-plane V8
// and chopper lope the paper describes. Offsets are renormalised so the cycle
// always closes exactly, and reduce to k/N when asymmetry is zero.

inline constexpr float kAsymmetrySkew = 0.5f;

template <std::size_t MaxCylinders>
void cylinderPhaseOffsets(const int count,
                          const float asymmetry,
                          std::array<float, MaxCylinders>& out) noexcept
{
    const int n = std::clamp(count, 1, static_cast<int>(MaxCylinders));
    const float a = std::clamp(asymmetry, 0.0f, 1.0f);

    float total = 0.0f;
    for (int k = 0; k < n; ++k)
    {
        out[static_cast<std::size_t>(k)] = total;
        const float sign = (k & 1) ? -1.0f : 1.0f;
        total += (1.0f + a * kAsymmetrySkew * sign) / static_cast<float>(n);
    }

    if (total > 1.0e-6f)
    {
        const float norm = 1.0f / total;
        for (int k = 0; k < n; ++k)
            out[static_cast<std::size_t>(k)] *= norm;
    }

    for (auto k = static_cast<std::size_t>(n); k < MaxCylinders; ++k)
        out[k] = 0.0f;
}

}  // namespace downspout::magneto
