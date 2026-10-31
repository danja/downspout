#include "downspout/test_assert.h"

#include "interleaver_engine.hpp"
#include "interleaver_params.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <vector>

using namespace downspout::interleaver;

static int gPassed = 0;
static int gFailed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        ++gPassed;
    } else {
        std::printf("FAIL: %s\n", name);
        ++gFailed;
    }
}

constexpr float kSr = 48000.0f;
constexpr float kPi = 3.14159265358979f;

struct Signal {
    std::vector<float> l, r;
    explicit Signal(size_t n = 0) : l(n, 0.0f), r(n, 0.0f) {}
    size_t size() const { return l.size(); }
};

static Signal constant(size_t n, float v)
{
    Signal s(n);
    std::fill(s.l.begin(), s.l.end(), v);
    std::fill(s.r.begin(), s.r.end(), v);
    return s;
}

// A sine centred exactly on FFT bin `bin` (no leakage at the analysis size).
static Signal binSine(size_t n, int bin, float amp)
{
    Signal s(n);
    for (size_t i = 0; i < n; ++i) {
        const float v = amp * std::sin(2.0f * kPi * static_cast<float>(bin) * static_cast<float>(i) / static_cast<float>(kFftSize));
        s.l[i] = s.r[i] = v;
    }
    return s;
}

// Deterministic noise in [-amp, amp].
static Signal noise(size_t n, float amp, unsigned seed)
{
    Signal s(n);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        s.l[i] = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * amp;
        seed = seed * 1664525u + 1013904223u;
        s.r[i] = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * amp;
    }
    return s;
}

// A harmonic-rich tone (fundamental plus four overtones) so the period is unambiguous.
static Signal richTone(size_t n, float hz, float amp)
{
    Signal s(n);
    for (size_t i = 0; i < n; ++i) {
        float v = 0.0f;
        for (int h = 1; h <= 5; ++h)
            v += std::sin(2.0f * kPi * hz * static_cast<float>(h) * static_cast<float>(i) / kSr) / static_cast<float>(h);
        s.l[i] = s.r[i] = amp * v * 0.5f;
    }
    return s;
}

struct Result {
    Signal out;
    Status status;
};

static Result run(const Parameters& p, const Signal& a, const Signal& b, uint32_t block = 256)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(kSr);
    Result r{Signal(a.size()), {}};
    for (size_t pos = 0; pos < a.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, a.size() - pos));
        const float* in[4] = {a.l.data() + pos, a.r.data() + pos, b.l.data() + pos, b.r.data() + pos};
        float* out[2] = {r.out.l.data() + pos, r.out.r.data() + pos};
        engine->process(p, n, in, out);
    }
    r.status = engine->status();
    return r;
}

static float maxDiff(const std::vector<float>& x, const std::vector<float>& y, size_t from, size_t to)
{
    float d = 0.0f;
    for (size_t i = from; i < to; ++i)
        d = std::max(d, std::fabs(x[i] - y[i]));
    return d;
}

// Amplitude of a bin-centred tone in a stretch of signal (Goertzel at the exact bin frequency).
static float toneAmplitude(const std::vector<float>& v, size_t from, size_t count, int bin)
{
    std::complex<double> acc = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double a = -2.0 * 3.14159265358979 * bin * static_cast<double>(from + i) / kFftSize;
        acc += static_cast<double>(v[from + i]) * std::complex<double>(std::cos(a), std::sin(a));
    }
    return static_cast<float>(2.0 * std::abs(acc) / static_cast<double>(count));
}

static Parameters withParam(Parameters p, ParameterIndex i, float v)
{
    field(p, i) = v;
    return p;
}

static void testParameterTable()
{
    Parameters p;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto& s = kParameterSpecs[i];
        check("default within range", s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        check("log range has positive minimum", !s.logarithmic || s.minimum > 0.0f);
    }
    for (std::uint32_t i = 0; i < kFirstOutputParameter; ++i)
        check("settable parameters are not outputs", !kParameterSpecs[i].output);
    for (std::uint32_t i = kFirstOutputParameter; i < kParameterCount; ++i)
        check("status parameters are outputs", kParameterSpecs[i].output);

    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        field(p, i) = 1.0e9f;
    p = clampParameters(p);
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        check("clamp to maximum", field(p, i) == kParameterSpecs[i].maximum);
    field(p, kBalance) = std::nanf("");
    check("NaN falls back to default", field(clampParameters(p), kBalance) == kParameterSpecs[kBalance].defaultValue);
}

