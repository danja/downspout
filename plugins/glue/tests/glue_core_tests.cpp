#include "glue_core.hpp"
#include "glue_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::glue;

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

// Runs a stereo signal through a fresh engine in blocks.
static Result run(const Parameters& p, const std::vector<float>& inL, const std::vector<float>& inR,
                  uint32_t block = 128, float sr = kSr)
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

// Steady-state gain in dB of a stereo-identical sine, measured over its last half.
static double gainDb(const Parameters& p, double hz, double amp, double seconds = 1.5)
{
    const auto in = sine(hz, amp, seconds);
    const Result r = run(p, in, in);
    return dbOf(rms(r.l, in.size() / 2) / rms(in, in.size() / 2));
}

// A plain compressor with the options that would hide the formula switched off.
static Parameters plain()
{
    Parameters p;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.kneeDb = 0.0f;
    p.attackMs = 1.0f;
    p.releaseMs = 100.0f;
    p.scHpfHz = kHpfOffHz;
    p.rangeDb = 40.0f;
    return p;
}

// Steady-state gain follows the textbook curve: GR = (1 - 1/R) (T - level) above the threshold.
static void testStaticCurveOnAudio()
{
    for (double levelDb : {-40.0, -25.0, -10.0, -2.0}) {
        const double amp = std::pow(10.0, levelDb / 20.0);  // peak level
        const double expected = staticGainDb(plain(), static_cast<float>(levelDb));
        const double got = gainDb(plain(), 1000.0, amp);
        if (std::fabs(got - expected) >= 0.6)
            std::printf("  level %.0f dB: expected %.2f, got %.2f\n", levelDb, expected, got);
        check("steady-state gain matches the static curve", std::fabs(got - expected) < 0.6);
    }
    check("below the threshold the gain is unity", std::fabs(staticGainDb(plain(), -40.0f)) < 1e-6);
    check("10 dB over at 4:1 reduces by 7.5 dB", std::fabs(staticGainDb(plain(), -10.0f) + 7.5f) < 1e-4);
    Parameters p = plain();
    p.ratio = 10.0f;
    check("10 dB over at 10:1 reduces by 9 dB", std::fabs(staticGainDb(p, -10.0f) + 9.0f) < 1e-4);
}

static void testSoftKnee()
{
    Parameters p = plain();
    p.kneeDb = 12.0f;
    // Giannoulis: at the threshold the reduction is slope * (W/2)^2 / (2W) = 0.75 * 36 / 24.
    check("knee gives 1.125 dB at the threshold", std::fabs(staticGainDb(p, -20.0f) + 1.125f) < 1e-4);
    check("no reduction at the bottom of the knee", std::fabs(staticGainDb(p, -26.0f)) < 1e-5);
    check("knee meets the straight line at its top", std::fabs(staticGainDb(p, -14.0f) - (-0.75f * 6.0f)) < 1e-4);
    // Continuous and non-increasing across the whole range.
    float last = staticGainDb(p, -60.0f);
    bool ok = true;
    for (float x = -59.99f; x <= 0.0f; x += 0.01f) {
        const float g = staticGainDb(p, x);
        if (g > last + 1e-6f || std::fabs(g - last) > 0.0076f + 1e-4f) ok = false;
        last = g;
    }
    check("static curve is continuous and never rises with level", ok);
    // And the audio agrees inside the knee.
    const double amp = std::pow(10.0, -20.0 / 20.0);
    check("audio at the threshold shows the knee", std::fabs(gainDb(p, 1000.0, amp) + 1.125) < 0.5);
}

static void testRangeCap()
{
    Parameters p = plain();
    p.rangeDb = 6.0f;
    check("static curve is capped by Range", staticGainDb(p, 0.0f) >= -6.0f - 1e-6f);
    check("audio gain is capped by Range", gainDb(p, 1000.0, 0.9) > -6.3);
}

// A step to a constant level is a clean one-pole in dB: 63 % of the way after one time constant.
static double timeToFraction(const std::vector<float>& gainDbTrace, double from, double to, double fraction)
{
    for (size_t i = 0; i < gainDbTrace.size(); ++i)
        if ((gainDbTrace[i] - from) / (to - from) >= fraction) return static_cast<double>(i) / kSr * 1000.0;
    return -1.0;
}

static std::vector<float> gainTrace(const Parameters& p, const std::vector<float>& in)
{
    // Block size 1 so the trace is per sample; the gain is read back as out / in.
    const Result r = run(p, in, in, 1);
    std::vector<float> g(in.size());
    for (size_t i = 0; i < in.size(); ++i) g[i] = in[i] != 0.0f ? static_cast<float>(dbOf(r.l[i] / in[i])) : 0.0f;
    return g;
}

