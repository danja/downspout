#include "downspout/test_assert.h"
#include "chorale_core.hpp"
#include "chorale_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace downspout::chorale;

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

static Signal tone(double f, float amp)
{
    return [=](std::size_t n) { return amp * static_cast<float>(std::sin(2.0 * kPi * f * static_cast<double>(n) / kSr)); };
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

struct Timed { std::uint32_t frame; MidiEvent ev; };
static Timed controller(std::uint32_t frame, int number, int value, int channel = 0)
{
    return {frame, {0, static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)}};
}

// One engine kept running across calls, so parameters can change part-way. Records input and output.
struct Run {
    EngineState state;
    std::vector<float> inL, inR, l, r;
    std::size_t done = 0;
    std::size_t block = 512;

    void go(const Parameters& p, std::size_t frames, const Signal& sl, const Signal& sr, const std::vector<Timed>& events = {})
    {
        const std::size_t end = done + frames;
        while (done < end) {
            const std::size_t n = std::min(block, end - done);
            std::vector<float> a(n), b(n), oa(n), ob(n);
            for (std::size_t i = 0; i < n; ++i) { a[i] = sl(done + i); b[i] = sr(done + i); }
            std::vector<MidiEvent> ev;
            for (const auto& t : events)
                if (t.frame >= done && t.frame < done + n) { MidiEvent e = t.ev; e.frame = static_cast<std::uint32_t>(t.frame - done); ev.push_back(e); }
            processBlock(state, p, static_cast<float>(kSr), static_cast<std::uint32_t>(n), ev.data(), static_cast<std::uint32_t>(ev.size()),
                         a.data(), b.data(), oa.data(), ob.data());
            inL.insert(inL.end(), a.begin(), a.end()); inR.insert(inR.end(), b.begin(), b.end());
            l.insert(l.end(), oa.begin(), oa.end()); r.insert(r.end(), ob.begin(), ob.end());
            done += n;
        }
    }
};

static Run render(const Parameters& p, std::size_t frames, const Signal& sl, const Signal& sr, const std::vector<Timed>& events = {}, std::size_t block = 512)
{
    Run run;
    run.block = block;
    run.go(p, frames, sl, sr, events);
    return run;
}

static double rms(const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i) e += static_cast<double>(v[i]) * v[i];
    return std::sqrt(e / static_cast<double>(to - from));
}

static double maxDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::abs(static_cast<double>(a[i]) - b[i]));
    return m;
}

static bool identical(const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// Normalised correlation of the output with the mono input delayed by `lag`, over a window around `centre`.
static double corrAt(const Run& run, const std::vector<float>& out, std::size_t centre, std::size_t half, std::size_t lag)
{
    double xy = 0.0, xx = 0.0;
    for (std::size_t n = centre - half; n < centre + half; ++n) {
        const double x = static_cast<double>(run.inL[n - lag]) + run.inR[n - lag];
        xy += x * out[n];
        xx += x * x;
    }
    return xy / xx;
}

// The lag (samples) that best explains the output, searched over 1..maxLag.
static std::size_t bestLag(const Run& run, const std::vector<float>& out, std::size_t centre, std::size_t half, std::size_t maxLag = 1500)
{
    std::size_t best = 0;
    double bestC = -1e9;
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        const double c = corrAt(run, out, centre, half, lag);
        if (c > bestC) { bestC = c; best = lag; }
    }
    return best;
}

static Parameters custom()
{
    Parameters p;
    p.mode = 3; p.mix = 1.0f; p.tone = 1.0f; p.voices = 1; p.depth = 0.0f; p.delay = 8.0f; p.rate = 0.5f; p.width = 1.0f;
    return p;
}

// ---- an independent, straight-line transcription of the Juno chorus, used as the expected output --------------------------------------------------

