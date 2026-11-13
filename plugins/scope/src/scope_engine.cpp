#include "scope_engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>

namespace downspout::scope {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFmin = 20.0f;
constexpr float kFmax = 20000.0f;

void fft(std::complex<float>* a, int n)
{
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * kPi / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

float bandHz(double sr, double t)
{
    const double nyquist = sr * 0.5;
    const double top = std::min(static_cast<double>(kFmax), nyquist);
    return static_cast<float>(kFmin * std::pow(top / kFmin, t));
}

}  // namespace

float bandEdgeHz(const int band) noexcept
{
    return bandHz(48000.0, static_cast<double>(band) / static_cast<double>(kBands));
}

float bandCentreHz(const int band) noexcept
{
    return bandHz(48000.0, (static_cast<double>(band) + 0.5) / static_cast<double>(kBands));
}

float lufsFromPower(const float power)
{
    const float safePower = std::max(power, 1.0e-12f);
    return -0.691f + 10.0f * std::log10(safePower);
}

void activate(Engine& e, const double sr)
{
    e.sampleRate = sr;
    e.allocated = true;
    e.demo = std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr;

    const int n = kFftSize;
    e.fifo.assign(n, 0.0f);
    e.window.assign(n, 0.0f);
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float w = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(n - 1));
        e.window[static_cast<std::size_t>(i)] = std::sqrt(w);
        sum += e.window[static_cast<std::size_t>(i)];
    }
    e.windowMean = std::max(sum / static_cast<float>(n), 1.0e-3f);

    e.fifoPos = 0;
    e.hopCounter = 0;
    e.filled = 0;
    e.waveformPos = 0;
    e.waveformCounter = 0;
    e.waveformFresh = false;
    e.demoPhase = 0.0f;
    e.fftBuf.assign(n, std::complex<float>(0.0f, 0.0f));
    e.spectrum.assign(kBins, -120.0f);
    e.bands.fill(-120.0f);
    e.waveformL.fill(0.0f);
    e.waveformR.fill(0.0f);
    e.fresh = false;
    e.primed = false;
    e.peakL = e.peakR = e.rmsL = e.rmsR = 0.0f;
    e.corr = 0.0f;
    e.momentaryL = e.momentaryR = -120.0f;
    e.shortL = e.shortR = -120.0f;
    e.integratedL = e.integratedR = -120.0f;
    e.accLL = e.accRR = e.accLR = 0.0f;
}

void reset(Engine& e)
{
    if (!e.allocated)
        return;
    std::fill(e.fifo.begin(), e.fifo.end(), 0.0f);
    e.fifoPos = 0;
    e.hopCounter = 0;
    e.filled = 0;
    e.waveformPos = 0;
    e.waveformCounter = 0;
    e.waveformFresh = false;
    e.demoPhase = 0.0f;
    std::fill(e.spectrum.begin(), e.spectrum.end(), -120.0f);
    e.bands.fill(-120.0f);
    e.waveformL.fill(0.0f);
    e.waveformR.fill(0.0f);
    e.fresh = false;
    e.primed = false;
    e.peakL = e.peakR = e.rmsL = e.rmsR = 0.0f;
    e.corr = 0.0f;
    e.momentaryL = e.momentaryR = -120.0f;
    e.shortL = e.shortR = -120.0f;
    e.integratedL = e.integratedR = -120.0f;
    e.accLL = e.accRR = e.accLR = 0.0f;
}

