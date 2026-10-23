#pragma once

#include <algorithm>
#include <cmath>

namespace downspout::dsp {

// Trapezoidal (zero-delay-feedback) state-variable filter in double precision, shared by the synths that carry
// the same five-type filter (Strata, Quartet). `resonance` 0..1 maps to Q = 1/sqrt(2) (Butterworth) .. 20.
//
// Moving a plugin onto this header must not change its output: the code is the code those plugins had, moved
// verbatim, and the plugins' tests (and a bit-exact render checksum) hold it to that.

enum class SvfMode : int { LowPass12 = 0, LowPass24, HighPass12, BandPass, Notch };

// g = tan(pi fc / sr), k = 1 / Q, and the three terms the per-sample update needs.
struct SvfCoeff {
    double g = 0.0, k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
};

// One filter memory; a stereo voice keeps one per channel (and a second per channel for the 24 dB low-pass).
struct Svf {
    double ic1 = 0.0, ic2 = 0.0;
};

struct SvfOut {
    double lp, bp, hp;
};

[[nodiscard]] inline SvfCoeff makeSvf(const double cutoff, const double resonance, const double sr) noexcept
{
    constexpr double kPi = 3.14159265358979323846;
    const double fc = std::min(std::max(cutoff, 20.0), std::min(20000.0, sr * 0.45));
    SvfCoeff c;
    c.g = std::tan(kPi * fc / sr);
    // res 0 -> Q = 1/sqrt(2) (Butterworth), res 1 -> Q = 20.
    c.k = 1.41421356 - (1.41421356 - 0.05) * std::min(std::max(resonance, 0.0), 1.0);
    c.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    c.a2 = c.g * c.a1;
    c.a3 = c.g * c.a2;
    return c;
}

[[nodiscard]] inline SvfOut runSvf(Svf& s, const SvfCoeff& c, const double x) noexcept
{
    const double v3 = x - s.ic2;
    const double v1 = c.a1 * s.ic1 + c.a2 * v3;
    const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    return {v2, v1, x - c.k * v1 - v2};
}

// One sample through the chosen response. `c2` (Q = 1/sqrt(2)) and `s2` are the second stage of the 24 dB low-pass.
[[nodiscard]] inline double filterSample(const SvfMode mode, Svf& s1, Svf& s2, const SvfCoeff& c, const SvfCoeff& c2, const double x) noexcept
{
    const SvfOut a = runSvf(s1, c, x);
    switch (mode) {
    case SvfMode::LowPass12:  return a.lp;
    case SvfMode::LowPass24:  return runSvf(s2, c2, a.lp).lp;
    case SvfMode::HighPass12: return a.hp;
    case SvfMode::BandPass:   return c.k * a.bp;
    default:                  return a.lp + a.hp;  // notch
    }
}

}  // namespace downspout::dsp