static void reference(int mode, double tone, double mix, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<double>& oL, std::vector<double>& oR)
{
    const double sr = kSr;
    std::vector<double> line(8192, 0.0);
    int w = 0;
    double lfo1 = -1.0, lfo2 = -1.0;
    const double ga = mode == 0 ? 1.0 : mode == 1 ? std::sqrt(0.5) : 0.0;
    const double gb = mode == 0 ? 0.0 : mode == 1 ? std::sqrt(0.5) : 1.0;
    const double dry = std::sqrt(1.0 - mix), wet = std::sqrt(mix);
    auto coeffs = [&](double hz, double& ff, double& fb) {
        const double kk = std::tan(kPi * std::min(hz / sr, 0.49));
        ff = kk / (kk + 1.0);
        fb = (kk - 1.0) / (kk + 1.0);
    };
    double preFf, preFb, postFf, postFb;
    coeffs(2000.0 + tone * tone * 21500.0, preFf, preFb);
    coeffs(6000.0 + tone * tone * 17500.0, postFf, postFb);
    double zPre = 0, zL = 0, zR = 0;
    auto filt = [](double x, double ff, double fb, double& z) { const double acc = ff * x + z; z = ff * x - fb * acc; return acc; };
    auto at = [&](double pos) {
        const int b = static_cast<int>(pos);
        const double f = pos - b;
        const double s0 = line[static_cast<std::size_t>((w + b) & 8191)];
        return s0 + f * (line[static_cast<std::size_t>((w + b + 1) & 8191)] - s0);
    };
    oL.assign(inL.size(), 0.0); oR.assign(inL.size(), 0.0);
    for (std::size_t n = 0; n < inL.size(); ++n) {
        const double x = static_cast<double>(inL[n]) + inR[n];
        line[static_cast<std::size_t>(w)] = filt(x * (27 + x * x) / (27 + 9 * x * x), preFf, preFb, zPre);
        w = (w + 8191) & 8191;
        lfo1 += 2 * 0.513 / sr; if (lfo1 >= 1) lfo1 -= 2;
        lfo2 += 2 * 0.863 / sr; if (lfo2 >= 1) lfo2 -= 2;
        const double a = std::abs(lfo1), b = std::abs(lfo2);
        const double wl = ga * at((0.00154 + (0.00515 - 0.00154) * a) * sr) + gb * at((0.00154 + (0.00515 - 0.00154) * b) * sr);
        const double wr = ga * at((0.00151 + (0.00540 - 0.00151) * (1 - a)) * sr) + gb * at((0.00151 + (0.00540 - 0.00151) * (1 - b)) * sr);
        oL[n] = inL[n] * dry + filt(wl, postFf, postFb, zL) * wet;
        oR[n] = inR[n] * dry + filt(wr, postFf, postFb, zR) * wet;
    }
}

static void testJunoMatchesReference()
{
    const std::size_t frames = 192000;   // four seconds: both triangles wrap
    struct Case { int mode; float tone, mix; };
    const Case cases[] = {{0, 0.5f, 0.5f}, {1, 0.5f, 0.5f}, {2, 0.5f, 0.5f}, {0, 0.0f, 1.0f}, {1, 1.0f, 0.3f}, {2, 0.8f, 1.0f}, {1, 0.2f, 0.0f}};
    for (const auto& c : cases) {
        Parameters p;
        p.mode = static_cast<float>(c.mode); p.tone = c.tone; p.mix = c.mix;
        const Run run = render(p, frames, noise(1, 0.3f), noise(2, 0.3f));
        std::vector<double> rl, rr;
        reference(c.mode, c.tone, c.mix, run.inL, run.inR, rl, rr);
        double err = 0.0;
        for (std::size_t n = 0; n < frames; ++n) err = std::max({err, std::abs(rl[n] - run.l[n]), std::abs(rr[n] - run.r[n])});
        std::printf("  juno mode %d tone %.1f mix %.1f: max error vs reference %.2e\n", c.mode, static_cast<double>(c.tone), static_cast<double>(c.mix), err);
        check("juno output matches the reference transcription", err < 2e-5);
    }
}