static void testMaskFunctions()
{
    for (int shape = 0; shape < kShapeCount; ++shape) {
        for (float balance : {0.2f, 0.5f, 0.75f}) {
            for (float cross : {0.0f, 40.0f, 100.0f}) {
                const float w = edgeWidth(balance, cross, static_cast<Shape>(shape));
                double sum = 0.0;
                float lo = 1.0f, hi = 0.0f;
                const int steps = 20000;
                for (int i = 0; i < steps; ++i) {
                    const float m = timeMask((static_cast<float>(i) + 0.5f) / steps, balance, w, static_cast<Shape>(shape));
                    sum += m;
                    lo = std::min(lo, m);
                    hi = std::max(hi, m);
                }
                // A is balance of the cycle, so B (the mask) averages 1 - balance whatever the edge shape.
                check("mask averages to the B share", std::fabs(sum / steps - (1.0 - balance)) < 2.0e-3);
                check("mask stays in 0..1", lo >= 0.0f && hi <= 1.0f);
            }
        }
    }
    check("hard mask: A then B", timeMask(0.1f, 0.5f, 0.0f, kShapeHard) == 0.0f && timeMask(0.9f, 0.5f, 0.0f, kShapeHard) == 1.0f);
    check("balance 100 is all A", timeMask(0.7f, 1.0f, 0.1f, kShapeCosine) == 0.0f);
    check("balance 0 is all B", timeMask(0.3f, 0.0f, 0.1f, kShapeCosine) == 1.0f);

    // Softened edges are continuous: no step bigger than the slope allows.
    float worst = 0.0f;
    float prev = timeMask(0.0f, 0.5f, edgeWidth(0.5f, 100.0f, kShapeCosine), kShapeCosine);
    for (int i = 1; i <= 10000; ++i) {
        const float m = timeMask(static_cast<float>(i) / 10000.0f, 0.5f, edgeWidth(0.5f, 100.0f, kShapeCosine), kShapeCosine);
        worst = std::max(worst, std::fabs(m - prev));
        prev = m;
    }
    check("cosine edges are continuous", worst < 0.02f);

    check("bin mask alternates at width 1", !spectralPicksB(0, 0.0f, 1, 0.5f) && spectralPicksB(1, 0.0f, 1, 0.5f) && !spectralPicksB(2, 0.0f, 1, 0.5f));
    check("bin mask groups at width 4", !spectralPicksB(3, 0.0f, 4, 0.5f) && spectralPicksB(4, 0.0f, 4, 0.5f) && spectralPicksB(7, 0.0f, 4, 0.5f) && !spectralPicksB(8, 0.0f, 4, 0.5f));
    check("bin mask offset rotates", spectralPicksB(0, 4.0f, 4, 0.5f));
    int countB = 0;
    for (int k = 0; k < 1024; ++k)
        countB += spectralPicksB(k, 0.0f, 4, 0.25f) ? 1 : 0;
    check("bin mask honours the balance", countB == 768);
}

static void testLatency()
{
    Signal impulse(kLatencySamples * 3);
    impulse.l[10] = impulse.r[10] = 1.0f;
    Signal silence(impulse.size());

    Parameters time;
    field(time, kBalance) = 100.0f;
    auto r = run(time, impulse, silence);
    size_t peak = 0;
    for (size_t i = 0; i < r.out.size(); ++i)
        if (std::fabs(r.out.l[i]) > std::fabs(r.out.l[peak]))
            peak = i;
    check("time mode is delayed by exactly the reported latency", peak == 10 + kLatencySamples && std::fabs(r.out.l[peak] - 1.0f) < 1.0e-6f);

    Parameters spectral = withParam(time, kMode, 1.0f);
    // Wait for the 20 ms mode fade, then feed the impulse.
    Signal late(kLatencySamples * 4);
    late.l[3000] = late.r[3000] = 1.0f;
    Signal quiet(late.size());
    r = run(spectral, late, quiet);
    peak = 0;
    for (size_t i = 0; i < r.out.size(); ++i)
        if (std::fabs(r.out.l[i]) > std::fabs(r.out.l[peak]))
            peak = i;
    check("spectral mode has the same latency", peak == 3000 + kLatencySamples && r.out.l[peak] > 0.9f);
}

