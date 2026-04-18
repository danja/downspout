#pragma once

#include <cstdint>

namespace downspout::bassgen {

class Rng {
public:
    void seed(std::uint32_t seedValue) {
        state_ = seedValue ? seedValue : 0x12345678u;
    }

    [[nodiscard]] std::uint32_t nextU32() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] float nextFloat() {
        return static_cast<float>(nextU32() & 0x00FFFFFFu) / 16777215.0f;
    }

    [[nodiscard]] int nextInt(int minValue, int maxValue) {
        if (maxValue <= minValue) {
            return minValue;
        }

        const std::uint32_t span = static_cast<std::uint32_t>(maxValue - minValue + 1);
        return minValue + static_cast<int>(nextU32() % span);
    }

private:
    std::uint32_t state_ = 0x12345678u;
};

}  // namespace downspout::bassgen
