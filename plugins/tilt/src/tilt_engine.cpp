#include "tilt_engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace downspout::tilt {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLn2 = 0.69314718055994530942;
constexpr double kLowCornerHz = 20.0;
constexpr double kHighCornerHz = 20000.0;
constexpr double kTiny = 1.0e-20;
constexpr double kMaxLinearGain = 15.848931924611134;  // 10^(24/20)

struct Design {
    std::array<FirstOrder, kMaxSections> sections {};
    int count = 0;
    double normalizer = 1.0;
};

[[nodiscard]] double idealFrom(const double logPivot, const double exp, const double hz) noexcept
{
    if (!(hz > 0.0))
        return 0.0;
    const double raw = 6.0 * exp * (std::log(hz) - logPivot) / kLn2;
    return std::clamp(raw, -static_cast<double>(kMaxTiltDb), static_cast<double>(kMaxTiltDb));
}

// A first-order shelf with the bilinear transform applied to the analogue prototype
// (1 + s/wz)/(1 + s/wp). Using the raw analogue frequencies rather than prewarped ones
// keeps the plateau ratio wp/wz exactly equal to the requested frequency ratio. Prewarping
// would inflate the ratio without bound as a corner approaches Nyquist, making the slope
// far steeper than the ideal line and pushing the cascade well past +-24 dB.
[[nodiscard]] FirstOrder section(const double fz, const double fp, const double sr) noexcept
{
    const double kz = sr / (kPi * std::max(fz, 0.01));
    const double kp = sr / (kPi * std::max(fp, 0.01));
    const double den = 1.0 + kp;
    FirstOrder s;
    s.b0 = (1.0 + kz) / den;
    s.b1 = (1.0 - kz) / den;
    s.a1 = (1.0 - kp) / den;
    return s;
}

[[nodiscard]] double magnitude(const FirstOrder& s, const double w) noexcept
{
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> num = s.b0 + s.b1 * z1;
    const std::complex<double> den = 1.0 + s.a1 * z1;
    return std::abs(num / den);
}

// Peak magnitude of a design at one digital frequency.
[[nodiscard]] double designMagnitude(const Design& d, const double w) noexcept
{
    double mag = d.normalizer;
    for (int i = 0; i < d.count; ++i)
        mag *= magnitude(d.sections[static_cast<std::size_t>(i)], w);
    return mag;
}

