#include "qune_shifters.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::qune {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kMaxEnvelopeGain = 2.3f;  // about 20 dB either way
constexpr float kLogFloor = 1.0e-6f;

[[nodiscard]] inline float wrapPi(const float x) noexcept
{
    return x - kTwoPi * std::floor(x / kTwoPi + 0.5f);
}

[[nodiscard]] std::uint32_t nextPow2(std::uint64_t value)
{
    std::uint64_t p = 1;
    while (p < value)
        p <<= 1;
    return static_cast<std::uint32_t>(p);
}

}  // namespace

void SpectralShifter::prepare(const double sampleRate)
{
    sampleRate_ = sampleRate;
    size_ = (sampleRate <= 50000.0) ? 2048 : (sampleRate <= 100000.0 ? 4096 : 8192);
    hop_ = size_ / 4;
    bins_ = size_ / 2 + 1;

    fft_.init(size_);
    work_.assign(static_cast<std::size_t>(2 * size_), 0.0f);

    window_.resize(static_cast<std::size_t>(size_));
    for (int j = 0; j < size_; ++j)
        window_[static_cast<std::size_t>(j)] = 0.5f - 0.5f * static_cast<float>(std::cos(2.0 * 3.14159265358979323846 * j / size_));
    double overlap = 0.0;
    for (int m = 0; m < 4; ++m)
    {
        const double w = window_[static_cast<std::size_t>((hop_ / 2 + m * hop_) % size_)];
        overlap += w * w;
    }
    olaScale_ = static_cast<float>(1.0 / overlap);

    for (Channel& c : channels_)
    {
        const auto n = static_cast<std::size_t>(bins_ + 2);
        for (auto* v : {&c.re, &c.im, &c.mag, &c.phase, &c.prevPhase, &c.psi, &c.newPsi, &c.outRe, &c.outIm,
                        &c.outMag2, &c.envelope})
            v->assign(n, 0.0f);
    }
    peaks_.assign(static_cast<std::size_t>(bins_), 0);
    peaks_.clear();
    peaks_.reserve(static_cast<std::size_t>(bins_));

    const std::uint32_t outSize = nextPow2(static_cast<std::uint64_t>(size_) * 4u);
    for (auto& v : output_)
        v.assign(outSize, 0.0f);
    outputMask_ = outSize - 1u;
    reset();
}

void SpectralShifter::reset()
{
    for (auto& v : output_)
        std::fill(v.begin(), v.end(), 0.0f);
    for (Channel& c : channels_)
    {
        std::fill(c.prevPhase.begin(), c.prevPhase.end(), 0.0f);
        std::fill(c.psi.begin(), c.psi.end(), 0.0f);
    }
    initialised_ = false;
}

void SpectralShifter::process(const History& history, const ShiftControl& control, float* outLeft,
                              float* outRight, const int frames)
{
    const std::int64_t end = history.count();
    const std::int64_t start = end - frames;
    if (!initialised_)
    {
        initialised_ = true;
        nextFrame_ = start;
    }

    while (nextFrame_ + size_ <= end)
    {
        processFrame(history, nextFrame_, control);
        nextFrame_ += hop_;
    }

    for (int i = 0; i < frames; ++i)
    {
        const std::int64_t index = start + i - size_;
        const std::size_t slot = static_cast<std::size_t>(static_cast<std::uint64_t>(index) & outputMask_);
        outLeft[i] = output_[0][slot];
        outRight[i] = output_[1][slot];
        output_[0][slot] = 0.0f;
        output_[1][slot] = 0.0f;
    }
}

