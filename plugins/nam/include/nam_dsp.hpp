#pragma once

// Small portable DSP building blocks for the NAM plugin: biquads, a partitioned convolver and a
// streaming resampler. No framework or NAM dependency.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace downspout::namfx {

inline constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] inline float dbToGain(const float db) { return std::pow(10.0f, db * 0.05f); }
[[nodiscard]] inline float gainToDb(const float gain) { return 20.0f * std::log10(std::max(gain, 1.0e-6f)); }

// Neural networks leave denormals in their recurrent state; flush them for the duration of a block.
class ScopedNoDenormals {
public:
    ScopedNoDenormals()
    {
#if defined(__SSE__) || defined(_M_X64)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u);
#endif
    }
    ~ScopedNoDenormals()
    {
#if defined(__SSE__) || defined(_M_X64)
        _mm_setcsr(saved_);
#endif
    }
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    unsigned int saved_ = 0;
};

// ── Biquads (RBJ cookbook) ──────────────────────────────────────────────────

enum class FilterType { LowShelf, HighShelf, Peak, HighPass, LowPass };

struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

[[nodiscard]] inline BiquadCoeffs designBiquad(const FilterType type, const double sampleRate, double freq,
                                               const double q, const double gainDb)
{
    freq = std::clamp(freq, 10.0, sampleRate * 0.49);
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cs = std::cos(w0);
    const double sn = std::sin(w0);
    const double alpha = sn / (2.0 * q);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double beta = 2.0 * std::sqrt(a) * alpha;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
    switch (type) {
    case FilterType::Peak:
        b0 = 1.0 + alpha * a; b1 = -2.0 * cs; b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a; a1 = -2.0 * cs; a2 = 1.0 - alpha / a;
        break;
    case FilterType::LowShelf:
        b0 = a * ((a + 1.0) - (a - 1.0) * cs + beta);
        b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cs);
        b2 = a * ((a + 1.0) - (a - 1.0) * cs - beta);
        a0 = (a + 1.0) + (a - 1.0) * cs + beta;
        a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cs);
        a2 = (a + 1.0) + (a - 1.0) * cs - beta;
        break;
    case FilterType::HighShelf:
        b0 = a * ((a + 1.0) + (a - 1.0) * cs + beta);
        b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cs);
        b2 = a * ((a + 1.0) + (a - 1.0) * cs - beta);
        a0 = (a + 1.0) - (a - 1.0) * cs + beta;
        a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cs);
        a2 = (a + 1.0) - (a - 1.0) * cs - beta;
        break;
    case FilterType::HighPass:
        b0 = (1.0 + cs) * 0.5; b1 = -(1.0 + cs); b2 = (1.0 + cs) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * cs; a2 = 1.0 - alpha;
        break;
    case FilterType::LowPass:
        b0 = (1.0 - cs) * 0.5; b1 = 1.0 - cs; b2 = (1.0 - cs) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * cs; a2 = 1.0 - alpha;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

