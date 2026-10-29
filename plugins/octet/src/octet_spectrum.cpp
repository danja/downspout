#include "octet_spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::octet {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
constexpr float kUpdatesPerSecond = 30.0f;

struct Tables {
    std::array<double, kFftSize> window {};
    std::array<double, kFftSize / 2> cosTable {}, sinTable {};
    std::array<int, kFftSize> reverse {};
    Tables()
    {
        for (int i = 0; i < kFftSize; ++i)
            window[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kFftSize);
        for (int i = 0; i < kFftSize / 2; ++i) {
            cosTable[static_cast<size_t>(i)] = std::cos(2.0 * kPi * i / kFftSize);
            sinTable[static_cast<size_t>(i)] = -std::sin(2.0 * kPi * i / kFftSize);
        }
        int bits = 0;
        while ((1 << bits) < kFftSize) ++bits;
        for (int i = 0; i < kFftSize; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            reverse[static_cast<size_t>(i)] = r;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

// In-place radix-2 complex FFT.
void fft(std::array<double, kFftSize>& re, std::array<double, kFftSize>& im)
{
    const Tables& t = tables();
    for (int i = 0; i < kFftSize; ++i) {
        const int j = t.reverse[static_cast<size_t>(i)];
        if (j > i) {
            std::swap(re[static_cast<size_t>(i)], re[static_cast<size_t>(j)]);
            std::swap(im[static_cast<size_t>(i)], im[static_cast<size_t>(j)]);
        }
    }
    for (int len = 2; len <= kFftSize; len <<= 1) {
        const int half = len / 2, step = kFftSize / len;
        for (int start = 0; start < kFftSize; start += len) {
            for (int k = 0; k < half; ++k) {
                const double wr = t.cosTable[static_cast<size_t>(k * step)], wi = t.sinTable[static_cast<size_t>(k * step)];
                const size_t a = static_cast<size_t>(start + k), b = static_cast<size_t>(start + k + half);
                const double xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}

void refresh(SpectrumState& s, const float sampleRate)
{
    const Tables& t = tables();
    auto& re = s.re;
    auto& im = s.im;
    for (int i = 0; i < kFftSize; ++i) {
        re[static_cast<size_t>(i)] = static_cast<double>(s.ring[static_cast<size_t>((s.writePos + i) % kFftSize)]) * t.window[static_cast<size_t>(i)];
        im[static_cast<size_t>(i)] = 0.0;
    }
    fft(re, im);

    // A sine of amplitude A peaks at A * N / 4 with a Hann window.
    const double scale = 4.0 / kFftSize;
    auto magnitude = [&](const int k) { return std::sqrt(re[static_cast<size_t>(k)] * re[static_cast<size_t>(k)] + im[static_cast<size_t>(k)] * im[static_cast<size_t>(k)]) * scale; };
    const double binHz = static_cast<double>(sampleRate) / kFftSize;
    const double nyquist = 0.5 * static_cast<double>(sampleRate);
    const float fall = kSpectrumFallDbPerSecond / kUpdatesPerSecond;

    for (int k = 0; k < kSpectrumBins; ++k) {
        const double lo = spectrumBandEdgeHz(k), hi = spectrumBandEdgeHz(k + 1);
        float db = kSpectrumFloorDb;
        if (lo < nyquist * 0.98) {
            // Narrow low bands fall between bins: interpolate at the band centre. Wide bands take their loudest bin.
            const double centre = std::sqrt(lo * hi) / binHz;
            const int c0 = std::clamp(static_cast<int>(std::floor(centre)), 0, kFftSize / 2 - 1);
            const double frac = centre - std::floor(centre);
            double m = magnitude(c0) * (1.0 - frac) + magnitude(std::min(c0 + 1, kFftSize / 2)) * frac;
            const int first = static_cast<int>(std::ceil(lo / binHz)), last = std::min(static_cast<int>(std::floor(hi / binHz)), kFftSize / 2);
            for (int b = first; b <= last; ++b) m = std::max(m, magnitude(b));
            db = std::clamp(static_cast<float>(20.0 * std::log10(m + 1e-9)), kSpectrumFloorDb, 6.0f);
        }
        float& held = s.levelDb[static_cast<size_t>(k)];
        held = s.primed ? std::max(db, held - fall) : db;
    }
    s.primed = true;
}

}  // namespace

double spectrumBandEdgeHz(const int k) noexcept
{
    return kMinHz * std::pow(kMaxHz / kMinHz, static_cast<double>(k) / kSpectrumBins);
}

void resetSpectrum(SpectrumState& state) noexcept
{
    state.ring.fill(0.0f);
    state.writePos = 0;
    state.sinceUpdate = 0;
    state.levelDb.fill(kSpectrumFloorDb);
    state.primed = false;
}

void analyseBlock(SpectrumState& state, const float sampleRate, const std::uint32_t frames, const float* left, const float* right) noexcept
{
    if (!(sampleRate > 0.0f)) return;
    const int hop = std::max(1, static_cast<int>(sampleRate / kUpdatesPerSecond));
    for (std::uint32_t i = 0; i < frames; ++i) {
        state.ring[static_cast<size_t>(state.writePos)] = 0.5f * (left[i] + right[i]);
        state.writePos = (state.writePos + 1) % kFftSize;
        if (++state.sinceUpdate >= hop) {
            state.sinceUpdate = 0;
            refresh(state, sampleRate);
        }
    }
}

void resetMeter(MeterState& state) noexcept
{
    state.peakDb.fill(kMeterFloorDb);
}

void updateMeter(MeterState& state, const float sampleRate, const std::uint32_t frames, const float* left, const float* right) noexcept
{
    if (!(sampleRate > 0.0f) || frames == 0) return;
    const float fall = kMeterFallDbPerSecond * static_cast<float>(frames) / sampleRate;
    const float* const channels[kMeterChannels] = {left, right};
    for (int c = 0; c < kMeterChannels; ++c) {
        float peak = 0.0f;
        for (std::uint32_t i = 0; i < frames; ++i)
            peak = std::max(peak, std::fabs(channels[c][i]));
        const float db = peak > 1e-6f ? 20.0f * std::log10(peak) : kMeterFloorDb;
        float& level = state.peakDb[static_cast<size_t>(c)];
        level = std::max(std::max(db, level - fall), kMeterFloorDb);
    }
}

}  // namespace downspout::octet
