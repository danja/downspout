#include "strata_tables.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace downspout::strata {

namespace {

constexpr double kPi = 3.14159265358979323846;
using Cx = std::complex<double>;

// In-place iterative radix-2 FFT (forward when sign = -1, inverse unscaled when +1).
void fft(std::vector<Cx>& a, int sign)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / static_cast<double>(len) * sign;
        const Cx wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Cx w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const Cx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// A frame as sine-series coefficients: x(theta) = sum_h s[h] sin(h theta) + c[h] cos(h theta).
struct Series {
    std::array<double, kMaxHarmonics + 1> s {};
    std::array<double, kMaxHarmonics + 1> c {};
};

Series fromTimeDomain(const std::vector<double>& x)
{
    std::vector<Cx> a(x.size());
    for (size_t i = 0; i < x.size(); ++i) a[i] = x[i];
    fft(a, -1);
    Series r;
    const double n = static_cast<double>(x.size());
    for (int h = 1; h < kMaxHarmonics; ++h) {
        r.c[static_cast<size_t>(h)] = 2.0 * a[static_cast<size_t>(h)].real() / n;
        r.s[static_cast<size_t>(h)] = -2.0 * a[static_cast<size_t>(h)].imag() / n;
    }
    return r;
}

Series basicSeries(double morph)
{
    // sine -> triangle -> saw -> square, each as its sine series.
    auto sine = [](int h) { return h == 1 ? 1.0 : 0.0; };
    auto tri = [](int h) { return (h % 2 == 1) ? (8.0 / (kPi * kPi)) * ((((h - 1) / 2) % 2 == 0) ? 1.0 : -1.0) / (static_cast<double>(h) * h) : 0.0; };
    auto saw = [](int h) { return (2.0 / kPi) * ((h % 2 == 1) ? 1.0 : -1.0) / h; };
    auto sq = [](int h) { return (h % 2 == 1) ? (4.0 / kPi) / h : 0.0; };
    Series r;
    for (int h = 1; h < kMaxHarmonics; ++h) {
        double v;
        if (morph < 1.0 / 3.0) { const double t = morph * 3.0; v = (1 - t) * sine(h) + t * tri(h); }
        else if (morph < 2.0 / 3.0) { const double t = (morph - 1.0 / 3.0) * 3.0; v = (1 - t) * tri(h) + t * saw(h); }
        else { const double t = (morph - 2.0 / 3.0) * 3.0; v = (1 - t) * saw(h) + t * sq(h); }
        r.s[static_cast<size_t>(h)] = v;
    }
    return r;
}

Series harmonicsSeries(double morph)
{
    Series r;
    for (int h = 1; h <= 16; ++h) {
        const double w = (h % 2 == 0) ? morph / h : (1.0 - morph * 0.5) / std::pow(static_cast<double>(h), 1.0 + morph);
        r.s[static_cast<size_t>(h)] = 0.7 * w;
    }
    return r;
}

Series formantSeries(double morph)
{
    // Two formant peaks on a harmonic-number axis sweep from A towards U; only harmonics under about 40.
    const double f1 = 2.0 + morph * 8.0, f2 = 6.0 + (1.0 - morph) * 12.0;
    Series r;
    for (int h = 1; h <= 48; ++h) {
        const double a1 = std::exp(-(h - f1) * (h - f1) / (2.0 * 1.6 * 1.6));
        const double a2 = 0.6 * std::exp(-(h - f2) * (h - f2) / (2.0 * 2.4 * 2.4));
        r.s[static_cast<size_t>(h)] = (a1 + a2) / h;
    }
    return r;
}

Series digitalSeries(double morph)
{
    // FM and fold: sin(sin(theta + I sin(2 theta)) * (1 + 2 morph)), analysed from a smooth waveform.
    std::vector<double> x(kTableSize);
    const double idx = 1.0 + morph * 6.0;
    for (int i = 0; i < kTableSize; ++i) {
        const double th = 2.0 * kPi * i / kTableSize;
        const double carrier = std::sin(th + idx * std::sin(2.0 * th));
        x[static_cast<size_t>(i)] = std::sin(carrier * (1.0 + morph * 2.0));
    }
    return fromTimeDomain(x);
}

Series vintageSeries(double morph)
{
    // A saturated saw built additively (no hard edge), plus sub-octave warmth.
    std::vector<double> x(kTableSize);
    const double drive = 1.0 + morph * 2.5;
    for (int i = 0; i < kTableSize; ++i) {
        const double th = 2.0 * kPi * i / kTableSize;
        double saw = 0.0;
        for (int h = 1; h <= 80; ++h) saw += ((h % 2 == 1) ? 1.0 : -1.0) * std::sin(h * th) / h * (2.0 / kPi);
        x[static_cast<size_t>(i)] = std::tanh(saw * drive) * 0.85;
    }
    return fromTimeDomain(x);
}

Series makeSeries(int category, double morph)
{
    switch (static_cast<Category>(category)) {
    case Category::Basic:     return basicSeries(morph);
    case Category::Harmonics: return harmonicsSeries(morph);
    case Category::Formant:   return formantSeries(morph);
    case Category::Digital:   return digitalSeries(morph);
    default:                  return vintageSeries(morph);
    }
}

// Inverse transform of the series truncated to `limit` harmonics.
std::vector<double> synthesize(const Series& s, int limit)
{
    std::vector<Cx> a(kTableSize, Cx(0.0, 0.0));
    for (int h = 1; h <= std::min(limit, kMaxHarmonics - 1); ++h) {
        // x(theta) = sum s sin + c cos  ->  X[h] = (c - j s) N / 2
        const Cx v(s.c[static_cast<size_t>(h)] * kTableSize / 2.0, -s.s[static_cast<size_t>(h)] * kTableSize / 2.0);
        a[static_cast<size_t>(h)] = v;
        a[static_cast<size_t>(kTableSize - h)] = std::conj(v);
    }
    fft(a, +1);
    std::vector<double> x(kTableSize);
    for (int i = 0; i < kTableSize; ++i) x[static_cast<size_t>(i)] = a[static_cast<size_t>(i)].real() / kTableSize;
    return x;
}

}  // namespace

