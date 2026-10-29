#include "spense_core.hpp"
#include "spense_params.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::spense;

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

struct Stereo {
    std::vector<float> l, r;
};

// Runs a stereo signal through a fresh engine in blocks of `block` frames.
static Stereo run(const Parameters& p, const std::vector<float>& inL, const std::vector<float>& inR, float sr = 48000.0f,
                  uint32_t block = 128)
{
    EngineState st;
    Stereo out{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
    for (size_t pos = 0; pos < inL.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, inL.size() - pos));
        const float* ins[2] = {inL.data() + pos, inR.data() + pos};
        float* outs[2]      = {out.l.data() + pos, out.r.data() + pos};
        processBlock(st, p, sr, n, ins, outs);
    }
    return out;
}

static Stereo run(const Parameters& p, const std::vector<float>& in, float sr = 48000.0f, uint32_t block = 128)
{
    return run(p, in, in, sr, block);
}

static std::vector<float> sine(double hz, double amp, float sr, double seconds)
{
    std::vector<float> v(static_cast<size_t>(sr * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * hz * i / sr));
    return v;
}

static std::vector<float> impulse(size_t n)
{
    std::vector<float> v(n, 0.0f);
    v[0] = 1.0f;
    return v;
}

// Amplitude gain and phase of a sine, by correlation over the second half of the run.
struct Tone {
    double gain, phase;
};

static Tone measure(const Parameters& p, double hz, int channel, float sr = 48000.0f, double seconds = 1.5)
{
    const auto in = sine(hz, 0.25, sr, seconds);
    const Stereo out = run(p, in, sr);
    const auto& y = channel == 0 ? out.l : out.r;
    double sc = 0, ss = 0;
    size_t n = 0;
    for (size_t i = y.size() / 2; i < y.size(); ++i) {
        const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / sr;
        sc += y[i] * std::cos(ph);
        ss += y[i] * std::sin(ph);
        ++n;
    }
    return {2.0 * std::hypot(sc, ss) / static_cast<double>(n) / 0.25, std::atan2(sc, ss)};
}

static double wrap(double x)
{
    return std::remainder(x, 2.0 * M_PI);
}

static double maxDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a[i] - b[i])));
    return worst;
}

static Parameters withStack(float stack)
{
    Parameters p;
    p.stack = stack;
    return p;
}

// ---- The source algorithm, written out literally, as the independent reference -----------------

static float refExp2(float x)
{
    const float i = std::floor(x);
    const float f = x - i;
    const float r = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013333f))));
    int32_t v = std::bit_cast<int32_t>(r);
    v += static_cast<int32_t>(i) << 23;
    return std::bit_cast<float>(v);
}

// One Dispenser CascadeAPF over a buffer with constant, already-settled parameters.
static void refCascade(std::vector<float>& buf, const std::array<float, 8>& table, float freq, float res, float drift, float sr)
{
    std::array<float, 8> s1{}, s2{}, hist{};
    const float maxF = sr * 0.49f;
    const float scale = 3.14159265358979f * static_cast<float>(1.0 / sr);
    for (float& sample : buf) {
        const float k = 1.0f / std::max(res, 0.001f);
        std::array<float, 8> a1, a2, a3;
        for (size_t i = 0; i < 8; ++i) {
            const float detune = refExp2(drift * table[i] * 6.37f);
            const float f = std::clamp(freq * detune, 20.0f, maxF);
            const float x = f * scale;
            const float g = x * (1.0f + (x * x) * 0.333333f);
            const float inv = 1.0f / (1.0f + g * (g + k));
            a1[i] = inv;
            a2[i] = g * inv;
            a3[i] = g * g * inv;
        }
        std::array<float, 8> in{};
        in[0] = sample;
        for (size_t i = 1; i < 8; ++i) in[i] = hist[i - 1];
        for (size_t i = 0; i < 8; ++i) {
            const float v3 = in[i] - s2[i];
            const float v1 = a1[i] * s1[i] + a2[i] * v3;
            const float v2 = s2[i] + a2[i] * s1[i] + a3[i] * v3;
            s1[i] = 2.0f * v1 - s1[i];
            s2[i] = 2.0f * v2 - s2[i];
            hist[i] = in[i] - 2.0f * k * v1;
        }
        sample = hist[7];
    }
}

