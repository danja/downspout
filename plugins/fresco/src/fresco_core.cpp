#include "fresco_core.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace downspout::fresco {

namespace {

constexpr double kPi = 3.14159265358979323846;
using Cx = std::complex<float>;

struct Tables {
    std::array<float, kFrame> window {};
    std::array<Cx, kFrame / 2> twiddle {};
    std::array<std::uint16_t, kFrame> reverse {};
    Tables()
    {
        for (int n = 0; n < kFrame; ++n) window[static_cast<std::size_t>(n)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * n / kFrame));   // periodic Hann
        for (int k = 0; k < kFrame / 2; ++k) twiddle[static_cast<std::size_t>(k)] = Cx(static_cast<float>(std::cos(2.0 * kPi * k / kFrame)), static_cast<float>(-std::sin(2.0 * kPi * k / kFrame)));
        int bits = 0;
        while ((1 << bits) < kFrame) ++bits;
        for (int i = 0; i < kFrame; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            reverse[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(r);
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

// In-place radix-2 FFT; `inverse` conjugates the twiddles (no scaling).
void fft(std::array<Cx, kFrame>& a, const bool inverse) noexcept
{
    const Tables& t = tables();
    for (int i = 0; i < kFrame; ++i) {
        const int j = t.reverse[static_cast<std::size_t>(i)];
        if (j > i) std::swap(a[static_cast<std::size_t>(i)], a[static_cast<std::size_t>(j)]);
    }
    for (int len = 2; len <= kFrame; len <<= 1) {
        const int half = len / 2, step = kFrame / len;
        for (int i = 0; i < kFrame; i += len) {
            for (int k = 0; k < half; ++k) {
                Cx w = t.twiddle[static_cast<std::size_t>(k * step)];
                if (inverse) w = std::conj(w);
                const Cx u = a[static_cast<std::size_t>(i + k)], v = a[static_cast<std::size_t>(i + k + half)] * w;
                a[static_cast<std::size_t>(i + k)] = u + v;
                a[static_cast<std::size_t>(i + k + half)] = u - v;
            }
        }
    }
}

float nextRandom(std::uint32_t& s) noexcept
{
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
}

float clampf(const float v, const float lo, const float hi) noexcept { return std::min(hi, std::max(lo, std::isfinite(v) ? v : lo)); }

void seedNoise(EngineState& s) noexcept
{
    for (std::size_t c = 0; c < 2; ++c) {
        for (std::size_t k = 0; k < static_cast<std::size_t>(kBins); ++k) {
            const float phase = static_cast<float>(2.0 * kPi) * nextRandom(s.rng);
            s.ch[c].noiseCos[k] = std::cos(phase);
            s.ch[c].noiseSin[k] = std::sin(phase);
        }
    }
}

float pixel(const Image& im, const double x, const double y) noexcept
{
    // bilinear, pixel centres at +0.5, edges clamped
    const double fx = std::min(std::max(x, 0.0), static_cast<double>(im.width - 1)), fy = std::min(std::max(y, 0.0), static_cast<double>(im.height - 1));
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, im.width - 1), y1 = std::min(y0 + 1, im.height - 1);
    const double tx = fx - x0, ty = fy - y0;
    const double top = im.at(x0, y0) * (1.0 - tx) + im.at(x1, y0) * tx, bottom = im.at(x0, y1) * (1.0 - tx) + im.at(x1, y1) * tx;
    return static_cast<float>((top * (1.0 - ty) + bottom * ty) / 255.0);
}

float averagePixelRows(const Image& im, const double x, const double top, const double bottom, const Parameters& p) noexcept
{
    const double lo = std::clamp(top, 0.0, static_cast<double>(im.height));
    const double hi = std::clamp(bottom, 0.0, static_cast<double>(im.height));
    const double span = hi - lo;
    const auto magnitude = [&](const double brightness) {
        const double v = p.invert >= 0.5f ? 1.0 - brightness : brightness;
        if (v <= 0.0)
            return 0.0;
        const double g = std::pow(v, p.contrast);
        return std::pow(10.0, -p.rangeDb * (1.0 - g) / 20.0);
    };
    if (!(span > 0.0))
        return static_cast<float>(magnitude(pixel(im, x, lo - 0.5)));

    double area = 0.0;
    for (int row = static_cast<int>(std::floor(lo)); row < im.height && row < hi; ++row) {
        const double overlap = std::min(hi, static_cast<double>(row + 1)) - std::max(lo, static_cast<double>(row));
        if (overlap > 0.0)
            area += magnitude(pixel(im, x, row)) * overlap;
    }
    return static_cast<float>(area / span);
}

}  // namespace

Parameters clampParameters(const Parameters& in) noexcept
{
    Parameters p = in;
    p.lengthS = clampf(p.lengthS, 0.5f, 120.0f);
    p.rangeDb = clampf(p.rangeDb, 20.0f, 100.0f);
    p.contrast = clampf(p.contrast, 0.25f, 4.0f);
    p.minHz = clampf(p.minHz, 20.0f, 10000.0f);
    p.maxHz = clampf(p.maxHz, 200.0f, 20000.0f);
    if (p.maxHz < p.minHz * 1.5f) p.maxHz = std::min(20000.0f, p.minHz * 1.5f);
    if (p.maxHz < p.minHz * 1.5f) p.minHz = p.maxHz / 1.5f;
    p.logAxis = p.logAxis >= 0.5f ? 1.0f : 0.0f;
    p.invert = p.invert >= 0.5f ? 1.0f : 0.0f;
    p.source = std::floor(clampf(p.source, 0.0f, static_cast<float>(kSourceCount - 1)) + 0.5f);
    p.follow = clampf(p.follow, 0.0f, 1.0f);
    p.transport = p.transport >= 0.5f ? 1.0f : 0.0f;
    p.mix = clampf(p.mix, 0.0f, 1.0f);
    p.outputDb = clampf(p.outputDb, -24.0f, 12.0f);
    return p;
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
    seedNoise(state);
}

void columnMagnitudes(const Image& image, const Parameters& p, const float sampleRate, const double playhead, float* mags) noexcept
{
    for (int k = 0; k < kBins; ++k) mags[k] = 0.0f;
    if (image.empty()) return;
    const double x = std::min(std::max(playhead, 0.0), 1.0) * image.width - 0.5;
    const double lo = p.minHz, hi = p.maxHz, lnRatio = std::log(hi / lo);
    const double binWidth = static_cast<double>(sampleRate) / kFrame;
    for (int k = 1; k < kFrame / 2; ++k) {
        const double f = static_cast<double>(k) * sampleRate / kFrame;
        if (f < lo || f > hi) continue;
        const double binLow = std::max(lo, f - binWidth * 0.5);
        const double binHigh = std::min(hi, f + binWidth * 0.5);
        const auto row = [&](const double hz) {
            const double fraction = p.logAxis >= 0.5f ? std::log(hi / hz) / lnRatio : (hi - hz) / (hi - lo);
            return fraction * image.height;
        };
        mags[k] = averagePixelRows(image, x, row(binHigh), row(binLow), p);
    }
}

void processBlock(EngineState& state, const Parameters& paramsIn, const Image* image, const float sampleRate, const std::uint32_t frames, const Transport& transport,
                  const float* inLeft, const float* inRight, float* outLeft, float* outRight) noexcept
{
    const Parameters p = clampParameters(paramsIn);
    const Tables& t = tables();
    const bool havePicture = image != nullptr && !image->empty();
    const float outGain = std::pow(10.0f, p.outputDb / 20.0f);
    const bool useInput = p.source < 0.5f;
    const float norm = useInput ? 1.0f / 1.5f : 1.0f / 2.0f;   // overlap-add gain of w^2 (analysis and synthesis) or w (synthesis only) at hop N/4
    const float mixCoef = 1.0f - std::exp(-1.0f / (0.02f * sampleRate));
    const bool follow = p.transport >= 0.5f && transport.valid && transport.bpm > 0.0;
    std::array<float, kBins> mags {};
    std::array<Cx, kFrame> buf {};
    if (!state.mixStarted) { state.mixSmooth = p.mix; state.mixStarted = true; }

    for (std::uint32_t i = 0; i < frames; ++i) {
        const float xin[2] = {inLeft[i], inRight[i]};
        float wet[2] = {0.0f, 0.0f}, dryOut[2] = {0.0f, 0.0f};
        for (std::size_t c = 0; c < 2; ++c) {
            Channel& ch = state.ch[c];
            ch.in[static_cast<std::size_t>(ch.inPos)] = std::isfinite(xin[c]) ? xin[c] : 0.0f;
            ch.inPos = (ch.inPos + 1) % kFrame;
            dryOut[c] = ch.dry[static_cast<std::size_t>(ch.dryPos)];
            ch.dry[static_cast<std::size_t>(ch.dryPos)] = ch.in[static_cast<std::size_t>((ch.inPos + kFrame - 1) % kFrame)];
            ch.dryPos = (ch.dryPos + 1) % kFrame;
            wet[c] = ch.queue[static_cast<std::size_t>(ch.queuePos)];
        }
        for (std::size_t c = 0; c < 2; ++c) state.ch[c].queuePos = (state.ch[c].queuePos + 1) % kHop;   // queue holds one hop

        if (++state.hopCount >= kHop) {
            state.hopCount = 0;
            double position;   // seconds along the picture
            if (follow) position = (transport.beatPosition * 60.0 / transport.bpm) + static_cast<double>(i) / sampleRate;
            else position = state.timeS;
            double playhead = std::fmod(position / p.lengthS, 1.0);
            if (playhead < 0.0) playhead += 1.0;
            state.timeS += static_cast<double>(kHop) / sampleRate;
            if (state.timeS > 1.0e6) state.timeS = std::fmod(state.timeS, static_cast<double>(p.lengthS));
            state.meters.playhead = static_cast<float>(playhead);

            if (havePicture) columnMagnitudes(*image, p, sampleRate, playhead, mags.data());
            else mags.fill(0.0f);
            float sum = 0.0f;
            for (int k = 1; k < kFrame / 2; ++k) sum += mags[static_cast<std::size_t>(k)];
            state.meters.level = std::min(1.0f, sum / static_cast<float>(kFrame / 2) * 8.0f);

            for (std::size_t c = 0; c < 2; ++c) {
                Channel& ch = state.ch[c];
                float energy = 0.0f;
                for (int n = 0; n < kFrame; ++n) {
                    const float x = ch.in[static_cast<std::size_t>((ch.inPos + n) % kFrame)];
                    energy += x * x;
                    buf[static_cast<std::size_t>(n)] = Cx(x * t.window[static_cast<std::size_t>(n)], 0.0f);
                }
                const float rms = std::sqrt(energy / kFrame);
                fft(buf, false);
                const float levelGain = (1.0f - p.follow) + p.follow * std::min(1.0f, rms * 8.0f);
                const float scale = kBinAmplitude * (static_cast<float>(kFrame) * 0.5f) * levelGain * outGain;
                for (int k = 0; k < kBins; ++k) {
                    Cx unit;
                    const std::size_t ku = static_cast<std::size_t>(k);
                    const float magIn = std::abs(buf[ku]);
                    if (useInput && magIn > 1.0e-6f) {
                        unit = buf[ku] / magIn;
                    } else {
                        // bin-centred sinusoid: advances k*hop/N turns per frame = k quarter turns
                        Cx u(ch.noiseCos[ku], ch.noiseSin[ku]);
                        switch (static_cast<std::uint64_t>(k) * state.frameIndex & 3u) {
                        case 1: u *= Cx(0.0f, 1.0f); break;
                        case 2: u = -u; break;
                        case 3: u *= Cx(0.0f, -1.0f); break;
                        default: break;
                        }
                        unit = u;
                    }
                    const float m = (k == 0 || k == kFrame / 2) ? 0.0f : mags[ku] * scale;
                    buf[ku] = unit * m;
                    if (k > 0 && k < kFrame / 2) buf[static_cast<std::size_t>(kFrame - k)] = std::conj(buf[ku]);
                }
                fft(buf, true);
                for (int n = 0; n < kFrame; ++n) {
                    float& a = ch.acc[static_cast<std::size_t>((ch.accPos + n) % kFrame)];
                    a += buf[static_cast<std::size_t>(n)].real() * (1.0f / kFrame) * t.window[static_cast<std::size_t>(n)] * norm;
                }
                for (int n = 0; n < kHop; ++n) {
                    float& a = ch.acc[static_cast<std::size_t>((ch.accPos + n) % kFrame)];
                    ch.queue[static_cast<std::size_t>(n)] = a;
                    a = 0.0f;
                }
                ch.accPos = (ch.accPos + kHop) % kFrame;
                ch.queuePos = 0;
            }
            ++state.frameIndex;
        }

        state.mixSmooth += (p.mix - state.mixSmooth) * mixCoef;
        outLeft[i] = dryOut[0] * (1.0f - state.mixSmooth) + wet[0] * state.mixSmooth;
        outRight[i] = dryOut[1] * (1.0f - state.mixSmooth) + wet[1] * state.mixSmooth;
    }
}

}  // namespace downspout::fresco
