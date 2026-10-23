#include "ladder_core.hpp"
#include "ladder_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::ladder;

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

// Runs a mono signal through both channels of a fresh engine in blocks of `block` frames.
static std::vector<float> run(const Parameters& p, const std::vector<float>& in, float sr = 48000.0f,
                              uint32_t block = 128)
{
    EngineState st;
    std::vector<float> out(in.size()), dummy(in.size());
    for (size_t pos = 0; pos < in.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, in.size() - pos));
        const float* ins[2]  = {in.data() + pos, in.data() + pos};
        float* outs[2]       = {out.data() + pos, dummy.data() + pos};
        processBlock(st, p, sr, n, ins, outs);
    }
    return out;
}

static std::vector<float> sine(double hz, double amp, float sr, double seconds)
{
    std::vector<float> v(static_cast<size_t>(sr * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * hz * i / sr));
    return v;
}

// Sine gain in dB measured by correlation over the second half of the run.
static double gainDb(const Parameters& p, double hz, double amp = 0.001, float sr = 48000.0f)
{
    const auto in = sine(hz, amp, sr, 0.6);
    const auto out = run(p, in, sr);
    double sc = 0, ss = 0;
    size_t n = 0;
    for (size_t i = out.size() / 2; i < out.size(); ++i) {
        const double ph = 2.0 * M_PI * hz * i / sr;
        sc += out[i] * std::cos(ph);
        ss += out[i] * std::sin(ph);
        ++n;
    }
    return 20.0 * std::log10(2.0 * std::hypot(sc, ss) / n / amp + 1e-12);
}

static Parameters flat()
{
    Parameters p;
    p.cutoffHz = 1000.0f;
    p.resonance = 0.0f;
    p.bassComp = 0.0f;
    return p;
}

// With no resonance the filter is four cascaded one-poles: |H| = (1 + (f/fc)^2)^-2.
static void testMatchesAnalyticResponse()
{
    for (double m : {0.1, 0.25, 0.5, 1.0, 2.0}) {
        const double ideal = -40.0 * std::log10(1.0 + m * m);
        const double got = gainDb(flat(), 1000.0 * m);
        check("response matches 4 cascaded one-poles at 48k", std::fabs(got - ideal) < 0.3);
    }
    for (float sr : {44100.0f, 96000.0f}) {
        const double got = gainDb(flat(), 1000.0, 0.001, sr);
        check("-12 dB at the cutoff at other sample rates", std::fabs(got + 12.04) < 0.4);
    }
}

// The taper claim: peak height rises steadily with the control, not all in the last tenth.
static void testResonancePeakTaper()
{
    // Measured with full bass compensation so the passband sits at 0 dB and the peak is
    // the height over it. Probed values: 10 % 2, 20 % 6, 30 % 10, 50 % 18, 70 % 28 dB.
    double last = -1000.0;
    double peakAt50 = 0.0, peakAt20 = 0.0;
    for (int pct = 10; pct <= 80; pct += 10) {
        Parameters p = flat();
        p.resonance = static_cast<float>(pct);
        p.bassComp = 100.0f;
        double best = -1000.0;
        for (double m = 0.3; m <= 1.3; m += 0.02) best = std::max(best, gainDb(p, 1000.0 * m));
        check("resonance peak rises by at least 1 dB per 10 % of the control", best > last + 1.0);
        if (pct == 50) peakAt50 = best;
        if (pct == 20) peakAt20 = best;
        last = best;
    }
    check("20 % resonance is already a clear peak (4-9 dB)", peakAt20 > 4.0 && peakAt20 < 9.0);
    check("50 % resonance peaks 12-24 dB over the passband", peakAt50 > 12.0 && peakAt50 < 24.0);
}

