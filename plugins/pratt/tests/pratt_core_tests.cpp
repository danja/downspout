#include "downspout/test_assert.h"
#include "pratt_poly.hpp"
#include "pratt_wavetable.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace downspout::pratt;

namespace {

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

void testPolynomials() {
    assert((polynomial(1) == std::vector<std::int64_t>{1}));
    assert((polynomial(2) == std::vector<std::int64_t>{0, 1}));
    assert((polynomial(3) == std::vector<std::int64_t>{1, 1}));
    assert((polynomial(5) == std::vector<std::int64_t>{1, 0, 1}));
    assert(polynomial(35) == ([] {
        const auto& a = polynomial(5);
        const auto& b = polynomial(7);
        std::vector<std::int64_t> c(a.size() + b.size() - 1, 0);
        for (std::size_t i = 0; i < a.size(); ++i)
            for (std::size_t j = 0; j < b.size(); ++j) c[i + j] += a[i] * b[j];
        return c;
    })());

    int maxDegree = 0;
    std::int64_t maxCoef = 0;
    for (int n = 1; n <= kMaxIndex; ++n) {
        const auto& c = polynomial(n);
        std::int64_t at2 = 0;
        for (std::size_t i = c.size(); i-- > 0;) at2 = at2 * 2 + c[i];
        assert(at2 == n);  // f_n(2) = n, exact
        maxDegree = std::max(maxDegree, degree(n));
        for (auto v : c) maxCoef = std::max(maxCoef, v);
    }
    assert(maxDegree <= 13 && maxCoef <= 70);  // int64 headroom claim in the header

    bool threw = false;
    try { polynomial(0); } catch (const std::out_of_range&) { threw = true; }
    assert(threw);
    threw = false;
    try { polynomial(kMaxIndex + 1); } catch (const std::out_of_range&) { threw = true; }
    assert(threw);
}

void testResponseMatchesPython() {
    struct Case { int n; double x, re, im; };
    // Values from pratt_midi_synth.py `response()`.
    const Case cases[] = {
        {35, 0.7, 0.44469739942008013, -0.7732829917267897},
        {97, 1.3, -0.3813435066533337, 0.05754950206172991},
        {4736, 0.15, 0.7010016828085618, -0.6757049235852264},
        {360, 2.5, 0.06228338335049234, 0.03487075555732143},
    };
    for (const auto& c : cases) {
        const Complex r = response(c.n, c.x);
        assert(near(r.real(), c.re, 1e-12) && near(r.imag(), c.im, 1e-12));
    }
    assert(near(std::abs(response(77, 0.0) - 1.0), 0.0, 1e-15));  // H(0) = 1
}

void testCascadeIdentity() {
    for (double x = 0.01; x < 20.0; x += 0.0071) {
        assert(std::abs(response(35, x) - response(5, x) * response(7, x)) < 1e-12);
        assert(std::abs(response(360, x) - response(8, x) * response(9, x) * response(5, x)) < 1e-12);
    }
}

void testRootsAndStability() {
    // Every index the synth can produce (pitch+1 in 1..128, base 1..64) plus a
    // sweep of the rest: roots are zeros of f_n and poles are stable.
    double worstResidual = 0.0, worstRe = -1e9;
    // Every index, not a sample: the worst root is at n = 6173, which a stride of 7 skips.
    for (int n = 2; n <= kMaxIndex; ++n) {
        const auto& r = roots(n);
        assert(static_cast<int>(r.size()) == degree(n));
        for (const Complex& t : r) {
            worstResidual = std::max(worstResidual, std::abs(evaluate(n, t)) / static_cast<double>(n));
            worstRe = std::max(worstRe, t.real() - 2.0);
        }
    }
    std::printf("roots: worst |f_n(t)|/n = %.3g, max Re(t-2) = %.4f\n", worstResidual, worstRe);
    assert(worstResidual < 1e-6);
    assert(worstRe < -0.99);  // measured worst case over all n <= 8192 is -0.9977 (n = 6173)
}

void testSections() {
    const double fs = 48000.0, f0 = 220.0, w0 = 2.0 * M_PI * f0;
    for (int n : {3, 12, 35, 97, 360, 4096, 4736}) {
        const auto sec = makeSections(n, w0, fs);
        assert(!sec.empty());
        // Unity DC gain and agreement with the analog H_n at low frequency.
        assert(near(std::abs(sectionsResponse(sec, 0.01, fs)), 1.0, 1e-6));
        for (double hz : {20.0, 60.0, 110.0}) {
            const Complex analog = response(n, hz / f0);  // s/w0 = i * f/f0
            const Complex digital = sectionsResponse(sec, hz, fs);
            assert(std::abs(analog - digital) < 0.02 * std::max(1.0, std::abs(analog)) + 0.01);
        }
        // Stability: poles of every section inside the unit circle.
        for (const Biquad& b : sec) {
            assert(std::abs(b.a2) < 1.0 && std::abs(b.a1) < 1.0 + b.a2);
        }
    }
    // Filtering an impulse must decay (time-domain stability smoke test).
    const auto sec = makeSections(4096, w0, fs);
    std::vector<BiquadState> st(sec.size());
    float peakTail = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        float x = i == 0 ? 1.0f : 0.0f;
        for (std::size_t s = 0; s < sec.size(); ++s) x = processSection(sec[s], st[s], x);
        assert(std::isfinite(x));
        if (i > 40000) peakTail = std::max(peakTail, std::abs(x));
    }
    assert(peakTail < 1e-6f);
}

