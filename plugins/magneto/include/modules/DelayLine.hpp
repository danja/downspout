#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::magneto {

// Four-point Hermite interpolation (Catmull-Rom), the same formulation used by
// plugins/floozy/include/flues/pm/modules/interface/utils/DelayUtils.hpp.
[[nodiscard]] inline float hermiteInterpolate(const float xm1,
                                              const float x0,
                                              const float x1,
                                              const float x2,
                                              const float frac) noexcept
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((((a * frac) - b) * frac + c) * frac + x0);
}

// Fixed-capacity delay line with a power-of-two buffer and mask wrapping.
//
// Read-then-write within one sample: read(d) returns the sample written d steps
// ago, so the smallest usable delay is 1 sample (4 for the Hermite path, which
// needs one tap either side).
template <std::size_t N>
class DelayLine {
    static_assert(N >= 8, "delay line needs at least eight slots");
    static_assert((N & (N - 1)) == 0, "delay line length must be a power of two");

public:
    static constexpr std::size_t kCapacity = N;
    static constexpr float kMinDelay = 4.0f;
    static constexpr float kMaxDelay = static_cast<float>(N - 4);

    void reset() noexcept
    {
        buffer_.fill(0.0f);
        writePos_ = 0;
    }

    void write(const float sample) noexcept
    {
        buffer_[writePos_] = sample;
        writePos_ = (writePos_ + 1) & kMask;
    }

    [[nodiscard]] float readLinear(const float delaySamples) const noexcept
    {
        const float d = std::clamp(delaySamples, 1.0f, kMaxDelay);
        const auto whole = static_cast<std::size_t>(d);
        const float frac = d - static_cast<float>(whole);
        const std::size_t i0 = (writePos_ + N - whole) & kMask;
        const std::size_t i1 = (i0 + N - 1) & kMask;
        return buffer_[i0] + frac * (buffer_[i1] - buffer_[i0]);
    }

    // Hermite read. Required on the cylinder lines, whose fractional part sweeps
    // the full [0,1) range twice per engine cycle: linear interpolation there is
    // a fraction-dependent low-pass and shows up as piston-locked amplitude
    // modulation.
    [[nodiscard]] float readHermite(const float delaySamples) const noexcept
    {
        const float d = std::clamp(delaySamples, kMinDelay, kMaxDelay);
        const auto whole = static_cast<std::size_t>(d);
        const float frac = d - static_cast<float>(whole);
        const std::size_t i0 = (writePos_ + N - whole) & kMask;
        const std::size_t im1 = (i0 + 1) & kMask;  // one sample newer
        const std::size_t i1 = (i0 + N - 1) & kMask;
        const std::size_t i2 = (i0 + N - 2) & kMask;
        return hermiteInterpolate(buffer_[im1], buffer_[i0], buffer_[i1], buffer_[i2], frac);
    }

private:
    static constexpr std::size_t kMask = N - 1;

    std::array<float, N> buffer_ {};
    std::size_t writePos_ = 0;
};

}  // namespace downspout::magneto