static void testMatchesTheSourceAlgorithm()
{
    const std::array<float, 8> left  = {-0.12f, -0.08f, 0.11f, -0.03f, 0.15f, -0.07f, 0.02f, -0.07f};
    const std::array<float, 8> right = {-0.12f, 0.05f, -0.08f, 0.15f, -0.03f, 0.11f, -0.07f, 0.02f};
    for (float sr : {44100.0f, 48000.0f, 96000.0f}) {
        for (int stack : {1, 3, 8}) {
            Parameters p;
            p.stack = static_cast<float>(stack);
            p.frequencyHz = 700.0f;
            p.resonance = 1.8f;
            p.drift = 0.55f;
            // A noise-like input that has energy everywhere.
            std::vector<float> in(4000);
            unsigned seed = 12345;
            for (float& v : in) {
                seed = seed * 1664525u + 1013904223u;
                v = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * 0.5f;
            }
            std::vector<float> refL = in, refR = in;
            for (int c = 0; c < stack; ++c) {
                refCascade(refL, left, p.frequencyHz, p.resonance, p.drift, sr);
                refCascade(refR, right, p.frequencyHz, p.resonance, p.drift, sr);
            }
            const Stereo got = run(p, in, sr);
            const double worst = std::max(maxDifference(got.l, refL), maxDifference(got.r, refR));
            if (worst > 1e-4) std::printf("  source mismatch %.3g at sr %.0f stack %d\n", worst, sr, stack);
            check("output equals the source's per-cascade algorithm", worst < 1e-4);
        }
    }
}

// ---- All-pass behaviour -------------------------------------------------------------------------

static void testMagnitudeIsFlat()
{
    for (float stack : {1.0f, 4.0f, 8.0f}) {
        for (float q : {0.3f, 0.707f, 3.0f}) {
            Parameters p = withStack(stack);
            p.resonance = q;
            p.frequencyHz = 800.0f;
            double worst = 0;
            for (double hz : {60.0, 300.0, 800.0, 1500.0, 6000.0}) worst = std::max(worst, std::fabs(measure(p, hz, 0).gain - 1.0));
            if (worst > 0.01) std::printf("  gain error %.4f at stack %.0f Q %.2f\n", worst, stack, q);
            check("all-pass: unity gain at every frequency", worst < 0.01);
        }
    }
}

static void testPhaseMatchesModel()
{
    for (float drift : {0.0f, 0.6f}) {
        for (float q : {0.707f, 2.5f}) {
            Parameters p = withStack(3.0f);
            p.drift = drift;
            p.resonance = q;
            for (int ch = 0; ch < 2; ++ch) {
                double worst = 0;
                for (double hz : {120.0, 450.0, 900.0, 1400.0, 3000.0, 7000.0})
                    worst = std::max(worst, std::fabs(wrap(measure(p, hz, ch).phase - phaseRadians(p, hz, ch, 48000.0))));
                if (worst > 0.04) std::printf("  phase error %.4f rad (drift %.1f Q %.2f ch %d)\n", worst, drift, q, ch);
                check("measured phase matches the model, seven-sample delays included", worst < 0.04);
            }
        }
    }
}

// The plotted curve is the group delay; hold it against the delay the filter actually has.
static void testGroupDelayMatchesMeasured()
{
    for (float stack : {1.0f, 5.0f}) {
        for (float q : {0.707f, 2.0f}) {
            Parameters p = withStack(stack);
            p.resonance = q;
            p.drift = 0.3f;
            for (double hz : {250.0, 700.0, 1000.0, 1600.0, 4000.0}) {
                const double d = 4.0;
                const double dPhase = wrap(measure(p, hz + d, 0).phase - measure(p, hz - d, 0).phase);
                const double measured = -dPhase / (2.0 * M_PI * 2.0 * d / 48000.0);
                const double expected = groupDelaySamples(p, hz, 0, 48000.0) + latencySamples(p);
                const double tolerance = 0.04 * expected + 0.6;
                if (std::fabs(measured - expected) > tolerance)
                    std::printf("  group delay %.2f vs model %.2f samples at %.0f Hz (stack %.0f Q %.2f)\n", measured, expected, hz, stack, q);
                check("plotted group delay matches the measured filter", std::fabs(measured - expected) < tolerance);
            }
        }
    }
}

