#include "trio_core.hpp"
#include "trio_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::trio;

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

struct Result {
    std::vector<float> l, r;
    EngineState state;
};

static Result run(const Parameters& p, const std::vector<float>& inL, const std::vector<float>& inR, float sr = kSr,
                  uint32_t block = 128)
{
    Result res;
    res.l.resize(inL.size());
    res.r.resize(inL.size());
    for (size_t pos = 0; pos < inL.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, inL.size() - pos));
        const float* ins[2] = {inL.data() + pos, inR.data() + pos};
        float* outs[2] = {res.l.data() + pos, res.r.data() + pos};
        processBlock(res.state, p, sr, n, ins, outs);
    }
    return res;
}

static std::vector<float> sine(double hz, double amp, double seconds, float sr = kSr)
{
    std::vector<float> v(static_cast<size_t>(sr * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * hz * i / sr));
    return v;
}

static double rms(const std::vector<float>& v, size_t from)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
    return std::sqrt(s / static_cast<double>(v.size() - from));
}

static double dbOf(double x) { return 20.0 * std::log10(x + 1e-12); }

static double gainDb(const Parameters& p, double hz, double amp, float sr = kSr, double seconds = 1.2)
{
    const auto in = sine(hz, amp, seconds, sr);
    const Result r = run(p, in, in, sr);
    return dbOf(rms(r.l, in.size() / 2) / rms(in, in.size() / 2));
}

// Dynamics switched off everywhere: ratio 1 compresses nothing, up ratio 1 lifts nothing.
static Parameters neutral()
{
    Parameters p;
    for (Band& b : p.bands) { b.downRatio = 1.0f; b.upRatio = 1.0f; }
    return p;
}

// Only one band does anything; the others are neutral.
static Parameters only(int band, float downThresh, float downRatio, float upThresh = -80.0f, float upRatio = 1.0f)
{
    Parameters p = neutral();
    Band& b = p.bands[static_cast<size_t>(band)];
    b.downThreshDb = downThresh; b.downRatio = downRatio; b.upThreshDb = upThresh; b.upRatio = upRatio;
    b.attackMs = 1.0f; b.releaseMs = 100.0f;
    return p;
}

// The three bands sum back to the input: a flat magnitude, whatever the crossovers.
static void testCrossoverIsFlat()
{
    struct X { float low, high, sr; };
    for (const X& x : {X{250.0f, 2500.0f, 48000.0f}, X{250.0f, 2500.0f, 96000.0f}, X{500.0f, 650.0f, 48000.0f}, X{40.0f, 12000.0f, 48000.0f},
                       X{1000.0f, 1250.0f, 44100.0f}}) {
        Parameters p = neutral();
        p.crossLowHz = x.low;
        p.crossHighHz = x.high;
        double worst = 0;
        for (double hz : {30.0, 60.0, 120.0, 200.0, 300.0, 450.0, 600.0, 800.0, 1200.0, 2000.0, 3000.0, 5000.0, 8000.0, 14000.0}) {
            if (hz > x.sr * 0.4) continue;
            worst = std::max(worst, std::fabs(gainDb(p, hz, 0.05, x.sr, hz < 100.0 ? 1.6 : 0.9)));
        }
        if (worst >= 0.06) std::printf("  crossovers %.0f/%.0f at %.0f: worst %.3f dB\n", x.low, x.high, x.sr, worst);
        check("neutral bands sum to a flat response", worst < 0.06);
    }
}

static void testBandTargetFormula()
{
    Band b;
    b.downThreshDb = -30.0f; b.downRatio = 4.0f; b.upThreshDb = -50.0f; b.upRatio = 2.0f;
    check("no change between the thresholds", bandTargetDb(b, -40.0f, 18.0f) == 0.0f);
    check("downward: 10 dB over at 4:1 is -7.5 dB", std::fabs(bandTargetDb(b, -20.0f, 18.0f) + 7.5f) < 1e-5f);
    check("upward: 10 dB under at 2:1 is +5 dB", std::fabs(bandTargetDb(b, -60.0f, 18.0f) - 5.0f) < 1e-5f);
    check("the upward lift is capped by the limit", std::fabs(bandTargetDb(b, -120.0f, 6.0f) - 6.0f) < 1e-5f);
    check("a zero limit disables the lift", bandTargetDb(b, -120.0f, 0.0f) == 0.0f);
    b.upThreshDb = -10.0f;  // above the down threshold: the two must not fight
    check("an up threshold above the down threshold is held to it", bandTargetDb(b, -35.0f, 18.0f) > 0.0f &&
          bandTargetDb(b, -25.0f, 18.0f) < 0.0f);
    float last = 100.0f;
    bool monotonic = true;
    for (float x = -100.0f; x <= 0.0f; x += 0.1f) {
        const float t = bandTargetDb(Parameters{}.bands[1], x, 18.0f);
        if (t > last + 1e-4f && x > -20.0f) monotonic = false;
        last = t;
    }
    check("the default band never boosts a loud signal", monotonic);
}

