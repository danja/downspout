#include "downspout/test_assert.h"
#include "silken_core.hpp"
#include "silken_params.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace downspout::silken;

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

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

using Signal = std::function<float(std::size_t)>;

static Signal tone(double f, float amp, double sr = kSr)
{
    return [=](std::size_t n) { return amp * static_cast<float>(std::sin(2.0 * kPi * f * static_cast<double>(n) / sr)); };
}

static Signal sum(std::vector<Signal> parts)
{
    return [=](std::size_t n) { float v = 0.0f; for (const auto& p : parts) v += p(n); return v; };
}

static Signal noise(std::uint32_t seed, float amp)
{
    return [=](std::size_t n) {
        std::uint32_t x = static_cast<std::uint32_t>(n) * 2654435761u + seed;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
        return amp * static_cast<float>(static_cast<std::int32_t>(x)) * (1.0f / 2147483648.0f);
    };
}

static Signal silence() { return [](std::size_t) { return 0.0f; }; }

struct Run {
    EngineState state;
    std::vector<float> inL, inR, l, r;
    std::size_t done = 0;
    std::size_t block = 512;
    double sr = kSr;

    void go(const Parameters& p, std::size_t frames, const Signal& sl, const Signal& sr_)
    {
        const std::size_t end = done + frames;
        while (done < end) {
            const std::size_t n = std::min(block, end - done);
            std::vector<float> a(n), b(n), oa(n), ob(n);
            for (std::size_t i = 0; i < n; ++i) { a[i] = sl(done + i); b[i] = sr_(done + i); }
            processBlock(state, p, static_cast<float>(sr), static_cast<std::uint32_t>(n), a.data(), b.data(), oa.data(), ob.data());
            inL.insert(inL.end(), a.begin(), a.end()); inR.insert(inR.end(), b.begin(), b.end());
            l.insert(l.end(), oa.begin(), oa.end()); r.insert(r.end(), ob.begin(), ob.end());
            done += n;
        }
    }
};

static Run render(const Parameters& p, std::size_t frames, const Signal& sl, const Signal& sr, std::size_t block = 512, double rate = kSr)
{
    Run run;
    run.block = block;
    run.sr = rate;
    run.go(p, frames, sl, sr);
    return run;
}

static Parameters neutral()
{
    Parameters p;
    p.hpfOn = 0; p.plosiveOn = 0; p.essOn = 0; p.warmthDb = 0; p.presenceDb = 0; p.airDb = 0; p.sparkle = 0; p.outputDb = 0;
    return p;
}

static bool identical(const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

static double maxDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::abs(static_cast<double>(a[i]) - b[i]));
    return m;
}

// Amplitude of the component at `f` (Hann-windowed, exact frequency) over [from, to).
static double amplitude(const std::vector<float>& v, double f, std::size_t from, std::size_t to, double rate = kSr)
{
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    const std::size_t n = to - from;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n));
        acc += w * static_cast<double>(v[from + i]) * std::polar(1.0, -2.0 * kPi * f * static_cast<double>(from + i) / rate);
        wsum += w;
    }
    return 2.0 * std::abs(acc) / wsum;
}

static double toDb(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-12)); }

// Level change (dB) of a tone through the strip, measured over the last half second of one second.
static double toneGainDb(const Parameters& p, double f, float amp, double rate = kSr)
{
    const auto frames = static_cast<std::size_t>(rate);
    const Run run = render(p, frames, tone(f, amp, rate), tone(f, amp, rate), 512, rate);
    return toDb(amplitude(run.l, f, frames / 2, frames, rate) / amp);
}

// Level change (dB) of the component at `f` in a signal made of several tones.
static double componentGainDb(const Parameters& p, const Signal& in, double f, float amp)
{
    const Run run = render(p, 48000, in, in);
    return toDb(amplitude(run.l, f, 24000, 48000) / amp);
}

static void testNeutral()
{
    const Run run = render(neutral(), 30000, noise(1, 0.5f), noise(2, 0.5f));
    check("every stage off: the output is the input, sample for sample", identical(run.l, run.inL) && identical(run.r, run.inR));
    for (const float db : {-12.0f, -3.0f, 6.0f, 12.0f}) {
        Parameters p = neutral(); p.outputDb = db;
        const Run o = render(p, 30000, noise(1, 0.5f), noise(2, 0.5f));
        const double gain = std::pow(10.0, static_cast<double>(db) / 20.0);
        double worst = 0.0;
        for (std::size_t i = 0; i < o.l.size(); ++i) worst = std::max(worst, std::abs(o.l[i] - o.inL[i] * gain));
        check("output gain is exact", worst < 2e-6);
    }
    // The defaults leave a quiet vowel-range signal alone (the rumble filter, the plosive guard and the de-esser have nothing to do).
    const Parameters d;
    check("defaults leave a quiet 1 kHz tone alone", std::abs(toneGainDb(d, 1000, 0.05f)) < 0.05);
    check("defaults leave a quiet 200 Hz tone alone", std::abs(toneGainDb(d, 200, 0.05f)) < 0.25);
    check("defaults leave a loud 1 kHz tone alone", std::abs(toneGainDb(d, 1000, 0.5f)) < 0.1);
    check("defaults leave a loud 200 Hz vowel alone", std::abs(toneGainDb(d, 200, 0.1f)) < 0.1 + 0.9);
}

