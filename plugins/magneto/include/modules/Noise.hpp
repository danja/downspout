#pragma once

#include <cmath>
#include <cstdint>

namespace downspout::magneto {

// Cheap, deterministic, allocation-free building blocks shared by the engine.

// Flush denormals so long feedback tails do not fall off a performance cliff.
[[nodiscard]] inline float flushDenormal(const float value) noexcept
{
    return (std::fabs(value) < 1.0e-20f) ? 0.0f : value;
}

[[nodiscard]] inline float softClip(const float value) noexcept
{
    return std::tanh(value);
}

// Linear congruential generator; same constants as the drumkit noise source.
struct Lcg {
    std::uint32_t state = 123456789u;

    void seed(const std::uint32_t value) noexcept
    {
        state = value != 0u ? value : 123456789u;
    }

    // Uniform in [0,1).
    [[nodiscard]] float uniform() noexcept
    {
        state = state * 1103515245u + 12345u;
        return static_cast<float>((state >> 8) & 0x00FFFFFFu) / 16777216.0f;
    }

    // Uniform in [-1,1).
    [[nodiscard]] float bipolar() noexcept
    {
        return uniform() * 2.0f - 1.0f;
    }
};

// One-pole low-pass: y += a * (x - y).
struct OnePoleLowpass {
    float y = 0.0f;

    void reset() noexcept { y = 0.0f; }

    [[nodiscard]] float process(const float x, const float a) noexcept
    {
        y += a * (x - y);
        y = flushDenormal(y);
        return y;
    }
};

// kTwoPi also lives in CycleFunctions.hpp; keep a local copy so this header can
// be included on its own.
inline constexpr float kTwoPiApprox = 6.28318530717958647692f;

// Coefficient for a one-pole low-pass with the given -3 dB cutoff.
[[nodiscard]] inline float onePoleCoefficient(const float cutoffHz, const float sampleRate) noexcept
{
    if (sampleRate <= 0.0f)
        return 1.0f;
    const float x = std::exp(-kTwoPiApprox * cutoffHz / sampleRate);
    return 1.0f - x;
}

// y[n] = x[n] - x[n-1] + R * y[n-1]
struct DCBlocker {
    float x1 = 0.0f;
    float y1 = 0.0f;

    void reset() noexcept
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    [[nodiscard]] float process(const float x, const float r = 0.999f) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = flushDenormal(y);
        return y1;
    }
};

// One-pole parameter ramp used for control-rate values that would otherwise
// zipper (pipe lengths, mix gains, RPM).
struct Smoother {
    float value = 0.0f;

    void reset(const float v = 0.0f) noexcept { value = v; }

    float process(const float target, const float coefficient) noexcept
    {
        value += coefficient * (target - value);
        return value;
    }
};

}  // namespace downspout::magneto
