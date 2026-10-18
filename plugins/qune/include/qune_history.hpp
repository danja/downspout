#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace downspout::qune {

// Stereo input history addressed by absolute sample index. Every shifter reads
// from the same ring, so the dry path and each algorithm agree on time.
class History {
public:
    void prepare(const std::uint32_t minimumSize)
    {
        std::uint32_t size = 1024;
        while (size < minimumSize)
            size <<= 1;
        for (auto& channel : data_)
            channel.assign(size, 0.0f);
        mask_ = size - 1u;
        count_ = 0;
    }

    void clear()
    {
        for (auto& channel : data_)
            std::fill(channel.begin(), channel.end(), 0.0f);
        count_ = 0;
    }

    void push(const float* left, const float* right, const int frames) noexcept
    {
        for (int i = 0; i < frames; ++i)
        {
            const std::size_t slot = static_cast<std::size_t>(count_ & mask_);
            data_[0][slot] = left[i];
            data_[1][slot] = right[i];
            ++count_;
        }
    }

    // Number of samples written so far; the newest sample has index count() - 1.
    [[nodiscard]] std::int64_t count() const noexcept { return static_cast<std::int64_t>(count_); }
    [[nodiscard]] std::uint32_t capacity() const noexcept { return mask_ + 1u; }

    [[nodiscard]] float sample(const int channel, const std::int64_t index) const noexcept
    {
        if (index < 0 || index >= count() || count() - index > static_cast<std::int64_t>(mask_))
            return 0.0f;
        return data_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(static_cast<std::uint64_t>(index) & mask_)];
    }

    [[nodiscard]] float mono(const std::int64_t index) const noexcept
    {
        return 0.5f * (sample(0, index) + sample(1, index));
    }

    // Catmull-Rom interpolation at a fractional absolute position.
    [[nodiscard]] float cubic(const int channel, const double position) const noexcept
    {
        const auto base = static_cast<std::int64_t>(position >= 0.0 ? position : position - 1.0);
        const float f = static_cast<float>(position - static_cast<double>(base));
        float y0, y1, y2, y3;
        if (base >= 1 && base + 2 < count() && count() - (base - 1) <= static_cast<std::int64_t>(mask_))
        {
            // The whole four-point window is live: index it directly.
            const float* d = data_[static_cast<std::size_t>(channel)].data();
            y0 = d[static_cast<std::uint64_t>(base - 1) & mask_];
            y1 = d[static_cast<std::uint64_t>(base) & mask_];
            y2 = d[static_cast<std::uint64_t>(base + 1) & mask_];
            y3 = d[static_cast<std::uint64_t>(base + 2) & mask_];
        }
        else
        {
            y0 = sample(channel, base - 1);
            y1 = sample(channel, base);
            y2 = sample(channel, base + 1);
            y3 = sample(channel, base + 2);
        }
        return y1 + 0.5f * f * ((y2 - y0) + f * ((2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3)
                                                  + f * (3.0f * (y1 - y2) + y3 - y0)));
    }

private:
    std::array<std::vector<float>, 2> data_;
    std::uint32_t mask_ = 1023;
    std::uint64_t count_ = 0;
};

// What the engine tells a shifter about the current moment.
struct ShiftControl {
    float ratio = 1.0f;         // pitch ratio, output / input
    float formantRatio = 1.0f;  // spectral envelope scale; 1 keeps the formants
    float period = 0.0f;        // input period in samples, 0 when unknown
    bool voiced = false;
};

}  // namespace downspout::qune
