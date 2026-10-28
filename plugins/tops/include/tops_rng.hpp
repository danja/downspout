#pragma once

#include <cstdint>

namespace downspout::tops {

// xorshift32: three shifts per sample, no multiply and no state beyond one word. Deterministic, so a
// render repeats exactly (tests rely on it) and nothing touches the C library's shared rand() state.
class Rng {
public:
    void seed(std::uint32_t seedValue) { state_ = seedValue ? seedValue : 0x2545F491u; }

    // Uniform in [-1, 1).
    [[nodiscard]] float nextSigned() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(static_cast<std::int32_t>(state_)) * (1.0f / 2147483648.0f);
    }

private:
    std::uint32_t state_ = 0x2545F491u;
};

}  // namespace downspout::tops