static void testDispersionGrowsWithStackAndPeaksAtFrequency()
{
    Parameters one = withStack(1.0f), four = withStack(4.0f);
    check("stack scales the dispersion", std::fabs(groupDelaySamples(four, 1000.0, 0, 48000.0) / groupDelaySamples(one, 1000.0, 0, 48000.0) - 4.0) < 1e-9);
    const auto peakHz = [](const Parameters& q) {
        double best = 0, bestHz = 0;
        for (double hz = 100.0; hz < 8000.0; hz *= 1.002) {
            const double d = groupDelaySamples(q, hz, 0, 48000.0);
            if (d > best) { best = d; bestHz = hz; }
        }
        return bestHz;
    };
    // At Q = 0.707 the delay of a 2nd-order all-pass is (1 + v^2) / (1 + v^4) in v = f / f0, which
    // peaks where 1 - 2 v^2 - v^4 = 0, i.e. at f0 sqrt(sqrt(2) - 1) = 0.644 f0; the peak moves up
    // to f0 as Q rises.
    check("at Q 0.707 the delay peaks below the Frequency control, at 0.644 of it", std::fabs(peakHz(one) / 644.0 - 1.0) < 0.03);
    Parameters sharp = one;
    sharp.resonance = 5.0f;
    check("at higher Q the delay peaks at the Frequency control", std::fabs(peakHz(sharp) / 1000.0 - 1.0) < 0.03);
    Parameters highQ = one;
    highQ.resonance = 6.0f;
    check("higher Q gives a longer, narrower smear", groupDelaySamples(highQ, 1000.0, 0, 48000.0) > 4.0 * groupDelaySamples(one, 1000.0, 0, 48000.0));
}

static void testLatencyIsSevenSamplesPerCascade()
{
    for (int stack = 1; stack <= 8; ++stack) check("latency is 7 samples per cascade", latencySamples(withStack(static_cast<float>(stack))) == 7 * stack);
    check("latency follows the rounded stack", latencySamples(withStack(2.6f)) == 21 && latencySamples(withStack(0.0f)) == 7 && latencySamples(withStack(99.0f)) == 56);
}

// ---- Controls -------------------------------------------------------------------------------------

static void testEveryControlMatters()
{
    // Broadband input, so every control has something to act on.
    std::vector<float> in(6000);
    unsigned seed = 7;
    for (float& v : in) {
        seed = seed * 1664525u + 1013904223u;
        v = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * 0.4f;
    }
    Parameters base;
    base.stack = 2.0f;
    base.drift = 0.4f;
    const Stereo ref = run(base, in);
    auto differs = [&](Parameters q) {
        const Stereo o = run(q, in);
        return std::max(maxDifference(o.l, ref.l), maxDifference(o.r, ref.r)) > 1e-3;
    };
    Parameters q = base; q.stack = 5.0f;        check("stack has an effect", differs(q));
    q = base; q.frequencyHz = 3000.0f;           check("frequency has an effect", differs(q));
    q = base; q.resonance = 4.0f;                check("resonance has an effect", differs(q));
    q = base; q.drift = 0.0f;                    check("drift has an effect", differs(q));
    q = base; q.preGainDb = -9.0f;               check("pre gain has an effect", differs(q));
    q = base; q.postGainDb = -9.0f;              check("post gain has an effect", differs(q));
    q = base; q.preGainDb = 12.0f; q.postGainDb = 12.0f; q.clip = 1.0f;
    const Stereo clipped = run(q, in);
    q.clip = 0.0f;
    check("clip has an effect once the level passes 0 dB", std::max(maxDifference(run(q, in).l, clipped.l), 0.0) > 1e-3);
}

