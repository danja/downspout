#include "prism_engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace downspout::prism {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBandSmoothSeconds = 0.1;
constexpr double kSoloSmoothSeconds = 0.03;
constexpr double kMixSmoothSeconds = 0.05;

[[nodiscard]] int framesForDelay(const double sampleRate, const int hop)
{
    return static_cast<int>(std::lround(static_cast<double>(kMaxDelayMs) * 0.001 * sampleRate / hop));
}

void clearChannel(Engine::Channel& c)
{
    std::fill(c.fifo.begin(), c.fifo.end(), 0.0f);
    std::fill(c.accum.begin(), c.accum.end(), 0.0f);
    std::fill(c.history.begin(), c.history.end(), 0.0f);
    c.position = 0;
    c.frame = 0;
}

// Switches resolution. Only touches memory sized in activate().
void applySize(Engine& e, const int index)
{
    e.sizeIndex = index;
    e.fftSize = kFftSizes[static_cast<std::size_t>(index)];
    e.hop = e.fftSize / kOverlap;
    e.bins = e.fftSize / 2 + 1;
    e.maxFrames = framesForDelay(e.sampleRate, e.hop);
    e.rows = e.maxFrames + 1;
    e.hopCounter = 0;
    e.mappedBands = 0;  // bin -> band map is stale
    e.mappedLowCut = -1.0f;
    e.mappedHighCut = -1.0f;
    e.smoothedValid = false;
    for (auto& c : e.channels)
        clearChannel(c);
    std::fill(e.soloWeight.begin(), e.soloWeight.end(), 1.0f);
}

[[nodiscard]] float interpolate(const Engine& e, const std::size_t bin, const std::size_t field, const int bands)
{
    const int lo = e.binBand[bin];
    const int hi = std::min(lo + 1, bands - 1);
    const float t = e.binFrac[bin];
    return e.smoothed[static_cast<std::size_t>(lo) * 3 + field] * (1.0f - t) +
           e.smoothed[static_cast<std::size_t>(hi) * 3 + field] * t;
}

// Once per hop: smooth the band values, then rebuild each bin's delay, rotation, feedback
// and solo weight.
void updateFrame(Engine& e, const Parameters& p)
{
    const int bands = p.bands();
    const double sr = e.sampleRate;

    if (e.mappedBands != bands)
    {
        e.mappedBands = bands;
        for (int k = 0; k < e.bins; ++k)
        {
            const auto pos = bandPosition(static_cast<double>(k) * sr / e.fftSize, bands);
            e.binBand[static_cast<std::size_t>(k)] = pos.band;
            e.binFrac[static_cast<std::size_t>(k)] = pos.frac;
        }
    }

    const float lowCut = p[kLowCut];
    const float highCut = p[kHighCut];
    if (lowCut != e.mappedLowCut || highCut != e.mappedHighCut)
    {
        e.mappedLowCut = lowCut;
        e.mappedHighCut = highCut;
        for (int k = 0; k < e.bins; ++k)
        {
            const double hz = static_cast<double>(k) * sr / e.fftSize;
            e.binCut[static_cast<std::size_t>(k)] = lowCutWeight(hz, lowCut) * highCutWeight(hz, highCut);
        }
    }

    // Band values. R follows L when linked.
    const float bandCoef = 1.0f - static_cast<float>(std::exp(-static_cast<double>(e.hop) / (kBandSmoothSeconds * sr)));
    for (std::size_t b = 0; b < kMaxBands; ++b)
    {
        // Linked, the right side takes everything from the left. A synced side is a note
        // division of the host tempo, limited to the longest delay like any other.
        const auto sideMs = [&](const std::size_t channel) {
            const bool left = channel == 0 || p.linked();
            const float ms = p.band(b, left ? kDelayL : kDelayR);
            const float division = p.band(b, left ? kDivL : kDivR);
            return std::clamp(p.synced(channel) ? divisionMs(static_cast<int>(std::lround(division)), e.bpm) : ms, 0.0f,
                              kMaxDelayMs);
        };
        const float target[3] = {sideMs(0), sideMs(1), p.band(b, kFeedback)};
        for (std::size_t f = 0; f < 3; ++f)
        {
            float& s = e.smoothed[b * 3 + f];
            s = e.smoothedValid ? s + (target[f] - s) * bandCoef : target[f];
        }
    }
    e.smoothedValid = true;

    // Solo: 1 at a soloed band's centre, 0 at the others, interpolated like everything else.
    std::uint32_t mask = 0;
    for (int b = 0; b < bands; ++b)
        if (p.band(static_cast<std::size_t>(b), kSolo) >= 0.5f)
            mask |= 1u << b;
    const float soloCoef = 1.0f - static_cast<float>(std::exp(-static_cast<double>(e.hop) / (kSoloSmoothSeconds * sr)));
    for (int k = 0; k < e.bins; ++k)
    {
        const auto i = static_cast<std::size_t>(k);
        float target = 1.0f;
        if (mask != 0)
        {
            const int lo = e.binBand[i];
            const int hi = std::min(lo + 1, bands - 1);
            const float t = e.binFrac[i];
            target = static_cast<float>((mask >> lo) & 1u) * (1.0f - t) + static_cast<float>((mask >> hi) & 1u) * t;
        }
        e.soloTarget[i] = target;
        e.soloWeight[i] += (target - e.soloWeight[i]) * soloCoef;
    }

    for (int k = 0; k < e.bins; ++k)
    {
        const auto i = static_cast<std::size_t>(k);
        const double hz = static_cast<double>(k) * sr / e.fftSize;
        const float cut = e.binCut[i];
        e.binFeedback[i] = std::clamp(interpolate(e, i, 2, bands) * 0.01f * cut, 0.0f, kMaxFeedback);

        for (std::size_t c = 0; c < 2; ++c)
        {
            auto& ch = e.channels[c];
            const float ms = std::clamp(interpolate(e, i, c, bands), 0.0f, kMaxDelayMs);
            const double delaySamples = static_cast<double>(ms) * 0.001 * static_cast<double>(cut) * sr;
            // Nearest whole frame; what is left over is within half a hop either way.
            const double exact = delaySamples / e.hop;
            const int frames = std::min(static_cast<int>(std::lround(exact)), e.maxFrames);
            const double residual = (exact - frames) * e.hop;  // samples, signed
            ch.delayFrames[i] = frames;
            const double angle = -2.0 * kPi * hz * residual / sr;
            ch.phase[2 * i] = static_cast<float>(std::cos(angle));
            ch.phase[2 * i + 1] = static_cast<float>(std::sin(angle));
            // A rotated frame loses cos(pi r / N) of its level through the two windows.
            ch.gain[i] = static_cast<float>(1.0 / std::cos(kPi * residual / e.fftSize));
        }
    }
}