static void testTimeInterleave()
{
    const size_t n = 48000;
    const Signal a = constant(n, 1.0f), b = constant(n, -1.0f);

    Parameters p;
    field(p, kShape) = kShapeHard;
    field(p, kSlice) = 8.0f;  // cycle 16 ms = 768 samples, 384 of A then 384 of B
    auto r = run(p, a, b);
    size_t positive = 0, flips = 0;
    const size_t from = kLatencySamples + 1000;
    for (size_t i = from; i < n; ++i) {
        positive += r.out.l[i] > 0.0f ? 1 : 0;
        flips += (r.out.l[i] > 0.0f) != (r.out.l[i - 1] > 0.0f) ? 1 : 0;
    }
    const double share = static_cast<double>(positive) / static_cast<double>(n - from);
    check("balance 50 gives equal A and B", std::fabs(share - 0.5) < 0.01);
    const double cycleSamples = 2.0 * static_cast<double>(n - from) / static_cast<double>(flips);
    check("hard cycle length is twice the slice", std::fabs(cycleSamples - 768.0) < 8.0);
    check("status reports the cycle length", std::fabs(r.status.cycleMs - 16.0f) < 0.2f);

    // Balance moves the share.
    r = run(withParam(p, kBalance, 25.0f), a, b);
    positive = 0;
    for (size_t i = from; i < n; ++i)
        positive += r.out.l[i] > 0.0f ? 1 : 0;
    check("balance 25 gives a quarter A", std::fabs(static_cast<double>(positive) / static_cast<double>(n - from) - 0.25) < 0.01);

    // Extremes pass one source untouched.
    const Signal ta = noise(n, 0.5f, 1), tb = noise(n, 0.5f, 2);
    r = run(withParam(p, kBalance, 100.0f), ta, tb);
    check("balance 100 is A exactly", maxDiff(r.out.l, std::vector<float>(n, 0.0f), 0, kLatencySamples) < 1.0e-9f &&
              [&] { for (size_t i = kLatencySamples; i < n; ++i) if (std::fabs(r.out.l[i] - ta.l[i - kLatencySamples]) > 1.0e-6f) return false; return true; }());
    r = run(withParam(p, kBalance, 0.0f), ta, tb);
    check("balance 0 is B exactly", [&] { for (size_t i = kLatencySamples; i < n; ++i) if (std::fabs(r.out.r[i] - tb.r[i - kLatencySamples]) > 1.0e-6f) return false; return true; }());

    // One-sample slices zip the two sources sample by sample.
    Parameters zip = withParam(p, kSlice, 0.02f);  // 0.96 samples; clamps to a 2 sample cycle
    r = run(zip, ta, tb);
    size_t fromA = 0, fromB = 0;
    for (size_t i = kLatencySamples + 100; i < n; ++i) {
        fromA += std::fabs(r.out.l[i] - ta.l[i - kLatencySamples]) < 1.0e-6f ? 1 : 0;
        fromB += std::fabs(r.out.l[i] - tb.l[i - kLatencySamples]) < 1.0e-6f ? 1 : 0;
    }
    const double total = static_cast<double>(n - kLatencySamples - 100);
    check("minimum slice zips A and B sample by sample", fromA / total > 0.45 && fromB / total > 0.45 && (fromA + fromB) / total > 0.98);

    // Stereo split puts the right channel opposite the left.
    r = run(withParam(p, kSplit, 100.0f), a, b);
    size_t opposite = 0;
    for (size_t i = from; i < n; ++i)
        opposite += (r.out.l[i] > 0.0f) != (r.out.r[i] > 0.0f) ? 1 : 0;
    check("split 100 is complementary L/R", static_cast<double>(opposite) / static_cast<double>(n - from) > 0.98);
    r = run(p, a, b);
    opposite = 0;
    for (size_t i = from; i < n; ++i)
        opposite += (r.out.l[i] > 0.0f) != (r.out.r[i] > 0.0f) ? 1 : 0;
    check("split 0 keeps L and R identical", opposite == 0);

    // Raised-cosine edges remove the hard steps.
    auto stepSize = [&](const Parameters& q) {
        auto res = run(q, a, b);
        float worst = 0.0f;
        for (size_t i = from; i < n; ++i)
            worst = std::max(worst, std::fabs(res.out.l[i] - res.out.l[i - 1]));
        return worst;
    };
    check("hard edges jump the full 2.0", stepSize(p) > 1.9f);
    check("cosine crossfade removes the jump", stepSize(withParam(withParam(p, kShape, kShapeCosine), kCrossfade, 100.0f)) < 0.1f);
    check("linear crossfade removes the jump", stepSize(withParam(withParam(p, kShape, kShapeLinear), kCrossfade, 100.0f)) < 0.1f);
}

