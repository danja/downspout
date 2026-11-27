#include "energise_analysis.hpp"

#include "energise_fft.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace downspout::energise {
namespace {

struct Novelty {
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float total = 0.0f;
};

struct SpectrumBands {
    int lowBegin = 1;
    int lowEnd = 2;
    int midBegin = 2;
    int midEnd = 3;
    int highBegin = 3;
    int highEnd = 4;
};

[[nodiscard]] SpectrumBands bandsFor(const int fftSize, const int sampleRate)
{
    SpectrumBands bands {};
    const double binHz = static_cast<double>(sampleRate) / fftSize;
    const int limit = fftSize / 2;
    const auto bin = [binHz, limit](const double hz) {
        return std::clamp(static_cast<int>(std::ceil(hz / binHz)), 1, limit - 1);
    };
    bands.lowBegin = bin(30.0);
    bands.lowEnd = std::max(bands.lowBegin + 1, bin(250.0));
    bands.midBegin = bands.lowEnd;
    bands.midEnd = std::max(bands.midBegin + 1, bin(2500.0));
    bands.highBegin = bands.midEnd;
    bands.highEnd = std::max(bands.highBegin + 1, bin(12000.0));
    return bands;
}

[[nodiscard]] float bandFlux(const std::vector<float>& mag, const std::vector<float>& previous,
                             const int begin, const int end)
{
    float sum = 0.0f;
    for (int i = begin; i < end && i < static_cast<int>(mag.size()); ++i)
        sum += std::max(0.0f, mag[static_cast<std::size_t>(i)] - previous[static_cast<std::size_t>(i)]);
    return sum / static_cast<float>(std::max(1, end - begin));
}

}  // namespace

