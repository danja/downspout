#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace downspout::fathom {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

// Flush values too small to matter; recursive paths decay into denormals.
[[nodiscard]] inline float flush(const float v) noexcept
{
    return std::fabs(v) < 1.0e-20f ? 0.0f : v;
}

[[nodiscard]] inline float softClip(const float x) noexcept
{
    // Cubic soft clip with unity slope at zero; bounded at +-1.
    const float c = std::min(std::max(x, -1.5f), 1.5f);
    return c - (c * c * c) * (4.0f / 27.0f);
}

// ── Sine table ─────────────────────────────────────────────────────────────

class SineTable {
public:
    static constexpr int kSize = 4096;

    SineTable()
    {
        for (int i = 0; i <= kSize; ++i)
            table_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(kTwoPi * i / kSize));
    }

    // phase in [0, 1)
    [[nodiscard]] float operator()(const double phase) const noexcept
    {
        const double x = phase * kSize;
        const int i = static_cast<int>(x);
        const float f = static_cast<float>(x - i);
        const float a = table_[static_cast<std::size_t>(i)];
        const float b = table_[static_cast<std::size_t>(i) + 1u];
        return a + (b - a) * f;
    }

private:
    std::array<float, kSize + 1> table_ {};
};

[[nodiscard]] inline const SineTable& sineTable()
{
    static const SineTable table;
    return table;
}

[[nodiscard]] inline double wrapPhase(double p) noexcept
{
    p -= std::floor(p);
    return p;
}

// ── Band-limited saw and pulse (PolyBLEP) ──────────────────────────────────

[[nodiscard]] inline float polyBlep(double t, const double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return static_cast<float>(t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return static_cast<float>(t * t + t + t + 1.0);
    }
    return 0.0f;
}

[[nodiscard]] inline float blepSaw(const double phase, const double dt) noexcept
{
    return static_cast<float>(2.0 * phase - 1.0) - polyBlep(phase, dt);
}

[[nodiscard]] inline float blepPulse(const double phase, const double dt, const double width) noexcept
{
    float v = phase < width ? 1.0f : -1.0f;
    v += polyBlep(phase, dt);
    v -= polyBlep(wrapPhase(phase - width), dt);
    return v;
}

// ── Smooth value noise, deterministic ──────────────────────────────────────

[[nodiscard]] inline float hashNoise(const std::uint32_t a, const std::uint32_t b) noexcept
{
    std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu;
    h ^= h >> 15u;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12u;
    h *= 0x297A2D39u;
    h ^= h >> 15u;
    return static_cast<float>(h) * (2.0f / 4294967296.0f) - 1.0f;
}

// Smoothly interpolated noise in [-1, 1]; x >= 0.
[[nodiscard]] inline float valueNoise(const float x, const std::uint32_t channel) noexcept
{
    const float base = std::floor(x);
    const auto i = static_cast<std::uint32_t>(base);
    float f = x - base;
    f = f * f * (3.0f - 2.0f * f);
    const float a = hashNoise(i, channel);
    const float b = hashNoise(i + 1u, channel);
    return a + (b - a) * f;
}

// ── Topology-preserving state variable filter ──────────────────────────────

class Svf {
public:
    void reset() noexcept
    {
        ic1_ = 0.0f;
        ic2_ = 0.0f;
    }

    void setCutoff(const double hertz, const double sampleRate, const float k) noexcept
    {
        const double fc = std::min(std::max(hertz, 10.0), 0.45 * sampleRate);
        const double g = std::tan(kPi * fc / sampleRate);
        k_ = k;
        a1_ = static_cast<float>(1.0 / (1.0 + g * (g + k)));
        a2_ = static_cast<float>(g) * a1_;
        a3_ = static_cast<float>(g) * a2_;
    }

    // Runs one sample; low-pass and band-pass are read back afterwards.
    void process(const float x) noexcept
    {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = flush(2.0f * v1 - ic1_);
        ic2_ = flush(2.0f * v2 - ic2_);
        lowpass_ = v2;
        bandpass_ = v1;
    }