void SpectralShifter::processFrame(const History& history, const std::int64_t start,
                                   const ShiftControl& control)
{
    const int n = size_;
    const int half = n / 2;
    Channel& left = channels_[0];
    Channel& right = channels_[1];

    for (int j = 0; j < n; ++j)
    {
        const float w = window_[static_cast<std::size_t>(j)];
        work_[static_cast<std::size_t>(2 * j)] = w * history.sample(0, start + j);
        work_[static_cast<std::size_t>(2 * j + 1)] = w * history.sample(1, start + j);
    }
    fft_.forward(work_.data());

    // One complex FFT carried both channels; pull them apart using the
    // conjugate symmetry of a real signal's spectrum.
    left.re[0] = work_[0];
    right.re[0] = work_[1];
    left.re[static_cast<std::size_t>(half)] = work_[static_cast<std::size_t>(n)];
    right.re[static_cast<std::size_t>(half)] = work_[static_cast<std::size_t>(n + 1)];
    left.im[0] = right.im[0] = left.im[static_cast<std::size_t>(half)] = right.im[static_cast<std::size_t>(half)] = 0.0f;
    for (int k = 1; k < half; ++k)
    {
        const float a = work_[static_cast<std::size_t>(2 * k)];
        const float b = work_[static_cast<std::size_t>(2 * k + 1)];
        const float c = work_[static_cast<std::size_t>(2 * (n - k))];
        const float d = work_[static_cast<std::size_t>(2 * (n - k) + 1)];
        const auto s = static_cast<std::size_t>(k);
        left.re[s] = 0.5f * (a + c);
        left.im[s] = 0.5f * (b - d);
        right.re[s] = 0.5f * (b + d);
        right.im[s] = -0.5f * (a - c);
    }

    for (Channel& c : channels_)
    {
        for (int k = 0; k < bins_; ++k)
        {
            const auto s = static_cast<std::size_t>(k);
            c.mag[s] = std::hypot(c.re[s], c.im[s]);
            c.phase[s] = std::atan2(c.im[s], c.re[s]);
        }
    }

    const float ratio = std::clamp(control.ratio, 0.25f, 4.0f);
    const float formant = std::clamp(control.formantRatio, 0.5f, 2.0f);
    const bool identity = std::fabs(ratio - 1.0f) < 1.0e-4f && std::fabs(formant - 1.0f) < 1.0e-4f;

    if (identity)
    {
        for (Channel& c : channels_)
        {
            c.outRe = c.re;
            c.outIm = c.im;
            c.psi = c.phase;
            c.prevPhase = c.phase;
        }
    }
    else
    {
        const bool correct = std::fabs(std::log(formant / ratio)) > 1.0e-3f;
        if (correct)
            computeEnvelopes(control.period);
        for (Channel& c : channels_)
            shiftChannel(c, ratio, formant, correct);
    }

    // Recombine the two channels into one complex spectrum.
    work_[0] = left.outRe[0];
    work_[1] = right.outRe[0];
    work_[static_cast<std::size_t>(n)] = left.outRe[static_cast<std::size_t>(half)];
    work_[static_cast<std::size_t>(n + 1)] = right.outRe[static_cast<std::size_t>(half)];
    for (int k = 1; k < half; ++k)
    {
        const auto s = static_cast<std::size_t>(k);
        work_[static_cast<std::size_t>(2 * k)] = left.outRe[s] - right.outIm[s];
        work_[static_cast<std::size_t>(2 * k + 1)] = left.outIm[s] + right.outRe[s];
        work_[static_cast<std::size_t>(2 * (n - k))] = left.outRe[s] + right.outIm[s];
        work_[static_cast<std::size_t>(2 * (n - k) + 1)] = -left.outIm[s] + right.outRe[s];
    }
    fft_.inverse(work_.data());

    for (int j = 0; j < n; ++j)
    {
        const float w = olaScale_ * window_[static_cast<std::size_t>(j)];
        const std::size_t slot = static_cast<std::size_t>(static_cast<std::uint64_t>(start + j) & outputMask_);
        output_[0][slot] += w * work_[static_cast<std::size_t>(2 * j)];
        output_[1][slot] += w * work_[static_cast<std::size_t>(2 * j + 1)];
    }
}

void SpectralShifter::computeEnvelopes(const float period)
{
    // Cepstral envelope: low-pass the log magnitude along quefrency. Both
    // channels ride one complex transform, since a real, even sequence has a
    // real transform.
    const int n = size_;
    const int half = n / 2;
    Channel& left = channels_[0];
    Channel& right = channels_[1];

    for (int k = 0; k <= half; ++k)
    {
        const auto s = static_cast<std::size_t>(k);
        const float l = std::log(left.mag[s] + kLogFloor);
        const float r = std::log(right.mag[s] + kLogFloor);
        work_[static_cast<std::size_t>(2 * k)] = l;
        work_[static_cast<std::size_t>(2 * k + 1)] = r;
        if (k > 0 && k < half)
        {
            work_[static_cast<std::size_t>(2 * (n - k))] = l;
            work_[static_cast<std::size_t>(2 * (n - k) + 1)] = r;
        }
    }
    fft_.inverse(work_.data());

    // Cut below the pitch period, so the envelope does not resolve harmonics.
    const int cutoff = (period > 0.0f) ? std::clamp(static_cast<int>(0.6f * period), 24, 240) : 60;
    const int taperStart = cutoff * 7 / 10;
    for (int q = 0; q < n; ++q)
    {
        const int distance = std::min(q, n - q);
        float factor = 0.0f;
        if (distance < taperStart)
            factor = 1.0f;
        else if (distance < cutoff)
            factor = 0.5f + 0.5f * std::cos(kPi * static_cast<float>(distance - taperStart)
                                             / static_cast<float>(cutoff - taperStart));
        work_[static_cast<std::size_t>(2 * q)] *= factor;
        work_[static_cast<std::size_t>(2 * q + 1)] *= factor;
    }
    fft_.forward(work_.data());

    for (int k = 0; k <= half; ++k)
    {
        left.envelope[static_cast<std::size_t>(k)] = work_[static_cast<std::size_t>(2 * k)];
        right.envelope[static_cast<std::size_t>(k)] = work_[static_cast<std::size_t>(2 * k + 1)];
    }
}

