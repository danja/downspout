#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace downspout::qune {

// In-place radix-2 complex FFT over interleaved (re, im) floats. Sized once in
// init(); transform() never allocates.
class Fft {
public:
    void init(const int size)
    {
        size_ = size;
        int bits = 0;
        while ((1 << bits) < size)
            ++bits;
        reverse_.assign(static_cast<std::size_t>(size), 0);
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            reverse_[static_cast<std::size_t>(i)] = r;
        }
        cosTable_.resize(static_cast<std::size_t>(size / 2));
        sinTable_.resize(static_cast<std::size_t>(size / 2));
        for (int k = 0; k < size / 2; ++k)
        {
            const double angle = -2.0 * 3.14159265358979323846 * k / size;
            cosTable_[static_cast<std::size_t>(k)] = static_cast<float>(std::cos(angle));
            sinTable_[static_cast<std::size_t>(k)] = static_cast<float>(std::sin(angle));
        }
    }

    [[nodiscard]] int size() const noexcept { return size_; }

    // exp(-j...) kernel, unscaled.
    void forward(float* data) const noexcept { transform(data, false); }

    // exp(+j...) kernel, scaled by 1/N so inverse(forward(x)) == x.
    void inverse(float* data) const noexcept
    {
        transform(data, true);
        const float scale = 1.0f / static_cast<float>(size_);
        for (int i = 0; i < 2 * size_; ++i)
            data[i] *= scale;
    }

private:
    void transform(float* data, const bool inverse) const noexcept
    {
        const int n = size_;
        for (int i = 0; i < n; ++i)
        {
            const int j = reverse_[static_cast<std::size_t>(i)];
            if (j > i)
            {
                std::swap(data[2 * i], data[2 * j]);
                std::swap(data[2 * i + 1], data[2 * j + 1]);
            }
        }

        const float sign = inverse ? -1.0f : 1.0f;
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const int step = n / len;
            for (int base = 0; base < n; base += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosTable_[static_cast<std::size_t>(k * step)];
                    const float wi = sign * sinTable_[static_cast<std::size_t>(k * step)];
                    float* a = data + 2 * (base + k);
                    float* b = data + 2 * (base + k + half);
                    const float tr = b[0] * wr - b[1] * wi;
                    const float ti = b[0] * wi + b[1] * wr;
                    b[0] = a[0] - tr;
                    b[1] = a[1] - ti;
                    a[0] += tr;
                    a[1] += ti;
                }
            }
        }
    }

    int size_ = 0;
    std::vector<int> reverse_;
    std::vector<float> cosTable_;
    std::vector<float> sinTable_;
};

}  // namespace downspout::qune