static void testSelfOscillation()
{
    for (float fc : {100.0f, 1000.0f, 8000.0f}) {
        Parameters p = flat();
        p.cutoffHz = fc;
        p.resonance = 100.0f;
        std::vector<float> in(48000, 0.0f);
        in[0] = 1.0f;
        const auto out = run(p, in);
        double rms = 0;
        int crossings = 0;
        for (size_t i = 24000; i < out.size(); ++i) {
            rms += out[i] * out[i];
            if (out[i - 1] <= 0.0f && out[i] > 0.0f) ++crossings;
        }
        rms = std::sqrt(rms / 24000.0);
        const double hz = crossings * 2.0;
        check("self-oscillation is sustained", rms > 0.01);
        check("self-oscillation is bounded", rms < 2.0);
        check("self-oscillation sits at the cutoff", std::fabs(hz - fc) < 0.03 * fc + 4.0);
    }
    // And a moderate setting rings out instead.
    Parameters p = flat();
    p.resonance = 30.0f;
    std::vector<float> in(48000, 0.0f);
    in[0] = 1.0f;
    const auto out = run(p, in);
    double tail = 0;
    for (size_t i = 40000; i < out.size(); ++i) tail = std::max(tail, static_cast<double>(std::fabs(out[i])));
    check("30 % resonance decays", tail < 1e-3);
}

static void testBassCompensation()
{
    Parameters p = flat();
    p.resonance = 50.0f;
    p.bassComp = 0.0f;
    const double lost = gainDb(p, 50.0);
    check("resonance removes bass without compensation", lost < -6.0);
    p.bassComp = 100.0f;
    const double kept = gainDb(p, 50.0);
    check("full compensation restores the passband to within 1 dB", std::fabs(kept) < 1.0);
    p.bassComp = 50.0f;
    const double half = gainDb(p, 50.0);
    check("half compensation lies between", half > lost && half < kept);
}

// The panel plots responseDb(), so it must agree with what the filter really does.
static void testPlottedResponseMatchesMeasuredFilter()
{
    struct Case { float res, comp, mix, tol; };
    const Case cases[] = {{0, 0, 100, 0.4f}, {30, 50, 100, 0.8f}, {50, 100, 100, 1.2f},
                          {50, 0, 60, 1.0f}, {65, 100, 100, 2.5f}};
    for (const Case& c : cases) {
        Parameters p = flat();
        p.resonance = c.res;
        p.bassComp = c.comp;
        p.mix = c.mix;
        double worst = 0;
        for (double hz : {60.0, 200.0, 500.0, 800.0, 1000.0, 1500.0, 2500.0, 4000.0})
            worst = std::max(worst, std::fabs(responseDb(p, hz) - gainDb(p, hz)));
        if (worst >= c.tol) std::printf("  response mismatch %.2f dB at res %.0f comp %.0f mix %.0f\n", worst, c.res, c.comp, c.mix);
        check("plotted response matches the measured filter", worst < c.tol);
    }
    Parameters q = flat();
    q.outputDb = -6.0f;
    check("plotted response includes output gain", std::fabs(responseDb(q, 100.0) - (responseDb(flat(), 100.0) - 6.0)) < 1e-6);
}

// Changing gain-type controls at a block edge must glide, not step.
static void testGainChangesAreSmoothed()
{
    EngineState st;
    Parameters p = flat();
    p.cutoffHz = 18000.0f;
    std::vector<float> in = sine(200.0, 0.5, 48000.0f, 0.05), a(in.size()), b(in.size());
    const uint32_t half = static_cast<uint32_t>(in.size() / 2);
    const float* i1[2] = {in.data(), in.data()};
    float* o1[2] = {a.data(), b.data()};
    processBlock(st, p, 48000.0f, half, i1, o1);
    p.outputDb = -24.0f;
    p.mix = 20.0f;
    const float* i2[2] = {in.data() + half, in.data() + half};
    float* o2[2] = {a.data() + half, b.data() + half};
    processBlock(st, p, 48000.0f, half, i2, o2);
    double worst = 0;
    for (size_t i = half - 2; i < half + 2; ++i) worst = std::max(worst, static_cast<double>(std::fabs(a[i + 1] - a[i])));
    // A 200 Hz sine at 0.5 moves at most ~0.013 per sample; a stepped gain would jump by ~0.4.
    check("gain changes do not step at the block edge", worst < 0.05);
}

static void testStabilityUnderSweepsAndOverload()
{
    for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
        EngineState st;
        std::vector<float> in(256), l(256), r(256);
        double mx = 0;
        bool bad = false;
        for (int blk = 0; blk < 800; ++blk) {
            Parameters p;
            p.cutoffHz = 20.0f + static_cast<float>((blk * 997) % 17980);
            p.resonance = (blk / 7) % 2 ? 100.0f : 0.0f;
            p.driveDb = (blk % 5) * 6.0f;
            for (size_t i = 0; i < in.size(); ++i) in[i] = ((blk + i) % 11 == 0) ? 4.0f : -1.5f;
            const float* ins[2] = {in.data(), in.data()};
            float* outs[2] = {l.data(), r.data()};
            processBlock(st, p, sr, 256, ins, outs);
            for (float v : l) {
                if (!std::isfinite(v)) bad = true;
                mx = std::max(mx, static_cast<double>(std::fabs(v)));
            }
        }
        check("no NaN or inf under random sweeps and overload", !bad);
        check("output stays bounded under overload", mx < 200.0);
    }
}