[[nodiscard]] Design design(const double logPivot, const double exp, const double sr) noexcept
{
    Design d;
    const double magnitudeExp = std::fabs(exp);
    if (!(magnitudeExp > 1.0e-9) || !(sr > 0.0))
        return d;

    // Frequencies where the ideal straight line reaches -24 and +24 dB. These are the
    // shelf corners: outside them the cascade is flat, so the response cannot exceed the
    // law. The bilinear transform maps these analogue frequencies to the digital axis
    // without the tan() blow-up near Nyquist that prewarping would cause.
    const double pivotHz = std::exp(logPivot);
    const double octaves = 4.0 / magnitudeExp;
    const double fLow = std::max(pivotHz / std::pow(2.0, octaves), 0.5);
    const double fHigh = std::min(pivotHz * std::pow(2.0, octaves), sr * 0.95);
    const double effectiveHigh = fHigh > fLow * 1.01 ? fHigh : fLow * 1.01;

    // Integer orders are one whole shelf each; the fractional remainder uses an
    // Oustaloup ladder of log-spaced pole/zero pairs to approximate f^alpha.
    const int full = std::min(static_cast<int>(std::floor(magnitudeExp + 1.0e-9)), 4);
    double rest = magnitudeExp - static_cast<double>(full);
    if (rest < 1.0e-6)
        rest = 0.0;
    const bool boost = exp > 0.0;
    auto add = [&](const double fz, const double fp) {
        if (d.count >= kMaxSections)
            return;
        d.sections[static_cast<std::size_t>(d.count++)] =
            section(boost ? fz : fp, boost ? fp : fz, sr);
    };
    for (int i = 0; i < full; ++i)
        add(fLow, effectiveHigh);
    if (rest > 0.0) {
        const double decade = std::log10(effectiveHigh / fLow);
        const double denominator = 2.0 * kOustaloupSections + 1.0 - rest;
        for (int k = 1; k <= kOustaloupSections; ++k) {
            const double zExp = (2.0 * k - 1.0 - rest) / denominator;
            const double pExp = (2.0 * k - 1.0 + rest) / denominator;
            add(std::pow(10.0, std::log10(fLow) + zExp * decade),
                std::pow(10.0, std::log10(fLow) + pExp * decade));
        }
    }

    // Normalise at the pivot, then trim any residual overshoot so the audio path really
    // stays inside the +-24 dB law that responseDb() and the FFT mode advertise.
    const double pivotClamped = std::clamp(pivotHz, 1.0, sr * 0.499);
    const double wPivot = 2.0 * kPi * pivotClamped / sr;
    double product = 1.0;
    for (int i = 0; i < d.count; ++i)
        product *= magnitude(d.sections[static_cast<std::size_t>(i)], wPivot);
    d.normalizer = product > 1.0e-12 ? 1.0 / product : 1.0e12;

    const double topHz = std::min(kHighCornerHz, sr * 0.499);
    for (int k = 0; k <= 48; ++k) {
        const double hz = kLowCornerHz * std::pow(topHz / kLowCornerHz, static_cast<double>(k) / 48.0);
        const double w = 2.0 * kPi * hz / sr;
        const double mag = designMagnitude(d, w);
        if (mag > kMaxLinearGain)
            d.normalizer *= (kMaxLinearGain * 0.99) / mag;  // 0.09 dB of headroom between sweep points
    }
    return d;
}

// The UI samples the curve once per plot pixel, all with the same settings. Cache the
// last design so the gain-ceiling sweep runs once per change instead of once per pixel.
[[nodiscard]] Design designCached(const double logPivot, const double exp, const double sr) noexcept
{
    static thread_local double lastLogPivot = -1.0e300;
    static thread_local double lastExp = -1.0e300;
    static thread_local double lastSr = -1.0;
    static thread_local Design last {};
    if (logPivot == lastLogPivot && exp == lastExp && sr == lastSr)
        return last;
    last = design(logPivot, exp, sr);
    lastLogPivot = logPivot;
    lastExp = exp;
    lastSr = sr;
    return last;
}

inline double runSection(IirChannel& ch, const FirstOrder& s, const int index, const double x) noexcept
{
    double& z = ch.z[static_cast<std::size_t>(index)];
    const double y = s.b0 * x + z;
    z = s.b1 * x - s.a1 * y;
    if (std::fabs(z) < kTiny)
        z = 0.0;
    return y;
}

void updateDesign(Engine& e) noexcept
{
    const Design d = design(e.logPivot, e.exponent, e.sampleRate);
    e.sections = d.sections;
    e.sectionCount = d.count;
    e.normalizer = d.normalizer;
}

void clearChannel(FftChannel& c) noexcept
{
    std::fill(c.fifo.begin(), c.fifo.end(), 0.0f);
    std::fill(c.accum.begin(), c.accum.end(), 0.0f);
    c.position = 0;
}