static void testBallistics()
{
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    p.attackMs = 10.0f;
    p.releaseMs = 200.0f;
    // Constant 0.5 (-6 dB): target reduction 0.75 * (-30 + 6) = -18 dB.
    std::vector<float> step(static_cast<size_t>(kSr * 0.5), 0.5f);
    const auto g = gainTrace(p, step);
    std::vector<float> reduction(g.size());
    for (size_t i = 0; i < g.size(); ++i) reduction[i] = -g[i];
    const double attack63 = timeToFraction(reduction, 0.0, 18.0, 0.632);
    check("attack reaches 63 % in one attack time constant (10 ms +-1.5)", attack63 > 8.5 && attack63 < 11.5);
    check("attack settles at the static reduction", std::fabs(g.back() + 18.0f) < 0.3f);

    // Release: hold the reduction, then drop to a level far below the threshold.
    std::vector<float> hold(static_cast<size_t>(kSr * 0.5), 0.5f);
    std::vector<float> quiet(static_cast<size_t>(kSr * 1.0), 0.0001f);
    std::vector<float> both = hold;
    both.insert(both.end(), quiet.begin(), quiet.end());
    const Result r = run(p, both, both, 1);
    std::vector<float> rel(quiet.size());
    for (size_t i = 0; i < quiet.size(); ++i) rel[i] = static_cast<float>(dbOf(r.l[hold.size() + i] / 0.0001));
    // Gain starts near -18 dB and returns to 0; 63 % of the way is one release time constant.
    const double release63 = timeToFraction(rel, rel.front(), 0.0, 0.632);
    check("release reaches 63 % in one release time constant (200 ms +-20 %)", release63 > 160.0 && release63 < 240.0);
}

// Auto release: after a short burst it recovers quickly, after sustained compression slowly.
static double recovery63Ms(const Parameters& p, double loudSeconds)
{
    std::vector<float> in(static_cast<size_t>(kSr * loudSeconds), 0.5f);
    const size_t loudEnd = in.size();
    in.insert(in.end(), static_cast<size_t>(kSr * 6.0), 0.0001f);
    const Result r = run(p, in, in, 1);
    std::vector<float> rel(in.size() - loudEnd);
    for (size_t i = 0; i < rel.size(); ++i) rel[i] = static_cast<float>(dbOf(r.l[loudEnd + i] / 0.0001));
    return timeToFraction(rel, rel.front(), 0.0, 0.632);
}

static void testAutoRelease()
{
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    p.attackMs = 2.0f;
    p.autoRelease = 1.0f;
    p.releaseMs = 20.0f;  // must be ignored while Auto is on
    const double burst = recovery63Ms(p, 0.05);
    const double sustained = recovery63Ms(p, 5.0);
    std::printf("  auto release 63 %%: burst %.0f ms, sustained %.0f ms\n", burst, sustained);
    check("auto release is slower after sustained compression than after a burst", sustained > 2.0 * burst);
    check("auto release ignores the Release knob", burst > 60.0);
    check("auto release stays within its documented bounds", sustained < 1500.0 && burst > 0.0);
    check("auto release time table is monotonic",
          autoReleaseMs(0.0f) < autoReleaseMs(4.0f) && autoReleaseMs(4.0f) < autoReleaseMs(8.0f));
}

static void testSidechainHighPass()
{
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    p.scHpfHz = 150.0f;
    const double lowHpf = gainDb(p, 40.0, 0.5);
    const double highHpf = gainDb(p, 1000.0, 0.5);
    check("40 Hz is compressed much less than 1 kHz through a 150 Hz sidechain filter", lowHpf > highHpf + 6.0);
    p.scHpfHz = kHpfOffHz;
    const double lowOff = gainDb(p, 40.0, 0.5);
    check("with the filter off both tones are compressed alike", std::fabs(lowOff - highHpf) < 1.0);
}

// The reduction is set by the louder channel, so the stereo image does not move.
static void testStereoLink()
{
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    const auto loud = sine(1000.0, 0.8, 1.5);
    const auto quiet = sine(1000.0, 0.08, 1.5);
    const Result r = run(p, loud, quiet);
    const size_t half = loud.size() / 2;
    const double inRatio = dbOf(rms(loud, half)) - dbOf(rms(quiet, half));
    const double outRatio = dbOf(rms(r.l, half)) - dbOf(rms(r.r, half));
    check("channel balance is unchanged by compression", std::fabs(inRatio - outRatio) < 0.05);
    check("the louder channel set the reduction", dbOf(rms(r.l, half)) - dbOf(rms(loud, half)) < -8.0);
    // A quiet channel alone would be below the threshold; linked, it is reduced as much as the loud one.
    check("the quiet channel is reduced too",
          std::fabs((dbOf(rms(r.r, half)) - dbOf(rms(quiet, half))) - (dbOf(rms(r.l, half)) - dbOf(rms(loud, half)))) < 0.05);
}

