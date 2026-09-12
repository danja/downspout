#pragma once

#include "modules/DelayLine.hpp"
#include "modules/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace downspout::magneto {

// Bidirectional digital waveguide (SIVE'15 figure 4): two delay lines of equal
// length with a reflection coefficient at each end.
//
//   * a positive coefficient preserves phase, modelling a closed end;
//   * a negative coefficient inverts phase, modelling an open end;
//   * the radiated signal at an end is (1 - |g|) times the wave arriving there,
//     so |g| = 1 radiates nothing and |g| = 0 radiates everything.
//
// Both the two-port split above and the 1/sqrt(M) parallel junction below are
// non-expansive, and every pass is additionally scaled by a loss gain derived
// from a target round-trip gain. Together those three facts are what keep a
// network of thirty of these bounded. See docs/design.md.
//
// Use is strictly two-phase across the whole network: read() every waveguide,
// then compute every junction, then write() every waveguide. That is what keeps
// mutually connected elements free of delay-free loops.

inline constexpr float kMaxReflection = 0.95f;
inline constexpr float kSpeedOfSound = 343.0f;  // m/s
inline constexpr float kDelaySlewPerSample = 0.10f;
inline constexpr float kDelayLineClamp = 4.0f;

// One-way travel time, in samples, for a tube of the given length. The round
// trip is twice this, so the fundamental resonance is c / (2L).
[[nodiscard]] inline float tubeDelaySamples(const float lengthMetres, const float sampleRate) noexcept
{
    return lengthMetres * sampleRate / kSpeedOfSound;
}

// Per-pass gain that realises `roundTrip` total gain over a there-and-back trip
// of `delay` samples each way, independent of length and sample rate.
[[nodiscard]] inline float lossGainForRoundTrip(const float roundTrip, const float delay) noexcept
{
    const float d = std::max(delay, 1.0f);
    return std::pow(std::clamp(roundTrip, 0.01f, 0.999f), 1.0f / (2.0f * d));
}

template <std::size_t N>
struct Waveguide {
    DelayLine<N> forward;  // travels near end -> far end
    DelayLine<N> reverse;  // travels far end -> near end
    OnePoleLowpass lossForward;
    OnePoleLowpass lossReverse;

    float delay = 8.0f;      // current one-way delay in samples (slew limited)
    float gain = 0.999f;     // per-pass loss gain
    float damping = 0.5f;    // one-pole coefficient, 1.0 = no damping
    float atFar = 0.0f;      // wave arriving at the far end this sample
    float atNear = 0.0f;     // wave arriving at the near end this sample

    void reset() noexcept
    {
        forward.reset();
        reverse.reset();
        lossForward.reset();
        lossReverse.reset();
        atFar = 0.0f;
        atNear = 0.0f;
    }

    // Move toward a new one-way delay without letting the read pointer approach
    // the write pointer. An unbounded jump would both corrupt the read and give
    // the loop a transient Doppler gain of 1 / (1 - dD/dn).
    void setDelayTarget(const float target) noexcept
    {
        const float wanted = std::clamp(target, DelayLine<N>::kMinDelay, DelayLine<N>::kMaxDelay);
        const float step = std::clamp(wanted - delay, -kDelaySlewPerSample, kDelaySlewPerSample);
        delay += step;
    }

    void read() noexcept
    {
        atFar = gain * lossForward.process(forward.readHermite(delay), damping);
        atNear = gain * lossReverse.process(reverse.readHermite(delay), damping);
    }

    [[nodiscard]] float radiatedNear(const float alpha) const noexcept
    {
        return (1.0f - std::fabs(std::clamp(alpha, -kMaxReflection, kMaxReflection))) * atNear;
    }

    [[nodiscard]] float radiatedFar(const float beta) const noexcept
    {
        return (1.0f - std::fabs(std::clamp(beta, -kMaxReflection, kMaxReflection))) * atFar;
    }

    void write(const float inputNear, const float inputFar, const float alpha, const float beta) noexcept
    {
        const float a = std::clamp(alpha, -kMaxReflection, kMaxReflection);
        const float b = std::clamp(beta, -kMaxReflection, kMaxReflection);
        forward.write(std::clamp(flushDenormal(a * atNear + inputNear), -kDelayLineClamp, kDelayLineClamp));
        reverse.write(std::clamp(flushDenormal(b * atFar + inputFar), -kDelayLineClamp, kDelayLineClamp));
    }
};

}  // namespace downspout::magneto