std::vector<DetectedOnset> detectOnsets(const CaptureBuffer& capture)
{
    const int sr = capture.sampleRate;
    const std::size_t frames = std::min(capture.left.size(), capture.right.size());
    if (sr <= 0 || frames < 2048)
        return {};

    std::vector<float> mono(frames);
    double mean = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        mono[i] = 0.5f * (capture.left[i] + capture.right[i]);
        mean += mono[i];
    }
    mean /= static_cast<double>(frames);
    for (float& sample : mono)
        sample -= static_cast<float>(mean);

    constexpr int kLarge = 2048;
    constexpr int kSmall = 1024;
    constexpr int kHop = 128;
    if (frames < static_cast<std::size_t>(kLarge))
        return {};
    const std::size_t frameCount = (frames - kLarge) / kHop + 1;

    Fft largeFft {};
    Fft smallFft {};
    largeFft.init(kLarge);
    smallFft.init(kSmall);
    const SpectrumBands largeBands = bandsFor(kLarge, sr);
    const SpectrumBands smallBands = bandsFor(kSmall, sr);

    std::vector<float> windowLarge(kLarge);
    std::vector<float> windowSmall(kSmall);
    std::vector<float> magLarge(kLarge);
    std::vector<float> magSmall(kSmall);
    std::vector<float> previousLarge(kLarge, 0.0f);
    std::vector<float> previousSmall(kSmall, 0.0f);
    std::vector<float> re {};
    std::vector<float> im {};
    std::vector<Novelty> novelty(frameCount);

    for (std::size_t f = 0; f < frameCount; ++f) {
        const std::size_t base = f * kHop;
        for (int i = 0; i < kLarge; ++i) {
            const double phase = 2.0 * 3.14159265358979323846 * i / (kLarge - 1);
            const float w = static_cast<float>(0.5 - 0.5 * std::cos(phase));
            windowLarge[static_cast<std::size_t>(i)] = mono[base + static_cast<std::size_t>(i)] * w;
        }
        const std::size_t smallBase = base + (kLarge - kSmall) / 2;
        for (int i = 0; i < kSmall; ++i) {
            const double phase = 2.0 * 3.14159265358979323846 * i / (kSmall - 1);
            const float w = static_cast<float>(0.5 - 0.5 * std::cos(phase));
            windowSmall[static_cast<std::size_t>(i)] = mono[smallBase + static_cast<std::size_t>(i)] * w;
        }

        largeFft.forward(windowLarge.data(), re, im);
        for (int i = 0; i < kLarge / 2; ++i)
            magLarge[static_cast<std::size_t>(i)] = std::hypot(re[static_cast<std::size_t>(i)], im[static_cast<std::size_t>(i)]) / kLarge;
        smallFft.forward(windowSmall.data(), re, im);
        for (int i = 0; i < kSmall / 2; ++i)
            magSmall[static_cast<std::size_t>(i)] = std::hypot(re[static_cast<std::size_t>(i)], im[static_cast<std::size_t>(i)]) / kSmall;

        Novelty sample {};
        sample.low = bandFlux(magLarge, previousLarge, largeBands.lowBegin, largeBands.lowEnd);
        sample.mid = bandFlux(magSmall, previousSmall, smallBands.midBegin, smallBands.midEnd);
        sample.high = bandFlux(magSmall, previousSmall, smallBands.highBegin, smallBands.highEnd);
        sample.total = 0.9f * sample.low + 1.0f * sample.mid + 1.3f * sample.high;
        novelty[f] = sample;
        std::swap(previousLarge, magLarge);
        std::swap(previousSmall, magSmall);
    }

    // Kicks carry far more low-band energy than hats carry high-band energy.
    // Normalising each band by its own peak lets quiet cymbals compete with
    // loud kicks in the peak picker, instead of disappearing under them.
    float lowPeak = 0.0f;
    float midPeak = 0.0f;
    float highPeak = 0.0f;
    for (const Novelty& sample : novelty) {
        lowPeak = std::max(lowPeak, sample.low);
        midPeak = std::max(midPeak, sample.mid);
        highPeak = std::max(highPeak, sample.high);
    }
    if (lowPeak <= 1e-10f && midPeak <= 1e-10f && highPeak <= 1e-10f)
        return {};
    const float lowNorm = std::max(1e-8f, lowPeak);
    const float midNorm = std::max(1e-8f, midPeak);
    const float highNorm = std::max(1e-8f, highPeak);
    float globalPeak = 0.0f;
    for (Novelty& sample : novelty) {
        sample.total = 0.9f * (sample.low / lowNorm)
                     + 1.0f * (sample.mid / midNorm)
                     + 1.3f * (sample.high / highNorm);
        globalPeak = std::max(globalPeak, sample.total);
    }
    if (globalPeak <= 1e-8f)
        return {};

    constexpr int kLocalWindow = 12;   // about 32 ms at 48 kHz with a 128-sample hop
    constexpr int kMinSeparation = 4;  // about 11 ms
    std::vector<std::size_t> candidates {};
    for (std::size_t f = kLocalWindow; f + kLocalWindow < frameCount; ++f) {
        double sum = 0.0;
        double square = 0.0;
        for (int k = -kLocalWindow; k <= kLocalWindow; ++k) {
            const float v = novelty[static_cast<std::size_t>(static_cast<int>(f) + k)].total;
            sum += v;
            square += static_cast<double>(v) * v;
        }
        const double count = 2.0 * kLocalWindow + 1.0;
        const double mean = sum / count;
        const double deviation = std::sqrt(std::max(0.0, square / count - mean * mean));
        const float value = novelty[f].total;
        if (value < mean + 1.35 * deviation + 1e-7f)
            continue;
        if (value < globalPeak * 0.07f)
            continue;
        if (value < novelty[f - 1].total || value < novelty[f + 1].total)
            continue;
        if (!candidates.empty() && f - candidates.back() < static_cast<std::size_t>(kMinSeparation)) {
            if (value > novelty[candidates.back()].total)
                candidates.back() = f;
            continue;
        }
        candidates.push_back(f);
    }

    std::vector<DetectedOnset> onsets {};
    onsets.reserve(candidates.size());
    for (const std::size_t f : candidates) {
        const double coarseTime = static_cast<double>(f * static_cast<std::size_t>(kHop) + kLarge / 2) / static_cast<double>(sr);
        const int searchHalf = static_cast<int>(0.010 * sr);
        const std::size_t centre = static_cast<std::size_t>(std::clamp(coarseTime * sr, 0.0, static_cast<double>(frames - 1)));
        const std::size_t begin = centre > static_cast<std::size_t>(searchHalf) ? centre - static_cast<std::size_t>(searchHalf) : 0;
        const std::size_t end = std::min(frames - 1, centre + static_cast<std::size_t>(searchHalf));
        std::size_t best = centre;
        float bestDerivative = -1.0f;
        for (std::size_t i = begin + 1; i <= end; ++i) {
            const float derivative = std::fabs(mono[i] - mono[i - 1]);
            if (derivative > bestDerivative) {
                bestDerivative = derivative;
                best = i;
            }
        }
        double refined = static_cast<double>(best) / static_cast<double>(sr);
        refined = std::clamp(refined, coarseTime - 0.010, coarseTime + 0.010);

        DetectedOnset onset {};
        onset.timeSec = refined;
        onset.strength = novelty[f].total / globalPeak;
        onset.low = novelty[f].low;
        onset.mid = novelty[f].mid;
        onset.high = novelty[f].high;
        if (!onsets.empty() && refined - onsets.back().timeSec < 0.008) {
            DetectedOnset& previous = onsets.back();
            const float weight = std::max(onset.strength, previous.strength);
            previous.low += onset.low;
            previous.mid += onset.mid;
            previous.high += onset.high;
            previous.strength = std::max(previous.strength, weight);
            continue;
        }
        onsets.push_back(onset);
    }
    return onsets;
}

}  // namespace downspout::energise