static void testDriftDefaultIsInertAndSpreadsTheImage()
{
    check("drift defaults to zero", Parameters{}.drift == 0.0f);
    const auto in = sine(900.0, 0.3, 48000.0f, 0.2);
    Parameters p = withStack(2.0f);
    const Stereo flat = run(p, in);
    check("with no drift the channels are identical", maxDifference(flat.l, flat.r) < 1e-6);
    p.drift = 0.8f;
    const Stereo drifted = run(p, in);
    check("drift makes left and right differ", maxDifference(drifted.l, drifted.r) > 0.01);
    check("drift moves the stage frequencies", std::fabs(stageFrequencyHz(p, 4, 0, 48000.0f) - 1000.0f) > 100.0f && stageFrequencyHz(Parameters{}, 4, 0, 48000.0f) == 1000.0f);
}

static void testGainStagesAndClip()
{
    const auto in = sine(440.0, 0.5, 48000.0f, 0.2);
    Parameters p;
    p.preGainDb = -100.0f;
    const Stereo silent = run(p, in);
    check("pre gain at the bottom is silence", maxDifference(silent.l, std::vector<float>(in.size(), 0.0f)) == 0.0);
    p = Parameters{};
    p.postGainDb = -100.0f;
    const Stereo silentPost = run(p, in);
    check("post gain at the bottom is silence", maxDifference(silentPost.r, std::vector<float>(in.size(), 0.0f)) == 0.0);
    check("-100 dB is zero and 0 dB is one", gainFromDb(-100.0f) == 0.0f && std::fabs(gainFromDb(0.0f) - 1.0f) < 1e-6f && std::fabs(gainFromDb(12.0f) - 3.981f) < 0.01f);

    p = Parameters{};
    p.postGainDb = 12.0f;
    p.clip = 1.0f;
    float peak = 0.0f;
    for (float v : run(p, sine(500.0, 0.9, 48000.0f, 0.3)).l) peak = std::max(peak, std::fabs(v));
    check("clip holds the output at 0 dBFS", peak <= 1.0f && peak > 0.99f);
    p.clip = 0.0f;
    peak = 0.0f;
    for (float v : run(p, sine(500.0, 0.9, 48000.0f, 0.3)).l) peak = std::max(peak, std::fabs(v));
    check("without clip the output passes 0 dBFS", peak > 2.0f);
    // The two gains are in series with the cascade: 6 dB in and 6 dB out is 12 dB in total.
    Parameters a, b;
    a.preGainDb = 6.0f; a.postGainDb = 6.0f;
    b.preGainDb = 12.0f;
    check("pre and post gain are both linear gains", maxDifference(run(a, in).l, run(b, in).l) < 1e-4);
}

static void testControlChangesGlide()
{
    EngineState st;
    Parameters p;
    const auto in = sine(200.0, 0.5, 48000.0f, 0.1);
    std::vector<float> a(in.size()), b(in.size());
    const uint32_t half = static_cast<uint32_t>(in.size() / 2);
    const float* i1[2] = {in.data(), in.data()};
    float* o1[2] = {a.data(), b.data()};
    processBlock(st, p, 48000.0f, half, i1, o1);
    p.postGainDb = -40.0f;
    p.frequencyHz = 40.0f;
    const float* i2[2] = {in.data() + half, in.data() + half};
    float* o2[2] = {a.data() + half, b.data() + half};
    processBlock(st, p, 48000.0f, half, i2, o2);
    double worst = 0;
    for (size_t i = half - 2; i < half + 8; ++i) worst = std::max(worst, static_cast<double>(std::fabs(a[i + 1] - a[i])));
    // A 200 Hz sine at 0.5 moves at most ~0.013 per sample; a stepped gain would jump by ~0.5.
    check("control changes do not step at the block edge", worst < 0.05);
}

// ---- Robustness -----------------------------------------------------------------------------------