static void testDryAndMix()
{
    // Before any wet signal can arrive (the shortest delay is 1.5 ms = 72 samples) the output is the dry input times sqrt(1 - mix).
    for (int mode = 0; mode < kModeCount; ++mode) {
        for (const float mix : {0.0f, 0.36f, 1.0f}) {
            Parameters p = custom(); p.mode = static_cast<float>(mode); p.mix = mix;
            const Run run = render(p, 64, tone(440, 0.5f), tone(660, 0.5f));
            double worst = 0.0;
            for (std::size_t n = 8; n < 40; ++n)
                worst = std::max({worst, std::abs(run.l[n] - run.inL[n] * std::sqrt(1.0 - mix)), std::abs(run.r[n] - run.inR[n] * std::sqrt(1.0 - mix))});
            check("dry path is the input at sqrt(1 - mix)", worst < 1e-5);
        }
        Parameters p = custom(); p.mode = static_cast<float>(mode); p.mix = 0.0f;
        const Run run = render(p, 20000, noise(3, 0.4f), noise(4, 0.4f));
        check("mix 0 is the input, sample for sample", identical(run.l, run.inL) && identical(run.r, run.inR));
    }
    // 1 kHz delayed by 8 ms is eight whole cycles, so wet and dry add in phase. The wet signal is the sum L + R (as in the source), so for the
    // same tone on both sides its gain is 2: sqrt(1 - m) + 2 sqrt(m).
    for (const float m : {0.25f, 0.5f, 0.8f}) {
        Parameters p = custom(); p.mix = m;
        const Run run = render(p, 48000, tone(1000, 0.05f), tone(1000, 0.05f));
        const double gain = rms(run.l, 24000, 48000) / rms(run.inL, 24000, 48000);
        const double expect = std::sqrt(1.0 - m) + 2.0 * std::sqrt(m);
        check("mix adds the in-phase wet sum to the dry: sqrt(1-m) + 2 sqrt(m)", std::abs(gain - expect) < 0.03 * expect);
    }
}

static void testCustomDelayRateDepth()
{
    // Depth 0: every tap sits at Delay, so the output is the input delayed by Delay milliseconds.
    for (const float d : {2.0f, 8.0f, 20.0f, 30.0f}) {
        Parameters p = custom(); p.delay = d;
        const Run run = render(p, 24000, noise(5, 0.05f), noise(5, 0.05f));
        const long want = std::lround(static_cast<double>(d) * kSr / 1000.0);
        const auto lagL = static_cast<long>(bestLag(run, run.l, 18000, 1200)), lagR = static_cast<long>(bestLag(run, run.r, 18000, 1200));
        check("custom delay equals the Delay setting (left)", std::abs(lagL - want) <= 1);
        check("custom delay equals the Delay setting (right)", std::abs(lagR - want) <= 1);
    }
    // Depth and phase: the left tap is Delay + Depth*sin(2 pi rate t), the right tap the opposite. 8 ms = 384, 1 ms = 48 samples.
    {
        Parameters p = custom(); p.depth = 3.0f; p.rate = 0.5f;
        const Run run = render(p, 80000, noise(6, 0.05f), noise(6, 0.05f));
        const auto at = [&](std::size_t centre, const std::vector<float>& ch) { return static_cast<long>(bestLag(run, ch, centre, 600)); };
        check("at the sweep peak the left tap is Delay + Depth", std::abs(at(24000, run.l) - 528) <= 1);
        check("at the sweep peak the right tap is Delay - Depth", std::abs(at(24000, run.r) - 240) <= 1);
        check("half a cycle later the taps swap", std::abs(at(72000, run.l) - 240) <= 1 && std::abs(at(72000, run.r) - 528) <= 1);
    }
    {
        Parameters p = custom(); p.depth = 6.0f; p.rate = 0.5f;
        const Run run = render(p, 30000, noise(7, 0.05f), noise(7, 0.05f));
        check("depth sets the swing (6 ms: 384 + 288)", std::abs(static_cast<long>(bestLag(run, run.l, 24000, 600)) - 672) <= 1);
    }
    // Rate: at 0.25 s a 1 Hz sweep is at its peak, a 0.5 Hz sweep is 45 degrees in (sin = 0.7071: 384 + 0.7071 * 144 = 486).
    {
        Parameters p = custom(); p.depth = 3.0f; p.rate = 1.0f;
        const Run fast = render(p, 14000, noise(8, 0.05f), noise(8, 0.05f));
        p.rate = 0.5f;
        const Run slow = render(p, 14000, noise(8, 0.05f), noise(8, 0.05f));
        check("rate 1 Hz peaks at 0.25 s", std::abs(static_cast<long>(bestLag(fast, fast.l, 12000, 500)) - 528) <= 1);
        check("rate 0.5 Hz is at 45 degrees at 0.25 s", std::abs(static_cast<long>(bestLag(slow, slow.l, 12000, 500)) - 486) <= 2);
    }
    // The sweep never reaches negative delay: Depth is held under Delay.
    {
        Parameters p = custom(); p.delay = 1.0f; p.depth = 10.0f; p.rate = 2.0f;
        const Run run = render(p, 48000, noise(9, 0.3f), noise(9, 0.3f));
        bool finite = true;
        for (float v : run.l) finite = finite && std::isfinite(v) && std::abs(v) < 2.0f;
        check("depth larger than delay stays finite and bounded", finite);
    }
}