double spectralPeakHz(const std::vector<float>& table, double f0, double fs) {
    const int N = 1 << 15;
    // Probe the table with a single-frequency read and look for the strongest DFT bin
    // by scanning a narrow band around each multiple of f0 is overkill; just
    // check the fundamental dominates a read at exactly f0.
    std::vector<double> x(N);
    for (int i = 0; i < N; ++i) x[i] = readWavetable(table, f0 * i / fs);
    double best = 0.0, bestHz = 0.0;
    for (double hz = f0 - 5.0; hz <= f0 * 1.0 + 5.0; hz += 0.05) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < N; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / N);
            re += w * x[i] * std::cos(2.0 * M_PI * hz * i / fs);
            im -= w * x[i] * std::sin(2.0 * M_PI * hz * i / fs);
        }
        const double m = re * re + im * im;
        if (m > best) { best = m; bestHz = hz; }
    }
    return bestHz;
}

void testWavetable() {
    // A4, piano base 5, velocity bucket 4 -> v = (4+0.5)/8. Golden from the Python.
    TableParams a4;
    a4.pitch = 69; a4.base = 5; a4.roll = 0.92; a4.xi = 0.15; a4.velocity = 4.5 / 8.0;
    const Wavetable t = buildWavetable(a4);
    assert(t.index == 350 && t.harmonics == 42);
    assert(t.samples.size() == 4097 && t.samples[4096] == t.samples[0]);
    assert(near(t.samples[100], -0.9993669453711741, 1e-5));
    assert(near(t.samples[1000], 0.6656939113651363, 1e-5));
    assert(near(t.samples[2500], -0.08133961386953284, 1e-5));
    float peak = 0.0f;
    for (float v : t.samples) peak = std::max(peak, std::abs(v));
    assert(near(peak, 1.0, 1e-6));
    assert(near(spectralPeakHz(t.samples, 440.0, 44100.0), 440.0, 1.0));

    // Dark bass variant, E2: exercises the extraRoll path and the 56-harmonic cap.
    TableParams e2;
    e2.pitch = 40; e2.base = 7; e2.roll = 1.02; e2.xi = 0.16; e2.velocity = 2.5 / 8.0; e2.extraRoll = 0.75;
    const Wavetable d = buildWavetable(e2);
    assert(d.index == 287 && d.harmonics == 56);
    assert(near(d.samples[100], -0.7690688985027125, 1e-5));
    assert(near(d.samples[1000], 0.9978657525436063, 1e-5));
    assert(near(d.samples[2500], -0.1084191360248469, 1e-5));

    // Band limit: highest partial stays under 0.42 * fs at the top of the keyboard.
    TableParams hi;
    hi.pitch = 127;
    const Wavetable h = buildWavetable(hi);
    assert(h.harmonics >= 1 && h.harmonics * pitchToHz(127) < 44100.0 * 0.42 + 1e-9);

    // Wrap-around reads are periodic.
    assert(near(readWavetable(t.samples, 0.25), readWavetable(t.samples, 3.25), 1e-6));
    assert(near(readWavetable(t.samples, -0.75), readWavetable(t.samples, 0.25), 1e-6));
}

// The timbre chain is H_timbre cascaded onto the note's own filter (H_mn = H_m H_n).
void testTimbreChain() {
    // At pitch 63 (note+1 = 64): base 1 with timbre 3 is the same filter as base 3 with
    // no timbre (index 192 either way), and the same pitch gives the same harmonics.
    TableParams chained, direct, plain;
    chained.pitch = direct.pitch = plain.pitch = 63;
    chained.base = 1; chained.timbre = 3;
    direct.base = 3;
    plain.base = 1;
    const Wavetable c = buildWavetable(chained), d = buildWavetable(direct), p = buildWavetable(plain);
    assert(c.index == 64 && c.timbre == 3 && d.index == 192 && p.timbre == 1);
    assert(c.harmonics == d.harmonics);
    double worst = 0.0, differs = 0.0;
    for (std::size_t i = 0; i < c.samples.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(c.samples[i] - d.samples[i])));
        differs = std::max(differs, static_cast<double>(std::abs(c.samples[i] - p.samples[i])));
    }
    assert(worst < 1e-5);
    assert(differs > 0.01);

    // A chain beyond what a single product index could reach still builds: base 64 on
    // the top key is already n = 8192, and the chain multiplies another filter in.
    TableParams top;
    top.pitch = 127; top.base = 64; top.timbre = 8192;
    const Wavetable t = buildWavetable(top);
    assert(t.index == kMaxIndex && t.timbre == kMaxIndex);
    for (float v : t.samples) assert(std::isfinite(v) && std::abs(v) <= 1.0f + 1e-6f);

    // A deeper chain darkens: less energy in the upper partials of a fixed note.
    const auto upperShare = [](const Wavetable& w) {
        // Crude spectral centroid from the first differences.
        double lo = 0.0, hi = 0.0;
        for (std::size_t i = 0; i + 1 < w.samples.size(); ++i) {
            lo += static_cast<double>(w.samples[i]) * w.samples[i];
            const double dv = w.samples[i + 1] - w.samples[i];
            hi += dv * dv;
        }
        return hi / lo;
    };
    TableParams a, b;
    a.pitch = b.pitch = 48;
    b.timbre = 1024;
    assert(upperShare(buildWavetable(b)) < upperShare(buildWavetable(a)));
}

}  // namespace

int main() {
    testPolynomials();
    testResponseMatchesPython();
    testCascadeIdentity();
    testRootsAndStability();
    testSections();
    testWavetable();
    testTimbreChain();
    std::puts("pratt core tests passed");
    return 0;
}
