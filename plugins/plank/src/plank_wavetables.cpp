#include "plank_voice.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::plank {
namespace {

// Harmonic recipe for one table.
struct Shape {
    int harmonics;         // how many partials to sum
    float amplitudePower;  // amplitude falls off as 1/k^power
    float amplitudeScale;  // overall trim before normalising
    float phaseSpread;     // 0 keeps every partial in phase, 1 scatters them
    float evenBias;        // weight on even harmonics, relative to odd
    int pulseWidth;        // for pulse shapes: duty cycle in 1/16ths, 0 = saw
};

// Seventeen tables, ordered from bright and buzzy to dark and round. The first
// eight are the classic shapes Plinky opens its morph with; the last nine step
// a Miunau-style harmonic series down to a near-sine.
//
// Index 0..3 widen and square up, 4..7 thin to a couple of partials, and 8..16
// sweep the harmonic count and spectral tilt.
constexpr Shape kShapes[kWavetableCount] = {
    //  harmonics  power  scale   spread  even   pulse(1/16)
    {        64,   1.00f,  1.00f,  0.00f, 1.00f,  0},  // saw
    {        64,   1.00f,  0.72f,  0.05f, 0.65f,  2},  // narrow pulse
    {        64,   1.00f,  0.86f,  0.08f, 0.90f,  4},  // pulse
    {        64,   1.35f,  0.60f,  0.10f, 0.00f,  8},  // square, odd only
    {        32,   1.80f,  0.90f,  0.12f, 0.00f,  0},  // triangle-ish
    {         5,   0.70f,  1.00f,  0.00f, 1.00f,  0},  // five partials
    {         3,   0.40f,  1.00f,  0.00f, 0.80f,  0},  // three partials
    {         2,   0.20f,  1.00f,  0.00f, 0.50f,  0},  // two partials

    {        64,   0.85f,  1.00f,  0.35f, 1.00f,  0},
    {        48,   0.95f,  1.00f,  0.45f, 0.90f,  0},
    {        32,   1.10f,  1.00f,  0.55f, 0.85f,  0},
    {        24,   1.25f,  1.00f,  0.65f, 0.80f,  0},
    {        18,   1.40f,  1.00f,  0.75f, 0.75f,  0},
    {        12,   1.60f,  1.00f,  0.85f, 0.70f,  0},
    {         9,   1.80f,  1.00f,  0.90f, 0.65f,  0},
    {         6,   2.10f,  1.00f,  0.95f, 0.60f,  0},
    {         4,   0.30f,  1.00f,  1.00f, 1.00f,  0},  // near-sine, warm
};

// Deterministic phase scatter, so the tables are identical on every run and in
// every host. A fixed LCG stands in for a noise source.
[[nodiscard]] float scatteredPhase(const int harmonic, const std::size_t shape, const float spread) noexcept
{
    if (spread <= 0.0f)
        return (harmonic % 2 == 0) ? 0.0f : 3.14159265358979f;

    std::uint32_t state = static_cast<std::uint32_t>(harmonic) * 2654435761u +
                          static_cast<std::uint32_t>(shape) * 40503u + 12345u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0xffffu) * (3.14159265358979f / 32768.0f) * spread;
}

void synthesise(WavetableSet& set) noexcept
{
    constexpr float kTwoPi = 6.28318530717958647692f;

    for (std::size_t shape = 0; shape < kWavetableCount; ++shape)
    {
        const Shape& spec = kShapes[shape];

        // A pulse of duty d has the exact series
        //   sum over k of  sin(k * pi * d) / k  *  cos(2 * pi * k * t)
        // so it is generated additively like every other table. That keeps the
        // edges band-limited; a stepped pulse would put a full-scale
        // discontinuity at both edges and at the loop point.
        if (spec.pulseWidth > 0)
        {
            const float duty = static_cast<float>(spec.pulseWidth) / 8.0f;
            float peak = 0.0f;
            float samples[kWavetableSize];

            for (std::size_t i = 0; i < kWavetableSize; ++i)
                samples[i] = 0.0f;

            for (int harmonic = 1; harmonic <= spec.harmonics; ++harmonic)
            {
                const float k = static_cast<float>(harmonic);
                const float amplitude =
                    std::sin(k * 3.14159265358979f * duty) / (k * 3.14159265358979f);
                const float step = kTwoPi * k;

                for (std::size_t i = 0; i < kWavetableSize; ++i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(kWavetableSize);
                    samples[i] += amplitude * std::cos(step * t);
                }
            }

            for (std::size_t i = 0; i < kWavetableSize; ++i)
                peak = std::max(peak, std::fabs(samples[i]));

            const float normalise = peak > 0.0f ? 30000.0f / peak : 0.0f;
            for (std::size_t i = 0; i < kWavetableSize; ++i)
            {
                const float clamped = std::max(-32000.0f, std::min(32000.0f, samples[i] * normalise));
                set.data[shape][i] = static_cast<std::int16_t>(clamped);
            }
            continue;
        }

        float peak = 0.0f;
        float samples[kWavetableSize];

        for (std::size_t i = 0; i < kWavetableSize; ++i)
            samples[i] = 0.0f;

        for (int harmonic = 1; harmonic <= spec.harmonics; ++harmonic)
        {
            const float k = static_cast<float>(harmonic);
            const bool even = (harmonic % 2) == 0;

            // Odd-only shapes (square, triangle) get no even partials at all.
            float weight = 1.0f / std::pow(k, spec.amplitudePower);
            if (spec.evenBias <= 0.0f && even)
                continue;
            weight *= even ? spec.evenBias : 1.0f;

            const float amplitude = weight * spec.amplitudeScale;
            const float phase = scatteredPhase(harmonic, shape, spec.phaseSpread);
            const float step = kTwoPi * k;

            for (std::size_t i = 0; i < kWavetableSize; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(kWavetableSize);
                samples[i] += amplitude * std::sin(step * t + phase);
            }
        }

        for (std::size_t i = 0; i < kWavetableSize; ++i)
            peak = std::max(peak, std::fabs(samples[i]));

        const float normalise = peak > 0.0f ? 30000.0f / peak : 0.0f;
        for (std::size_t i = 0; i < kWavetableSize; ++i)
        {
            const float clamped = std::max(-32000.0f, std::min(32000.0f, samples[i] * normalise));
            set.data[shape][i] = static_cast<std::int16_t>(clamped);
        }
    }
}

}  // namespace

const WavetableSet& wavetables()
{
    // Function-local static: built once, thread-safe, and free of static
    // initialisation order problems.
    static const WavetableSet set = [] {
        WavetableSet built {};
        synthesise(built);
        return built;
    }();
    return set;
}

}  // namespace downspout::plank