static void testVoices()
{
    // Two voices are a quarter turn apart, and each takes half the gain. At 2.5 s (rate 0.1 Hz) voice 1 is at the sweep peak and voice 2 at its
    // centre, so the output holds the input at 528 and at 384 samples, each at about half strength.
    Parameters p = custom(); p.depth = 3.0f; p.rate = 0.1f; p.voices = 2;
    const Run two = render(p, 130000, noise(10, 0.05f), noise(10, 0.05f));
    const double peakA = corrAt(two, two.l, 120000, 400, 528), peakB = std::max({corrAt(two, two.l, 120000, 400, 383), corrAt(two, two.l, 120000, 400, 384), corrAt(two, two.l, 120000, 400, 385)}), between = corrAt(two, two.l, 120000, 400, 456);
    check("two voices: a tap at the sweep peak carries about half", peakA > 0.35 && peakA < 0.65);
    check("two voices: a tap at the centre (moving fastest, so smeared over a sample) carries about half", peakB > 0.3 && peakB < 0.65);
    check("two voices: nothing between the taps", std::abs(between) < 0.15);
    p.voices = 1;
    const Run one = render(p, 130000, noise(10, 0.05f), noise(10, 0.05f));
    check("one voice: the whole signal is at the sweep peak", corrAt(one, one.l, 120000, 400, 528) > 0.85);

    // Level stays comparable with more voices, and each count sounds different.
    std::vector<Run> runs;
    for (int v = 1; v <= 4; ++v) {
        Parameters q = custom(); q.depth = 4.0f; q.rate = 1.5f; q.voices = static_cast<float>(v); q.mix = 0.5f;
        runs.push_back(render(q, 96000, noise(11, 0.2f), noise(12, 0.2f)));
        const double level = rms(runs.back().l, 48000, 96000) / rms(runs.back().inL, 48000, 96000);
        check("voice count keeps the level in range", level > 0.5 && level < 1.6);
    }
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            check("each voice count sounds different", maxDiff(runs[static_cast<std::size_t>(a)].l, runs[static_cast<std::size_t>(b)].l) > 0.01);
}

static void testWidthAndTone()
{
    Parameters p = custom(); p.depth = 4.0f; p.rate = 1.0f; p.width = 0.0f;
    const Run mono = render(p, 48000, noise(13, 0.2f), noise(14, 0.2f));
    check("width 0 makes the wet signal mono", identical(mono.l, mono.r));
    p.width = 1.0f;
    const Run full = render(p, 48000, noise(13, 0.2f), noise(14, 0.2f));
    p.width = 0.5f;
    const Run half = render(p, 48000, noise(13, 0.2f), noise(14, 0.2f));
    std::vector<float> dFull(full.l.size()), dHalf(half.l.size());
    for (std::size_t i = 0; i < dFull.size(); ++i) { dFull[i] = full.l[i] - full.r[i]; dHalf[i] = half.l[i] - half.r[i]; }
    check("width 1 is stereo", rms(dFull, 24000, 48000) > 0.01);
    const double ratio = rms(dHalf, 24000, 48000) / rms(dFull, 24000, 48000);
    check("width 0.5 halves the stereo difference", std::abs(ratio - 0.5) < 0.06);

    // Tone: the filters open with Tone, so a 10 kHz tone passes more and more.
    double last = 0.0;
    for (const float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        Parameters q = custom(); q.tone = t;
        const Run run = render(q, 48000, tone(10000, 0.05f), tone(10000, 0.05f));
        const double level = rms(run.l, 24000, 48000);
        check("tone opens the wet filters monotonically", level > last);
        last = level;
    }
    Parameters closed = custom(); closed.tone = 0.0f;
    const Run a = render(closed, 48000, tone(10000, 0.05f), tone(10000, 0.05f));
    check("tone 0 takes the top off", rms(a.l, 24000, 48000) < 0.25 * rms(a.inL, 24000, 48000) * 2.0);
}