// Steady-state gain of a tone inside one band follows the formula.
static void testBandsFollowTheirFormula()
{
    struct Case { int band; double hz; float low, high; };
    // Crossovers are placed well away from the tone: an LR4 split leaks about -40 dB into the
    // neighbouring bands one octave-ish out, which moves a heavily reduced band by about 1 dB.
    for (const Case& c : {Case{0, 60.0, 250.0f, 2500.0f}, Case{1, 800.0, 120.0f, 8000.0f}, Case{2, 9000.0, 250.0f, 2500.0f}}) {
        // Downward: tone at -10 dB peak, threshold -30, 4:1 -> -15 dB.
        Parameters down = only(c.band, -30.0f, 4.0f);
        down.crossLowHz = c.low; down.crossHighHz = c.high;
        const double d = gainDb(down, c.hz, std::pow(10.0, -10.0 / 20.0), kSr, c.hz < 100 ? 2.0 : 1.2);
        if (std::fabs(d + 15.0) >= 0.8) std::printf("  band %d down: %.2f\n", c.band, d);
        check("downward compression follows its formula in each band", std::fabs(d + 15.0) < 0.8);
        // Upward: tone at -50 dB, up threshold -36, 2.5:1 -> +8.4 dB.
        Parameters up = only(c.band, 0.0f, 1.0f, -36.0f, 2.5f);
        up.crossLowHz = c.low; up.crossHighHz = c.high;
        const double u = gainDb(up, c.hz, std::pow(10.0, -50.0 / 20.0), kSr, c.hz < 100 ? 2.0 : 1.2);
        if (std::fabs(u - 8.4) >= 0.8) std::printf("  band %d up: %.2f\n", c.band, u);
        check("upward lift follows its formula in each band", std::fabs(u - 8.4) < 0.8);
    }
}

// A tone in one band is not touched by compression set in the others.
static void testBandsAreIndependent()
{
    const Parameters p = only(1, -60.0f, 20.0f);  // the mid band clamps hard
    check("a low tone is untouched by a heavy mid-band compressor", std::fabs(gainDb(p, 60.0, 0.3, kSr, 2.0)) < 0.3);
    check("a high tone is untouched by a heavy mid-band compressor", std::fabs(gainDb(p, 12000.0, 0.3)) < 0.3);
    check("a mid tone is compressed by it", gainDb(p, 800.0, 0.3) < -10.0);
}

// The bug in the source: one envelope shared by both channels, updated twice per sample.
static void testStereoChannelsShareOneDetector()
{
    Parameters p = only(1, -30.0f, 4.0f);
    const auto loud = sine(800.0, 0.5, 1.2);
    const auto quiet = sine(800.0, 0.005, 1.2);
    const Result r = run(p, loud, quiet);
    const size_t half = loud.size() / 2;
    const double inRatio = dbOf(rms(loud, half)) - dbOf(rms(quiet, half));
    const double outRatio = dbOf(rms(r.l, half)) - dbOf(rms(r.r, half));
    check("channel balance is unchanged by the dynamics", std::fabs(inRatio - outRatio) < 0.05);
    check("the louder channel set the reduction", dbOf(rms(r.l, half)) - dbOf(rms(loud, half)) < -8.0);
    check("the quiet channel is reduced by the same amount",
          std::fabs((dbOf(rms(r.r, half)) - dbOf(rms(quiet, half))) - (dbOf(rms(r.l, half)) - dbOf(rms(loud, half)))) < 0.05);
}

// 63 % of the way in one time constant: this fails by a factor of two if the envelope is stepped twice per sample.
static double timeToFraction(const std::vector<float>& trace, double from, double to, double fraction)
{
    for (size_t i = 0; i < trace.size(); ++i)
        if ((trace[i] - from) / (to - from) >= fraction) return static_cast<double>(i) / kSr * 1000.0;
    return -1.0;
}