static void testStabilityUnderSweepsAndOverload()
{
    for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
        EngineState st;
        std::vector<float> in(256), l(256), r(256);
        double mx = 0;
        bool bad = false;
        for (int blk = 0; blk < 600; ++blk) {
            Parameters p;
            p.stack = 8.0f;
            p.frequencyHz = 40.0f + static_cast<float>((blk * 997) % 19960);
            p.resonance = (blk / 5) % 2 ? 10.0f : 0.1f;
            p.drift = (blk % 4) * 0.33f;
            p.preGainDb = 12.0f;
            for (size_t i = 0; i < in.size(); ++i) in[i] = ((blk + i) % 11 == 0) ? 1.0f : -0.4f;
            const float* ins[2] = {in.data(), in.data()};
            float* outs[2] = {l.data(), r.data()};
            processBlock(st, p, sr, 256, ins, outs);
            for (size_t i = 0; i < l.size(); ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) bad = true;
                mx = std::max(mx, static_cast<double>(std::fabs(l[i])));
            }
        }
        check("no NaN or inf under random sweeps and overload", !bad);
        check("an all-pass stack stays bounded", mx < 2000.0);
    }
}

static void testImpulseResponseDecays()
{
    for (float q : {0.1f, 0.707f, 10.0f}) {
        Parameters p = withStack(8.0f);
        p.resonance = q;
        p.frequencyHz = 2000.0f;
        const Stereo out = run(p, impulse(96000), 48000.0f);
        double tail = 0;
        for (size_t i = 80000; i < out.l.size(); ++i) tail = std::max(tail, static_cast<double>(std::fabs(out.l[i])));
        check("the impulse response rings out", tail < 1e-4);
    }
}

static void testBlockSizeIndependence()
{
    Parameters p;
    p.stack = 4.0f;
    p.drift = 0.5f;
    p.resonance = 3.0f;
    const auto in = sine(440.0, 0.5, 48000.0f, 0.2);
    const Stereo a = run(p, in, 48000.0f, 16);
    const Stereo b = run(p, in, 48000.0f, 480);
    check("block size does not change the output", std::max(maxDifference(a.l, b.l), maxDifference(a.r, b.r)) < 1e-5);
}

static void testInPlaceAndMissingRightInput()
{
    Parameters p;
    p.stack = 3.0f;
    const auto in = sine(700.0, 0.4, 48000.0f, 0.1);
    const Stereo ref = run(p, in, in);

    EngineState st;
    std::vector<float> l = in, r = in;
    const float* ins[2] = {l.data(), r.data()};
    float* outs[2] = {l.data(), r.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(in.size()), ins, outs);
    check("processing in place gives the same output", std::max(maxDifference(l, ref.l), maxDifference(r, ref.r)) < 1e-6);

    EngineState st2;
    std::vector<float> l2 = in, r2(in.size(), 0.0f);
    const float* ins2[2] = {l2.data(), nullptr};
    float* outs2[2] = {l2.data(), r2.data()};
    processBlock(st2, p, 48000.0f, static_cast<uint32_t>(in.size()), ins2, outs2);
    check("a missing right input is treated as the left, even in place", std::max(maxDifference(l2, ref.l), maxDifference(r2, ref.r)) < 1e-6);
}

static void testSilentDecayLeavesNoDenormals()
{
    EngineState st;
    Parameters p;
    p.stack = 8.0f;
    p.resonance = 6.0f;
    std::vector<float> loud(4800, 0.8f), silent(480, 0.0f), l(4800), r(4800);
    const float* li[2] = {loud.data(), loud.data()};
    float* lo[2] = {l.data(), r.data()};
    processBlock(st, p, 48000.0f, 4800, li, lo);
    for (int blk = 0; blk < 4000; ++blk) {
        const float* si[2] = {silent.data(), silent.data()};
        processBlock(st, p, 48000.0f, 480, si, lo);
    }
    bool denormal = false;
    for (const auto& channel : st.channels)
        for (const auto& c : channel)
            for (size_t i = 0; i < 8; ++i)
                for (float v : {c.s1[i], c.s2[i], c.out[i]})
                    if (v != 0.0f && std::fabs(v) < 1.17549435e-38f) denormal = true;
    check("states are flushed to zero, never left denormal", !denormal);
}

// ---- Lifecycle ------------------------------------------------------------------------------------