static void testModes()
{
    std::vector<Run> runs;
    for (int m = 0; m < kModeCount; ++m) {
        Parameters p; p.mode = static_cast<float>(m); p.mix = 1.0f;
        runs.push_back(render(p, 48000, noise(15, 0.2f), noise(16, 0.2f)));
    }
    for (int a = 0; a < kModeCount; ++a)
        for (int b = a + 1; b < kModeCount; ++b)
            check("each mode sounds different", maxDiff(runs[static_cast<std::size_t>(a)].l, runs[static_cast<std::size_t>(b)].l) > 0.01);

    // The custom controls do nothing in the Juno modes: their defaults are inert there.
    for (int m = 0; m < 3; ++m) {
        Parameters a; a.mode = static_cast<float>(m);
        Parameters b = a; b.rate = 5.0f; b.depth = 9.0f; b.delay = 25.0f; b.voices = 4.0f;
        check("rate, depth, delay and voices are inert in the Juno modes",
              identical(render(a, 20000, noise(17, 0.3f), noise(18, 0.3f)).l, render(b, 20000, noise(17, 0.3f), noise(18, 0.3f)).l));
    }

    // Switching mode mid-signal glides: no step in a 200 Hz tone.
    for (const int to : {1, 2, 3}) {
        Parameters p; p.mode = 0.0f; p.mix = 1.0f;
        Run run;
        run.go(p, 48000, tone(200, 0.3f), tone(200, 0.3f));
        p.mode = static_cast<float>(to);
        run.go(p, 4800, tone(200, 0.3f), tone(200, 0.3f));
        double worst = 0.0;
        for (std::size_t n = 48001; n < run.l.size(); ++n) worst = std::max(worst, static_cast<double>(std::abs(run.l[n] - run.l[n - 1])));
        check("a mode change glides, no click", worst < 0.03);
    }
}

static void testControlsAudible()
{
    const auto base = [] { Parameters p = custom(); p.depth = 3.0f; p.voices = 2; p.mix = 0.6f; p.tone = 0.7f; p.width = 0.8f; return p; };
    const Run ref = render(base(), 96000, noise(19, 0.2f), noise(20, 0.2f));
    const auto differs = [&](const char* name, Parameters p) {
        const Run run = render(p, 96000, noise(19, 0.2f), noise(20, 0.2f));
        check(name, maxDiff(run.l, ref.l) > 0.005 || maxDiff(run.r, ref.r) > 0.005);
    };
    Parameters p;
    p = base(); p.mode = 1; differs("Mode changes the sound", p);
    p = base(); p.tone = 0.1f; differs("Tone changes the sound", p);
    p = base(); p.mix = 0.2f; differs("Mix changes the sound", p);
    p = base(); p.rate = 3.0f; differs("Rate changes the sound", p);
    p = base(); p.depth = 7.0f; differs("Depth changes the sound", p);
    p = base(); p.delay = 15.0f; differs("Delay changes the sound", p);
    p = base(); p.voices = 4; differs("Voices changes the sound", p);
    p = base(); p.width = 0.1f; differs("Width changes the sound", p);
}