static void testRumbleFilter()
{
    // 2nd-order Butterworth: |H| = 1 / sqrt(1 + (fc / f)^4).
    for (const float fc : {40.0f, 80.0f, 150.0f, 300.0f}) {
        for (const double f : {20.0, 40.0, 80.0, 160.0, 500.0, 2000.0}) {
            Parameters p = neutral(); p.hpfOn = 1; p.hpfHz = fc;
            const double expect = -10.0 * std::log10(1.0 + std::pow(static_cast<double>(fc) / f, 4.0));
            check("rumble filter follows the Butterworth curve", std::abs(toneGainDb(p, f, 0.1f) - expect) < 0.35);
        }
    }
    Parameters p = neutral(); p.hpfOn = 1; p.hpfHz = 80;
    check("rumble filter is -3 dB at its frequency", std::abs(toneGainDb(p, 80, 0.1f) + 3.01) < 0.2);
    p.hpfOn = 0;
    check("rumble filter off passes 20 Hz", std::abs(toneGainDb(p, 20, 0.1f)) < 0.01);
    // The curve is the same at other sample rates.
    p.hpfOn = 1;
    for (const double rate : {44100.0, 96000.0, 192000.0})
        check("rumble filter follows the curve at other sample rates", std::abs(toneGainDb(p, 40, 0.1f, rate) - (-10.0 * std::log10(17.0))) < 0.35);
}

static void testPlosiveGuard()
{
    const auto base = [] { Parameters p = neutral(); p.plosiveOn = 1; p.plosiveThrDb = -20.0f; return p; };
    // A loud 60 Hz thump: the detector sees 0.8 through the 150 Hz low-pass (|H| 0.9872): -2.05 dB, 17.95 dB over, 5/6 of that is the reduction.
    // The signal is cross-faded to the 150 Hz high-pass by (1 - g), so at 60 Hz the output is (g + (1-g) Hhp).
    {
        const double r = 60.0 / 150.0;
        const std::complex<double> hp = std::complex<double>(-r * r, 0.0) / std::complex<double>(1.0 - r * r, std::sqrt(2.0) * r);
        const double gr = (toDb(0.8 * 0.9872) * -1.0 * -1.0 + 20.0) * (5.0 / 6.0);
        const double g = std::pow(10.0, -gr / 20.0);
        const double expectDb = toDb(std::abs(g + (1.0 - g) * hp));
        const Run run = render(base(), 24000, tone(60, 0.8f), tone(60, 0.8f));
        const double got = toDb(amplitude(run.l, 60, 12000, 24000) / 0.8);
        std::printf("  plosive: 60 Hz thump %.2f dB (expected %.2f), meter %.2f dB\n", got, expectDb, static_cast<double>(run.state.meters.plosiveReductionDb));
        check("plosive guard takes the thump down by the predicted amount", std::abs(got - expectDb) < 1.5 && got < -12.0);
        check("the meter reports the guard's gain reduction", std::abs(run.state.meters.plosiveReductionDb - gr) < 1.0);
    }
    check("plosive guard leaves a loud 1 kHz tone alone", std::abs(toneGainDb(base(), 1000, 0.5f)) < 0.1);
    check("plosive guard leaves a 200 Hz vowel alone", std::abs(toneGainDb(base(), 200, 0.1f)) < 0.1);
    check("plosive guard leaves a quiet low tone alone", std::abs(toneGainDb(base(), 60, 0.02f)) < 0.1);
    {
        Parameters off = base(); off.plosiveOn = 0;
        check("plosive guard off lets the thump through", std::abs(toneGainDb(off, 60, 0.8f)) < 0.01);
    }
    // Threshold: a higher threshold takes less off.
    {
        double last = 1e9;
        // (Below about -30 the guard is at its 24 dB limit and the cross-fade's phase makes the result wander by a couple of dB, so the sweep starts there.)
        for (const float thr : {-30.0f, -20.0f, -10.0f, 0.0f}) {
            Parameters p = base(); p.plosiveThrDb = thr;
            const double g = toneGainDb(p, 60, 0.5f);
            check("a higher plosive threshold takes less off", g > -90.0 && (last > 1e8 || g >= last - 0.01));
            last = g;
        }
    }
    // Release: after the burst the gain comes back (60 ms).
    {
        Run run;
        run.go(base(), 24000, tone(60, 0.8f), tone(60, 0.8f));
        check("during the thump the guard is working", run.state.meters.plosiveReductionDb > 10.0f);
        run.go(base(), 28800, silence(), silence());
        check("0.6 s after the thump the guard has let go", run.state.meters.plosiveReductionDb < 0.5f);
    }
}