static void testRmsDetector()
{
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    const double peakGain = gainDb(p, 1000.0, 0.5);
    p.rms = 1.0f;
    const double rmsGain = gainDb(p, 1000.0, 0.5);
    // A sine's RMS is 3 dB under its peak, so the RMS detector reduces 0.75 * 3 = 2.25 dB less.
    if (std::fabs((rmsGain - peakGain) - 2.25) >= 0.8)
        std::printf("  peak %.2f rms %.2f\n", peakGain, rmsGain);
    check("RMS detector reduces about 2.25 dB less than peak on a sine", std::fabs((rmsGain - peakGain) - 2.25) < 0.8);
}

static void testMixMakeupAndSoftClip()
{
    Parameters p = plain();
    p.mix = 0.0f;
    const auto in = sine(1000.0, 0.9, 0.2);
    const Result dry = run(p, in, in);
    double worst = 0;
    for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(dry.l[i] - in[i])));
    check("mix 0 returns the dry signal", worst < 1e-6);

    p = plain();
    p.makeupDb = 6.0f;
    check("makeup adds its gain below the threshold", std::fabs(gainDb(p, 1000.0, 0.01) - 6.0) < 0.2);

    check("soft clip is off by default", Parameters{}.softClip < 0.5f);
    // The shape: linear to 0.5, continuous in value and slope, bounded, monotonic.
    bool ok = true;
    float last = softClipSample(0.0f);
    for (float x = 0.0001f; x <= 4.0f; x += 0.0001f) {
        const float y = softClipSample(x);
        if (y < last - 1e-7f || y > 1.0f + 1e-6f || y - last > 1.01f * 0.0001f + 1e-7f) ok = false;
        if (x <= 0.5f && std::fabs(y - x) > 1e-6f) ok = false;
        last = y;
    }
    check("soft clip is linear below 0.5, continuous, monotonic and bounded", ok);
    check("soft clip is odd", std::fabs(softClipSample(-0.8f) + softClipSample(0.8f)) < 1e-7f);
    p = plain();
    p.thresholdDb = 0.0f;
    p.makeupDb = 12.0f;
    p.softClip = 1.0f;
    const auto hot = sine(1000.0, 1.0, 0.1);
    const Result r = run(p, hot, hot);
    float peak = 0;
    for (float v : r.l) peak = std::max(peak, std::fabs(v));
    check("soft clip keeps hot output within 1.0", peak <= 1.0f + 1e-5f);
    // Below -6 dBFS the soft clipper must not change anything.
    p = plain();
    p.thresholdDb = 0.0f;
    const auto small = sine(1000.0, 0.4, 0.1);
    const Result off = run(p, small, small);
    p.softClip = 1.0f;
    const Result on = run(p, small, small);
    double d = 0;
    for (size_t i = 0; i < small.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(on.l[i] - off.l[i])));
    check("soft clip leaves signals under 0.5 untouched", d < 1e-6);
}

static void testTransparentBelowThreshold()
{
    check("a quiet signal passes at unity with the default settings", std::fabs(gainDb(Parameters{}, 1000.0, 0.003)) < 0.1);
}

static void testMetersReportTheAudio()
{
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    const auto in = sine(1000.0, 0.5, 1.5);
    const Result r = run(p, in, in);
    const double expected = -staticGainDb(p, -6.0206f);
    check("reduction meter is positive and matches the static curve", r.state.meters.reductionDb > 0.0f &&
          std::fabs(r.state.meters.reductionDb - expected) < 1.5);
    check("level meter is a decibel value near the signal peak", r.state.meters.levelDb > -30.0f && r.state.meters.levelDb < 0.5f);
    const std::vector<float> silence(4800, 0.0f);
    const Result idle = run(p, silence, silence);
    check("meters read zero reduction on silence", idle.state.meters.reductionDb == 0.0f);
    check("meters read the floor on silence", idle.state.meters.levelDb <= -119.0f);
}