static void testPitchTracking()
{
    const size_t n = 48000;
    const Signal tone = richTone(n, 200.0f, 0.5f);
    const Signal silence(n);

    Parameters p;
    field(p, kSync) = kSyncTrackA;
    field(p, kShape) = kShapeHard;
    auto r = run(p, tone, silence);
    check("tracks 200 Hz on A", std::fabs(r.status.detectedHz - 200.0f) < 2.0f);
    check("cycle is two periods long", std::fabs(r.status.cycleMs - 10.0f) < 0.2f);
    r = run(withParam(p, kPeriods, 4.0f), tone, silence);
    check("Periods scales the cycle", std::fabs(r.status.cycleMs - 40.0f) < 0.8f);

    r = run(withParam(p, kSync, kSyncTrackB), silence, richTone(n, 330.0f, 0.5f));
    check("tracks 330 Hz on B", std::fabs(r.status.detectedHz - 330.0f) < 4.0f);

    r = run(withParam(p, kSync, kSyncTrackB), tone, silence);
    check("silent tracked source falls back to the free slice", r.status.detectedHz == 0.0f && std::fabs(r.status.cycleMs - 16.0f) < 0.2f);

    r = run(withParam(p, kSync, kSyncFree), tone, silence);
    check("free sync reports no pitch", r.status.detectedHz == 0.0f);

    for (float hz : {80.0f, 120.0f, 440.0f, 800.0f}) {
        r = run(p, richTone(n, hz, 0.5f), silence);
        check("pitch accuracy across the range", std::fabs(r.status.detectedHz - hz) < 0.02f * hz);
    }
    r = run(p, noise(n, 0.5f, 7), silence);
    check("noise is not reported as pitched", r.status.detectedHz == 0.0f);
}