static void testResetClearsMemoryOnly()
{
    EngineState st;
    Parameters p;
    p.stack = 2.0f;
    p.resonance = 5.0f;
    const auto in = sine(500.0, 0.5, 48000.0f, 0.05);
    Stereo out{std::vector<float>(in.size()), std::vector<float>(in.size())};
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {out.l.data(), out.r.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(in.size()), ins, outs);
    check("the engine holds filter memory after a block", st.channels[0][0].s1[3] != 0.0f);
    resetState(st);
    bool clear = true;
    for (const auto& channel : st.channels)
        for (const auto& c : channel)
            for (size_t i = 0; i < 8; ++i) clear = clear && c.s1[i] == 0.0f && c.s2[i] == 0.0f && c.out[i] == 0.0f;
    check("reset zeroes the memory", clear);
    check("reset leaves the parameters alone", p.stack == 2.0f && p.resonance == 5.0f);
}

static void testLoweringTheStackClearsTheDroppedCascades()
{
    EngineState st;
    Parameters p = withStack(5.0f);
    const auto in = sine(600.0, 0.5, 48000.0f, 0.05);
    Stereo out{std::vector<float>(in.size()), std::vector<float>(in.size())};
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {out.l.data(), out.r.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(in.size()), ins, outs);
    check("all five cascades hold signal", st.channels[0][4].s1[0] != 0.0f);
    p.stack = 2.0f;
    processBlock(st, p, 48000.0f, 64, ins, outs);
    check("cascades above the stack are cleared", st.channels[0][2].s1[0] == 0.0f && st.channels[1][4].out[7] == 0.0f);
    check("the cascades still in use keep running", st.channels[0][1].s1[0] != 0.0f);
}

static void testFirstBlockDoesNotFadeIn()
{
    Parameters p;
    p.postGainDb = -6.0f;
    const auto in = sine(100.0, 0.5, 48000.0f, 0.02);
    const Stereo out = run(p, in);
    double early = 0, late = 0;
    for (size_t i = 24; i < 200; ++i) early = std::max(early, static_cast<double>(std::fabs(out.l[i])));
    for (size_t i = 600; i < 900; ++i) late = std::max(late, static_cast<double>(std::fabs(out.l[i])));
    check("the first block starts at the set gain, not at zero", early > 0.1 * late);
}

// The host-facing table and the core must agree on every default, range and field.
static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = kParameterSpecs[i];
        check("table default equals the core default", field(defaults, i) == s.defaultValue);
        check("default is inside its range", s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        field(probe, i) = s.minimum - 1000.0f;
        check("core clamps to the table minimum", field(clampParameters(probe), i) == s.minimum);
        field(probe, i) = s.maximum + 1000.0f;
        check("core clamps to the table maximum", field(clampParameters(probe), i) == s.maximum);
        field(probe, i) = s.defaultValue;
        check("a logarithmic range has a positive minimum", !s.logarithmic || s.minimum > 0.0f);
    }
    check("the source's defaults: stack 1, 1 kHz, Q 0.707, no drift, 0 dB, no clip",
          defaults.stack == 1.0f && defaults.frequencyHz == 1000.0f && defaults.resonance == 0.707f && defaults.drift == 0.0f &&
              defaults.preGainDb == 0.0f && defaults.postGainDb == 0.0f && defaults.clip == 0.0f);
}

int main()
{
    testMatchesTheSourceAlgorithm();
    testMagnitudeIsFlat();
    testPhaseMatchesModel();
    testGroupDelayMatchesMeasured();
    testDispersionGrowsWithStackAndPeaksAtFrequency();
    testLatencyIsSevenSamplesPerCascade();
    testEveryControlMatters();
    testDriftDefaultIsInertAndSpreadsTheImage();
    testGainStagesAndClip();
    testControlChangesGlide();
    testStabilityUnderSweepsAndOverload();
    testImpulseResponseDecays();
    testBlockSizeIndependence();
    testInPlaceAndMissingRightInput();
    testSilentDecayLeavesNoDenormals();
    testResetClearsMemoryOnly();
    testLoweringTheStackClearsTheDroppedCascades();
    testFirstBlockDoesNotFadeIn();
    testParameterTableMatchesCore();
    std::printf("spense_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