static void testBlockSizeIndependence()
{
    Parameters p;
    p.resonance = 70.0f;
    const auto in = sine(440.0, 0.5, 48000.0f, 0.2);
    // Coefficients glide per block, so with constant parameters every block size must agree.
    const auto a = run(p, in, 48000.0f, 16);
    const auto b = run(p, in, 48000.0f, 480);
    double worst = 0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a[i] - b[i])));
    check("block size does not change the output", worst < 1e-5);
}

static void testMixZeroIsDryAndDefaultsAreAudible()
{
    Parameters p;
    p.mix = 0.0f;
    const auto in = sine(3000.0, 0.5, 48000.0f, 0.1);
    const auto out = run(p, in);
    double worst = 0;
    for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(out[i] - in[i])));
    check("mix 0 returns the dry signal", worst < 1e-6);
    p.mix = 100.0f;
    const auto wet = run(p, in);
    double diff = 0;
    for (size_t i = 0; i < in.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(wet[i] - in[i])));
    check("default settings filter 3 kHz audibly", diff > 0.1);
}

// Every control must have an observable effect on the output.
static void testEveryControlMatters()
{
    const auto in = sine(900.0, 0.3, 48000.0f, 0.2);
    Parameters base;
    base.resonance = 60.0f;
    base.mix = 80.0f;
    const auto ref = run(base, in);
    auto differs = [&](Parameters q) {
        const auto o = run(q, in);
        double d = 0;
        for (size_t i = 0; i < o.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(o[i] - ref[i])));
        return d > 1e-3;
    };
    Parameters q = base; q.cutoffHz = 3000.0f;  check("cutoff has an effect", differs(q));
    q = base; q.resonance = 90.0f;               check("resonance has an effect", differs(q));
    q = base; q.driveDb = 18.0f;                 check("drive has an effect", differs(q));
    q = base; q.bassComp = 0.0f;                 check("bass compensation has an effect", differs(q));
    q = base; q.mix = 30.0f;                     check("mix has an effect", differs(q));
    q = base; q.outputDb = -12.0f;               check("output has an effect", differs(q));
}

static void testSilentDecayLeavesNoDenormals()
{
    EngineState st;
    Parameters p;
    p.resonance = 40.0f;
    std::vector<float> loud(4800, 0.8f), silent(480, 0.0f), l(4800), r(4800);
    const float* li[2] = {loud.data(), loud.data()};
    float* lo[2] = {l.data(), r.data()};
    processBlock(st, p, 48000.0f, 4800, li, lo);
    for (int blk = 0; blk < 4000; ++blk) {
        const float* si[2] = {silent.data(), silent.data()};
        processBlock(st, p, 48000.0f, 480, si, lo);
    }
    bool denormal = false;
    for (const auto& c : st.channels)
        for (float v : {c.s1, c.s2, c.s3, c.s4, c.prevInput})
            if (v != 0.0f && std::fabs(v) < 1.17549435e-38f) denormal = true;
    check("states are flushed to zero, never left denormal", !denormal);
}

static void testResetClearsMemoryOnly()
{
    EngineState st;
    Parameters p;
    p.resonance = 80.0f;
    const auto in = sine(500.0, 0.5, 48000.0f, 0.05);
    std::vector<float> a(in.size()), b(in.size());
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {a.data(), b.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(in.size()), ins, outs);
    resetState(st);
    check("reset zeroes the memory", st.channels[0].s4 == 0.0f && st.channels[1].s1 == 0.0f);
    check("reset leaves the parameters alone", p.resonance == 80.0f);
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
        // The core must accept exactly the table's range: neither narrower nor wider.
        field(probe, i) = s.minimum - 1000.0f;
        check("core clamps to the table minimum", field(clampParameters(probe), i) == s.minimum);
        field(probe, i) = s.maximum + 1000.0f;
        check("core clamps to the table maximum", field(clampParameters(probe), i) == s.maximum);
        field(probe, i) = s.defaultValue;
        check("a logarithmic range has a positive minimum", !s.logarithmic || s.minimum > 0.0f);
    }
}