static void testControllers()
{
    const auto at = [](const Run& run, std::size_t centre, const std::vector<float>& ch) { return static_cast<long>(bestLag(run, ch, centre, 600)); };
    Parameters p = custom(); p.depth = 3.0f; p.rate = 0.5f;
    const Signal n1 = noise(21, 0.05f);

    // CC 1 takes Rate over: 0 is the slowest rate, reached with the 20 ms smoothing, so the phase at 0.5 s is 0.05 * 0.5 + (0.5 - 0.05) * 0.02 turns
    // (0.034: sin = 0.2119, 384 + 0.2119 * 144 = 414.5).
    {
        const Run run = render(p, 30000, n1, n1, {controller(0, 1, 0)});
        check("CC 1 at 0 takes Rate to its minimum", std::abs(at(run, 24000, run.l) - 414) <= 2);
        check("the status reports the controller's rate", std::abs(run.state.meters.rate - kRateMin) < 1e-5f);
    }
    {
        const Run run = render(p, 30000, n1, n1, {controller(0, 1, 127)});
        check("CC 1 at 127 takes Rate to its maximum", std::abs(run.state.meters.rate - kRateMax) < 1e-4f);
    }
    // CC 2 takes Depth over (10 ms, held under Delay: 7.95 ms = 382 samples either side of 384 at the peak).
    {
        const Run run = render(p, 30000, n1, n1, {controller(0, 2, 127)});
        check("CC 2 at 127 takes Depth to the delay limit", std::abs(at(run, 24000, run.l) - 766) <= 3);
        check("the status reports the controller's depth", std::abs(run.state.meters.depth - kDepthMax) < 1e-4f);
    }
    // CC 3 takes Width over: 0 is mono. CC 4 takes Mix over: 0 is the dry signal.
    {
        Parameters q = custom(); q.depth = 4.0f; q.rate = 1.0f;
        const Run run = render(q, 48000, noise(22, 0.2f), noise(23, 0.2f), {controller(0, 3, 0)});
        double d = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i) d = std::max(d, static_cast<double>(std::abs(run.l[i] - run.r[i])));
        check("CC 3 at 0 makes the wet signal mono", d < 1e-5);
        const Run mixed = render(q, 48000, noise(22, 0.2f), noise(23, 0.2f), {controller(0, 4, 0)});
        double m = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i) m = std::max(m, static_cast<double>(std::abs(mixed.l[i] - mixed.inL[i])));
        check("CC 4 at 0 leaves the dry signal", m < 1e-5);
    }
    // Only the CC channel, only CC 1-4, and not when off.
    {
        const Run ref = render(p, 30000, n1, n1);
        const Run other = render(p, 30000, n1, n1, {controller(0, 1, 0, 1), controller(0, 2, 127, 4)});
        check("a controller on another channel is ignored", identical(ref.l, other.l));
        const Run unmapped = render(p, 30000, n1, n1, {controller(0, 7, 0), controller(0, 74, 127)});
        check("an unmapped controller is ignored", identical(ref.l, unmapped.l));
        Parameters off = p; off.ccChannel = 0.0f;
        check("CC channel 0 ignores controllers", identical(ref.l, render(off, 30000, n1, n1, {controller(0, 1, 0)}).l));
        Parameters ch5 = p; ch5.ccChannel = 5.0f;
        check("a controller on the set channel is heard", !identical(ref.l, render(ch5, 30000, n1, n1, {controller(0, 1, 0, 4)}).l));
        MidiEvent note; note.status = 0x90; note.data1 = 1; note.data2 = 100;
        std::vector<float> a(512, 0.0f), o(512), o2(512);
        EngineState s; processBlock(s, p, 48000.0f, 512, &note, 1, a.data(), a.data(), o.data(), o2.data());
        check("a note is not a controller", !s.ccActive[0]);
    }
    // Sample accurate: nothing changes before the event's frame.
    {
        const Run ref = render(p, 8192, n1, n1);
        const Run late = render(p, 8192, n1, n1, {controller(4000, 1, 0)});
        bool same = true;
        for (std::size_t i = 0; i < 4000; ++i) same = same && ref.l[i] == late.l[i];
        check("a controller acts from its own frame", same);
        check("and then does change the sound", maxDiff(ref.l, late.l) > 1e-4);
    }
    // The panel or host takes the control back.
    {
        Run run;
        run.go(p, 4096, n1, n1, {controller(0, 1, 0)});
        check("the controller holds Rate", std::abs(run.state.meters.rate - kRateMin) < 1e-5f);
        releaseCc(run.state, kCcRate);
        run.go(p, 512, n1, n1);
        check("releasing hands Rate back to the parameter", std::abs(run.state.meters.rate - p.rate) < 1e-5f);
        check("ccToValue spans each range", std::abs(ccToValue(kCcRate, 0.0f) - kRateMin) < 1e-6f && std::abs(ccToValue(kCcRate, 1.0f) - kRateMax) < 1e-4f
              && std::abs(ccToValue(kCcDepth, 0.5f) - 5.0f) < 1e-5f && ccToValue(kCcMix, 0.25f) == 0.25f);
        check("ccToValue is logarithmic for Rate", std::abs(ccToValue(kCcRate, 0.5f) - std::sqrt(kRateMin * kRateMax)) < 1e-4f);
    }
}