static void testDeEsser()
{
    const auto base = [] { Parameters p = neutral(); p.essOn = 1; p.essFreqHz = 6500; p.essThrDb = -12; p.essRangeDb = 24; return p; };
    // 6.5 kHz at 0.5 is -6.02 dB at the detector (unity at its centre): 5.98 dB over, x 3/4 = 4.49 dB.
    {
        const Run run = render(base(), 48000, tone(6500, 0.5f), tone(6500, 0.5f));
        const double g = toDb(amplitude(run.l, 6500, 24000, 48000) / 0.5);
        std::printf("  de-esser: %.2f dB (expected -4.49), meter %.2f, detector %.2f dB\n", g, static_cast<double>(run.state.meters.essReductionDb), static_cast<double>(run.state.meters.detectorDb));
        check("de-esser takes the sibilance down by the predicted amount", std::abs(g + 4.49) < 0.8);
        check("the meter reports the reduction", std::abs(run.state.meters.essReductionDb - 4.49) < 0.8);
        check("the detector level is the band level", std::abs(run.state.meters.detectorDb + 6.02) < 1.0);
    }
    // Range caps the reduction.
    for (const float range : {3.0f, 6.0f, 12.0f}) {
        Parameters p = base(); p.essThrDb = -40; p.essRangeDb = range;
        const double g = toneGainDb(p, 6500, 0.5f);
        check("range caps the reduction", std::abs(g + static_cast<double>(range)) < 0.6);
    }
    {
        Parameters p = base(); p.essRangeDb = 0;
        check("range 0 does nothing", std::abs(toneGainDb(p, 6500, 0.5f)) < 0.01);
    }
    // A lower threshold takes more off.
    {
        double last = 1.0;
        for (const float thr : {0.0f, -12.0f, -20.0f, -30.0f, -40.0f}) {
            Parameters p = base(); p.essThrDb = thr;
            const double g = toneGainDb(p, 6500, 0.5f);
            check("a lower de-esser threshold takes more off", g <= last + 0.01);
            last = g;
        }
    }
    // It follows the frequency, and ignores the rest of the voice.
    {
        Parameters p = base(); p.essFreqHz = 4000;
        check("de-esser reduces at its frequency (4 kHz)", toneGainDb(p, 4000, 0.5f) < -3.0);
        check("and not at a distant sibilance (9 kHz)", std::abs(toneGainDb(p, 9000, 0.5f)) < 0.5);
        p.essFreqHz = 9000;
        check("moving the frequency moves the reduction", toneGainDb(p, 9000, 0.5f) < -3.0 && std::abs(toneGainDb(p, 4000, 0.5f)) < 0.5);
        check("a loud 1 kHz vowel does not trigger it", std::abs(toneGainDb(base(), 1000, 0.5f)) < 0.3);
        check("a loud 200 Hz vowel does not trigger it", std::abs(toneGainDb(base(), 200, 0.5f)) < 0.05);
        // The detector is two band-passes deep, so a loud vowel formant at 1 kHz or 2 kHz does not even register (3.5 kHz is near enough to count) (the meter, not just the output).
        Parameters q = base(); q.essThrDb = -30; q.essRangeDb = 10;
        for (const double f : {1000.0, 2000.0}) {
            const Run run = render(q, 24000, tone(f, 0.5f), tone(f, 0.5f));
            check("a loud vowel formant does not move the de-esser's meter", run.state.meters.essReductionDb < 0.1f);
        }
    }
    // Band mode cuts only around the frequency; Wide mode turns everything down.
    {
        const Signal in = sum({tone(200, 0.2f), tone(6500, 0.5f)});
        const Parameters b = base();
        Parameters w = base(); w.essMode = 1;
        const double bandBody = componentGainDb(b, in, 200, 0.2f), bandEss = componentGainDb(b, in, 6500, 0.5f);
        const double wideBody = componentGainDb(w, in, 200, 0.2f), wideEss = componentGainDb(w, in, 6500, 0.5f);
        std::printf("  band: body %.2f ess %.2f; wide: body %.2f ess %.2f\n", bandBody, bandEss, wideBody, wideEss);
        check("Band mode leaves the body of the voice alone", std::abs(bandBody) < 0.1);
        check("Band mode reduces the sibilance", bandEss < -3.0);
        check("Wide mode turns the whole voice down", wideBody < -3.0 && std::abs(wideBody - wideEss) < 0.3);
    }
    // Listen plays the detector.
    {
        Parameters p = base(); p.essListen = 1;
        check("Listen passes the sibilance band", std::abs(toneGainDb(p, 6500, 0.3f)) < 0.3);
        check("Listen drops the body (500 Hz)", toneGainDb(p, 500, 0.3f) < -25.0);
        check("Listen drops the low end (100 Hz)", toneGainDb(p, 100, 0.3f) < -40.0);
        p.warmthDb = 12; p.airDb = 12; p.sparkle = 1; p.outputDb = 0;
        check("Listen is the detector alone: the EQ and sparkle are not in it", std::abs(toneGainDb(p, 6500, 0.3f)) < 0.3);
    }
    // Linked: a quiet right channel is reduced with the left.
    {
        Run run = render(base(), 48000, tone(6500, 0.5f), tone(6500, 0.05f));
        const double gl = toDb(amplitude(run.l, 6500, 24000, 48000) / 0.5), gr = toDb(amplitude(run.r, 6500, 24000, 48000) / 0.05);
        check("the two channels are reduced together", std::abs(gl - gr) < 0.1 && gl < -3.0);
        Run swapped = render(base(), 48000, tone(6500, 0.05f), tone(6500, 0.5f));
        const double sl = toDb(amplitude(swapped.l, 6500, 24000, 48000) / 0.05), sr = toDb(amplitude(swapped.r, 6500, 24000, 48000) / 0.5);
        check("whichever channel is loud drives both", std::abs(sl - sr) < 0.1 && sr < -3.0);
    }
    // Attack and release.
    {
        Parameters p = base(); p.essThrDb = -30;
        Run run;
        run.go(p, 4800, silence(), silence());
        run.go(p, 96, tone(6500, 0.5f), tone(6500, 0.5f));   // 2 ms
        const float soon = run.state.meters.essReductionDb;
        run.go(p, 9600, tone(6500, 0.5f), tone(6500, 0.5f));
        const float full = run.state.meters.essReductionDb;
        check("the de-esser acts within a couple of milliseconds", soon > 0.6f * full && full > 5.0f);
        run.go(p, 14400, silence(), silence());
        check("and lets go in a fraction of a second", run.state.meters.essReductionDb < 0.3f);
    }
    {
        Parameters off = base(); off.essOn = 0;
        check("de-esser off does nothing", std::abs(toneGainDb(off, 6500, 0.5f)) < 0.01);
    }
}