[[nodiscard]] inline double biquadMagnitude(const BiquadCoeffs& c, const double freq, const double sampleRate)
{
    const double w = 2.0 * kPi * freq / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    return std::abs((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

class Biquad {
public:
    void set(const BiquadCoeffs& c) { c_ = c; }
    void reset() { z1_ = z2_ = 0.0; }
    [[nodiscard]] float process(const float x)
    {
        const double in = x;
        const double out = c_.b0 * in + z1_;
        z1_ = c_.b1 * in - c_.a1 * out + z2_;
        z2_ = c_.b2 * in - c_.a2 * out;
        return static_cast<float>(out);
    }

private:
    BiquadCoeffs c_;
    double z1_ = 0.0, z2_ = 0.0;
};

// ── FFT ─────────────────────────────────────────────────────────────────────

// In-place radix-2 complex FFT of a fixed power-of-two size.
class Fft {
public:
    explicit Fft(const int size = 2)
        : n_(size)
        , rev_(static_cast<std::size_t>(size))
        , tw_(static_cast<std::size_t>(size / 2))
    {
        int bits = 0;
        while ((1 << bits) < n_)
            ++bits;
        for (int i = 0; i < n_; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev_[static_cast<std::size_t>(i)] = r;
        }
        for (int i = 0; i < n_ / 2; ++i)
            tw_[static_cast<std::size_t>(i)] = std::polar(1.0f, static_cast<float>(-2.0 * kPi * i / n_));
    }

    [[nodiscard]] int size() const { return n_; }

    void forward(std::complex<float>* a) const { run(a, false); }
    // Unnormalised inverse; the caller divides by size().
    void inverse(std::complex<float>* a) const { run(a, true); }

private:
    void run(std::complex<float>* a, const bool inv) const
    {
        for (int i = 0; i < n_; ++i) {
            const int j = rev_[static_cast<std::size_t>(i)];
            if (i < j)
                std::swap(a[i], a[j]);
        }
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len >> 1;
            const int stride = n_ / len;
            for (int i = 0; i < n_; i += len) {
                for (int k = 0; k < half; ++k) {
                    std::complex<float> w = tw_[static_cast<std::size_t>(k * stride)];
                    if (inv)
                        w = std::conj(w);
                    const std::complex<float> u = a[i + k];
                    const std::complex<float> v = a[i + k + half] * w;
                    a[i + k] = u + v;
                    a[i + k + half] = u - v;
                }
            }
        }
    }

    int n_;
    std::vector<int> rev_;
    std::vector<std::complex<float>> tw_;
};

// ── Convolver ───────────────────────────────────────────────────────────────

// Uniform-partitioned FFT convolution with a direct-form head, so it adds no latency. The first
// kBlock taps run sample by sample; the rest run in the frequency domain one block behind.
// setImpulse() allocates; process() and clear() do not.
class PartitionedConvolver {
public:
    static constexpr int kBlock = 128;

    PartitionedConvolver() : fft_(2 * kBlock) { setImpulse(nullptr, 0); }

    void setImpulse(const float* ir, const int length)
    {
        length_ = std::max(0, length);
        headRev_.assign(kBlock, 0.0f);
        for (int i = 0; i < std::min(length_, kBlock); ++i)
            headRev_[static_cast<std::size_t>(kBlock - 1 - i)] = ir[i];

        tailParts_ = length_ > kBlock ? (length_ - kBlock + kBlock - 1) / kBlock : 0;
        constexpr int bins = kBlock + 1;
        spectra_.assign(static_cast<std::size_t>(tailParts_ * bins), {});
        std::vector<std::complex<float>> buf(2 * kBlock);
        for (int p = 0; p < tailParts_; ++p) {
            std::fill(buf.begin(), buf.end(), std::complex<float>());
            for (int i = 0; i < kBlock; ++i) {
                const int idx = (p + 1) * kBlock + i;
                if (idx < length_)
                    buf[static_cast<std::size_t>(i)] = ir[idx];
            }
            fft_.forward(buf.data());
            std::copy(buf.begin(), buf.begin() + bins, spectra_.begin() + static_cast<std::ptrdiff_t>(p * bins));
        }
        fdl_.assign(static_cast<std::size_t>(tailParts_ * bins), {});
        work_.assign(2 * kBlock, {});
        acc_.assign(bins, {});
        hist_.assign(2 * kBlock, 0.0f);
        inBlock_.assign(kBlock, 0.0f);
        ready_.assign(kBlock, 0.0f);
        tail_.assign(kBlock, 0.0f);
        clear();
    }

    [[nodiscard]] int length() const { return length_; }

    void clear()
    {
        std::fill(hist_.begin(), hist_.end(), 0.0f);
        std::fill(inBlock_.begin(), inBlock_.end(), 0.0f);
        std::fill(ready_.begin(), ready_.end(), 0.0f);
        std::fill(tail_.begin(), tail_.end(), 0.0f);
        std::fill(fdl_.begin(), fdl_.end(), std::complex<float>());
        pos_ = 0;
        head_ = 0;
    }

    // `in` and `out` may be the same buffer.
    void process(const float* in, float* out, const int n)
    {
        for (int s = 0; s < n; ++s) {
            const float x = in[s];
            hist_[static_cast<std::size_t>(wp_)] = x;
            hist_[static_cast<std::size_t>(wp_ + kBlock)] = x;
            const float* window = hist_.data() + wp_ + 1;
            const float* h = headRev_.data();
            float acc = 0.0f;
            for (int j = 0; j < kBlock; ++j)
                acc += h[j] * window[j];
            wp_ = (wp_ + 1) % kBlock;

            inBlock_[static_cast<std::size_t>(pos_)] = x;
            out[s] = acc + ready_[static_cast<std::size_t>(pos_)];
            if (++pos_ == kBlock) {
                pos_ = 0;
                if (tailParts_ > 0)
                    advanceBlock();
            }
        }
    }

private:
    void advanceBlock()
    {
        constexpr int bins = kBlock + 1;
        std::fill(work_.begin(), work_.end(), std::complex<float>());
        for (int i = 0; i < kBlock; ++i)
            work_[static_cast<std::size_t>(i)] = inBlock_[static_cast<std::size_t>(i)];
        fft_.forward(work_.data());

        head_ = (head_ + tailParts_ - 1) % tailParts_;
        std::copy(work_.begin(), work_.begin() + bins, fdl_.begin() + static_cast<std::ptrdiff_t>(head_ * bins));

        std::fill(acc_.begin(), acc_.end(), std::complex<float>());
        for (int p = 0; p < tailParts_; ++p) {
            const std::complex<float>* x = fdl_.data() + ((head_ + p) % tailParts_) * bins;
            const std::complex<float>* h = spectra_.data() + p * bins;
            for (int b = 0; b < bins; ++b)
                acc_[static_cast<std::size_t>(b)] += x[b] * h[b];
        }
        for (int b = 0; b < bins; ++b)
            work_[static_cast<std::size_t>(b)] = acc_[static_cast<std::size_t>(b)];
        for (int b = 1; b < kBlock; ++b)
            work_[static_cast<std::size_t>(2 * kBlock - b)] = std::conj(acc_[static_cast<std::size_t>(b)]);
        fft_.inverse(work_.data());

        const float scale = 1.0f / static_cast<float>(2 * kBlock);
        for (int i = 0; i < kBlock; ++i) {
            ready_[static_cast<std::size_t>(i)] = work_[static_cast<std::size_t>(i)].real() * scale + tail_[static_cast<std::size_t>(i)];
            tail_[static_cast<std::size_t>(i)] = work_[static_cast<std::size_t>(kBlock + i)].real() * scale;
        }
    }

    Fft fft_;
    int length_ = 0;
    int tailParts_ = 0;
    int pos_ = 0;
    int head_ = 0;
    int wp_ = 0;
    std::vector<float> headRev_, hist_, inBlock_, ready_, tail_;
    std::vector<std::complex<float>> spectra_, fdl_, work_, acc_;
};

// ── Resampler ───────────────────────────────────────────────────────────────

// Streaming windowed-sinc resampler for a fixed ratio. process() does not allocate for inputs up
// to kMaxChunk samples (longer inputs are split).
class StreamResampler {
public:
    static constexpr int kHalf = 16;        // kernel half-width in input samples when the rate does not fall
    static constexpr int kMaxHalf = 128;
    static constexpr int kPhases = 256;
    static constexpr int kMaxChunk = 512;

    void configure(const double inRate, const double outRate)
    {
        step_ = inRate / outRate;
        const double ratio = std::min(1.0, outRate / inRate);
        // Lowering the rate narrows the passband, so the kernel must span more input samples to
        // keep the same transition width at the new cutoff.
        half_ = std::clamp(static_cast<int>(std::ceil(kHalf / ratio)), kHalf, kMaxHalf);
        taps_ = 2 * half_;
        const double fc = 0.46 * ratio;  // cycles per input sample
        table_.assign(static_cast<std::size_t>((kPhases + 1) * taps_), 0.0f);
        for (int ph = 0; ph <= kPhases; ++ph) {
            const double frac = static_cast<double>(ph) / kPhases;
            for (int k = 0; k < taps_; ++k) {
                const double x = (k - (half_ - 1)) - frac;  // distance from the output instant
                const double u = x / half_;
                double sinc = 1.0;
                if (std::fabs(x) > 1.0e-9)
                    sinc = std::sin(2.0 * kPi * fc * x) / (2.0 * kPi * fc * x);
                const double w = std::fabs(u) >= 1.0
                    ? 0.0
                    : 0.35875 + 0.48829 * std::cos(kPi * u) + 0.14128 * std::cos(2.0 * kPi * u)
                          + 0.01168 * std::cos(3.0 * kPi * u);
                table_[static_cast<std::size_t>(ph * taps_ + k)] = static_cast<float>(2.0 * fc * sinc * w);
            }
        }
        buf_.assign(static_cast<std::size_t>(kMaxChunk + 2 * taps_ + 8), 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        have_ = half_ - 1;  // zeros before the first sample
        pos_ = static_cast<double>(half_ - 1);
    }

    // Input samples of delay this stage adds.
    [[nodiscard]] int latencyInSamples() const { return half_ - 1; }

    // Returns how many samples were written to `out` (at most `maxOut`). The caller must leave room
    // for every output the input can produce, or the unprocessed remainder piles up.
    int process(const float* in, int n, float* out, const int maxOut)
    {
        int written = 0;
        while (n > 0) {
            const int chunk = std::min(n, kMaxChunk);
            std::copy(in, in + chunk, buf_.begin() + have_);
            have_ += chunk;
            in += chunk;
            n -= chunk;
            while (written < maxOut) {
                const int base = static_cast<int>(pos_);
                if (base + half_ >= have_)
                    break;
                const double fph = (pos_ - base) * kPhases;
                const int ph = static_cast<int>(fph);
                const float mix = static_cast<float>(fph - ph);
                const float* a = table_.data() + static_cast<std::ptrdiff_t>(ph) * taps_;
                const float* b = a + taps_;
                const float* x = buf_.data() + base - (half_ - 1);
                float sum = 0.0f;
                for (int k = 0; k < taps_; ++k)
                    sum += x[k] * (a[k] + mix * (b[k] - a[k]));
                out[written++] = sum;
                pos_ += step_;
            }
            // Drop what the next output can no longer reach.
            const int drop = static_cast<int>(pos_) - (half_ - 1);
            if (drop > 0) {
                std::copy(buf_.begin() + drop, buf_.begin() + have_, buf_.begin());
                have_ -= drop;
                pos_ -= drop;
            }
        }
        return written;
    }

private:
    double step_ = 1.0;
    double pos_ = 0.0;
    int half_ = kHalf;
    int taps_ = 2 * kHalf;
    int have_ = 0;
    std::vector<float> table_, buf_;
};

}  // namespace downspout::namfx