static void testSpectral()
{
    const size_t n = 32768;
    Parameters p;
    field(p, kMode) = 1.0f;
    field(p, kBinWidth) = 4.0f;

    // Balance 100 takes every bin from A: a delayed copy.
    const Signal ta = noise(n, 0.4f, 11), tb = noise(n, 0.4f, 12);
    auto r = run(withParam(p, kBalance, 100.0f), ta, tb);
    float err = 0.0f;
    for (size_t i = 8000; i < n; ++i)
        err = std::max(err, std::fabs(r.out.l[i] - ta.l[i - kLatencySamples]));
    check("spectral balance 100 reconstructs A", err < 1.0e-3f);
    r = run(withParam(p, kBalance, 0.0f), ta, tb);
    err = 0.0f;
    for (size_t i = 8000; i < n; ++i)
        err = std::max(err, std::fabs(r.out.r[i] - tb.r[i - kLatencySamples]));
    check("spectral balance 0 reconstructs B", err < 1.0e-3f);

    // Width 4, balance 50: bins 96-99 are A, 100-103 are B.
    r = run(p, binSine(n, 98, 0.5f), binSine(n, 102, 0.25f));
    check("A tone in an A band passes", std::fabs(toneAmplitude(r.out.l, 20000, 4096, 98) - 0.5f) < 0.03f);
    check("B tone in a B band passes", std::fabs(toneAmplitude(r.out.l, 20000, 4096, 102) - 0.25f) < 0.03f);
    r = run(p, binSine(n, 102, 0.5f), binSine(n, 98, 0.25f));
    check("A tone in a B band is removed", toneAmplitude(r.out.l, 20000, 4096, 102) < 0.03f);
    check("B tone in an A band is removed", toneAmplitude(r.out.l, 20000, 4096, 98) < 0.03f);

    // Drift moves the mask: after rotating, the removed tone comes back.
    // 20 bins/s turns an 8-bin period in 0.4 s, slow enough for a 2048-sample window to see it.
    const size_t longN = 65536;
    Parameters drifting = withParam(p, kDrift, 20.0f);
    r = run(drifting, binSine(longN, 102, 0.5f), binSine(longN, 98, 0.25f));
    float lo = 1.0f, hi = 0.0f;
    for (size_t at = 12000; at + 2048 <= longN; at += 2048) {
        const float amp = toneAmplitude(r.out.l, at, 2048, 102);
        lo = std::min(lo, amp);
        hi = std::max(hi, amp);
    }
    check("drift sweeps the mask across a tone", hi - lo > 0.3f);

    // Mag/Phase: A's magnitude with B's phase keeps A's level where A is loud.
    r = run(withParam(p, kScheme, kSchemeMagPhase), binSine(n, 98, 0.5f), binSine(n, 98, 0.25f));
    check("mag/phase takes magnitude from A in an A band", std::fabs(toneAmplitude(r.out.l, 20000, 4096, 98) - 0.5f) < 0.05f);
    r = run(withParam(p, kScheme, kSchemeMagPhase), binSine(n, 102, 0.5f), binSine(n, 102, 0.25f));
    check("mag/phase takes magnitude from B in a B band", std::fabs(toneAmplitude(r.out.l, 20000, 4096, 102) - 0.25f) < 0.05f);

    // Loudest: the louder source wins each bin, balance biases it.
    r = run(withParam(p, kScheme, kSchemeLoudest), binSine(n, 98, 0.5f), binSine(n, 98, 0.1f));
    check("loudest keeps A when A is louder", std::fabs(toneAmplitude(r.out.l, 20000, 4096, 98) - 0.5f) < 0.04f);
    r = run(withParam(p, kScheme, kSchemeLoudest), binSine(n, 98, 0.1f), binSine(n, 98, 0.5f));
    check("loudest keeps B when B is louder", std::fabs(toneAmplitude(r.out.l, 20000, 4096, 98) - 0.5f) < 0.04f);
    r = run(withParam(withParam(p, kScheme, kSchemeLoudest), kBalance, 0.0f), binSine(n, 98, 0.3f), binSine(n, 98, 0.2f));
    check("loudest balance 0 favours B by 24 dB", std::fabs(toneAmplitude(r.out.l, 20000, 4096, 98) - 0.2f) < 0.04f);

    // Output is finite and bounded for noisy input.
    r = run(p, noise(n, 1.0f, 3), noise(n, 1.0f, 4));
    bool finite = true;
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        finite = finite && std::isfinite(r.out.l[i]) && std::isfinite(r.out.r[i]);
        peak = std::max(peak, std::fabs(r.out.l[i]));
    }
    check("spectral output is finite", finite);
    check("spectral output stays bounded", peak < 3.0f);
}

