#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace downspout::voxmod {

// ── Live cross-synthesis convolver ──────────────────────────────────────────
//
// Unlike a normal convolver there is no stored impulse response. Both sides are
// live: the source (inputs 1/2) and the "IR" feed (inputs 3/4) are framed with a
// Hann window every kHop samples, their FFT frames are multiplied, and the
// inverse transform is overlap-added into the output. Because the IR frame is
// re-analysed on every hop, a moving IR feed becomes a moving filter.
//
// The FFT is twice the frame length, so a frame pair produces a linear (not
// circular) convolution of the two 1024-sample windows. At fourfold overlap
// the Hann windows sum to two, hence kAnalysisGain.

inline constexpr int kConvolutionFrame = 1024;
inline constexpr int kConvolutionHop = 256;
inline constexpr int kConvolutionFft = 2 * kConvolutionFrame;
inline constexpr float kConvolutionAnalysisGain = 0.5f;

// In-place radix-2 complex FFT. Twiddles and bit reversal are precomputed once
// and reused by every frame; the inverse transform is unnormalised.
class VoxmodFft {
public:
    explicit VoxmodFft(const int size)
        : size_(size)
        , reverse_(static_cast<std::size_t>(size))
        , twiddle_(static_cast<std::size_t>(size / 2))
    {
        int bits = 0;
        while ((1 << bits) < size_)
            ++bits;
        for (int i = 0; i < size_; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            reverse_[static_cast<std::size_t>(i)] = r;
        }
        for (int i = 0; i < size_ / 2; ++i)
            twiddle_[static_cast<std::size_t>(i)] =
                std::polar(1.0f, -6.28318530717958647692f * static_cast<float>(i)
                           / static_cast<float>(size_));
    }

    void forward(std::complex<float>* data) const noexcept { run(data, false); }
    void inverse(std::complex<float>* data) const noexcept { run(data, true); }

private:
    void run(std::complex<float>* data, const bool inverse) const noexcept
    {
        for (int i = 0; i < size_; ++i) {
            const int j = reverse_[static_cast<std::size_t>(i)];
            if (i < j)
                std::swap(data[i], data[j]);
        }
        for (int len = 2; len <= size_; len <<= 1) {
            const int half = len >> 1;
            const int stride = size_ / len;
            for (int i = 0; i < size_; i += len) {
                for (int k = 0; k < half; ++k) {
                    const std::complex<float> twiddle = twiddle_[static_cast<std::size_t>(k * stride)];
                    const std::complex<float> w = inverse ? std::conj(twiddle) : twiddle;
                    const std::complex<float> u = data[i + k];
                    const std::complex<float> v = data[i + k + half] * w;
                    data[i + k] = u + v;
                    data[i + k + half] = u - v;
                }
            }
        }
    }

    int size_;
    std::vector<int> reverse_;
    std::vector<std::complex<float>> twiddle_;
};

// One channel of the sliding-window convolver. process() is allocation-free;
// its state lives in EngineState so activate() can clear it.
class LiveConvolver {
public:
    static constexpr int kFrame = kConvolutionFrame;
    static constexpr int kHop = kConvolutionHop;
    static constexpr int kFft = kConvolutionFft;
    static constexpr int kRing = kFft;  // overlap-add history, at least 2 frames

    LiveConvolver()
        : fft_(kFft)
    {
        for (int i = 0; i < kFrame; ++i) {
            // Periodic Hann: four overlapping frames sum to two.
            window_[static_cast<std::size_t>(i)] =
                0.5f - 0.5f * std::cos(6.28318530717958647692f * static_cast<float>(i)
                                      / static_cast<float>(kFrame));
        }
        clear();
    }

    void clear() noexcept
    {
        sourceRing_.fill(0.0f);
        irRing_.fill(0.0f);
        outRing_.fill(0.0f);
        sourceFrame_.fill(0.0f);
        irFrame_.fill(0.0f);
        block_.fill(0.0f);
        sourcePos_ = 0;
        outPos_ = 0;
        hopCount_ = 0;
    }