static void testVoiceEq()
{
    for (const float db : {-9.0f, 6.0f, 12.0f}) {
        Parameters p = neutral(); p.warmthDb = db;
        check("warmth is the full shelf gain at 40 Hz", std::abs(toneGainDb(p, 40, 0.1f) - static_cast<double>(db)) < 0.6);
        check("warmth leaves 5 kHz alone", std::abs(toneGainDb(p, 5000, 0.1f)) < 0.1);
        p = neutral(); p.presenceDb = db;
        check("presence is its gain at 3.2 kHz", std::abs(toneGainDb(p, 3200, 0.1f) - static_cast<double>(db)) < 0.3);
        check("presence leaves 100 Hz alone", std::abs(toneGainDb(p, 100, 0.1f)) < 0.2);
        check("presence leaves 12 kHz mostly alone", std::abs(toneGainDb(p, 12000, 0.1f)) < 1.0);
        p = neutral(); p.airDb = db;
        check("air is the shelf gain at 18 kHz", std::abs(toneGainDb(p, 18000, 0.1f) - static_cast<double>(db)) < 1.0);
        check("air leaves 1 kHz alone", std::abs(toneGainDb(p, 1000, 0.1f)) < 0.2);
    }
    // A switch on mid-signal does not click: the gain glides.
    {
        Parameters p = neutral();
        Run run;
        run.go(p, 24000, tone(1000, 0.3f), tone(1000, 0.3f));
        p.presenceDb = 6; p.warmthDb = 6; p.airDb = 6;
        run.go(p, 9600, tone(1000, 0.3f), tone(1000, 0.3f));
        double worst = 0.0;
        for (std::size_t i = 24001; i < run.l.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(run.l[i] - run.l[i - 1])));
        check("an EQ change glides, no click", worst < 0.3 * 2.0 * kPi * 1000.0 / kSr * 1.5);
    }
}

static void testSparkle()
{
    const auto harmonic = [](float amount, double f) {
        Parameters p = neutral(); p.sparkle = amount;
        const Run run = render(p, 48000, tone(f, 0.3f), tone(f, 0.3f));
        return amplitude(run.l, 3.0 * f, 24000, 48000);
    };
    const double off = harmonic(0.0f, 5500), full = harmonic(1.0f, 5500), half = harmonic(0.5f, 5500);
    std::printf("  sparkle: 3rd harmonic off %.2e, half %.4f, full %.4f\n", off, half, full);
    check("sparkle 0 adds no harmonics", off < 1e-4);
    check("sparkle adds harmonics above the voice", full > 0.005);
    check("sparkle is linear in its amount", std::abs(half / full - 0.5) < 0.08);
    {
        Parameters p = neutral(); p.sparkle = 1.0f;
        check("sparkle leaves low tones alone", std::abs(toneGainDb(p, 500, 0.3f)) < 0.05);
        const Run run = render(p, 48000, tone(500, 0.3f), tone(500, 0.3f));
        check("and adds nothing to them", amplitude(run.l, 1500, 24000, 48000) < 1e-4);
    }
}

static const Signal kVoiceLike = sum({tone(60, 0.5f), tone(200, 0.1f), tone(1000, 0.1f), tone(6500, 0.3f), tone(12000, 0.05f)});

static void testGate()
{
    const auto base = [] { Parameters p = neutral(); p.gateOn = 1; p.gateThrDb = -40; p.gateRangeDb = 20; return p; };
    // Far below the threshold the expander wants more than the range: the range is what it takes off.
    check("below the threshold the gate closes by Range", std::abs(toneGainDb(base(), 1000, 0.001f) + 20.0) < 1.0);
    check("a quiet tone is closed by less Range: 8 dB", [] { Parameters p = neutral(); p.gateOn = 1; p.gateThrDb = -40; p.gateRangeDb = 8; return std::abs(toneGainDb(p, 1000, 0.001f) + 8.0) < 0.6; }());
    check("above the threshold the gate is open", std::abs(toneGainDb(base(), 1000, 0.1f)) < 0.05);
    // 2:1: 10 dB under the threshold (level -50 dB) the expander has added 10 dB.
    check("the expander is 2:1 just under the threshold", [] {
        Parameters p = neutral(); p.gateOn = 1; p.gateThrDb = -40; p.gateRangeDb = 40;
        const double g = toneGainDb(p, 1000, static_cast<float>(std::pow(10.0, -50.0 / 20.0)));
        return std::abs(g + 10.0) < 1.2;
    }());
    { Parameters p = base(); p.gateOn = 0; check("gate off is exact", identical(render(p, 20000, tone(1000, 0.001f), tone(1000, 0.001f)).l, render(neutral(), 20000, tone(1000, 0.001f), tone(1000, 0.001f)).l)); }
    { Parameters p = base(); p.gateRangeDb = 0; check("range 0 does nothing", std::abs(toneGainDb(p, 1000, 0.001f)) < 0.01); }
    // After a burst the gate closes slowly (the envelope and the gain both take tens of milliseconds); opening is fast.
    {
        Run run;
        run.go(base(), 24000, tone(1000, 0.2f), tone(1000, 0.2f));
        check("open while the signal is loud", run.state.meters.gateReductionDb < 0.1f);
        run.go(base(), 480, silence(), silence());   // 10 ms
        check("barely closed 10 ms after the signal drops", run.state.meters.gateReductionDb < 3.0f);
        run.go(base(), 24000, silence(), silence());
        check("closed half a second later", run.state.meters.gateReductionDb > 19.0f);
        run.go(base(), 192, tone(1000, 0.2f), tone(1000, 0.2f));   // 4 ms
        check("opens within a few milliseconds", run.state.meters.gateReductionDb < 5.0f);
    }
    // Linked: a loud right channel keeps the left open.
    {
        const Run run = render(base(), 48000, tone(1000, 0.001f), tone(1000, 0.2f));
        check("the louder channel holds both open", toDb(amplitude(run.l, 1000, 24000, 48000) / 0.001) > -0.5);
    }
}