void processFrame(Engine& e, FftChannel& c) noexcept
{
    const int n = kFftSize;
    const float* window = e.window.data();
    float* data = e.scratch.data();
    for (int i = 0; i < n; ++i) {
        data[2 * i] = c.fifo[static_cast<std::size_t>((c.position + i) % n)] * window[static_cast<std::size_t>(i)];
        data[2 * i + 1] = 0.0f;
    }
    e.fft.forward(data);
    const int bins = n / 2;
    for (int k = 1; k < bins; ++k) {
        const float g = e.binGain[static_cast<std::size_t>(k)];
        data[2 * k] *= g;
        data[2 * k + 1] *= g;
    }
    data[1] = 0.0f;
    data[2 * bins] *= e.binGain[static_cast<std::size_t>(bins)];
    data[2 * bins + 1] = 0.0f;
    for (int k = 1; k < bins; ++k) {
        data[2 * (n - k)] = data[2 * k];
        data[2 * (n - k) + 1] = -data[2 * k + 1];
    }
    e.fft.inverse(data);
    for (int i = 0; i < n; ++i)
        c.accum[static_cast<std::size_t>((c.position + i) % n)] += data[2 * i] * window[static_cast<std::size_t>(i)] * e.cola;
}

void refreshBinGains(Engine& e, const bool immediately) noexcept
{
    const double hzPerBin = e.sampleRate / static_cast<double>(kFftSize);
    const float alpha = immediately ? 1.0f
                                    : static_cast<float>(1.0 - std::exp(-static_cast<double>(kFftHop) /
                                                                        (kSmoothSeconds * e.sampleRate)));
    for (int k = 0; k <= kFftSize / 2; ++k) {
        const double hz = static_cast<double>(k) * hzPerBin;
        const float target = static_cast<float>(std::pow(10.0, idealFrom(e.logPivot, e.exponent, hz) / 20.0));
        float& g = e.binGain[static_cast<std::size_t>(k)];
        g = immediately ? target : g + (target - g) * alpha;
    }
}

}  // namespace

void activate(Engine& e, const double sampleRate)
{
    e.sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    if (!e.allocated) {
        e.fft.init(kFftSize);
        e.window.resize(static_cast<std::size_t>(kFftSize));
        e.scratch.assign(static_cast<std::size_t>(2 * kFftSize), 0.0f);
        e.binGain.assign(static_cast<std::size_t>(kFftSize / 2 + 1), 1.0f);
        for (auto& c : e.channels) {
            c.fifo.assign(static_cast<std::size_t>(kFftSize), 0.0f);
            c.accum.assign(static_cast<std::size_t>(kFftSize), 0.0f);
        }
        for (auto& ring : e.iirDelay)
            ring.assign(static_cast<std::size_t>(kFftSize), 0.0f);
        for (auto& ring : e.dryDelay)
            ring.assign(static_cast<std::size_t>(kFftSize), 0.0f);
    }
    for (int i = 0; i < kFftSize; ++i)
        e.window[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * kPi * i / kFftSize)));
    double sum = 0.0;
    for (int m = 0; m < 8; ++m)
        sum += e.window[static_cast<std::size_t>((kFftHop * m) % kFftSize)] *
               e.window[static_cast<std::size_t>((kFftHop * m) % kFftSize)];
    e.cola = static_cast<float>(1.0 / std::max(sum, 1e-9));
    e.allocated = true;
    reset(e);
}

void reset(Engine& e)
{
    if (!e.allocated)
        return;
    for (auto& ch : e.iir)
        ch.z.fill(0.0);
    for (auto& c : e.channels)
        clearChannel(c);
    for (auto& ring : e.iirDelay)
        std::fill(ring.begin(), ring.end(), 0.0f);
    for (auto& ring : e.dryDelay)
        std::fill(ring.begin(), ring.end(), 0.0f);
    e.delayPos = {0, 0};
    e.hopCounter = 0;
    e.primed = false;
}

int latencySamples(Engine& e, const Parameters& p) noexcept
{
    return dspMode(p) == DspMode::Fft && e.allocated ? kFftSize : 0;
}

double responseDb(const Engine& e, const Parameters& p, const double hz) noexcept
{
    if (dspMode(p) == DspMode::Fft || !(hz > 0.0))
        return idealDb(p, hz);
    const double sr = e.sampleRate > 0.0 ? e.sampleRate : 48000.0;
    const Design d = designCached(std::log(std::clamp(p.pivot, kMinPivotHz, kMaxPivotHz)), exponent(p), sr);
    const double w = 2.0 * kPi * std::clamp(hz, 1.0, sr * 0.499) / sr;
    return 20.0 * std::log10(std::max(designMagnitude(d, w), 1e-12));
}