int Bank::harmonicLimit(int mip) noexcept
{
    return std::max(1, static_cast<int>(std::floor(kMaxHarmonics * std::pow(2.0, -0.5 * mip) + 1e-9)));
}

int Bank::mipForHarmonics(double allowed) noexcept
{
    for (int m = 0; m < kMips; ++m)
        if (harmonicLimit(m) <= allowed) return m;
    return kMips - 1;
}

Bank::Bank()
{
    data_.resize(static_cast<size_t>(kCategories * kFrames * kMips));
    for (int cat = 0; cat < kCategories; ++cat) {
        for (int f = 0; f < kFrames; ++f) {
            const Series series = makeSeries(cat, static_cast<double>(f) / (kFrames - 1));
            // Normalise every frame to the same peak at full bandwidth, so the level does not move with
            // the position; the same factor is applied to every mip of the frame.
            const std::vector<double> full = synthesize(series, kMaxHarmonics - 1);
            double peak = 1e-9;
            for (double v : full) peak = std::max(peak, std::fabs(v));
            const double scale = 0.8 / peak;
            for (int m = 0; m < kMips; ++m) {
                const std::vector<double> x = m == 0 ? full : synthesize(series, harmonicLimit(m));
                std::vector<float>& d = data_[static_cast<size_t>(((cat * kFrames) + f) * kMips + m)];
                d.assign(static_cast<size_t>(kTableSize + kPad), 0.0f);
                for (int i = 0; i < kTableSize + kPad; ++i)
                    d[static_cast<size_t>(i)] = static_cast<float>(x[static_cast<size_t>(((i - 1) % kTableSize + kTableSize) % kTableSize)] * scale);
            }
        }
    }
}

float Bank::read(int category, double position, int mip, double phase) const noexcept
{
    const double pos = std::min(std::max(position, 0.0), 1.0) * (kFrames - 1);
    const int f0 = std::min(static_cast<int>(pos), kFrames - 2);
    const float ff = static_cast<float>(pos - f0);
    double p = phase - std::floor(phase);
    const double x = p * kTableSize;
    const int i = static_cast<int>(x);
    const float t = static_cast<float>(x - i);
    auto cubic = [&](const float* d) {
        const float y0 = d[i], y1 = d[i + 1], y2 = d[i + 2], y3 = d[i + 3];  // guard offset: sample i is at d[i + 1]
        const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    };
    const float a = cubic(frame(category, f0, mip));
    const float b = cubic(frame(category, f0 + 1, mip));
    return a + ff * (b - a);
}

std::vector<double> Bank::spectrum(int category, int frameIndex, int mip) const
{
    const float* d = frame(category, frameIndex, mip);
    std::vector<Cx> a(kTableSize);
    for (int i = 0; i < kTableSize; ++i) a[static_cast<size_t>(i)] = d[i + 1];
    fft(a, -1);
    std::vector<double> r(kMaxHarmonics);
    for (int h = 1; h < kMaxHarmonics; ++h) r[static_cast<size_t>(h)] = 2.0 * std::abs(a[static_cast<size_t>(h)]) / kTableSize;
    return r;
}

const Bank& bank()
{
    static const Bank b;
    return b;
}

}  // namespace downspout::strata