float SpectralShifter::envelopeAt(const Channel& channel, const float bin, const int bins)
{
    const float clamped = std::clamp(bin, 0.0f, static_cast<float>(bins - 1));
    const int i = std::min(static_cast<int>(clamped), bins - 2);
    const float f = clamped - static_cast<float>(i);
    return channel.envelope[static_cast<std::size_t>(i)] * (1.0f - f)
           + channel.envelope[static_cast<std::size_t>(i + 1)] * f;
}

void SpectralShifter::shiftChannel(Channel& c, const float ratio, const float formantRatio,
                                   const bool correctFormants)
{
    const int n = size_;
    const int half = n / 2;
    const auto bins = static_cast<std::size_t>(bins_);

    // Spectral peaks: local maxima over a five-bin neighbourhood.
    peaks_.clear();
    float maxMag = 0.0f;
    for (int k = 0; k <= half; ++k)
        maxMag = std::max(maxMag, c.mag[static_cast<std::size_t>(k)]);
    const float floorMag = std::max(maxMag * 1.0e-4f, 1.0e-9f);
    for (int k = 2; k <= half - 2; ++k)
    {
        const auto s = static_cast<std::size_t>(k);
        const float m = c.mag[s];
        if (m > floorMag && m > c.mag[s - 1] && m >= c.mag[s + 1] && m > c.mag[s - 2] && m >= c.mag[s + 2])
            peaks_.push_back(k);
    }

    std::fill(c.outRe.begin(), c.outRe.end(), 0.0f);
    std::fill(c.outIm.begin(), c.outIm.end(), 0.0f);
    std::fill(c.outMag2.begin(), c.outMag2.end(), 0.0f);
    c.newPsi = c.phase;

    const float binToAdvance = kTwoPi * static_cast<float>(hop_) / static_cast<float>(n);

    for (std::size_t pi = 0; pi < peaks_.size(); ++pi)
    {
        const int p = peaks_[pi];
        // Below the first peak there is only rumble and window leakage; moving it
        // with the first region would reappear as a tone near the old pitch.
        const int lo = (pi == 0) ? std::max(0, p - 3) : (peaks_[pi - 1] + p) / 2 + 1;
        const int hi = (pi + 1 == peaks_.size()) ? half : (p + peaks_[pi + 1]) / 2;

        // True frequency of the peak, in bins, from the phase advance since the
        // previous frame.
        const auto ps = static_cast<std::size_t>(p);
        const float expected = binToAdvance * static_cast<float>(p);
        const float deviation = wrapPi(c.phase[ps] - c.prevPhase[ps] - expected);
        const float frequency = std::clamp(static_cast<float>(p) + deviation / binToAdvance, static_cast<float>(p) - 2.0f,
                                           static_cast<float>(p) + 2.0f);

        const float newFrequency = frequency * ratio;
        const int target = static_cast<int>(std::lround(newFrequency));
        if (target < 0 || target > half)
            continue;
        const int shift = target - p;

        const float psiPeak = wrapPi(c.psi[static_cast<std::size_t>(target)] + binToAdvance * newFrequency);
        const float rotation = psiPeak - c.phase[ps];
        const float cr = std::cos(rotation);
        const float ci = std::sin(rotation);

        for (int k = lo; k <= hi; ++k)
        {
            const int d = k + shift;
            if (d < 0 || d > half)
                continue;
            const auto ks = static_cast<std::size_t>(k);
            const auto ds = static_cast<std::size_t>(d);
            const float yr = cr * c.re[ks] - ci * c.im[ks];
            const float yi = cr * c.im[ks] + ci * c.re[ks];
            const float m2 = yr * yr + yi * yi;
            if (m2 > c.outMag2[ds])
            {
                c.outRe[ds] = yr;
                c.outIm[ds] = yi;
                c.outMag2[ds] = m2;
                c.newPsi[ds] = wrapPi(psiPeak + c.phase[ks] - c.phase[ps]);
            }
        }
    }

    if (peaks_.empty())
    {
        c.outRe = c.re;
        c.outIm = c.im;
        c.newPsi = c.phase;
    }
    else if (correctFormants)
    {
        // The shifted spectrum carries the original envelope scaled by `ratio`.
        // Wanted is the original envelope scaled by `formantRatio`.
        for (int k = 1; k < half; ++k)
        {
            const auto s = static_cast<std::size_t>(k);
            if (c.outMag2[s] <= 0.0f)
                continue;
            const float wanted = envelopeAt(c, static_cast<float>(k) / formantRatio, bins_);
            const float present = envelopeAt(c, static_cast<float>(k) / ratio, bins_);
            const float gain = std::exp(std::clamp(wanted - present, -kMaxEnvelopeGain, kMaxEnvelopeGain));
            c.outRe[s] *= gain;
            c.outIm[s] *= gain;
        }
    }
    c.outIm[0] = 0.0f;
    c.outIm[static_cast<std::size_t>(half)] = 0.0f;

    c.prevPhase = c.phase;
    c.psi = c.newPsi;
    (void)bins;
}

}  // namespace downspout::qune