void processBlock(Engine& e, const Params& p, const float* inL, const float* inR,
                  float* outL, float* outR, const std::uint32_t frames)
{
    if (!e.allocated) {
        for (std::uint32_t i = 0; i < frames; ++i) {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }

    const float speed = 0.01f + p.speed * 0.24f;
    const float rmsCoeff = 0.002f + p.speed * 0.02f;
    for (std::uint32_t i = 0; i < frames; ++i) {
        float a = inL[i];
        float b = inR[i];
        if (e.demo) {
            // Standalone/screenshot fallback: a moving harmonic tone so the
            // analyser and meters have something to show with no input connected.
            e.demoPhase += kPi * 2.0f * 220.0f / static_cast<float>(e.sampleRate);
            if (e.demoPhase > kPi * 2.0f)
                e.demoPhase -= kPi * 2.0f;
            const float t = e.demoPhase;
            a = 0.26f * std::sin(t) + 0.12f * std::sin(2.0f * t) + 0.06f * std::sin(3.01f * t)
                + 0.035f * std::sin(7.3f * t);
            b = a * 0.92f;
        }
        outL[i] = a;
        outR[i] = b;

        e.waveformL[static_cast<std::size_t>(e.waveformPos)] = a;
        e.waveformR[static_cast<std::size_t>(e.waveformPos)] = b;
        ++e.waveformCounter;
        if (e.waveformCounter >= 64) {
            e.waveformCounter = 0;
            e.waveformFresh = true;
        }
        e.waveformPos = (e.waveformPos + 1) % kWaveformSize;

        // Peak with a slow release, so the readout is a peak meter rather than
        // a sample-value display.
        e.peakL = std::max(e.peakL * 0.9995f, std::fabs(a));
        e.peakR = std::max(e.peakR * 0.9995f, std::fabs(b));

        const float powerL = a * a;
        const float powerR = b * b;
        e.rmsL += (powerL - e.rmsL) * rmsCoeff;
        e.rmsR += (powerR - e.rmsR) * rmsCoeff;
        e.momentaryL += (powerL - e.momentaryL) * 0.08f;
        e.momentaryR += (powerR - e.momentaryR) * 0.08f;
        e.shortL += (e.momentaryL - e.shortL) * 0.005f;
        e.shortR += (e.momentaryR - e.shortR) * 0.005f;
        e.integratedL += (e.momentaryL - e.integratedL) * 0.0004f;
        e.integratedR += (e.momentaryR - e.integratedR) * 0.0004f;

        e.accLL = e.accLL * 0.999f + powerL;
        e.accRR = e.accRR * 0.999f + powerR;
        e.accLR = e.accLR * 0.999f + a * b;
        const float denom = std::sqrt(e.accLL * e.accRR);
        e.corr = denom > 1.0e-12f ? e.accLR / denom : 0.0f;

        e.fifo[static_cast<std::size_t>(e.fifoPos)] = 0.5f * (a + b);
        e.fifoPos = (e.fifoPos + 1) % kFftSize;
        if (e.filled < kFftSize)
            ++e.filled;
        ++e.hopCounter;
        if (e.hopCounter < kFftSize / 4)
            continue;

        e.hopCounter = 0;
        if (e.filled < kFftSize)
            continue;   // wait for a full window before the first frame
        for (int n = 0; n < kFftSize; ++n) {
            const int idx = (e.fifoPos + n) % kFftSize;
            e.fftBuf[static_cast<std::size_t>(n)] =
                std::complex<float>(e.fifo[static_cast<std::size_t>(idx)] * e.window[static_cast<std::size_t>(n)], 0.0f);
        }
        fft(e.fftBuf.data(), kFftSize);

        // Normalise so a full-scale sine reads 0 dB.
        const float norm = 2.0f / (static_cast<float>(kFftSize) * e.windowMean);
        for (int bin = 0; bin < kBins; ++bin) {
            const float mag = std::abs(e.fftBuf[static_cast<std::size_t>(bin)]) * norm;
            const float db = 20.0f * std::log10(std::max(mag, 1.0e-6f));
            if (!e.primed)
                e.spectrum[static_cast<std::size_t>(bin)] = db;
            else
                e.spectrum[static_cast<std::size_t>(bin)] +=
                    (db - e.spectrum[static_cast<std::size_t>(bin)]) * speed;
        }
        e.primed = true;

        // Collapse the linear FFT bins into log-spaced bands. Each band takes the
        // loudest bin inside it, so narrow peaks stay visible at any frequency.
        const double nyquist = e.sampleRate * 0.5;
        const double top = std::min(static_cast<double>(kFmax), nyquist);
        const double ratio = top / static_cast<double>(kFmin);
        const double binHz = e.sampleRate / static_cast<double>(kFftSize);
        for (int band = 0; band < kBands; ++band) {
            const double f0 = kFmin * std::pow(ratio, static_cast<double>(band) / kBands);
            const double f1 = kFmin * std::pow(ratio, static_cast<double>(band + 1) / kBands);
            int b0 = static_cast<int>(std::lround(f0 / binHz));
            int b1 = static_cast<int>(std::lround(f1 / binHz));
            b0 = std::clamp(b0, 1, kBins - 2);
            b1 = std::clamp(b1, b0 + 1, kBins - 1);
            float peak = -120.0f;
            for (int bin = b0; bin <= b1; ++bin)
                peak = std::max(peak, e.spectrum[static_cast<std::size_t>(bin)]);
            e.bands[static_cast<std::size_t>(band)] = peak;
        }
        e.fresh = true;
    }
}

const std::array<float, kBands>& getBands(const Engine& e) { return e.bands; }
float getPeakL(const Engine& e) { return e.peakL; }
float getPeakR(const Engine& e) { return e.peakR; }
float getRmsL(const Engine& e) { return std::sqrt(e.rmsL); }
float getRmsR(const Engine& e) { return std::sqrt(e.rmsR); }
float getCorr(const Engine& e) { return e.corr; }
float getMomentaryLufs(const Engine& e) { return lufsFromPower(e.momentaryL); }
float getMomentaryRufs(const Engine& e) { return lufsFromPower(e.momentaryR); }
float getShortLufs(const Engine& e) { return lufsFromPower(e.shortL); }
float getShortRufs(const Engine& e) { return lufsFromPower(e.shortR); }
float getIntegratedLufs(const Engine& e) { return lufsFromPower(e.integratedL); }
float getIntegratedRufs(const Engine& e) { return lufsFromPower(e.integratedR); }
const std::array<float, kWaveformSize>& getWaveformL(const Engine& e) { return e.waveformL; }
const std::array<float, kWaveformSize>& getWaveformR(const Engine& e) { return e.waveformR; }

}  // namespace downspout::scope