static void testTame()
{
    const auto base = [] { Parameters p = neutral(); p.tameFreqHz = 3000; p.tameThrDb = -12; p.tameAmountDb = 12; return p; };
    // 3 kHz at 0.5 is -6.02 dB at the detector: 5.98 dB over, x 2/3 (3:1) = 3.99 dB.
    check("harshness tamer takes the predicted amount off", std::abs(toneGainDb(base(), 3000, 0.5f) + 3.99) < 0.8);
    { Parameters p = base(); p.tameThrDb = -40; check("Amount caps the reduction", std::abs(toneGainDb(p, 3000, 0.5f) + 12.0) < 0.6); }
    { Parameters p = base(); p.tameAmountDb = 0; check("Amount 0 is off", identical(render(p, 20000, tone(3000, 0.5f), tone(3000, 0.5f)).l, render(neutral(), 20000, tone(3000, 0.5f), tone(3000, 0.5f)).l)); }
    { Parameters p = base(); p.tameFreqHz = 1800; check("Frequency moves it", toneGainDb(p, 1800, 0.5f) < -3.0 && std::abs(toneGainDb(p, 3000, 0.5f)) < 0.5); }
    check("it leaves a 200 Hz vowel alone", std::abs(toneGainDb(base(), 200, 0.5f)) < 0.05);
    check("and the de-esser's band alone", std::abs(toneGainDb(base(), 7000, 0.5f)) < 0.4);
    // It cuts only around the frequency.
    {
        const Signal in = sum({tone(200, 0.2f), tone(3000, 0.5f)});
        check("the tamer leaves the body of the voice alone", std::abs(componentGainDb(base(), in, 200, 0.2f)) < 0.1);
        check("and reduces the harshness", componentGainDb(base(), in, 3000, 0.5f) < -3.0);
    }
}

static void testLeveller()
{
    const auto base = [] { Parameters p = neutral(); p.levelOn = 1; p.levelThrDb = -20; p.levelRatio = 3; p.levelMix = 1; return p; };
    // Make-up is half the reduction at full scale: 20 dB x 2/3 x 0.5 = 6.67 dB.
    // A -40 dB tone is far under the threshold: no reduction, just the make-up.
    check("quiet signal gets the make-up gain", std::abs(toneGainDb(base(), 1000, 0.01f) - 6.67) < 0.3);
    // A -6.02 dB tone is 13.98 dB over: reduction 9.32, net -2.65.
    {
        const Run run = render(base(), 96000, tone(1000, 0.5f), tone(1000, 0.5f));
        const double g = toDb(amplitude(run.l, 1000, 72000, 96000) / 0.5);
        std::printf("  leveller: loud tone %.2f dB (expected -2.65), meter %.2f\n", g, static_cast<double>(run.state.meters.levelReductionDb));
        // The static curve gives -2.65 dB / 9.32 dB. The peak detector's 5 ms release droops between the peaks of a 1 kHz tone (about 0.7 dB), so the
        // measured reduction is a little less.
        check("leveller follows the compression curve (within the detector's ripple)", g < -1.5 && g > -2.8);
        check("the meter reports the reduction", run.state.meters.levelReductionDb > 7.5f && run.state.meters.levelReductionDb < 9.5f);
    }
    check("the loud tone ends up closer to the quiet one (levelling)", toneGainDb(base(), 1000, 0.5f) < toneGainDb(base(), 1000, 0.01f) - 5.0);
    { Parameters p = base(); p.levelRatio = 10; const double g10 = toneGainDb(p, 1000, 0.5f); p.levelRatio = 1.5f; const double g15 = toneGainDb(p, 1000, 0.5f);
      std::printf("  ratio 10: %.2f dB, ratio 1.5: %.2f dB\n", g10, g15);
      check("a higher ratio takes more off a loud tone", g10 < g15 - 1.0 && g10 > -20.0); }
    { double last = 100.0; for (const float thr : {0.0f, -10.0f, -20.0f, -30.0f}) { Parameters p = base(); p.levelThrDb = thr; const double g = toneGainDb(p, 1000, 0.5f) - toneGainDb(p, 1000, 0.5f) * 0.0;
        (void)g; Parameters q = base(); q.levelThrDb = thr; Run run = render(q, 96000, tone(1000, 0.5f), tone(1000, 0.5f));
        const double red = run.state.meters.levelReductionDb; check("a lower threshold reduces more", last > 99.0 || red >= last - 0.01); last = red; } }
    // Mix: 0 is the input exactly; 0.5 is halfway in linear gain.
    {
        Parameters p = base(); p.levelMix = 0;
        check("mix 0 is the input, sample for sample", identical(render(p, 30000, noise(11, 0.3f), noise(12, 0.3f)).l, render(neutral(), 30000, noise(11, 0.3f), noise(12, 0.3f)).l));
        const double full = std::pow(10.0, toneGainDb(base(), 1000, 0.01f) / 20.0);
        p.levelMix = 0.5f;
        const double half = std::pow(10.0, toneGainDb(p, 1000, 0.01f) / 20.0);
        check("mix 0.5 is halfway in linear gain", std::abs(half - (1.0 + 0.5 * (full - 1.0))) < 0.01);
    }
    // Attack and release: gain comes down over about 8 ms and recovers over about 150 ms.
    {
        Run run;
        run.go(base(), 4800, silence(), silence());
        run.go(base(), 192, tone(1000, 0.5f), tone(1000, 0.5f));   // 4 ms
        const float early = run.state.meters.levelReductionDb;
        run.go(base(), 48000, tone(1000, 0.5f), tone(1000, 0.5f));
        const float full = run.state.meters.levelReductionDb;
        check("the leveller attacks over milliseconds, not instantly", early < 0.7f * full && full > 8.0f);
        run.go(base(), 4800, silence(), silence());   // 100 ms
        const float partial = run.state.meters.levelReductionDb;
        run.go(base(), 48000, silence(), silence());
        check("and releases over a fraction of a second", partial > 0.2f * full && run.state.meters.levelReductionDb < 0.1f);
    }
    // Linked, and exactly neutral when off.
    {
        const Run run = render(base(), 96000, tone(1000, 0.5f), tone(1000, 0.05f));
        const double gl = toDb(amplitude(run.l, 1000, 72000, 96000) / 0.5), gr = toDb(amplitude(run.r, 1000, 72000, 96000) / 0.05);
        check("both channels get the same gain", std::abs(gl - gr) < 0.05);
        const Run sw = render(base(), 96000, tone(1000, 0.05f), tone(1000, 0.5f));
        const double swl = toDb(amplitude(sw.l, 1000, 72000, 96000) / 0.05), swr = toDb(amplitude(sw.r, 1000, 72000, 96000) / 0.5);
        check("whichever channel is loud drives both", std::abs(swl - swr) < 0.05 && swr < -1.0);
        Parameters off = base(); off.levelOn = 0;
        check("leveller off is exact", identical(render(off, 20000, noise(13, 0.4f), noise(14, 0.4f)).l, render(neutral(), 20000, noise(13, 0.4f), noise(14, 0.4f)).l));
    }
}