void processBlock(Engine& e, const Parameters& params, const std::uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept
{
    if (!e.allocated)
        return;
    const double targetLogPivot = std::log(std::clamp(params.pivot, kMinPivotHz, kMaxPivotHz));
    const double targetExp = exponent(params);
    const double targetMode = dspMode(params) == DspMode::Fft ? 1.0 : 0.0;
    const double targetBypass = params.bypass >= 0.5 ? 1.0 : 0.0;
    const double targetGain = std::pow(10.0, std::clamp(params.outputDb, kMinOutputDb, kMaxOutputDb) / 20.0);
    if (!e.primed) {
        e.logPivot = targetLogPivot;
        e.exponent = targetExp;
        e.modeMix = targetMode;
        e.bypassMix = targetBypass;
        e.outputGain = targetGain;
        e.primed = true;
        updateDesign(e);
        refreshBinGains(e, true);
    } else if (frames > 0) {
        const double alpha = 1.0 - std::exp(-static_cast<double>(frames) / (kSmoothSeconds * e.sampleRate));
        e.logPivot += (targetLogPivot - e.logPivot) * alpha;
        e.exponent += (targetExp - e.exponent) * alpha;
        e.modeMix += (targetMode - e.modeMix) * alpha;
        e.bypassMix += (targetBypass - e.bypassMix) * alpha;
        e.outputGain += (targetGain - e.outputGain) * alpha;
        updateDesign(e);
    }

    const double align = std::clamp((0.1 - e.modeMix) / 0.1, 0.0, 1.0);
    const double dryDirect = std::clamp((e.bypassMix - 0.9) / 0.1, 0.0, 1.0);
    const float gain = static_cast<float>(e.outputGain);

    for (std::uint32_t s = 0; s < frames; ++s) {
        for (std::size_t c = 0; c < 2; ++c) {
            const int pos = e.delayPos[c];
            const double x = inputs[c][s];
            double y = x;
            for (int i = 0; i < e.sectionCount; ++i)
                y = runSection(e.iir[c], e.sections[static_cast<std::size_t>(i)], i, y);
            y *= e.normalizer;
            const float delayedIir = e.iirDelay[c][static_cast<std::size_t>(pos)];
            e.iirDelay[c][static_cast<std::size_t>(pos)] = static_cast<float>(y);
            const float delayedDry = e.dryDelay[c][static_cast<std::size_t>(pos)];
            e.dryDelay[c][static_cast<std::size_t>(pos)] = static_cast<float>(x);
            e.delayPos[c] = (pos + 1) % kFftSize;

            FftChannel& ch = e.channels[c];
            const int fpos = ch.position;
            const float wet = ch.accum[static_cast<std::size_t>(fpos)];
            ch.fifo[static_cast<std::size_t>(fpos)] = static_cast<float>(x);
            ch.accum[static_cast<std::size_t>(fpos)] = 0.0f;
            ch.position = (fpos + 1) % kFftSize;

            const double iirPart = align * y + (1.0 - align) * static_cast<double>(delayedIir);
            const double processed = (1.0 - e.modeMix) * iirPart + e.modeMix * static_cast<double>(wet);
            const double dryAligned = align * x + (1.0 - align) * static_cast<double>(delayedDry);
            const double dry = dryDirect * x + (1.0 - dryDirect) * dryAligned;
            outputs[c][s] = static_cast<float>(((1.0 - e.bypassMix) * processed + e.bypassMix * dry) * gain);
        }
        if (++e.hopCounter >= kFftHop) {
            e.hopCounter = 0;
            refreshBinGains(e, false);
            for (auto& c : e.channels)
                processFrame(e, c);
        }
    }
}

}  // namespace downspout::tilt