static void testDeterminismAndBlocks()
{
    for (int mode = 0; mode < kModeCount; ++mode) {
        Parameters p; p.mode = static_cast<float>(mode); p.depth = 4.0f; p.voices = 3; p.rate = 1.3f; p.mix = 0.7f;
        const std::vector<Timed> events = {controller(3000, 1, 90), controller(9000, 2, 20), controller(15000, 3, 40), controller(20000, 4, 100)};
        const Run whole = render(p, 40000, noise(24, 0.3f), noise(25, 0.3f), events, 40000);
        const Run again = render(p, 40000, noise(24, 0.3f), noise(25, 0.3f), events, 40000);
        check("same input and settings give the same output", identical(whole.l, again.l) && identical(whole.r, again.r));
        for (const std::size_t block : {1u, 7u, 64u, 480u, 4096u}) {
            const Run chopped = render(p, 40000, noise(24, 0.3f), noise(25, 0.3f), events, block);
            check("output does not depend on the block size", identical(whole.l, chopped.l) && identical(whole.r, chopped.r));
        }
    }
    // Sample rates.
    for (const float sr : {22050.0f, 44100.0f, 96000.0f, 192000.0f}) {
        Parameters p = custom(); p.delay = 30.0f; p.depth = 10.0f; p.voices = 4;
        EngineState s;
        std::vector<float> in(4096, 0.2f), ol(4096), orr(4096);
        bool ok = true;
        for (int b = 0; b < 40; ++b) {
            processBlock(s, p, sr, 4096, nullptr, 0, in.data(), in.data(), ol.data(), orr.data());
            for (float v : ol) ok = ok && std::isfinite(v) && std::abs(v) < 2.0f;
        }
        check("longest delay fits the line at every sample rate", ok);
    }
}

static void testIdleAndStability()
{
    Parameters p; p.mode = 1; p.mix = 1.0f;
    {
        const Run run = render(p, 48000, silence(), silence());
        double peak = 0.0;
        for (float v : run.l) peak = std::max(peak, static_cast<double>(std::abs(v)));
        check("silence in gives silence out", peak == 0.0);
    }
    for (int mode = 0; mode < kModeCount; ++mode) {
        Parameters q = custom(); q.mode = static_cast<float>(mode); q.voices = 4; q.depth = 5.0f;
        Run run;
        run.go(q, 24000, noise(26, 0.9f), noise(27, 0.9f));
        run.go(q, 48000 * 3, silence(), silence());
        double tail = 0.0;
        for (std::size_t i = run.l.size() - 24000; i < run.l.size(); ++i) tail = std::max(tail, static_cast<double>(std::abs(run.l[i])));
        check("after silence the output decays to exactly zero (no denormal tail)", tail == 0.0);
    }
    {
        const Run loud = render(p, 48000, noise(28, 100.0f), noise(29, 100.0f));
        bool ok = true;
        for (std::size_t i = 0; i < loud.l.size(); ++i) ok = ok && std::isfinite(loud.l[i]) && std::abs(loud.l[i]) < 400.0f;
        check("a very loud input stays finite", ok);
    }
    {
        Parameters bad;
        bad.mode = std::nanf(""); bad.tone = 1e9f; bad.mix = -3.0f; bad.rate = std::nanf(""); bad.depth = 1e9f; bad.delay = -1e9f;
        bad.voices = 99.0f; bad.width = 7.0f; bad.ccChannel = -4.0f;
        const Run run = render(bad, 24000, noise(30, 0.3f), noise(31, 0.3f));
        bool ok = true;
        for (std::size_t i = 0; i < run.l.size(); ++i) ok = ok && std::isfinite(run.l[i]) && std::isfinite(run.r[i]);
        check("garbage parameters are clamped and the output stays finite", ok);
    }
}