static void testEveryControlMatters()
{
    const auto in = sine(900.0, 0.6, 0.6);
    Parameters base = plain();
    base.thresholdDb = -30.0f;
    base.kneeDb = 6.0f;
    base.scHpfHz = 100.0f;
    const Result ref = run(base, in, in);
    auto differs = [&](const Parameters& q) {
        const Result o = run(q, in, in);
        double d = 0;
        for (size_t i = 0; i < in.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(o.l[i] - ref.l[i])));
        return d > 1e-3;
    };
    Parameters q = base; q.thresholdDb = -10.0f;  check("threshold has an effect", differs(q));
    q = base; q.ratio = 12.0f;                    check("ratio has an effect", differs(q));
    {   // The knee only acts within its width of the threshold, so put the signal inside it.
        Parameters k0 = base; k0.thresholdDb = -5.0f; k0.kneeDb = 0.0f;
        Parameters k1 = k0; k1.kneeDb = 20.0f;
        const Result a = run(k0, in, in);
        const Result b = run(k1, in, in);
        double d = 0;
        for (size_t i = 0; i < in.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        check("knee has an effect", d > 1e-3);
    }
    q = base; q.attackMs = 80.0f;                 check("attack has an effect", differs(q));
    {  // release only matters once the gain recovers, so use a short burst

        std::vector<float> burst(static_cast<size_t>(kSr * 0.3), 0.0f);
        for (size_t i = 0; i < 4800; ++i) burst[i] = static_cast<float>(0.6 * std::sin(0.13 * i));
        const Result a = run(base, burst, burst);
        Parameters slow = base; slow.releaseMs = 2000.0f;
        const Result b = run(slow, burst, burst);
        double d = 0;
        for (size_t i = 0; i < burst.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        check("release has an effect", d > 1e-4);
        Parameters aut = base; aut.autoRelease = 1.0f;
        const Result c = run(aut, burst, burst);
        double e = 0;
        for (size_t i = 0; i < burst.size(); ++i) e = std::max(e, static_cast<double>(std::fabs(a.l[i] - c.l[i])));
        check("auto release has an effect", e > 1e-4);
    }
    q = base; q.scHpfHz = 400.0f;                 check("sidechain filter has an effect", differs(q));
    q = base; q.rms = 1.0f;                       check("detector has an effect", differs(q));
    q = base; q.rangeDb = 3.0f;                   check("range has an effect", differs(q));
    q = base; q.makeupDb = 9.0f;                  check("makeup has an effect", differs(q));
    q = base; q.softClip = 1.0f; q.makeupDb = 12.0f; check("soft clip has an effect", differs(q));
    q = base; q.mix = 40.0f;                      check("mix has an effect", differs(q));
}

static void testStabilityAndBounds()
{
    for (float sr : {44100.0f, 96000.0f, 192000.0f}) {
        EngineState st;
        std::vector<float> in(256), l(256), r(256);
        bool bad = false;
        double mx = 0;
        for (int blk = 0; blk < 600; ++blk) {
            Parameters p;
            p.thresholdDb = -60.0f + static_cast<float>((blk * 7) % 61);
            p.ratio = 1.1f + static_cast<float>((blk * 13) % 190) * 0.1f;
            p.kneeDb = static_cast<float>((blk * 5) % 25);
            p.attackMs = 0.1f + static_cast<float>((blk * 11) % 100);
            p.releaseMs = 20.0f + static_cast<float>((blk * 37) % 1980);
            p.autoRelease = static_cast<float>(blk % 2);
            p.scHpfHz = 20.0f + static_cast<float>((blk * 17) % 380);
            p.rms = static_cast<float>((blk / 3) % 2);
            p.rangeDb = 1.0f + static_cast<float>((blk * 3) % 40);
            p.makeupDb = static_cast<float>((blk * 5) % 25);
            for (size_t i = 0; i < in.size(); ++i) in[i] = ((blk + i) % 9 == 0) ? 3.0f : -0.7f;
            const float* ins[2] = {in.data(), in.data()};
            float* outs[2] = {l.data(), r.data()};
            processBlock(st, p, sr, 256, ins, outs);
            for (float v : l) {
                if (!std::isfinite(v)) bad = true;
                mx = std::max(mx, static_cast<double>(std::fabs(v)));
            }
        }
        check("no NaN or inf under random parameters and overload", !bad);
        check("output stays bounded", mx < 200.0);
    }
    // Non-finite input is treated as silence, not propagated.
    EngineState st;
    std::vector<float> in(64, std::nanf("")), l(64), r(64);
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {l.data(), r.data()};
    processBlock(st, Parameters{}, kSr, 64, ins, outs);
    bool finite = true;
    for (float v : l) if (!std::isfinite(v)) finite = false;
    check("NaN input does not poison the output", finite);
    processBlock(st, Parameters{}, kSr, 64, ins, outs);
    check("and the state stays usable afterwards", std::isfinite(st.gainDb));
}

static void testBlockSizeIndependence()
{
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    p.scHpfHz = 80.0f;
    p.rms = 1.0f;
    const auto in = sine(440.0, 0.7, 0.3);
    const Result a = run(p, in, in, 16);
    const Result b = run(p, in, in, 480);
    double worst = 0;
    for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
    check("block size does not change the output", worst < 1e-5);
}

static void testGainChangesAreSmoothed()
{
    EngineState st;
    Parameters p = plain();
    p.thresholdDb = 0.0f;
    const auto in = sine(200.0, 0.3, 0.05);
    std::vector<float> l(in.size()), r(in.size());
    const uint32_t half = static_cast<uint32_t>(in.size() / 2);
    const float* i1[2] = {in.data(), in.data()};
    float* o1[2] = {l.data(), r.data()};
    processBlock(st, p, kSr, half, i1, o1);
    p.makeupDb = 18.0f;
    p.mix = 10.0f;
    const float* i2[2] = {in.data() + half, in.data() + half};
    float* o2[2] = {l.data() + half, r.data() + half};
    processBlock(st, p, kSr, half, i2, o2);
    double worst = 0;
    for (size_t i = half - 2; i < half + 2; ++i) worst = std::max(worst, static_cast<double>(std::fabs(l[i + 1] - l[i])));
    check("makeup and mix changes do not step at the block edge", worst < 0.05);
}

static void testSilentDecayLeavesNoDenormals()
{
    EngineState st;
    Parameters p;
    p.rms = 1.0f;
    std::vector<float> loud = sine(300.0, 0.9, 0.2), silent(480, 0.0f), l(loud.size()), r(loud.size());
    const float* li[2] = {loud.data(), loud.data()};
    float* lo[2] = {l.data(), r.data()};
    processBlock(st, p, kSr, static_cast<uint32_t>(loud.size()), li, lo);
    for (int blk = 0; blk < 4000; ++blk) {
        const float* si[2] = {silent.data(), silent.data()};
        processBlock(st, p, kSr, 480, si, lo);
    }
    bool denormal = false;
    auto test = [&](float v) { if (v != 0.0f && std::fabs(v) < 1.17549435e-38f) denormal = true; };
    for (const Biquad& b : st.hpf) { test(b.z1); test(b.z2); }
    for (float m : st.meanSquare) test(m);
    test(st.gainDb);
    test(st.autoDepth);
    check("states are flushed to zero, never left denormal", !denormal);
}

static void testResetClearsMemoryOnly()
{
    EngineState st;
    Parameters p = plain();
    p.thresholdDb = -30.0f;
    const auto in = sine(500.0, 0.8, 0.2);
    run(p, in, in);
    const auto res = run(p, in, in);
    EngineState used = res.state;
    check("state is non-trivial after audio", used.gainDb < -1.0f);
    resetState(used);
    check("reset clears the gain and detector memory", used.gainDb == 0.0f && used.meanSquare[0] == 0.0f &&
          used.hpf[0].z1 == 0.0f && used.meters.reductionDb == 0.0f);
    check("reset leaves the parameters alone", p.thresholdDb == -30.0f);
}

// The host-facing table and the core must agree on every default and range.
static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    for (std::uint32_t i = 0; i < kControlCount; ++i) {
        const ParameterSpec& s = kParameterSpecs[i];
        check("controls are not outputs", !s.output);
        check("table default equals the core default", field(defaults, i) == s.defaultValue);
        check("default is inside its range", s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        field(probe, i) = s.minimum - 1000.0f;
        check("core clamps to the table minimum", field(clampParameters(probe), i) == s.minimum);
        field(probe, i) = s.maximum + 1000.0f;
        check("core clamps to the table maximum", field(clampParameters(probe), i) == s.maximum);
        field(probe, i) = s.defaultValue;
        check("a logarithmic range has a positive minimum", !s.logarithmic || s.minimum > 0.0f);
    }
    for (std::uint32_t i = kControlCount; i < kParameterCount; ++i)
        check("status entries are outputs", kParameterSpecs[i].output);
}

int main()
{
    testStaticCurveOnAudio();
    testSoftKnee();
    testRangeCap();
    testBallistics();
    testAutoRelease();
    testSidechainHighPass();
    testStereoLink();
    testRmsDetector();
    testMixMakeupAndSoftClip();
    testTransparentBelowThreshold();
    testMetersReportTheAudio();
    testEveryControlMatters();
    testStabilityAndBounds();
    testBlockSizeIndependence();
    testGainChangesAreSmoothed();
    testSilentDecayLeavesNoDenormals();
    testResetClearsMemoryOnly();
    testParameterTableMatchesCore();
    std::printf("glue_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