static void testBallistics()
{
    Parameters p = only(1, -40.0f, 4.0f);
    p.bands[1].attackMs = 20.0f;
    p.bands[1].releaseMs = 200.0f;
    // A 1 kHz tone at -10 dB switches on: target reduction 0.75 * 30 = -22.5 dB.
    std::vector<float> in = sine(1000.0, std::pow(10.0, -10.0 / 20.0), 0.6);
    std::vector<float> trace(in.size());
    {
        EngineState st;
        std::vector<float> l(1), r(1);
        for (size_t i = 0; i < in.size(); ++i) {
            const float* ins[2] = {&in[i], &in[i]};
            float* outs[2] = {l.data(), r.data()};
            processBlock(st, p, kSr, 1, ins, outs);
            trace[i] = -st.meters.gainDb[1];
        }
    }
    const double attack63 = timeToFraction(trace, 0.0, 22.5, 0.632);
    check("attack reaches 63 % in one attack time constant (20 ms +-15 %)", attack63 > 17.0 && attack63 < 23.0);
    check("the attack settles at the static reduction", std::fabs(trace.back() - 22.5) < 0.8);

    // Release: the tone stops.
    std::vector<float> both = in;
    both.insert(both.end(), static_cast<size_t>(kSr * 1.5), 0.0f);
    std::vector<float> rel;
    {
        EngineState st;
        std::vector<float> l(1), r(1);
        for (size_t i = 0; i < both.size(); ++i) {
            const float* ins[2] = {&both[i], &both[i]};
            float* outs[2] = {l.data(), r.data()};
            processBlock(st, p, kSr, 1, ins, outs);
            if (i >= in.size()) rel.push_back(st.meters.gainDb[1]);
        }
    }
    // After the tone ends the level falls to the floor, which for a band with no lift targets 0 dB.
    const double release63 = timeToFraction(rel, rel.front(), 0.0, 0.632);
    check("release reaches 63 % in one release time constant (200 ms +-20 %)", release63 > 160.0 && release63 < 240.0);

    // The Time macro scales both.
    Parameters slow = p;
    slow.time = 2.0f;
    EngineState st;
    std::vector<float> l(1), r(1), t2(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const float* ins[2] = {&in[i], &in[i]};
        float* outs[2] = {l.data(), r.data()};
        processBlock(st, slow, kSr, 1, ins, outs);
        t2[i] = -st.meters.gainDb[1];
    }
    const double attack63Slow = timeToFraction(t2, 0.0, 22.5, 0.632);
    check("Time = 2 doubles the attack", std::fabs(attack63Slow / attack63 - 2.0) < 0.2);
}

static void testAmountInputOutput()
{
    Parameters p;
    p.amount = 0.0f;
    const auto in = sine(700.0, 0.5, 0.3);
    const Result dry = run(p, in, in);
    double worst = 0;
    for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(dry.l[i] - in[i])));
    check("Amount 0 returns the dry signal exactly", worst == 0.0);

    Parameters q = neutral();
    q.outputDb = -6.0f;
    check("Output scales the wet signal", std::fabs(gainDb(q, 700.0, 0.05) + 6.0) < 0.1);
    q = neutral();
    q.inputDb = 6.0f;
    check("Input drives the dynamics but not the dry path (Amount 100 %)", std::fabs(gainDb(q, 700.0, 0.05) - 6.0) < 0.1);
    q.amount = 0.0f;
    check("... and Amount 0 ignores Input", std::fabs(gainDb(q, 700.0, 0.05)) < 1e-4);
    q = neutral();
    q.amount = 50.0f;
    q.outputDb = 12.0f;
    // dry 1.0 + wet 3.98: halfway is 2.49 -> +7.9 dB
    check("Amount blends dry and wet linearly", std::fabs(gainDb(q, 700.0, 0.05) - dbOf(0.5 * 1.0 + 0.5 * std::pow(10.0, 12.0 / 20.0))) < 0.1);
}

static void testUpwardLimit()
{
    Parameters p = only(1, 0.0f, 1.0f, -20.0f, 10.0f);
    p.upLimitDb = 6.0f;
    check("the lift is capped by the upward limit", gainDb(p, 800.0, 0.0005) < 6.5 && gainDb(p, 800.0, 0.0005) > 5.0);
    p.upLimitDb = 0.0f;
    check("a zero limit turns the lift off", std::fabs(gainDb(p, 800.0, 0.0005)) < 0.1);
}