    // Multiply a source frame by an IR frame in the frequency domain and return
    // the 2N-sample overlap-add block. Public so a test can pin the frame math
    // with a delta IR. Scratch buffers belong to the instance, not the stack.
    void renderFramePair(const float* source, const float* ir, float* out) const noexcept
    {
        for (int i = 0; i < kFft; ++i) {
            workA_[static_cast<std::size_t>(i)] = std::complex<float>();
            workB_[static_cast<std::size_t>(i)] = std::complex<float>();
        }
        for (int i = 0; i < kFrame; ++i) {
            workA_[static_cast<std::size_t>(i)] = source[i] * window_[static_cast<std::size_t>(i)];
            workB_[static_cast<std::size_t>(i)] = ir[i] * window_[static_cast<std::size_t>(i)];
        }

        fft_.forward(workA_.data());
        fft_.forward(workB_.data());
        for (int i = 0; i < kFft; ++i)
            workA_[static_cast<std::size_t>(i)] *= workB_[static_cast<std::size_t>(i)];
        fft_.inverse(workA_.data());

        const float scale = kConvolutionAnalysisGain / static_cast<float>(kFft);
        for (int i = 0; i < kFft; ++i) {
            const float v = workA_[static_cast<std::size_t>(i)].real() * scale;
            out[i] = std::isfinite(v) ? v : 0.0f;
        }
    }

    // `source`/`ir` are the two live feeds; feedback taps the output one frame
    // back, so there is no algebraic loop within a frame.
    void process(const float* source, const float* ir, float* out,
                 const int frames, const float sourceFeedback, const float irFeedback) noexcept
    {
        const float srcFb = finiteGain(sourceFeedback);
        const float irFb = finiteGain(irFeedback);

        for (int s = 0; s < frames; ++s) {
            const float x = std::isfinite(source[s]) ? source[s] : 0.0f;
            const float h = std::isfinite(ir[s]) ? ir[s] : 0.0f;
            const int delayed = (outPos_ - kFrame + kRing) % kRing;
            const float fb = outRing_[static_cast<std::size_t>(delayed)];

            const float srcIn = finiteOrZero(x + srcFb * fb);
            const float irIn = finiteOrZero(h + irFb * fb);
            sourceRing_[static_cast<std::size_t>(sourcePos_)] = srcIn;
            irRing_[static_cast<std::size_t>(sourcePos_)] = irIn;
            ++sourcePos_;
            if (sourcePos_ == kFrame)
                sourcePos_ = 0;

            ++hopCount_;
            if (hopCount_ == kHop) {
                hopCount_ = 0;
                // sourcePos_ points at the oldest sample of the new frame.
                for (int i = 0; i < kFrame; ++i) {
                    const int idx = (sourcePos_ + i) % kFrame;
                    sourceFrame_[static_cast<std::size_t>(i)] = sourceRing_[static_cast<std::size_t>(idx)];
                    irFrame_[static_cast<std::size_t>(i)] = irRing_[static_cast<std::size_t>(idx)];
                }
                renderFramePair(sourceFrame_.data(), irFrame_.data(), block_.data());

                // The frame begins kFrame - 1 samples before the current one.
                const int start = (outPos_ - kFrame + 1 + kRing) % kRing;
                for (int i = 0; i < kFft; ++i) {
                    const int idx = (start + i) % kRing;
                    outRing_[static_cast<std::size_t>(idx)] =
                        finiteOrZero(outRing_[static_cast<std::size_t>(idx)] + block_[static_cast<std::size_t>(i)]);
                }
            }

            out[s] = std::isfinite(outRing_[static_cast<std::size_t>(outPos_)])
                ? outRing_[static_cast<std::size_t>(outPos_)] : 0.0f;
            outRing_[static_cast<std::size_t>(outPos_)] = 0.0f;
            ++outPos_;
            if (outPos_ == kRing)
                outPos_ = 0;
        }
    }

private:
    static float finiteGain(const float v) noexcept
    {
        return std::isfinite(v) ? std::clamp(v, 0.0f, 0.95f) : 0.0f;
    }
    static float finiteOrZero(const float v) noexcept { return std::isfinite(v) ? v : 0.0f; }

    VoxmodFft fft_;
    std::array<float, kFrame> window_ {};
    std::array<float, kFrame> sourceRing_ {}, irRing_ {}, sourceFrame_ {}, irFrame_ {};
    std::array<float, kRing> outRing_ {};
    std::array<float, kFft> block_ {};
    mutable std::array<std::complex<float>, kFft> workA_ {}, workB_ {};
    int sourcePos_ = 0;
    int outPos_ = 0;
    int hopCount_ = 0;
};

}  // namespace downspout::voxmod