    [[nodiscard]] float lowpass() const noexcept { return lowpass_; }
    [[nodiscard]] float bandpass() const noexcept { return bandpass_; }
    [[nodiscard]] float k() const noexcept { return k_; }

private:
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    float a3_ = 0.0f;
    float k_ = 1.0f;
    float ic1_ = 0.0f;
    float ic2_ = 0.0f;
    float lowpass_ = 0.0f;
    float bandpass_ = 0.0f;
};

// ── Delay line and Schroeder allpass ───────────────────────────────────────

class DelayLine {
public:
    void allocate(const std::size_t samples)
    {
        buffer_.assign(samples + 4u, 0.0f);
        write_ = 0;
    }

    void clear() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }

    void write(const float x) noexcept
    {
        buffer_[write_] = x;
        write_ = write_ + 1u == buffer_.size() ? 0u : write_ + 1u;
    }

    // Delay in samples (>= 1, linear interpolation).
    [[nodiscard]] float read(double delay) const noexcept
    {
        const double maximum = static_cast<double>(buffer_.size() - 2u);
        delay = delay < 1.0 ? 1.0 : (delay > maximum ? maximum : delay);
        double position = static_cast<double>(write_) - delay;
        if (position < 0.0)
            position += static_cast<double>(buffer_.size());
        const auto i0 = static_cast<std::size_t>(position);
        const std::size_t i1 = i0 + 1u == buffer_.size() ? 0u : i0 + 1u;
        const float f = static_cast<float>(position - static_cast<double>(i0));
        return buffer_[i0] + (buffer_[i1] - buffer_[i0]) * f;
    }

private:
    std::vector<float> buffer_;
    std::size_t write_ = 0;
};

class Allpass {
public:
    void allocate(const std::size_t samples)
    {
        buffer_.assign(std::max<std::size_t>(samples, 1u), 0.0f);
        position_ = 0;
    }

    void clear() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }

    [[nodiscard]] float process(const float x, const float g) noexcept
    {
        const float d = buffer_[position_];
        const float v = flush(x - g * d);
        buffer_[position_] = v;
        position_ = position_ + 1u == buffer_.size() ? 0u : position_ + 1u;
        return d + g * v;
    }

private:
    std::vector<float> buffer_;
    std::size_t position_ = 0;
};

// ── Single-sideband frequency shifter ──────────────────────────────────────
//
// Two chains of four allpass sections whose outputs stay a quarter cycle apart
// across the audio band (Olli Niemitalo's design), followed by a rotation by the
// shift phase. Returns the signal moved by +shift Hz for a positive rotation.

class FrequencyShifter {
public:
    void reset() noexcept
    {
        for (auto& s : sections1_)
            s = Section {};
        for (auto& s : sections2_)
            s = Section {};
        delayed_ = 0.0f;
    }

    [[nodiscard]] float process(const float x, const float cosine, const float sine) noexcept
    {
        const float i = delayed_;
        delayed_ = chain(sections1_, kCoefficients1, x);
        const float q = chain(sections2_, kCoefficients2, x);
        return i * cosine + q * sine;  // sign measured: positive rotation moves the tone up
    }

private:
    struct Section {
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };

    static float chain(std::array<Section, 4>& sections, const std::array<float, 4>& coefficients, float x) noexcept
    {
        for (std::size_t n = 0; n < 4; ++n)
        {
            Section& s = sections[n];
            const float c = coefficients[n];
            // H(z) = (c - z^-2) / (1 - c z^-2), with c the squared coefficient.
            const float y = flush(c * (x + s.y2) - s.x2);
            s.x2 = s.x1;
            s.x1 = x;
            s.y2 = s.y1;
            s.y1 = y;
            x = y;
        }
        return x;
    }

    static constexpr std::array<float, 4> kCoefficients1 = {{
        0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
        0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f,
    }};
    static constexpr std::array<float, 4> kCoefficients2 = {{
        0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
        0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f,
    }};

    std::array<Section, 4> sections1_ {};
    std::array<Section, 4> sections2_ {};
    float delayed_ = 0.0f;
};

}  // namespace downspout::fathom