static void testBandOnOff()
{
    Parameters p = only(1, -60.0f, 20.0f);
    p.bands[1].on = 0.0f;
    check("a band that is off passes its signal through", std::fabs(gainDb(p, 800.0, 0.3)) < 0.1);
    Parameters q = neutral();
    q.bands[1].gainDb = 6.0f;
    check("band gain trims the band", std::fabs(gainDb(q, 800.0, 0.05) - 6.0) < 0.3);
    q.bands[1].on = 0.0f;
    check("band gain does nothing while the band is off", std::fabs(gainDb(q, 800.0, 0.05)) < 0.1);
}

static void testMetersReportTheAudio()
{
    Parameters p = only(1, -30.0f, 4.0f);
    const auto in = sine(800.0, std::pow(10.0, -10.0 / 20.0), 1.2);
    const Result r = run(p, in, in);
    check("gain meter reports the reduction", std::fabs(r.state.meters.gainDb[1] + 15.0f) < 1.0f);
    check("level meter reports the band level", r.state.meters.levelDb[1] > -14.0f && r.state.meters.levelDb[1] < -9.0f);
    check("an untouched band reads zero", std::fabs(r.state.meters.gainDb[0]) < 1e-6f);
    check("a band with only crossover leakage reads about 40 dB down", r.state.meters.levelDb[2] < -42.0f && r.state.meters.levelDb[2] > -60.0f);
}

static void testEveryControlMatters()
{
    const auto in = sine(900.0, 0.2, 0.5);
    Parameters base;
    base.bands[1] = {1.0f, -30.0f, 4.0f, -45.0f, 2.0f, 5.0f, 80.0f, 0.0f};
    const Result ref = run(base, in, in);
    auto differs = [&](const Parameters& q) {
        const Result o = run(q, in, in);
        double d = 0;
        for (size_t i = 0; i < in.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(o.l[i] - ref.l[i])));
        return d > 1e-4;
    };
    Parameters q = base; q.inputDb = 6.0f;                 check("input has an effect", differs(q));
    q = base; q.outputDb = -4.0f;                          check("output has an effect", differs(q));
    q = base; q.amount = 40.0f;                            check("amount has an effect", differs(q));
    q = base; q.bands[1].on = 0.0f;                        check("band on has an effect", differs(q));
    q = base; q.bands[1].downThreshDb = -10.0f;            check("down threshold has an effect", differs(q));
    q = base; q.bands[1].downRatio = 12.0f;                check("down ratio has an effect", differs(q));
    q = base; q.bands[1].gainDb = 5.0f;                    check("band gain has an effect", differs(q));
    q = base; q.crossLowHz = 800.0f; q.crossHighHz = 900.0f; check("crossovers have an effect", differs(q));
    {   // Upward controls, attack, release, time and the limit need a signal that triggers them.
        std::vector<float> quiet = sine(900.0, 0.001, 0.3);
        std::vector<float> burst = sine(900.0, 0.5, 0.3);
        for (size_t i = 0; i < 2400; ++i) burst[i] = 0.0f;  // a gap, so release matters
        auto outputDiffers = [&](const std::vector<float>& sig, const Parameters& a, const Parameters& b) {
            const Result x = run(a, sig, sig), y = run(b, sig, sig);
            double d = 0;
            for (size_t i = 0; i < sig.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(x.l[i] - y.l[i])));
            return d > 1e-4;
        };
        Parameters u = base;
        u.bands[1].upThreshDb = -20.0f;
        Parameters u2 = u; u2.bands[1].upThreshDb = -50.0f;
        check("up threshold has an effect", outputDiffers(quiet, u, u2));
        u2 = u; u2.bands[1].upRatio = 8.0f;
        check("up ratio has an effect", outputDiffers(quiet, u, u2));
        u2 = u; u2.upLimitDb = 2.0f;
        check("upward limit has an effect", outputDiffers(quiet, u, u2));
        Parameters t = base;
        Parameters t2 = t; t2.bands[1].attackMs = 100.0f;
        check("attack has an effect", outputDiffers(burst, t, t2));
        t2 = t; t2.bands[1].releaseMs = 1500.0f;
        check("release has an effect", outputDiffers(burst, t, t2));
        t2 = t; t2.time = 4.0f;
        check("time has an effect", outputDiffers(burst, t, t2));
    }
}