static void testMudAndDrive()
{
    for (const float db : {-9.0f, 6.0f}) {
        Parameters p = neutral(); p.mudDb = db;
        check("mud is its gain at 300 Hz", std::abs(toneGainDb(p, 300, 0.1f) - static_cast<double>(db)) < 0.3);
        check("mud leaves 5 kHz alone", std::abs(toneGainDb(p, 5000, 0.1f)) < 0.2);
        check("mud leaves 40 Hz mostly alone", std::abs(toneGainDb(p, 40, 0.1f)) < 1.0);
    }
    // Drive: tanh(a x) / a with a = 1 + 2 drive. Unity for small signals; the third harmonic of a 0.5 sine from a numerical integral.
    {
        Parameters p = neutral(); p.drive = 1.0f;
        check("drive leaves small signals at unity", std::abs(toneGainDb(p, 1000, 0.005f)) < 0.05);
        const double a = 3.0;
        double h1 = 0.0, h3 = 0.0;
        const int N = 100000;
        for (int i = 0; i < N; ++i) {
            const double th = 2.0 * kPi * (i + 0.5) / N, v = std::tanh(a * 0.5 * std::sin(th)) / a;
            h1 += v * std::sin(th) * 2.0 / N; h3 += v * std::sin(3.0 * th) * 2.0 / N;
        }
        const Run run = render(p, 48000, tone(1000, 0.5f), tone(1000, 0.5f));
        const double g1 = amplitude(run.l, 1000, 24000, 48000), g3 = amplitude(run.l, 3000, 24000, 48000);
        std::printf("  drive: fundamental %.4f (expected %.4f), 3rd %.4f (expected %.4f)\n", g1, std::abs(h1), g3, std::abs(h3));
        check("drive's fundamental matches the saturation curve", std::abs(g1 - std::abs(h1)) < 0.01 * std::abs(h1) + 1e-3);
        check("drive's third harmonic matches the saturation curve", std::abs(g3 - std::abs(h3)) < 0.03 * std::abs(h3) + 1e-3);
        double last = -1.0;
        for (const float d : {0.0f, 0.25f, 0.5f, 1.0f}) {
            Parameters q = neutral(); q.drive = d;
            const Run r = render(q, 48000, tone(1000, 0.5f), tone(1000, 0.5f));
            const double third = amplitude(r.l, 3000, 24000, 48000);
            check("more drive, more third harmonic", third >= last);
            last = third;
        }
        Parameters off = neutral();
        check("drive 0 is exact", identical(render(off, 20000, noise(15, 0.5f), noise(16, 0.5f)).l, render(neutral(), 20000, noise(15, 0.5f), noise(16, 0.5f)).l));
    }
}