void processFrame(Engine& e, Engine::Channel& c)
{
    const int n = e.fftSize;
    const auto& window = e.windows[static_cast<std::size_t>(e.sizeIndex)];
    auto& fft = e.ffts[static_cast<std::size_t>(e.sizeIndex)];
    float* data = e.scratch.data();

    for (int i = 0; i < n; ++i)
    {
        data[2 * i] = c.fifo[static_cast<std::size_t>((c.position + i) % n)] * window[static_cast<std::size_t>(i)];
        data[2 * i + 1] = 0.0f;
    }
    fft.forward(data);

    const std::int64_t frame = c.frame++;
    float* write = c.history.data() + static_cast<std::size_t>(frame % e.rows) * static_cast<std::size_t>(e.bins) * 2;

    for (int k = 0; k < e.bins; ++k)
    {
        const auto i = static_cast<std::size_t>(k);
        const std::complex<float> in(data[2 * k], data[2 * k + 1]);
        const int frames = c.delayFrames[i];

        // frames >= 1 never reads the row being written (frames <= rows - 1).
        std::complex<float> delayed = in;
        if (frames > 0)
        {
            const std::int64_t source = frame - frames;
            if (source >= 0)
            {
                const float* row = c.history.data() +
                                   static_cast<std::size_t>(source % e.rows) * static_cast<std::size_t>(e.bins) * 2;
                delayed = {row[2 * k], row[2 * k + 1]};
            }
            else
            {
                delayed = {};
            }
        }
        delayed *= std::complex<float>(c.phase[2 * i], c.phase[2 * i + 1]);

        // The stored spectrum is input plus the rotated, delayed one, so each repeat is
        // late by the whole delay. A bin with no whole-frame delay would be a zero-delay
        // loop, so it gets no feedback.
        const std::complex<float> stored = frames > 0 ? in + e.binFeedback[i] * delayed : in;
        write[2 * k] = stored.real();
        write[2 * k + 1] = stored.imag();

        // Solo and the rotation's compensation touch only what is heard, never the loop.
        const std::complex<float> out = delayed * (e.soloWeight[i] * c.gain[i]);
        data[2 * k] = out.real();
        data[2 * k + 1] = out.imag();
    }
    data[1] = 0.0f;
    data[2 * (n / 2) + 1] = 0.0f;
    for (int k = 1; k < n / 2; ++k)
    {
        data[2 * (n - k)] = data[2 * k];
        data[2 * (n - k) + 1] = -data[2 * k + 1];
    }
    fft.inverse(data);

    const float scale = e.cola[static_cast<std::size_t>(e.sizeIndex)];
    for (int i = 0; i < n; ++i)
        c.accum[static_cast<std::size_t>((c.position + i) % n)] += data[2 * i] * window[static_cast<std::size_t>(i)] * scale;
}

}  // namespace