static void testStabilityAndBounds()
{
    for (float sr : {44100.0f, 96000.0f, 192000.0f}) {
        EngineState st;
        std::vector<float> in(256), l(256), r(256);
        bool bad = false;
        double mx = 0;
        for (int blk = 0; blk < 500; ++blk) {
            Parameters p;
            p.inputDb = -12.0f + static_cast<float>((blk * 7) % 37);
            p.outputDb = -24.0f + static_cast<float>((blk * 5) % 37);
            p.amount = static_cast<float>((blk * 11) % 101);
            p.time = 0.1f * std::pow(100.0f, static_cast<float>((blk * 3) % 100) / 99.0f);
            p.upLimitDb = static_cast<float>((blk * 13) % 25);
            p.crossLowHz = 40.0f + static_cast<float>((blk * 37) % 960);
            p.crossHighHz = 1000.0f + static_cast<float>((blk * 101) % 11000);
            for (int b = 0; b < kBands; ++b) {
                Band& bd = p.bands[static_cast<size_t>(b)];
                bd.on = static_cast<float>((blk + b) % 5 != 0);
                bd.downThreshDb = -60.0f + static_cast<float>((blk * 3 + b) % 61);
                bd.downRatio = 1.0f + static_cast<float>((blk * 7 + b) % 190) * 0.1f;
                bd.upThreshDb = -80.0f + static_cast<float>((blk * 5 + b) % 81);
                bd.upRatio = 1.0f + static_cast<float>((blk + b) % 91) * 0.1f;
                bd.attackMs = 0.5f + static_cast<float>((blk * 17 + b) % 200);
                bd.releaseMs = 5.0f + static_cast<float>((blk * 29 + b) % 1995);
                bd.gainDb = -18.0f + static_cast<float>((blk + b * 3) % 37);
            }
            for (size_t i = 0; i < in.size(); ++i) in[i] = ((blk + i) % 9 == 0) ? 3.0f : -0.4f;
            const float* ins[2] = {in.data(), in.data()};
            float* outs[2] = {l.data(), r.data()};
            processBlock(st, p, sr, 256, ins, outs);
            for (float v : l) { if (!std::isfinite(v)) bad = true; mx = std::max(mx, static_cast<double>(std::fabs(v))); }
        }
        check("no NaN or inf under random parameters and overload", !bad);
        check("output stays bounded (limited gain)", mx < 5000.0);
    }
    EngineState st;
    std::vector<float> in(64, std::nanf("")), l(64), r(64);
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {l.data(), r.data()};
    processBlock(st, Parameters{}, kSr, 64, ins, outs);
    bool finite = true;
    for (float v : l) if (!std::isfinite(v)) finite = false;
    check("NaN input does not poison the output", finite);
    processBlock(st, Parameters{}, kSr, 64, ins, outs);
    check("and the state stays usable", std::isfinite(st.envelopeDb[0]));
}

static void testBlockSizeIndependence()
{
    Parameters p;
    p.crossLowHz = 300.0f;
    const auto in = sine(440.0, 0.4, 0.3);
    const Result a = run(p, in, in, kSr, 16);
    const Result b = run(p, in, in, kSr, 480);
    double worst = 0;
    for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
    check("block size does not change the output", worst < 1e-5);
}

static void testGainChangesAreSmoothed()
{
    EngineState st;
    Parameters p = neutral();
    const auto in = sine(200.0, 0.3, 0.05);
    std::vector<float> l(in.size()), r(in.size());
    const uint32_t half = static_cast<uint32_t>(in.size() / 2);
    const float* i1[2] = {in.data(), in.data()};
    float* o1[2] = {l.data(), r.data()};
    processBlock(st, p, kSr, half, i1, o1);
    p.outputDb = -18.0f;
    p.inputDb = 12.0f;
    p.amount = 30.0f;
    const float* i2[2] = {in.data() + half, in.data() + half};
    float* o2[2] = {l.data() + half, r.data() + half};
    processBlock(st, p, kSr, half, i2, o2);
    double worst = 0;
    for (size_t i = half - 2; i < half + 2; ++i) worst = std::max(worst, static_cast<double>(std::fabs(l[i + 1] - l[i])));
    check("input, output and amount changes do not step at the block edge", worst < 0.05);
}