static void testControlsAudible()
{
    // Each control changes the sound when it has something to act on.
    Parameters base;
    base.warmthDb = 3; base.presenceDb = 3; base.airDb = 3; base.sparkle = 0.5f;
    const Run ref = render(base, 48000, kVoiceLike, kVoiceLike);
    const auto differs = [&](const char* name, const Parameters& p) {
        const Run run = render(p, 48000, kVoiceLike, kVoiceLike);
        check(name, maxDiff(run.l, ref.l) > 0.002);
    };
    Parameters p;
    p = base; p.hpfOn = 0; differs("Rumble Filter changes the sound", p);
    p = base; p.hpfHz = 250; differs("Rumble Freq changes the sound", p);
    p = base; p.plosiveOn = 0; differs("Plosive Guard changes the sound", p);
    p = base; p.plosiveThrDb = -40; differs("Plosive Threshold changes the sound", p);
    p = base; p.essOn = 0; differs("De-esser changes the sound", p);
    p = base; p.essFreqHz = 4000; differs("Ess Frequency changes the sound", p);
    p = base; p.essThrDb = -50; differs("Ess Threshold changes the sound", p);
    p = base; p.essRangeDb = 24; p.essThrDb = -50; differs("Ess Range changes the sound", p);
    p = base; p.essMode = 1; differs("Ess Mode changes the sound", p);
    p = base; p.essListen = 1; differs("Ess Listen changes the sound", p);
    p = base; p.warmthDb = -6; differs("Warmth changes the sound", p);
    p = base; p.presenceDb = -6; differs("Presence changes the sound", p);
    p = base; p.airDb = -6; differs("Air changes the sound", p);
    p = base; p.sparkle = 1; differs("Sparkle changes the sound", p);
    p = base; p.outputDb = -6; differs("Output changes the sound", p);
    p = base; p.gateOn = 1; p.gateThrDb = -20; p.gateRangeDb = 20; differs("Gate changes the sound", p);
    // The gate needs something below its threshold to act on: a quiet version of the voice-like signal (about -50 dB).
    const Signal quiet = [](std::size_t n) { return kVoiceLike(n) * 0.003f; };
    p = base; p.gateOn = 1; p.gateThrDb = -30; p.gateRangeDb = 5; { Parameters q = p; q.gateRangeDb = 30; const Run a = render(p, 48000, quiet, quiet), b = render(q, 48000, quiet, quiet); check("Gate Range changes the sound", maxDiff(a.l, b.l) > 1e-4); }
    p = base; p.gateOn = 1; p.gateRangeDb = 30; { Parameters q = p; p.gateThrDb = -80; q.gateThrDb = -30; const Run a = render(p, 48000, quiet, quiet), b = render(q, 48000, quiet, quiet); check("Gate Threshold changes the sound", maxDiff(a.l, b.l) > 1e-4); }
    p = base; p.tameAmountDb = 12; p.tameThrDb = -40; differs("Harsh Amount changes the sound", p);
    { Parameters q = base; q.tameAmountDb = 12; q.tameThrDb = -40; Parameters r = q; r.tameFreqHz = 1600; const Run a = render(q, 48000, kVoiceLike, kVoiceLike), b = render(r, 48000, kVoiceLike, kVoiceLike); check("Harsh Frequency changes the sound", maxDiff(a.l, b.l) > 0.002); }
    { Parameters q = base; q.tameAmountDb = 12; q.tameThrDb = -60; Parameters r = q; r.tameThrDb = -10; const Run a = render(q, 48000, kVoiceLike, kVoiceLike), b = render(r, 48000, kVoiceLike, kVoiceLike); check("Harsh Threshold changes the sound", maxDiff(a.l, b.l) > 0.002); }
    p = base; p.levelOn = 1; differs("Leveller changes the sound", p);
    { Parameters q = base; q.levelOn = 1; Parameters r = q; r.levelThrDb = -35; const Run a = render(q, 48000, kVoiceLike, kVoiceLike), b = render(r, 48000, kVoiceLike, kVoiceLike); check("Leveller Threshold changes the sound", maxDiff(a.l, b.l) > 0.002); }
    { Parameters q = base; q.levelOn = 1; Parameters r = q; r.levelRatio = 8; const Run a = render(q, 48000, kVoiceLike, kVoiceLike), b = render(r, 48000, kVoiceLike, kVoiceLike); check("Leveller Ratio changes the sound", maxDiff(a.l, b.l) > 0.002); }
    { Parameters q = base; q.levelOn = 1; Parameters r = q; r.levelMix = 0.3f; const Run a = render(q, 48000, kVoiceLike, kVoiceLike), b = render(r, 48000, kVoiceLike, kVoiceLike); check("Leveller Mix changes the sound", maxDiff(a.l, b.l) > 0.002); }
    p = base; p.mudDb = -6; differs("Mud changes the sound", p);
    p = base; p.drive = 0.8f; differs("Drive changes the sound", p);
}

static void testDeterminismAndStability()
{
    Parameters p;
    p.warmthDb = 4; p.presenceDb = 3; p.airDb = 5; p.sparkle = 0.6f; p.essThrDb = -40; p.plosiveThrDb = -30; p.outputDb = -2;
    p.gateOn = 1; p.gateThrDb = -45; p.tameAmountDb = 8; p.tameThrDb = -35; p.levelOn = 1; p.levelMix = 0.7f; p.mudDb = -3; p.drive = 0.4f;
    const Signal in = sum({noise(3, 0.2f), tone(60, 0.4f), tone(6500, 0.3f)});
    const Run whole = render(p, 48000, in, in, 48000);
    check("same input and settings give the same output", identical(whole.l, render(p, 48000, in, in, 48000).l));
    for (const std::size_t block : {1u, 7u, 64u, 480u, 4096u}) {
        const Run chopped = render(p, 48000, in, in, block);
        check("output does not depend on the block size", identical(whole.l, chopped.l) && identical(whole.r, chopped.r));
    }
    {
        // Parameter jumps part-way, still independent of the block size.
        const auto jumpy = [&](std::size_t block) {
            Run run; run.block = block;
            Parameters q = p;
            run.go(q, 10000, in, in);
            q.levelThrDb = -30; q.gateThrDb = -30; q.tameFreqHz = 2200; q.mudDb = 4; q.drive = 0.9f; q.levelMix = 0.2f;
            q.hpfHz = 200; q.essFreqHz = 4000; q.warmthDb = -5; q.presenceDb = 8; q.airDb = -3; q.sparkle = 0.1f;
            run.go(q, 10000, in, in);
            return run;
        };
        const Run a = jumpy(512), b = jumpy(33);
        check("automation does not depend on the block size", identical(a.l, b.l) && identical(a.r, b.r));
    }
    // Idle.
    {
        const Run run = render(p, 48000, silence(), silence());
        double peak = 0.0;
        for (float v : run.l) peak = std::max(peak, static_cast<double>(std::abs(v)));
        check("silence in gives silence out", peak == 0.0);
        Run burstRun;
        burstRun.go(p, 24000, noise(4, 0.8f), noise(5, 0.8f));
        burstRun.go(p, 48000 * 3, silence(), silence());
        double tail = 0.0;
        for (std::size_t i = burstRun.l.size() - 24000; i < burstRun.l.size(); ++i) tail = std::max(tail, static_cast<double>(std::abs(burstRun.l[i])));
        if (tail != 0.0) std::printf("  tail %.3e\n", tail);
        check("after silence the output decays to exactly zero (no denormal tail)", tail == 0.0);
    }
    // Loud and garbage input.
    {
        const Run loud = render(p, 48000, noise(6, 50.0f), noise(7, 50.0f));
        bool ok = true;
        for (float v : loud.l) ok = ok && std::isfinite(v) && std::abs(v) < 500.0f;
        check("a very loud input stays finite", ok);
        Parameters bad;
        bad.hpfHz = std::nanf(""); bad.plosiveThrDb = 1e9f; bad.essFreqHz = -5.0f; bad.essThrDb = std::nanf(""); bad.essRangeDb = 1e9f;
        bad.gateThrDb = std::nanf(""); bad.gateRangeDb = 1e9f; bad.tameFreqHz = -1e9f; bad.tameAmountDb = 1e9f; bad.levelRatio = 0.0f; bad.levelThrDb = 1e9f; bad.levelMix = -5.0f; bad.mudDb = std::nanf(""); bad.drive = 1e9f; bad.gateOn = std::nanf(""); bad.levelOn = 1.0f;
        bad.warmthDb = 1e9f; bad.presenceDb = -1e9f; bad.airDb = std::nanf(""); bad.sparkle = 99.0f; bad.outputDb = 1e9f; bad.essMode = std::nanf("");
        const Run run = render(bad, 24000, noise(8, 0.3f), noise(9, 0.3f));
        bool fine = true;
        for (std::size_t i = 0; i < run.l.size(); ++i) fine = fine && std::isfinite(run.l[i]) && std::isfinite(run.r[i]);
        check("garbage parameters are clamped and the output stays finite", fine);
    }
    for (const double rate : {22050.0, 44100.0, 96000.0, 192000.0}) {
        const Run run = render(p, static_cast<std::size_t>(rate), in, in, 512, rate);
        bool ok = true;
        for (float v : run.l) ok = ok && std::isfinite(v) && std::abs(v) < 4.0f;
        check("stable at every sample rate", ok);
    }
    {
        Run run;
        run.go(p, 20000, in, in);
        resetState(run.state);
        check("reset clears the detectors", run.state.ess.env == 0.0 && run.state.plosiveEnv == 0.0 && run.state.ess.gain == 1.0 && run.state.gateEnv == 0.0 && run.state.levelGr == 0.0 && !run.state.started);
    }
}