static void testMetersAndReset()
{
    Parameters p; p.mode = 0;
    Run run;
    run.go(p, 30000, noise(32, 0.2f), noise(33, 0.2f));
    bool range = true;
    for (float t : run.state.meters.tap) range = range && t >= 0.0f && t <= 1.0f;
    check("sweep meters are in 0..1", range);
    check("Juno mode: two sweeps are shown", run.state.meters.tap[0] > 0.0f && run.state.meters.tap[1] > 0.0f && run.state.meters.tap[2] == 0.0f && run.state.meters.tap[3] == 0.0f);
    Parameters c = custom(); c.voices = 3;
    Run cr;
    cr.go(c, 5000, noise(34, 0.2f), noise(35, 0.2f));
    check("Custom: one sweep per voice", cr.state.meters.tap[0] > 0.0f && cr.state.meters.tap[2] > 0.0f && cr.state.meters.tap[3] == 0.0f);
    check("status reports the parameter values when nothing overrides", std::abs(cr.state.meters.rate - c.rate) < 1e-5f && std::abs(cr.state.meters.mix - c.mix) < 1e-5f);

    resetState(run.state);
    std::vector<float> z(2048, 0.0f), o(2048), o2(2048);
    processBlock(run.state, p, 48000.0f, 2048, nullptr, 0, z.data(), z.data(), o.data(), o2.data());
    double peak = 0.0;
    for (float v : o) peak = std::max(peak, static_cast<double>(std::abs(v)));
    check("reset empties the delay line", peak == 0.0);
    check("reset drops controller takeovers", !run.state.ccActive[0]);
}

static void testParameters()
{
    check("parameter count is 17", kParameterCount == 17 && kControlCount == 9);
    check("ids are pinned", kMode == 0 && kTone == 1 && kMix == 2 && kRate == 3 && kDepth == 4 && kDelay == 5 && kVoices == 6 && kWidth == 7
          && kCcChannel == 8 && kStatusTap1 == 9 && kStatusMix == 16);
    check("outputs are flagged", !isOutput(kCcChannel) && isOutput(kStatusTap1) && isOutput(kStatusMix) && !isOutput(kParameterCount));
    const Parameters d;
    for (std::uint32_t i = 0; i < kControlCount; ++i) {
        const auto& s = specs()[i];
        check("spec default matches the core default", std::abs(field(d, i) - s.defaultValue) < 1e-6f);
        check("default is inside the range", s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        check("controls are not outputs", !s.output);
    }
    for (std::uint32_t i = kControlCount; i < kParameterCount; ++i) check("status parameters are outputs", specs()[i].output);
    check("symbols are unique", [] {
        for (std::uint32_t a = 0; a < kParameterCount; ++a)
            for (std::uint32_t b = a + 1; b < kParameterCount; ++b)
                if (std::string(specs()[a].symbol) == specs()[b].symbol) return false;
        return true;
    }());
    check("controller targets map to their controls", ccTargetOf(kRate) == kCcRate && ccTargetOf(kDepth) == kCcDepth && ccTargetOf(kWidth) == kCcWidth
          && ccTargetOf(kMix) == kCcMix && ccTargetOf(kTone) == -1 && ccTargetOf(kMode) == -1);
    Parameters c = clampParameters([] { Parameters p; p.mode = 2.6f; p.voices = 2.4f; p.rate = 100.0f; p.depth = -2.0f; p.delay = 0.0f; return p; }());
    check("clamp rounds the integers and limits the ranges", c.mode == 3.0f && c.voices == 2.0f && c.rate == kRateMax && c.depth == 0.0f && c.delay == kDelayMin);
}

int main()
{
    testParameters();
    testJunoMatchesReference();
    testDryAndMix();
    testCustomDelayRateDepth();
    testVoices();
    testWidthAndTone();
    testModes();
    testControlsAudible();
    testControllers();
    testDeterminismAndBlocks();
    testIdleAndStability();
    testMetersAndReset();
    std::printf("chorale core tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
