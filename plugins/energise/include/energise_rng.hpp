#pragma once

#include <cstdint>

namespace downspout::energise {

// Deterministic, stateless-per-hit randomness. Renders must be bitwise
// repeatable, so every voice derives its own seed from the stable event id.
class Rng {
public:
    void seed(const std::uint32_t value) noexcept { state_ = value == 0u ? 0x9E3779B9u : value; }

    [[nodiscard]] std::uint32_t nextU32() noexcept
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] float nextFloat() noexcept
    {
        return static_cast<float>(nextU32() & 0x00FFFFFFu) / 16777216.0f;
    }

    [[nodiscard]] float nextSigned() noexcept { return nextFloat() * 2.0f - 1.0f; }

    [[nodiscard]] int nextInt(const int lo, const int hi) noexcept
    {
        if (hi <= lo)
            return lo;
        return lo + static_cast<int>(nextU32() % static_cast<std::uint32_t>(hi - lo + 1));
    }

private:
    std::uint32_t state_ = 0x9E3779B9u;
};

[[nodiscard]] inline std::uint32_t hashSeed(const std::uint32_t base, const std::uint32_t value) noexcept
{
    std::uint32_t x = base ^ (value + 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

}  // namespace downspout::energise