static void testSilentDecayLeavesNoDenormals()
{
    EngineState st;
    Parameters p;
    std::vector<float> loud = sine(300.0, 0.8, 0.2), silent(480, 0.0f), l(loud.size()), r(loud.size());
    const float* li[2] = {loud.data(), loud.data()};
    float* lo[2] = {l.data(), r.data()};
    processBlock(st, p, kSr, static_cast<uint32_t>(loud.size()), li, lo);
    for (int blk = 0; blk < 6000; ++blk) {
        const float* si[2] = {silent.data(), silent.data()};
        processBlock(st, p, kSr, 480, si, lo);
    }
    bool denormal = false;
    auto test = [&](double v) { if (v != 0.0 && std::fabs(v) < 2.2250738585072014e-308) denormal = true; };
    for (const ChannelState& cs : st.channels)
        for (const Lr4* f : {&cs.lowLp, &cs.lowHp, &cs.midLp, &cs.highHp, &cs.lowApLp, &cs.lowApHp})
            for (const Cell& c : f->cell) { test(c.z1); test(c.z2); }
    for (float e : st.envelopeDb) test(static_cast<double>(e));
    for (float d : st.detector) test(static_cast<double>(d));
    check("states are flushed to zero, never left denormal", !denormal);
}

static void testResetClearsMemoryOnly()
{
    EngineState st;
    Parameters p = only(1, -30.0f, 4.0f);
    const auto in = sine(800.0, 0.5, 0.3);
    const Result res = run(p, in, in);
    EngineState used = res.state;
    check("state is non-trivial after audio", used.envelopeDb[1] < -1.0f);
    resetState(used);
    check("reset clears the memory", used.envelopeDb[1] == 0.0f && used.channels[0].lowLp.cell[0].z1 == 0.0 && !used.primed);
    check("reset leaves the parameters alone", p.bands[1].downThreshDb == -30.0f);
}

static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    check("thirty-seven parameters", kParameterCount == 37);
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = specs()[i];
        check("symbol and name are set", s.symbol[0] != '\0' && s.name[0] != '\0');
        if (i >= kControlCount) {
            check("meters are outputs", s.output);
            continue;
        }
        check("controls are not outputs", !s.output);
        check("table default equals the core default", field(defaults, i) == s.defaultValue);
        check("default is inside its range", s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        field(probe, i) = s.minimum - 1000.0f;
        const float lo = field(clampParameters(probe), i);
        field(probe, i) = s.maximum + 1000.0f;
        const float hi = field(clampParameters(probe), i);
        // The high crossover is also held 1.25x above the low one, so only its lower bound is the table's.
        check("core clamps to the table minimum", lo == s.minimum || (i == kCrossHigh && lo >= s.minimum));
        check("core clamps to the table maximum", hi == s.maximum);
        field(probe, i) = s.defaultValue;
        check("a logarithmic range has a positive minimum", !s.logarithmic || s.minimum > 0.0f);
    }
    bool unique = true;
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        for (std::uint32_t j = i + 1; j < kParameterCount; ++j)
            if (std::string(specs()[i].symbol) == specs()[j].symbol) unique = false;
    check("symbols are unique", unique);
}

static void testCrossoversStayApart()
{
    Parameters p;
    p.crossLowHz = 1000.0f;
    p.crossHighHz = 1000.0f;
    const Parameters c = clampParameters(p);
    check("the high crossover is held at least 1.25x the low one", c.crossHighHz >= 1250.0f - 1e-3f);
    p.crossLowHz = 1000.0f;
    p.crossHighHz = 12000.0f;
    check("a wide setting is left alone", clampParameters(p).crossHighHz == 12000.0f);
}

int main()
{
    testCrossoverIsFlat();
    testBandTargetFormula();
    testBandsFollowTheirFormula();
    testBandsAreIndependent();
    testStereoChannelsShareOneDetector();
    testBallistics();
    testAmountInputOutput();
    testUpwardLimit();
    testBandOnOff();
    testMetersReportTheAudio();
    testEveryControlMatters();
    testStabilityAndBounds();
    testBlockSizeIndependence();
    testGainChangesAreSmoothed();
    testSilentDecayLeavesNoDenormals();
    testResetClearsMemoryOnly();
    testParameterTableMatchesCore();
    testCrossoversStayApart();
    std::printf("trio_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
