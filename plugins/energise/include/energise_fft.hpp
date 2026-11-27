#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace downspout::energise {

// Small power-of-two radix-2 FFT for the offline analyser. Input is a real
// signal; output magnitude is unscaled, so divide by size when needed.
class Fft {
public:
    void init(const int requestedSize)
    {
        int size = 1;
        while (size < requestedSize)
            size <<= 1;
        size_ = size;
        reverse_.resize(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i) {
            unsigned bits = static_cast<unsigned>(i);
            unsigned rev = 0;
            for (int b = 0; b < bitCount(); ++b) {
                rev = (rev << 1u) | (bits & 1u);
                bits >>= 1u;
            }
            reverse_[static_cast<std::size_t>(i)] = static_cast<int>(rev);
        }
        cos_.resize(static_cast<std::size_t>(size / 2));
        sin_.resize(static_cast<std::size_t>(size / 2));
        for (int i = 0; i < size / 2; ++i) {
            const double angle = -2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(size);
            cos_[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(angle));
            sin_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(angle));
        }
    }

    [[nodiscard]] int size() const noexcept { return size_; }

    void forward(const float* input, std::vector<float>& re, std::vector<float>& im) const noexcept
    {
        if (size_ < 2)
            return;
        re.assign(static_cast<std::size_t>(size_), 0.0f);
        im.assign(static_cast<std::size_t>(size_), 0.0f);
        for (int i = 0; i < size_; ++i) {
            const std::size_t dst = static_cast<std::size_t>(reverse_[static_cast<std::size_t>(i)]);
            re[dst] = input[i];
        }
        for (int len = 2; len <= size_; len <<= 1) {
            const int half = len / 2;
            const int step = size_ / len;
            for (int start = 0; start < size_; start += len) {
                for (int j = 0; j < half; ++j) {
                    const std::size_t tw = static_cast<std::size_t>(j * step);
                    const float wr = cos_[tw];
                    const float wi = sin_[tw];
                    const std::size_t a = static_cast<std::size_t>(start + j);
                    const std::size_t b = static_cast<std::size_t>(start + j + half);
                    const float tr = wr * re[b] - wi * im[b];
                    const float ti = wr * im[b] + wi * re[b];
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

private:
    [[nodiscard]] int bitCount() const noexcept
    {
        int bits = 0;
        int n = size_;
        while (n > 1) {
            ++bits;
            n >>= 1;
        }
        return bits;
    }

    int size_ = 0;
    std::vector<int> reverse_ {};
    std::vector<float> cos_ {};
    std::vector<float> sin_ {};
};

}  // namespace downspout::energise