// MIDI control: the Drift convention, CC 1-4 on a channel.
static void testMidiControl()
{
    check("CC 0 and 127 reach the ends of every mapping",
          ccToValue(0, 0) == 20.0f && std::fabs(ccToValue(0, 127) - 18000.0f) < 0.5f && ccToValue(1, 0) == 0.0f && ccToValue(1, 127) == 100.0f &&
          ccToValue(2, 127) == 24.0f && ccToValue(3, 0) == 0.0f && ccToValue(3, 127) == 100.0f);
    check("the cutoff mapping is logarithmic (the middle CC is the geometric mean)", std::fabs(ccToValue(0, 64) / std::sqrt(20.0f * 18000.0f) - 1.0f) < 0.03f);
    bool monotonic = true;
    for (int slot = 0; slot < kCcSlots; ++slot)
        for (int cc = 1; cc <= 127; ++cc) if (ccToValue(slot, cc) < ccToValue(slot, cc - 1)) monotonic = false;
    check("every mapping rises with the controller", monotonic);

    check("a Control Change on the set channel matches", ccChannelMatches(0xB0, 1.0f) && ccChannelMatches(0xB4, 5.0f) && ccChannelMatches(0xBF, 16.0f));
    check("another channel, another message type, or channel 0 does not", !ccChannelMatches(0xB1, 1.0f) && !ccChannelMatches(0x90, 1.0f) && !ccChannelMatches(0xB0, 0.0f) && !ccChannelMatches(0xE0, 1.0f));

    LiveControl live;
    check("nothing overrides at first", withLive(Parameters{}, live).cutoffHz == Parameters{}.cutoffHz);
    handleController(live, 5, 100);
    handleController(live, 0, 100);
    handleController(live, 74, 100);
    check("other controllers are ignored", live.value[0] < 0.0f && live.value[1] < 0.0f && live.value[2] < 0.0f && live.value[3] < 0.0f);
    handleController(live, 1, 127);
    handleController(live, 2, 64);
    handleController(live, 3, 127);
    handleController(live, 4, 0);
    const Parameters p = withLive(Parameters{}, live);
    check("CC 1-4 override cutoff, resonance, drive and mix", std::fabs(p.cutoffHz - 18000.0f) < 0.5f && std::fabs(p.resonance - ccToValue(1, 64)) < 1e-4f &&
          p.driveDb == 24.0f && p.mix == 0.0f);
    check("the controls a controller does not own are untouched", p.bassComp == Parameters{}.bassComp && p.outputDb == Parameters{}.outputDb);
    // The audio follows: a CC that closes the filter makes the output quieter than the panel's setting.
    Parameters base;
    base.resonance = 0.0f; base.bassComp = 0.0f;
    LiveControl closed;
    handleController(closed, 1, 10);
    check("the override changes the sound", gainDb(withLive(base, closed), 4000.0) < gainDb(base, 4000.0) - 20.0);
    check("liveSlot names the controls a panel move must release", liveSlot(kCutoff) == 0 && liveSlot(kResonance) == 1 && liveSlot(kDrive) == 2 &&
          liveSlot(kMix) == 3 && liveSlot(kBassComp) == -1 && liveSlot(kOutput) == -1 && liveSlot(kCcChannel) == -1);
    check("the CC channel is clamped to 0-16", clampParameters([] { Parameters q; q.ccChannel = 99.0f; return q; }()).ccChannel == 16.0f &&
          clampParameters([] { Parameters q; q.ccChannel = -3.0f; return q; }()).ccChannel == 0.0f);
}

int main()
{
    testMatchesAnalyticResponse();
    testResonancePeakTaper();
    testSelfOscillation();
    testBassCompensation();
    testPlottedResponseMatchesMeasuredFilter();
    testGainChangesAreSmoothed();
    testStabilityUnderSweepsAndOverload();
    testBlockSizeIndependence();
    testMixZeroIsDryAndDefaultsAreAudible();
    testEveryControlMatters();
    testSilentDecayLeavesNoDenormals();
    testResetClearsMemoryOnly();
    testMidiControl();
    testParameterTableMatchesCore();
    std::printf("ladder_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