void activate(Engine& e, const double sampleRate)
{
    e.sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const int maxN = kFftSizes[kFftSizeCount - 1];

    e.historyCapacity = 0;
    for (std::size_t s = 0; s < kFftSizeCount; ++s)
    {
        const int n = kFftSizes[s];
        e.ffts[s].init(n);
        auto& w = e.windows[s];
        w.resize(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            w[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * kPi * i / n)));
        // Squared window summed at the hop spacing is constant; its inverse restores unity gain.
        double sum = 0.0;
        for (int m = 0; m < kOverlap; ++m)
        {
            const double v = w[static_cast<std::size_t>((n / kOverlap) * m)];
            sum += v * v;
        }
        e.cola[s] = static_cast<float>(1.0 / sum);
        const auto rows = static_cast<std::size_t>(framesForDelay(e.sampleRate, n / kOverlap) + 1);
        e.historyCapacity = std::max(e.historyCapacity, rows * static_cast<std::size_t>(n / 2 + 1) * 2);
    }

    e.scratch.assign(static_cast<std::size_t>(2 * maxN), 0.0f);
    const auto maxBins = static_cast<std::size_t>(maxN / 2 + 1);
    e.binBand.assign(maxBins, 0);
    e.binFrac.assign(maxBins, 0.0f);
    e.binCut.assign(maxBins, 1.0f);
    e.binFeedback.assign(maxBins, 0.0f);
    e.soloTarget.assign(maxBins, 1.0f);
    e.soloWeight.assign(maxBins, 1.0f);
    for (auto& c : e.channels)
    {
        c.fifo.assign(static_cast<std::size_t>(maxN), 0.0f);
        c.accum.assign(static_cast<std::size_t>(maxN), 0.0f);
        c.history.assign(e.historyCapacity, 0.0f);
        c.delayFrames.assign(maxBins, 0);
        c.phase.assign(maxBins * 2, 0.0f);
        c.gain.assign(maxBins, 1.0f);
    }
    e.allocated = true;
    e.sizeIndex = -1;
    e.mixPrimed = false;
    applySize(e, 2);
}

void setTempo(Engine& e, const double bpm) noexcept
{
    e.bpm = std::isfinite(bpm) && bpm >= 1.0 ? std::min(bpm, 1000.0) : static_cast<double>(kDefaultTempo);
}

void reset(Engine& e)
{
    if (!e.allocated)
        return;
    e.mixPrimed = false;
    applySize(e, e.sizeIndex < 0 ? 2 : e.sizeIndex);
}

int latencySamples(const Engine& e) noexcept
{
    return e.fftSize;
}

void processBlock(Engine& e, const Parameters& params, const float* inL, const float* inR, float* outL, float* outR,
                  const std::size_t frames)
{
    if (!e.allocated)
        return;
    if (params.sizeIndex() != e.sizeIndex)
        applySize(e, params.sizeIndex());

    bool anySolo = false;
    for (int b = 0; b < params.bands(); ++b)
        anySolo = anySolo || params.band(static_cast<std::size_t>(b), kSolo) >= 0.5f;
    // With a band soloed only the delayed sound is heard.
    const float mixTarget = anySolo ? 1.0f : std::clamp(params[kMix], 0.0f, 1.0f);
    if (!e.mixPrimed)
    {
        e.mixSmoothed = mixTarget;
        e.mixPrimed = true;
    }
    const float mixCoef = 1.0f - static_cast<float>(std::exp(-1.0 / (kMixSmoothSeconds * e.sampleRate)));

    const int n = e.fftSize;
    const float* in[2] = {inL, inR};
    float* out[2] = {outL, outR};

    for (std::size_t s = 0; s < frames; ++s)
    {
        e.mixSmoothed += (mixTarget - e.mixSmoothed) * mixCoef;
        for (std::size_t c = 0; c < 2; ++c)
        {
            auto& ch = e.channels[c];
            const auto pos = static_cast<std::size_t>(ch.position);
            const float dry = ch.fifo[pos];
            ch.fifo[pos] = in[c][s];
            const float wet = ch.accum[pos];
            ch.accum[pos] = 0.0f;
            ch.position = (ch.position + 1) % n;
            out[c][s] = wet * e.mixSmoothed + dry * (1.0f - e.mixSmoothed);
        }
        if (++e.hopCounter >= e.hop)
        {
            e.hopCounter = 0;
            updateFrame(e, params);
            for (auto& ch : e.channels)
                processFrame(e, ch);
        }
    }
}

}  // namespace downspout::prism