static void testParameters()
{
    check("parameter count is 32 (27 controls... 5 outputs)", kParameterCount == 32 && kOutputCount == 5);
    check("the first ids are pinned (saved projects)", kHpfOn == 0 && kHpf == 1 && kPlosiveOn == 2 && kPlosiveThr == 3 && kEssOn == 4 && kEssFreq == 5 && kEssThr == 6 && kEssRange == 7
          && kEssMode == 8 && kEssListen == 9 && kWarmth == 10 && kPresence == 11 && kAir == 12 && kSparkle == 13 && kOutput == 14
          && kStatusEss == 15 && kStatusPlosive == 16 && kStatusDetector == 17);
    check("ids added later are appended", kGateOn == 18 && kGateThr == 19 && kGateRange == 20 && kTameFreq == 21 && kTameThr == 22 && kTameAmount == 23
          && kLevelOn == 24 && kLevelThr == 25 && kLevelRatio == 26 && kLevelMix == 27 && kMud == 28 && kDrive == 29 && kStatusGate == 30 && kStatusLevel == 31);
    check("outputs are flagged", !isOutput(kOutput) && isOutput(kStatusEss) && isOutput(kStatusDetector) && isOutput(kStatusGate) && isOutput(kStatusLevel)
          && !isOutput(kDrive) && !isOutput(kGateOn) && !isOutput(kParameterCount));
    for (std::uint32_t i = 0; i < kParameterCount; ++i) check("isOutput agrees with the spec table", isOutput(i) == specs()[i].output);
    {
        std::vector<std::uint32_t> slots;
        for (std::uint32_t i = 0; i < kParameterCount; ++i) if (isOutput(i)) slots.push_back(outputSlot(i));
        check("output slots are 0..4 in order", slots == std::vector<std::uint32_t>({0, 1, 2, 3, 4}));
    }
    const Parameters d;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto& sp = specs()[i];
        check("default is inside the range", sp.defaultValue >= sp.minimum && sp.defaultValue <= sp.maximum);
        if (!sp.output) check("spec default matches the core default", std::abs(field(d, i) - sp.defaultValue) < 1e-6f);
    }
    check("symbols are unique", [] {
        for (std::uint32_t a = 0; a < kParameterCount; ++a)
            for (std::uint32_t b = a + 1; b < kParameterCount; ++b)
                if (std::string(specs()[a].symbol) == specs()[b].symbol) return false;
        return true;
    }());
    // Soft knee of 6 dB: nothing 3 dB or more below the threshold, a quadratic blend across it, then the straight slope (1 - 1/ratio).
    check("reduction curve: nothing below the knee", reductionDb(-40, -30, 4) == 0.0 && reductionDb(-33, -30, 4) == 0.0);
    check("reduction curve: the knee is 0.75 dB of 'over' at the threshold, x 3/4", std::abs(reductionDb(-30, -30, 4) - 0.5625) < 1e-12);
    check("reduction curve: the straight slope above the knee", std::abs(reductionDb(-20, -30, 4) - 7.5) < 1e-12 && std::abs(reductionDb(-27, -30, 4) - 2.25) < 1e-12);
    const Parameters c = clampParameters([] { Parameters q; q.hpfHz = 5.0f; q.essFreqHz = 1e6f; q.essRangeDb = 99.0f; q.essMode = 0.7f; q.sparkle = -1.0f; return q; }());
    check("clamp limits the ranges and rounds the switches", c.hpfHz == kHpfMin && c.essFreqHz == kEssFreqMax && c.essRangeDb == kEssRangeMax && c.essMode == 1.0f && c.sparkle == 0.0f);
}

int main()
{
    testParameters();
    testNeutral();
    testRumbleFilter();
    testPlosiveGuard();
    testDeEsser();
    testVoiceEq();
    testGate();
    testTame();
    testLeveller();
    testMudAndDrive();
    testSparkle();
    testControlsAudible();
    testDeterminismAndStability();
    std::printf("silken core tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
