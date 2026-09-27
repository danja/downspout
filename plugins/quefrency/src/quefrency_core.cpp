#include "quefrency_core.hpp"
#include "quefrency_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace downspout::quefrency {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] float wrapPhase(float x) noexcept
{
    return x - kTwoPi * std::round(x / kTwoPi);
}

void fftTransform(const EngineState& st,
                  std::array<float, kMaxN>& re, std::array<float, kMaxN>& im,
                  bool inverse, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto j = static_cast<std::size_t>(st.fftBitrev[i]);
        if (j > i)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    const float direction = inverse ? 1.0f : -1.0f;
    for (std::size_t size = 2; size <= n; size *= 2)
    {
        const std::size_t half = size / 2;
        const std::size_t stride = n / size;
        for (std::size_t start = 0; start < n; start += size)
        {
            for (std::size_t k = 0; k < half; ++k)
            {
                const float wr = st.fftCos[k * stride];
                const float wi = direction * st.fftSin[k * stride];
                const std::size_t a = start + k;
                const std::size_t b = a + half;
                const float tr = re[b] * wr - im[b] * wi;
                const float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
    if (inverse)
    {
        const float scale = 1.0f / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            re[i] *= scale;
            im[i] *= scale;
        }
    }
}

// Cepstral smoothing: src is a log magnitude over bins 0..n/2, dst receives
// the same with every quefrency at or above nc removed.
void lifter(const EngineState& st,
            std::array<float, kMaxN>& cre, std::array<float, kMaxN>& cim,
            const std::array<float, kMaxBins>& src, std::array<float, kMaxBins>& dst,
            std::size_t nc, std::size_t n)
{
    const std::size_t half = n / 2;
    cre[0] = src[0];
    cre[half] = src[half];
    for (std::size_t k = 1; k < half; ++k)
    {
        cre[k] = src[k];
        cre[n - k] = src[k];
    }
    std::fill(cim.begin(), cim.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
    fftTransform(st, cre, cim, true, n);
    for (std::size_t q = nc; q <= n - nc; ++q)
        cre[q] = 0.0f;
    std::fill(cim.begin(), cim.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
    fftTransform(st, cre, cim, false, n);
    for (std::size_t k = 0; k <= half; ++k)
        dst[k] = cre[k];
}

void rebuildTilt(EngineState& st)
{
    const std::size_t half = st.n / 2;
    const float perOctave = st.formantTiltDb * 0.23025850929940458f;  // ln(10)/20
    for (std::size_t k = 0; k <= half; ++k)
    {
        const float hz = std::max(k * static_cast<float>(st.sampleRate) / static_cast<float>(st.n), 20.0f);
        st.tilt[k] = perOctave * std::log2(hz / 1000.0f);
    }
}

void processFrame(EngineState& st, std::size_t channel)
{
    ChannelState& ch = st.channels[channel];
    ScratchState& sc = st.scratch;
    const std::size_t n = st.n;
    const std::size_t hop = st.hop;
    const std::size_t half = n / 2;
    const float sampleRate = static_cast<float>(st.sampleRate);

    for (std::size_t i = 0; i < n; ++i)
    {
        sc.re[i] = ch.inFifo[i] * st.window[i];
        sc.im[i] = 0.0f;
    }
    fftTransform(st, sc.re, sc.im, false, n);

    float loudest = -3.402823466e+38f;
    for (std::size_t k = 0; k <= half; ++k)
    {
        const float magnitude = std::sqrt(sc.re[k] * sc.re[k] + sc.im[k] * sc.im[k]);
        const float logMag = std::log(std::max(magnitude, kMagnitudeFloor));
        sc.logMag[k] = logMag;
        loudest = std::max(loudest, logMag);
    }

    const auto nc = static_cast<std::size_t>(
        std::clamp(static_cast<int>(std::round(st.lifterMs * 0.001f * sampleRate)), 1,
                   static_cast<int>(half) - 1));
    lifter(st, sc.cre, sc.cim, sc.logMag, sc.logEnv, nc, n);
    if (st.trueEnvelope)
    {
        for (std::size_t k = 0; k <= half; ++k)
            sc.upper[k] = sc.logMag[k];
        for (std::size_t iter = 0; iter < kTrueEnvelopeIterations; ++iter)
        {
            for (std::size_t k = 0; k <= half; ++k)
                sc.upper[k] = std::max(sc.upper[k], sc.logEnv[k]);
            lifter(st, sc.cre, sc.cim, sc.upper, sc.logEnv, nc, n);
        }
    }

    float sum = sc.logEnv[0] + sc.logEnv[half];
    for (std::size_t k = 1; k < half; ++k)
        sum += 2.0f * sc.logEnv[k];
    const float mean = sum / static_cast<float>(n);

    for (std::size_t j = 0; j <= half; ++j)
    {
        const float source = static_cast<float>(j) / st.formantRatio;
        float warped;
        if (source >= static_cast<float>(half))
        {
            warped = sc.logEnv[half];
        }
        else
        {
            const float below = std::floor(source);
            const auto index = static_cast<std::size_t>(below);
            const float fraction = source - below;
            warped = sc.logEnv[index] + (sc.logEnv[index + 1] - sc.logEnv[index]) * fraction;
        }
        sc.logEnvOut[j] = mean + st.formantDepthLin * (warped - mean) + st.tilt[j];
    }

    const float harmonic = st.harmonicDepthLin - 1.0f;
    const bool shifting = st.pitchRatio != 1.0f || st.freqShiftHz != 0.0f;

    if (!shifting)
    {
        for (std::size_t k = 0; k <= half; ++k)
        {
            const float excitation = sc.logMag[k] - sc.logEnv[k];
            const float exponent = harmonic * excitation + sc.logEnvOut[k] - sc.logEnv[k];
            const float gain = std::exp(clampf(exponent, -kExponentLimit, kExponentLimit));
            sc.yre[k] = sc.re[k] * gain;
            sc.yim[k] = sc.im[k] * gain;
        }
        std::fill(ch.rotation.begin(), ch.rotation.begin() + static_cast<std::ptrdiff_t>(half + 1), 0.0f);
    }
    else
    {
        std::fill(sc.yre.begin(), sc.yre.begin() + static_cast<std::ptrdiff_t>(half + 1), 0.0f);
        std::fill(sc.yim.begin(), sc.yim.begin() + static_cast<std::ptrdiff_t>(half + 1), 0.0f);
        std::fill(sc.nextRotation.begin(),
                  sc.nextRotation.begin() + static_cast<std::ptrdiff_t>(half + 1), 0.0f);

        std::size_t count = 0;
        for (std::size_t k = 2; k + 1 < half; ++k)
        {
            const float m = sc.logMag[k];
            if (m > loudest - kPeakRange
                && m > sc.logMag[k - 1] && m >= sc.logMag[k + 1]
                && m > sc.logMag[k - 2] && m >= sc.logMag[k + 2])
            {
                sc.peaks[count++] = static_cast<std::uint16_t>(k);
            }
        }

        const float binHz = kTwoPi / static_cast<float>(n);
        const float offset = kTwoPi * st.freqShiftHz / sampleRate;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto peak = static_cast<std::size_t>(sc.peaks[i]);
            const std::size_t low = (i == 0) ? 0
                : (static_cast<std::size_t>(sc.peaks[i - 1]) + peak) / 2 + 1;
            const std::size_t high = (i + 1 == count) ? half
                : (peak + static_cast<std::size_t>(sc.peaks[i + 1])) / 2;

            const float phase = std::atan2(sc.im[peak], sc.re[peak]);
            const float expected = binHz * static_cast<float>(peak) * static_cast<float>(hop);
            const float deviation = wrapPhase(phase - ch.lastPhase[peak] - expected);
            const float omega = binHz * static_cast<float>(peak) + deviation / static_cast<float>(hop);
            const float target = omega * st.pitchRatio + offset;
            if (target <= 0.0f || target >= kTwoPi / 2.0f)
                continue;
            const auto shift = static_cast<std::ptrdiff_t>(std::round((target - omega) / binHz));
            const float rotation = wrapPhase(ch.rotation[peak] + (target - omega) * static_cast<float>(hop));
            const float rc = std::cos(rotation);
            const float rs = std::sin(rotation);

            for (std::size_t k = low; k <= high; ++k)
            {
                sc.nextRotation[k] = rotation;
                const std::ptrdiff_t j = static_cast<std::ptrdiff_t>(k) + shift;
                if (j < 0 || j > static_cast<std::ptrdiff_t>(half))
                    continue;
                const auto ju = static_cast<std::size_t>(j);
                const float excitation = sc.logMag[k] - sc.logEnv[k];
                const float exponent = harmonic * excitation - sc.logEnv[k] + sc.logEnvOut[ju];
                const float gain = std::exp(clampf(exponent, -kExponentLimit, kExponentLimit));
                sc.yre[ju] += (sc.re[k] * rc - sc.im[k] * rs) * gain;
                sc.yim[ju] += (sc.re[k] * rs + sc.im[k] * rc) * gain;
            }
        }
        for (std::size_t k = 0; k <= half; ++k)
            ch.rotation[k] = sc.nextRotation[k];
    }

    for (std::size_t k = 0; k <= half; ++k)
        ch.lastPhase[k] = std::atan2(sc.im[k], sc.re[k]);

    for (std::size_t k = 1; k < half; ++k)
    {
        sc.yre[n - k] = sc.yre[k];
        sc.yim[n - k] = -sc.yim[k];
    }
    sc.yim[0] = 0.0f;
    sc.yim[half] = 0.0f;
    fftTransform(st, sc.yre, sc.yim, true, n);

    for (std::size_t i = 0; i < n; ++i)
        ch.accum[i] += sc.yre[i] * st.window[i] * 0.5f;
    for (std::size_t i = 0; i < hop; ++i)
        ch.outFifo[i] = ch.accum[i];
    for (std::size_t i = 0; i < n - hop; ++i)
        ch.accum[i] = ch.accum[i + hop];
    for (std::size_t i = n - hop; i < n; ++i)
        ch.accum[i] = 0.0f;
    for (std::size_t i = 0; i < n - hop; ++i)
        ch.inFifo[i] = ch.inFifo[i + hop];
}

}  // namespace

Parameters clampParameters(const Parameters& raw) noexcept
{
    Parameters p = raw;
    p.formantShift = clampf(p.formantShift, -12.0f, 12.0f);
    p.formantDepth = clampf(p.formantDepth, 0.0f, 200.0f);
    p.formantTilt = clampf(p.formantTilt, -6.0f, 6.0f);
    p.pitchShift = clampf(p.pitchShift, -24.0f, 24.0f);
    p.pitchFine = clampf(p.pitchFine, -100.0f, 100.0f);
    p.freqShift = clampf(p.freqShift, -1000.0f, 1000.0f);
    p.harmonicDepth = clampf(p.harmonicDepth, 0.0f, 200.0f);
    p.lifter = clampf(p.lifter, 0.5f, 5.0f);
    p.estimator = clampf(std::round(p.estimator), 0.0f, 1.0f);
    p.mix = clampf(p.mix, 0.0f, 1.0f);
    p.output = clampf(p.output, -24.0f, 12.0f);
    return p;
}

void activate(EngineState& st, double sampleRate)
{
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const std::size_t n = frameSizeFor(rate);
    const std::size_t hop = n / 4;
    st = EngineState{};
    st.sampleRate = rate;
    st.n = n;
    st.hop = hop;
    // FFT tables for the active size.
    for (std::size_t i = 0; i < n / 2; ++i)
    {
        const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(n);
        st.fftCos[i] = std::cos(angle);
        st.fftSin[i] = std::sin(angle);
    }
    unsigned bits = 0;
    for (std::size_t t = n; t > 1; t >>= 1)
        ++bits;
    for (std::size_t i = 0; i < n; ++i)
    {
        std::uint32_t r = 0;
        std::uint32_t x = static_cast<std::uint32_t>(i);
        for (unsigned b = 0; b < bits; ++b)
        {
            r = (r << 1) | (x & 1u);
            x >>= 1;
        }
        st.fftBitrev[i] = static_cast<std::uint16_t>(r);
    }
    for (std::size_t i = 0; i < n; ++i)
    {
        const float hann = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(n));
        st.window[i] = std::sqrt(std::max(hann, 0.0f));
    }
    // Source defaults, in order: rebuilds tilt with the live rate and size.
    const Parameters defaults {};
    const Parameters c = clampParameters(defaults);
    st.params = c;
    setParameter(st, 0, c.formantShift);
    setParameter(st, 1, c.formantDepth);
    setParameter(st, 2, c.formantTilt);
    setParameter(st, 3, c.pitchShift);
    setParameter(st, 4, c.pitchFine);
    setParameter(st, 5, c.freqShift);
    setParameter(st, 6, c.harmonicDepth);
    setParameter(st, 7, c.lifter);
    setParameter(st, 8, c.estimator);
    setParameter(st, 9, c.mix);
    setParameter(st, 10, c.output);
    st.initialized = true;
}

void setParameter(EngineState& st, std::uint32_t index, float value)
{
    if (index >= kInputParameterCount)
        return;
    const Parameters c = clampParameters([&] {
        Parameters p = st.params;
        switch (static_cast<ParamId>(index))
        {
        case ParamId::formantShift: p.formantShift = value; break;
        case ParamId::formantDepth: p.formantDepth = value; break;
        case ParamId::formantTilt: p.formantTilt = value; break;
        case ParamId::pitchShift: p.pitchShift = value; break;
        case ParamId::pitchFine: p.pitchFine = value; break;
        case ParamId::freqShift: p.freqShift = value; break;
        case ParamId::harmonicDepth: p.harmonicDepth = value; break;
        case ParamId::lifter: p.lifter = value; break;
        case ParamId::estimator: p.estimator = value; break;
        case ParamId::mix: p.mix = value; break;
        case ParamId::output: p.output = value; break;
        case ParamId::outLatency: break;
        }
        return p;
    }());
    st.params = c;
    switch (static_cast<ParamId>(index))
    {
    case ParamId::formantShift:
        st.formantRatio = std::pow(2.0f, c.formantShift / 12.0f);
        break;
    case ParamId::formantDepth:
        st.formantDepthLin = c.formantDepth / 100.0f;
        break;
    case ParamId::formantTilt:
        st.formantTiltDb = c.formantTilt;
        rebuildTilt(st);
        break;
    case ParamId::pitchShift:
        st.pitchSemitones = c.pitchShift;
        st.pitchRatio = std::pow(2.0f, (st.pitchSemitones + st.pitchCents / 100.0f) / 12.0f);
        break;
    case ParamId::pitchFine:
        st.pitchCents = c.pitchFine;
        st.pitchRatio = std::pow(2.0f, (st.pitchSemitones + st.pitchCents / 100.0f) / 12.0f);
        break;
    case ParamId::freqShift:
        st.freqShiftHz = c.freqShift;
        break;
    case ParamId::harmonicDepth:
        st.harmonicDepthLin = c.harmonicDepth / 100.0f;
        break;
    case ParamId::lifter:
        st.lifterMs = c.lifter;
        break;
    case ParamId::estimator:
        st.trueEnvelope = c.estimator >= 0.5f;
        break;
    case ParamId::mix:
        st.mixLin = clampf(c.mix, 0.0f, 1.0f);
        break;
    case ParamId::output:
        st.gainLin = std::pow(10.0f, c.output / 20.0f);
        break;
    case ParamId::outLatency:
        break;
    }
}

bool applyMidiEvent(EngineState& st, std::uint8_t status, std::uint8_t d1, std::uint8_t d2)
{
    if ((status & 0xF0) != 0xB0)
        return false;
    ParamId target = ParamId::mix;
    if (!controllerTarget(d1, target))
        return false;
    setParameter(st, static_cast<std::uint32_t>(target), controllerToParameter(target, d2));
    return true;
}

void processBlock(EngineState& st,
                  std::uint32_t nframes,
                  const float* const* inputs,
                  float* const* outputs) noexcept
{
    if (!st.initialized || st.n == 0)
    {
        if (outputs)
        {
            if (outputs[0])
                std::fill(outputs[0], outputs[0] + nframes, 0.0f);
            if (outputs[1])
                std::fill(outputs[1], outputs[1] + nframes, 0.0f);
        }
        return;
    }
    const std::size_t n = st.n;
    const std::size_t hop = st.hop;
    const std::size_t latency = latencyFor(n);
    const float mix = st.mixLin;
    const float gain = st.gainLin;

    const float* inL = inputs && inputs[0] ? inputs[0] : nullptr;
    const float* inR = inputs && inputs[1] ? inputs[1] : nullptr;
    float* outL = outputs ? outputs[0] : nullptr;
    float* outR = outputs ? outputs[1] : nullptr;
    if (!outL || !outR)
        return;

    for (std::size_t c = 0; c < kChannels; ++c)
    {
        ChannelState& ch = st.channels[c];
        const float* in = (c == 0) ? inL : inR;
        float* out = (c == 0) ? outL : outR;
        for (std::uint32_t i = 0; i < nframes; ++i)
        {
            const float x = in ? in[i] : 0.0f;
            ch.inFifo[n - hop + ch.filled] = x;
            ch.filled += 1;
            if (ch.filled == hop)
            {
                processFrame(st, c);
                ch.filled = 0;
            }
            // Read after the frame, so a sample is emitted the moment it is
            // complete: latency N - 1 rather than N.
            const float wet = ch.outFifo[ch.filled];

            ch.dry[ch.dryPos] = x;
            const float dry = ch.dry[(ch.dryPos + kMaxN - latency) % kMaxN];
            ch.dryPos = (ch.dryPos + 1) % kMaxN;

            float y = (dry * (1.0f - mix) + wet * mix) * gain;
            out[i] = std::isfinite(y) ? y : 0.0f;
        }
    }
}

float currentLatencySamples(const EngineState& st) noexcept
{
    return static_cast<float>(latencyFor(st.n));
}

// ── Text state (version=1 key=value, magneto shape) ─────────────────────────

namespace {
using FieldPointer = float Parameters::*;
constexpr std::array<FieldPointer, kInputParameterCount> kFields = {{
    &Parameters::formantShift, &Parameters::formantDepth, &Parameters::formantTilt,
    &Parameters::pitchShift, &Parameters::pitchFine, &Parameters::freqShift,
    &Parameters::harmonicDepth, &Parameters::lifter, &Parameters::estimator,
    &Parameters::mix, &Parameters::output,
}};
}  // namespace

std::string serializeParameters(const Parameters& params)
{
    std::string text = "version=1\n";
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
    {
        text += kParameterSpecs[i].symbol;
        text += '=';
        text += std::to_string(params.*kFields[i]);
        text += '\n';
    }
    return text;
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t nl = text.find('\n', start);
        const std::size_t end = nl == std::string::npos ? text.size() : nl;
        const std::string_view line(text.data() + start, end - start);
        start = end + 1;
        if (line.empty())
        {
            if (nl == std::string::npos) break;
            continue;
        }
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view val = line.substr(sep + 1);
        if (key == "version") continue;
        std::string buf(val);
        char* e = nullptr;
        const float v = std::strtof(buf.c_str(), &e);
        if (e == nullptr || *e != '\0') return std::nullopt;
        bool matched = false;
        for (std::size_t i = 0; i < kInputParameterCount; ++i)
        {
            if (key == kParameterSpecs[i].symbol)
            {
                p.*kFields[i] = v;
                matched = true;
                break;
            }
        }
        if (!matched) return std::nullopt;
        if (nl == std::string::npos) break;
    }
    return clampParameters(p);
}

}  // namespace downspout::quefrency