// Every control has to change what you hear.
static void testEveryControlIsAudible()
{
    const size_t n = 32768;
    const Signal a = richTone(n, 180.0f, 0.4f);
    Signal b = noise(n, 0.3f, 21);
    for (size_t i = 0; i < n; ++i)
        b.l[i] += 0.3f * std::sin(2.0f * kPi * 913.0f * static_cast<float>(i) / kSr);
    b.r = b.l;

    struct Case {
        ParameterIndex index;
        float value;
        bool spectral;
        ParameterIndex extra;
        float extraValue;
    };
    const Case cases[] = {
        {kMode, 1.0f, false, kMode, 0.0f},
        {kBalance, 30.0f, false, kMode, 0.0f},
        {kBalance, 30.0f, true, kMode, 1.0f},
        {kSplit, 80.0f, false, kMode, 0.0f},
        {kSplit, 80.0f, true, kMode, 1.0f},
        {kSlice, 2.0f, false, kMode, 0.0f},
        {kSync, 1.0f, false, kMode, 0.0f},
        {kPeriods, 3.0f, false, kSync, 1.0f},
        {kCrossfade, 90.0f, false, kMode, 0.0f},
        {kShape, 0.0f, false, kMode, 0.0f},
        {kJitter, 60.0f, false, kMode, 0.0f},
        {kScheme, 1.0f, true, kMode, 1.0f},
        {kScheme, 2.0f, true, kMode, 1.0f},
        {kBinWidth, 16.0f, true, kMode, 1.0f},
        {kDrift, 150.0f, true, kMode, 1.0f},
        {kOutput, -12.0f, false, kMode, 0.0f},
    };
    for (const Case& c : cases) {
        Parameters base = withParam(Parameters(), c.extra, c.extraValue);
        const Parameters changed = withParam(base, c.index, c.value);
        const auto x = run(base, a, b);
        const auto y = run(changed, a, b);
        const float d = maxDiff(x.out.l, y.out.l, 12000, n) + maxDiff(x.out.r, y.out.r, 12000, n);
        if (d <= 1.0e-3f)
            std::printf("  inert control: %s\n", kParameterSpecs[c.index].symbol);
        check("control changes the output", d > 1.0e-3f);
    }
}

static void testStabilityAndLifecycle()
{
    const size_t n = 65536;
    const Signal a = noise(n, 1.0f, 31), b = noise(n, 1.0f, 32);
    unsigned seed = 99;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 16777216.0f; };

    auto engine = std::make_unique<Engine>();
    engine->prepare(kSr);
    Signal out(n);
    bool finite = true;
    for (size_t pos = 0; pos + 128 <= n; pos += 128) {
        Parameters p;
        for (std::uint32_t i = 0; i < kFirstOutputParameter; ++i) {
            const auto& s = kParameterSpecs[i];
            field(p, i) = s.minimum + next() * (s.maximum - s.minimum);
        }
        const float* in[4] = {a.l.data() + pos, a.r.data() + pos, b.l.data() + pos, b.r.data() + pos};
        float* o[2] = {out.l.data() + pos, out.r.data() + pos};
        engine->process(p, 128, in, o);
        for (size_t i = pos; i < pos + 128; ++i)
            finite = finite && std::isfinite(out.l[i]) && std::isfinite(out.r[i]) && std::fabs(out.l[i]) < 50.0f;
    }
    check("randomised parameters every block stay finite", finite);

    // Block size must not change the result.
    Parameters p;
    field(p, kJitter) = 40.0f;
    const auto small = run(p, a, b, 64);
    const auto big = run(p, a, b, 1000);
    check("result is independent of block size", maxDiff(small.out.l, big.out.l, 0, n) < 1.0e-6f);

    // Sample rates other than 48 k work (decimation and cycle lengths follow the rate).
    for (float sr : {44100.0f, 96000.0f, 192000.0f}) {
        Engine e;
        e.prepare(sr);
        std::vector<float> ai(8192), bi(8192, 0.0f), oL(8192), oR(8192);
        for (size_t i = 0; i < ai.size(); ++i)
            ai[i] = 0.5f * std::sin(2.0f * kPi * 200.0f * static_cast<float>(i) / sr);
        Parameters q;
        field(q, kSync) = kSyncTrackA;
        const float* in[4] = {ai.data(), ai.data(), bi.data(), bi.data()};
        float* o[2] = {oL.data(), oR.data()};
        for (int rep = 0; rep < 8; ++rep)
            e.process(q, 8192, in, o);
        check("tracks a pitch at other sample rates", std::fabs(e.status().detectedHz - 200.0f) < 6.0f);
    }
}

int main()
{
    testParameterTable();
    testMaskFunctions();
    testLatency();
    testTimeInterleave();
    testPitchTracking();
    testSpectral();
    testEveryControlIsAudible();
    testStabilityAndLifecycle();
    std::printf("interleaver core tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
