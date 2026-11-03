#include "downspout/test_assert.h"
#include "whirligig_core.hpp"
#include "whirligig_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <tuple>
#include <string>
#include <vector>

using namespace downspout::whirligig;

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
constexpr double kPi = 3.14159265358979323846;

struct Timed { std::uint32_t frame; MidiEvent ev; };
static Timed controller(std::uint32_t frame, int number, int value, int channel = 0)
{
    return {frame, {0, static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)}};
}

using Signal = std::function<float(std::size_t)>;

static Signal tone(double f, float amp = 0.5f)
{
    return [=](std::size_t n) { return amp * static_cast<float>(std::sin(2.0 * kPi * f * static_cast<double>(n) / static_cast<double>(kSr))); };
}

// A deterministic noise source that differs between the two channels.
static Signal noise(std::uint32_t seed)
{
    return [=](std::size_t n) {
        std::uint32_t x = static_cast<std::uint32_t>(n) * 2654435761u + seed;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
        return static_cast<float>(static_cast<std::int32_t>(x)) * (1.0f / 2147483648.0f);
    };
}

// Keeps one engine running across several calls, so parameters can change part-way, and records the output and the meters.
struct Run {
    EngineState state;
    std::vector<float> l, r;
    std::size_t done = 0;
    Meters meters;
    // The transport the host reports: the beat position advances with the samples from `startBeat`.
    bool playing = false, valid = false;
    double bpm = 120.0, beat0 = 0.0;
    std::size_t origin = 0;       // the sample at which the beat position was beat0
    [[nodiscard]] double beatAt(std::size_t sample) const { return beat0 + static_cast<double>(sample - origin) / static_cast<double>(kSr) * bpm / 60.0; }
    void relocate(double beat) { beat0 = beat; origin = done; }                                 // the transport jumps
    void setTempo(double newBpm) { beat0 = beatAt(done); origin = done; bpm = newBpm; }         // the tempo changes, the position does not

    void go(const Parameters& p, float seconds, const Signal& inL, const Signal& inR, const std::vector<Timed>& events = {},
            std::uint32_t block = 256)
    {
        const auto total = static_cast<std::size_t>(seconds * kSr);
        std::vector<float> a(block), b(block), oa(block), ob(block);
        for (std::size_t pos = 0; pos < total; pos += block) {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, total - pos));
            for (std::uint32_t i = 0; i < n; ++i) { a[i] = inL(done + pos + i); b[i] = inR(done + pos + i); }
            std::vector<MidiEvent> here;
            for (const auto& te : events)
                if (te.frame >= done + pos && te.frame < done + pos + n) { MidiEvent e = te.ev; e.frame = static_cast<std::uint32_t>(te.frame - done - pos); here.push_back(e); }
            Transport t;
            t.playing = playing; t.valid = valid; t.bpm = bpm;
            t.beatPosition = beatAt(done + pos);
            processBlock(state, p, kSr, n, t, here.data(), static_cast<std::uint32_t>(here.size()), a.data(), b.data(), oa.data(), ob.data());
            l.insert(l.end(), oa.begin(), oa.begin() + n);
            r.insert(r.end(), ob.begin(), ob.begin() + n);
            meters = state.meters;
        }
        done += total;
    }
};

static Run run(const Parameters& p, float seconds, const Signal& in, const std::vector<Timed>& events = {}, std::uint32_t block = 256)
{
    Run r;
    r.go(p, seconds, in, in, events, block);
    return r;
}

// A cabinet with the effects off: both depths and Dopplers zero.
static Parameters flat()
{
    Parameters p;
    p.hornDepth = 0.0f; p.hornDoppler = 0.0f; p.drumDepth = 0.0f; p.drumDoppler = 0.0f;
    return p;
}

static double toneAmp(const std::vector<float>& x, std::size_t start, std::size_t n, double f)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const double dp = 2.0 * kPi * f / static_cast<double>(kSr), dw = 2.0 * kPi / static_cast<double>(n);
    const double rc = std::cos(dp), rs = std::sin(dp), wc = std::cos(dw), ws = std::sin(dw);
    double pc = std::cos(dp * static_cast<double>(start)), ps = std::sin(dp * static_cast<double>(start));
    double hc = 1.0, hs = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * hc;
        re += x[start + i] * w * pc;
        im -= x[start + i] * w * ps;
        wsum += w;
        const double npc = pc * rc - ps * rs; ps = ps * rc + pc * rs; pc = npc;
        const double nhc = hc * wc - hs * ws; hs = hs * wc + hc * ws; hc = nhc;
    }
    return 2.0 * std::hypot(re, im) / wsum;
}

// Peak of each block of `block` samples from `start`: the amplitude envelope of a tone.
static std::vector<double> envelope(const std::vector<float>& x, std::size_t start, std::size_t end, std::size_t block)
{
    std::vector<double> e;
    for (std::size_t p = start; p + block <= end; p += block) {
        double m = 0.0;
        for (std::size_t i = 0; i < block; ++i) m = std::max(m, static_cast<double>(std::fabs(x[p + i])));
        e.push_back(m);
    }
    return e;
}

static double ratioMinMax(const std::vector<double>& e)
{
    return *std::min_element(e.begin(), e.end()) / *std::max_element(e.begin(), e.end());
}

static double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
    double ma = 0.0, mb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double>(a.size()); mb /= static_cast<double>(a.size());
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    return sab / std::sqrt(saa * sbb + 1e-30);
}

// Instantaneous frequency of a clean tone from consecutive upward zero crossings (interpolated), over [start, end).
static std::pair<double, double> frequencySpan(const std::vector<float>& x, std::size_t start, std::size_t end)
{
    std::vector<double> t;
    for (std::size_t n = start + 1; n < end; ++n)
        if (x[n - 1] < 0.0f && x[n] >= 0.0f) t.push_back(static_cast<double>(n - 1) + static_cast<double>(-x[n - 1]) / static_cast<double>(x[n] - x[n - 1]));
    double lo = 1e9, hi = 0.0;
    for (std::size_t k = 0; k + 1 < t.size(); ++k) {
        const double f = static_cast<double>(kSr) / (t[k + 1] - t[k]);
        lo = std::min(lo, f); hi = std::max(hi, f);
    }
    return {lo, hi};
}

static bool allZero(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i < to; ++i) if (x[i] != 0.0f) return false;
    return true;
}

static double peak(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double m = 0.0;
    for (std::size_t i = from; i < to; ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
    return m;
}

// With the effects off the cabinet is a flat, delayed copy: the crossover bands sum back to unity at every frequency.
static void testFlatWhenEffectsAreOff()
{
    bool ok = true;
    for (const double f : {50.0, 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 12000.0}) {
        const Run r = run(flat(), 2.0f, tone(f));
        const double gain = toneAmp(r.l, 48000, 48000, f) / toneAmp(r.l, 48000, 48000, f) * (toneAmp(r.l, 48000, 48000, f) / 0.5);
        if (std::fabs(gain - 1.0) > 0.03) ok = false;
    }
    check("flat magnitude from 50 Hz to 12 kHz with the effects off", ok);
    for (const float xo : {300.0f, 1200.0f, 2000.0f}) {
        Parameters p = flat(); p.crossoverHz = xo;
        const Run r = run(p, 2.0f, tone(xo));
        check("flat at the crossover frequency", std::fabs(toneAmp(r.l, 48000, 48000, xo) / 0.5 - 1.0) < 0.03);
    }
    const Run r = run(flat(), 1.0f, tone(1000.0));
    check("L and R are identical with the effects off", r.l == r.r);
}

// Mix 0 is the dry signal, delayed to line up with the wet path, in stereo.
static void testDryPath()
{
    Parameters p; p.mix = 0.0f;
    Run r;
    r.go(p, 0.5f, noise(1), noise(2));
    const int d = dryDelaySamples(kSr);
    bool ok = d > 0;
    for (std::size_t n = static_cast<std::size_t>(d); n < r.l.size(); ++n)
        if (r.l[n] != noise(1)(n - static_cast<std::size_t>(d)) || r.r[n] != noise(2)(n - static_cast<std::size_t>(d))) ok = false;
    check("Mix 0 is the input delayed by the base delay, in stereo", ok);
    check("the dry delay is the base delay", d == static_cast<int>(std::lround(0.7e-3 * 48000.0)));
    Parameters q = flat(); q.outputDb = 6.0206f;
    const double a = toneAmp(run(flat(), 2.0f, tone(1000.0)).l, 48000, 48000, 1000.0);
    const double b = toneAmp(run(q, 2.0f, tone(1000.0)).l, 48000, 48000, 1000.0);
    check("Output +6.02 dB doubles the level", std::fabs(b / a - 2.0) < 0.002);
}

// Horn and drum levels move their own bands only.
static void testLevels()
{
    Parameters p = flat(); p.hornLevelDb = -24.0f;
    const double hi = toneAmp(run(p, 2.0f, tone(6000.0)).l, 48000, 48000, 6000.0) / 0.5;
    const double lo = toneAmp(run(p, 2.0f, tone(100.0)).l, 48000, 48000, 100.0) / 0.5;
    check("Horn Level -24 dB cuts the highs by 24 dB", std::fabs(hi / std::pow(10.0, -24.0 / 20.0) - 1.0) < 0.03);
    check("and leaves the lows alone", std::fabs(lo - 1.0) < 0.02);
    Parameters q = flat(); q.drumLevelDb = -24.0f;
    check("Drum Level -24 dB cuts the lows by 24 dB",
          std::fabs(toneAmp(run(q, 2.0f, tone(100.0)).l, 48000, 48000, 100.0) / 0.5 - std::pow(10.0, -24.0 / 20.0)) < 0.002);
    check("and leaves the highs alone", std::fabs(toneAmp(run(q, 2.0f, tone(3000.0)).l, 48000, 48000, 3000.0) / 0.5 - 1.0) < 0.02);
}

// The directional amplitude: a rotor facing the microphone is loudest, facing away it is (1 - depth) as loud.
static void testAmplitudeModulation()
{
    for (const float depth : {0.3f, 0.6f, 0.9f}) {
        Parameters p = flat(); p.hornDepth = depth; p.speed = 2.0f; p.hornAccelS = 0.1f;
        const Run r = run(p, 3.0f, tone(3000.0));
        const double got = ratioMinMax(envelope(r.l, 48000, 144000, 96));
        check("horn amplitude swings from 1 to 1 - depth", std::fabs(got - (1.0 - depth)) < 0.06);
    }
    for (const float depth : {0.4f, 0.8f}) {
        Parameters p = flat(); p.drumDepth = depth; p.speed = 2.0f; p.drumAccelS = 0.5f;
        const Run r = run(p, 4.0f, tone(100.0));
        const double got = ratioMinMax(envelope(r.l, 96000, 192000, 480));
        check("drum amplitude swings from 1 to 1 - depth", std::fabs(got - (1.0 - depth)) < 0.08);
    }
    Parameters q = flat(); q.hornDepth = 0.9f; q.drumDepth = 0.0f; q.speed = 2.0f; q.hornAccelS = 0.1f;
    check("the horn does not modulate the lows", ratioMinMax(envelope(run(q, 3.0f, tone(100.0)).l, 48000, 144000, 480)) > 0.97);
    Parameters s = flat(); s.drumDepth = 0.9f; s.speed = 2.0f; s.drumAccelS = 0.5f;
    check("the drum does not modulate the highs", ratioMinMax(envelope(run(s, 4.0f, tone(3000.0)).l, 96000, 192000, 96)) > 0.97);

    // The crossover decides which rotor a frequency belongs to.
    Parameters x = flat(); x.hornDepth = 0.9f; x.speed = 2.0f; x.hornAccelS = 0.1f;
    x.crossoverHz = 300.0f;
    const double lowXo = ratioMinMax(envelope(run(x, 3.0f, tone(1000.0)).l, 48000, 144000, 96));
    x.crossoverHz = 2000.0f;
    const double highXo = ratioMinMax(envelope(run(x, 3.0f, tone(1000.0)).l, 48000, 144000, 96));
    check("a low crossover hands 1 kHz to the horn, a high one to the drum", lowXo < 0.35 && highXo > 0.85);
}

// The shape of the lobes, read at a known angle: a stopped rotor sits at angle 0, and with the microphones 180 degrees apart
// each is a quarter turn off axis (lobe 0.5). The horn is tightly directional (lobe squared), the drum broader (linear).
static void testLobeShapes()
{
    for (const float depth : {0.4f, 0.8f}) {
        Parameters p = flat(); p.speed = 0.0f; p.micAngle = 180.0f; p.hornDepth = depth;
        const double got = toneAmp(run(p, 2.0f, tone(3000.0)).l, 48000, 48000, 3000.0) / 0.5;
        check("horn gain a quarter turn off axis is 1 - depth + depth / 4", std::fabs(got - (1.0 - depth + depth * 0.25)) < 0.01);
        Parameters d = flat(); d.speed = 0.0f; d.micAngle = 180.0f; d.drumDepth = depth;
        const double drum = toneAmp(run(d, 2.0f, tone(100.0)).l, 48000, 48000, 100.0) / 0.5;
        check("drum gain a quarter turn off axis is 1 - depth + depth / 2", std::fabs(drum - (1.0 - depth + depth * 0.5)) < 0.01);
    }
    Parameters on = flat(); on.speed = 0.0f; on.micAngle = 0.0f; on.hornDepth = 0.9f;
    check("facing the microphone is full level", std::fabs(toneAmp(run(on, 2.0f, tone(3000.0)).l, 48000, 48000, 3000.0) / 0.5 - 1.0) < 0.01);
}

// Doppler: the horn's pitch swings by +-2 pi f_rotor r / c of the tone frequency.
static void testDoppler()
{
    const double rate = 6.7, f = 3000.0;
    const double full = 2.0 * kPi * rate * kHornRadius / kSpeedOfSound;
    for (const float amount : {1.0f, 0.5f}) {
        Parameters p = flat(); p.hornDoppler = amount; p.speed = 2.0f; p.hornAccelS = 0.1f;
        const Run r = run(p, 3.0f, tone(f));
        const auto span = frequencySpan(r.l, 48000, 144000);
        const double dev = (span.second - span.first) * 0.5 / f;
        check("horn Doppler swing matches 2 pi f r / c", std::fabs(dev / (full * amount) - 1.0) < 0.05);
        check("and is centred on the tone", std::fabs((span.second + span.first) * 0.5 / f - 1.0) < 0.003);
    }
    Parameters off = flat(); off.speed = 2.0f; off.hornAccelS = 0.1f;
    const auto flatSpan = frequencySpan(run(off, 3.0f, tone(f)).l, 48000, 144000);
    check("no Doppler when it is off", (flatSpan.second - flatSpan.first) / f < 0.0005);
    Parameters d = flat(); d.drumDoppler = 1.0f; d.speed = 2.0f; d.drumAccelS = 0.5f;
    const auto drumSpan = frequencySpan(run(d, 5.0f, tone(100.0)).l, 144000, 240000);
    const double drumFull = 2.0 * kPi * 5.7 * kDrumRadius / kSpeedOfSound;
    check("drum Doppler swing matches its radius and rate", std::fabs((drumSpan.second - drumSpan.first) * 0.5 / 100.0 / drumFull - 1.0) < 0.12);
}

// The rotors' inertia: an exponential approach to the target with each rotor's own time constant, and a real stop.
static void testSpeedChanges()
{
    Parameters slow; slow.speed = 1.0f;
    Run r;
    r.go(slow, 40.0f, tone(440.0), tone(440.0));
    check("slow speeds are reached", std::fabs(r.meters.hornHz - slow.hornSlowHz) < 1e-3 && std::fabs(r.meters.drumHz - slow.drumSlowHz) < 1e-3);

    Parameters fast = slow; fast.speed = 2.0f;
    Run c;
    c.go(slow, 40.0f, tone(440.0), tone(440.0));
    c.go(fast, 0.8f, tone(440.0), tone(440.0));   // one horn time constant
    const double hornFrac = (c.meters.hornHz - slow.hornSlowHz) / (fast.hornFastHz - slow.hornSlowHz);
    check("the horn covers 63 % of the change in one time constant", std::fabs(hornFrac - (1.0 - std::exp(-1.0))) < 0.01);
    const double drumFrac = (c.meters.drumHz - slow.drumSlowHz) / (fast.drumFastHz - slow.drumSlowHz);
    check("the drum is much slower to get going", std::fabs(drumFrac - (1.0 - std::exp(-0.8 / 3.5))) < 0.01 && drumFrac < 0.5 * hornFrac);
    c.go(fast, 8.0f, tone(440.0), tone(440.0));
    check("the horn then reaches the fast rate", std::fabs(c.meters.hornHz - fast.hornFastHz) < 0.02);
    c.go(fast, 40.0f, tone(440.0), tone(440.0));
    check("and the drum too", std::fabs(c.meters.drumHz - fast.drumFastHz) < 0.01);

    // Stop: from fast, both rotors come to rest exactly and the angle stops moving.
    Parameters stop = fast; stop.speed = 0.0f; stop.hornAccelS = 0.1f; stop.drumAccelS = 0.5f;
    c.go(stop, 8.0f, tone(440.0), tone(440.0));
    check("Stop brings both rotors to rest exactly", c.meters.hornHz == 0.0f && c.meters.drumHz == 0.0f);
    const float turn = c.meters.hornTurn;
    c.go(stop, 1.0f, tone(440.0), tone(440.0));
    check("a stopped rotor does not move", c.meters.hornTurn == turn);
    Parameters sh = flat(); sh.speed = 0.0f;
    const Run still = run(sh, 2.0f, tone(3000.0));
    check("stopped rotors give a steady tone", ratioMinMax(envelope(still.l, 48000, 96000, 96)) > 0.999);
    Parameters sd = sh; sd.hornDepth = 0.6f; sd.hornDoppler = 1.0f;
    const Run still2 = run(sd, 2.0f, tone(3000.0));
    check("even with depth and Doppler up, a stopped rotor is static", ratioMinMax(envelope(still2.l, 48000, 96000, 96)) > 0.999);
}

// The two microphones: identical at 0 degrees, opposite at 180, in quadrature at 90.
static void testMicrophones()
{
    Parameters p = flat(); p.drumDepth = 0.8f; p.speed = 2.0f; p.drumAccelS = 0.5f;
    const auto corr = [&](float angle) {
        Parameters q = p; q.micAngle = angle;
        const Run r = run(q, 6.0f, tone(100.0));
        return correlation(envelope(r.l, 144000, 288000, 480), envelope(r.r, 144000, 288000, 480));
    };
    check("0 degrees: the two channels are the same", corr(0.0f) > 0.9999);
    check("180 degrees: the two channels are in antiphase", corr(180.0f) < -0.99);
    check("90 degrees: the two channels are in quadrature", std::fabs(corr(90.0f)) < 0.15);
    Parameters same = flat(); same.hornDepth = 0.6f; same.hornDoppler = 1.0f; same.micAngle = 0.0f; same.speed = 2.0f;
    const Run r = run(same, 1.0f, tone(2000.0));
    check("0 degrees: left and right are bit-identical, Doppler included", r.l == r.r);
    Parameters wide = same; wide.micAngle = 90.0f;
    const Run w = run(wide, 1.0f, tone(2000.0));
    check("90 degrees: left and right differ", w.l != w.r);
}

// MIDI: CC 1 and CC 2 take over Speed on the CC channel until the panel moves it.
static void testControllers()
{
    Parameters p; p.speed = 1.0f; p.hornAccelS = 0.1f; p.drumAccelS = 0.5f;
    const auto hornAt = [&](Parameters q, std::vector<Timed> ev, double secs = 3.0) {
        Run r; r.go(q, static_cast<float>(secs), tone(440.0), tone(440.0), ev);
        return static_cast<double>(r.meters.hornHz);
    };
    check("CC 1 at 127 selects Fast", std::fabs(hornAt(p, {controller(100, kCcSpeed, 127)}) - p.hornFastHz) < 0.01);
    Parameters fastP = p; fastP.speed = 2.0f;
    check("CC 1 below 64 selects Slow", std::fabs(hornAt(fastP, {controller(100, kCcSpeed, 10)}) - p.hornSlowHz) < 0.01);
    check("CC 2 at 127 brakes to Stop", hornAt(p, {controller(100, kCcBrake, 127)}, 6.0) == 0.0);
    check("CC on another channel is ignored", std::fabs(hornAt(p, {controller(100, kCcSpeed, 127, 5)}) - p.hornSlowHz) < 0.01);
    Parameters off = p; off.ccChannel = 0.0f;
    check("CC channel 0 ignores CC", std::fabs(hornAt(off, {controller(100, kCcSpeed, 127)}) - p.hornSlowHz) < 0.01);
    Parameters ch3 = p; ch3.ccChannel = 3.0f;
    check("CC follows the CC channel", std::fabs(hornAt(ch3, {controller(100, kCcSpeed, 127, 2)}) - p.hornFastHz) < 0.01);
    check("an unrelated CC does nothing", std::fabs(hornAt(p, {controller(100, 7, 127)}) - p.hornSlowHz) < 0.01);

    Run r; r.go(p, 1.0f, tone(440.0), tone(440.0), {controller(100, kCcSpeed, 127)});
    check("takeover is active", r.state.overrideActive);
    releaseOverride(r.state);
    r.go(p, 3.0f, tone(440.0), tone(440.0));
    check("after release the Speed parameter rules again", !r.state.overrideActive && std::fabs(r.meters.hornHz - p.hornSlowHz) < 0.01);
}

// Determinism and block-size independence: the render is a function of the input, events and sample position.
static void testDeterminism()
{
    Parameters p; p.speed = 2.0f; p.micAngle = 120.0f; p.crossoverHz = 650.0f;
    const std::vector<Timed> ev = {controller(5000, kCcBrake, 127), controller(60000, kCcSpeed, 127)};
    Run ref; ref.go(p, 2.0f, noise(7), noise(9), ev, 256);
    for (const std::uint32_t block : {1u, 7u, 64u, 333u, 4096u}) {
        Run o; o.go(p, 2.0f, noise(7), noise(9), ev, block);
        check("the render does not depend on the block size", o.l == ref.l && o.r == ref.r);
    }
    Run again; again.go(p, 2.0f, noise(7), noise(9), ev, 256);
    check("the render repeats exactly", again.l == ref.l);
    check("the render is not silent", peak(ref.l, 0, ref.l.size()) > 0.05);

    // In place: input and output may be the same buffer.
    EngineState s1, s2;
    std::vector<float> a(512), b(512), oa(512), ob(512);
    for (std::size_t i = 0; i < 512; ++i) { a[i] = noise(3)(i); b[i] = noise(4)(i); }
    std::vector<float> ia = a, ib = b;
    processBlock(s1, p, kSr, 512, Transport {}, nullptr, 0, a.data(), b.data(), oa.data(), ob.data());
    processBlock(s2, p, kSr, 512, Transport {}, nullptr, 0, ia.data(), ib.data(), ia.data(), ib.data());
    check("in-place processing gives the same result", ia == oa && ib == ob);
}

// Idle and stability: nothing rings forever, nothing blows up.
static void testIdleAndStability()
{
    Parameters p; p.speed = 2.0f;
    Run r;
    r.go(p, 1.0f, tone(500.0), tone(500.0));
    r.go(p, 6.0f, [](std::size_t) { return 0.0f; }, [](std::size_t) { return 0.0f; });
    check("exactly silent after a long idle", allZero(r.l, 48000 + 96000, r.l.size()) && allZero(r.r, 48000 + 96000, r.r.size()));
    Run n;
    n.go(p, 5.0f, noise(11), noise(12));
    bool finite = true;
    for (float v : n.l) if (!std::isfinite(v)) finite = false;
    check("full-scale noise stays finite and bounded", finite && peak(n.l, 0, n.l.size()) < 3.0);
    Parameters hot = p; hot.hornLevelDb = 12.0f; hot.drumLevelDb = 12.0f; hot.outputDb = 12.0f; hot.crossoverHz = 300.0f;
    Run h; h.go(hot, 3.0f, noise(13), noise(14));
    bool hotFinite = true;
    for (float v : h.l) if (!std::isfinite(v)) hotFinite = false;
    check("the extremes of every control stay finite", hotFinite);
}

static void testMetersAndReset()
{
    Parameters p; p.speed = 2.0f; p.hornAccelS = 0.1f;
    Run r;
    r.go(p, 2.0f, tone(440.0), tone(440.0));
    const float t0 = r.meters.hornTurn;
    r.go(p, 0.25f, tone(440.0), tone(440.0));
    double advanced = static_cast<double>(r.meters.hornTurn) - static_cast<double>(t0);
    advanced -= std::floor(advanced);
    double want = static_cast<double>(p.hornFastHz) * 0.25;
    want -= std::floor(want);
    check("the horn angle advances at the reported rate", std::fabs(advanced - want) < 0.01 || std::fabs(std::fabs(advanced - want) - 1.0) < 0.01);
    check("angles stay within one turn", r.meters.hornTurn >= 0.0f && r.meters.hornTurn < 1.0f && r.meters.drumTurn >= 0.0f && r.meters.drumTurn < 1.0f);
    resetState(r.state);
    check("reset stops the rotors at angle 0", r.state.hornHz == 0.0 && r.state.drumHz == 0.0 && r.state.hornTurn == 0.0 && !r.state.overrideActive);
    check("lobe is 1 facing the microphone and 0 facing away", std::fabs(lobe(0.0, 0.0) - 1.0) < 1e-12 && std::fabs(lobe(0.5, 0.0)) < 1e-12 && std::fabs(lobe(0.25, 0.0) - 0.5) < 1e-12);
    std::vector<float> z(16, 1.0f);
    processBlock(r.state, p, 0.0f, 16, Transport {}, nullptr, 0, z.data(), z.data(), z.data(), z.data());
    check("a zero sample rate gives silence", allZero(z, 0, 16));
}


// A locked rotor's angle seen from a block end, as the transport dictates it.
static double wrapDiff(double a, double b)
{
    double d = a - b;
    d -= std::floor(d + 0.5);
    return d;
}

// Beat mode: the rate comes from the tempo and the beats-per-turn table, and follows tempo changes.
static void testSyncRates()
{
    const double expected[12] = {8.0, 4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
    bool table = true;
    for (int i = 0; i < kDivisionCount; ++i) if (std::fabs(beatsPerTurn(static_cast<float>(i)) - expected[i]) > 1e-12) table = false;
    check("the beats-per-turn table", table && beatsPerTurn(-3.0f) == 8.0 && beatsPerTurn(99.0f) == 0.125);

    Parameters p; p.sync = 1.0f; p.speed = 2.0f; p.hornAccelS = 0.1f; p.drumAccelS = 0.5f;
    check("the target rate is tempo / 60 / beats per turn",
          std::fabs(targetHz(p, true, 120.0) - 2.0 / (1.0 / 3.0)) < 1e-9 && std::fabs(targetHz(p, false, 120.0) - 2.0 / 0.5) < 1e-9
          && std::fabs(targetHz(p, true, 90.0) - 1.5 * 3.0) < 1e-9);
    Parameters slow = p; slow.speed = 1.0f;
    check("Slow uses the slow divisions", std::fabs(targetHz(slow, true, 120.0) - 2.0 / 2.0) < 1e-9 && std::fabs(targetHz(slow, false, 120.0) - 2.0 / 3.0) < 1e-9);
    Parameters stop = p; stop.speed = 0.0f;
    check("Stop is still stop", targetHz(stop, true, 120.0) == 0.0);
    Parameters freeP = p; freeP.sync = 0.0f;
    check("Free mode ignores the tempo", targetHz(freeP, true, 60.0) == targetHz(freeP, true, 200.0) && targetHz(freeP, true, 60.0) == freeP.hornFastHz);
    check("no tempo falls back to 120 BPM", targetHz(p, true, 0.0) == targetHz(p, true, 120.0));

    Run r; r.playing = true; r.valid = true; r.bpm = 120.0;
    r.go(p, 6.0f, tone(440.0), tone(440.0));
    check("the rotors settle on the tempo-derived rates", std::fabs(r.meters.hornHz - 6.0) < 0.01 && std::fabs(r.meters.drumHz - 4.0) < 0.01);
    r.setTempo(90.0);
    r.go(p, 6.0f, tone(440.0), tone(440.0));
    check("and follow a tempo change", std::fabs(r.meters.hornHz - 4.5) < 0.01 && std::fabs(r.meters.drumHz - 3.0) < 0.01);
    Run nt; nt.playing = false; nt.valid = false;
    nt.go(p, 6.0f, tone(440.0), tone(440.0));
    check("without a transport it runs at the 120 BPM rate", std::fabs(nt.meters.hornHz - 6.0) < 0.01);
}

// Once up to speed, the angle is a function of the beat position alone: any starting point converges onto it.
static void testSyncLock()
{
    Parameters p; p.sync = 1.0f; p.speed = 2.0f; p.hornAccelS = 0.1f; p.drumAccelS = 0.5f;
    const double bpm = 120.0;
    struct Seen { double horn, drum, beat; float locked; };
    const auto angleAfter = [&](double startBeat, float seconds, const Parameters& q, bool playing = true) {
        Run r; r.playing = playing; r.valid = true; r.bpm = bpm; r.beat0 = startBeat;
        r.go(q, seconds, tone(440.0), tone(440.0));
        return Seen {static_cast<double>(r.meters.hornTurn), static_cast<double>(r.meters.drumTurn),
                     r.beatAt(r.done), r.meters.locked};
    };
    const Seen a = angleAfter(0.0, 12.0f, p);
    // The meters are read at the end of the last block, a few samples before the block end.
    const double slack = 0.01;
    check("the horn angle is the transport's", std::fabs(wrapDiff(a.horn, lockedTurn(a.beat, 1.0 / 3.0, 0.0))) < slack);
    check("the drum angle is the transport's", std::fabs(wrapDiff(a.drum, lockedTurn(a.beat, 0.5, 0.0))) < slack);
    check("the lock is reported", a.locked == 1.0f);

    const Seen b = angleAfter(37.3, 12.0f, p);
    check("a different start position locks to its own angle", std::fabs(wrapDiff(b.horn, lockedTurn(b.beat, 1.0 / 3.0, 0.0))) < slack);

    // Sync Phase shifts the lock.
    Parameters ph = p; ph.syncPhase = 90.0f;
    const Seen c = angleAfter(0.0, 12.0f, ph);
    check("Sync Phase 90 degrees moves the horn angle by a quarter turn", std::fabs(wrapDiff(c.horn, lockedTurn(c.beat, 1.0 / 3.0, 90.0))) < slack
          && std::fabs(wrapDiff(c.horn, a.horn + 0.25)) < slack + 0.002);

    // Two engines that have been somewhere else render the same signal once both are relocated to one position and have
    // locked: the sound is a function of the position, not of the history.
    Parameters q = p; q.hornDepth = 0.7f; q.hornDoppler = 1.0f;
    Run x, y;
    x.playing = y.playing = true; x.valid = y.valid = true; x.bpm = y.bpm = bpm;
    x.beat0 = 3.0; y.beat0 = 91.7;
    x.go(q, 3.0f, tone(3000.0), tone(3000.0));
    y.go(q, 2.0f, tone(3000.0), tone(3000.0));
    x.relocate(40.0); y.relocate(40.0);
    // Run each from its own sample counter but the same tone phase: use the same absolute input by aligning counters.
    const auto resume = [&](Run& r, std::size_t from) { r.done = from; r.origin = from; };
    resume(x, 0); resume(y, 0);
    x.l.clear(); y.l.clear();
    x.go(q, 10.0f, tone(3000.0), tone(3000.0));
    y.go(q, 10.0f, tone(3000.0), tone(3000.0));
    double worst = 0.0;
    for (std::size_t n = 8 * 48000; n < x.l.size(); ++n) worst = std::max(worst, static_cast<double>(std::fabs(x.l[n] - y.l[n])));
    check("the same position gives the same sound whatever came before", worst < 0.02);
    check("the horn is audibly modulated", ratioMinMax(envelope(x.l, 8 * 48000, 10 * 48000, 96)) < 0.5);

    // Relocating glides back onto the new position, and the lock returns.
    Run m; m.playing = true; m.valid = true; m.bpm = bpm;
    m.go(p, 8.0f, tone(440.0), tone(440.0));
    check("locked before the jump", m.meters.locked == 1.0f);
    m.relocate(100.37);
    m.go(p, 0.1f, tone(440.0), tone(440.0));
    m.go(p, 10.0f, tone(440.0), tone(440.0));
    check("after a relocation it is locked again, to the new position",
          m.meters.locked == 1.0f && std::fabs(wrapDiff(m.meters.hornTurn, lockedTurn(m.beatAt(m.done), 1.0 / 3.0, 0.0))) < slack);

    // A stopped transport does not lock; the rotors free-run at the tempo rate.
    const Seen s = angleAfter(0.0, 6.0f, p, false);
    check("a stopped transport is not locked", s.locked == 0.0f);
    // Free mode does not care about the transport at all.
    Parameters freeP = p; freeP.sync = 0.0f; freeP.hornDepth = 0.7f; freeP.hornDoppler = 1.0f;
    Run f1, f2; f1.playing = f2.playing = true; f1.valid = f2.valid = true; f1.bpm = f2.bpm = bpm;
    f2.beat0 = 55.5;
    f1.go(freeP, 2.0f, tone(3000.0), tone(3000.0)); f2.go(freeP, 2.0f, tone(3000.0), tone(3000.0));
    check("Free mode ignores the transport position", f1.l == f2.l);
    check("Free mode is never locked", f1.meters.locked == 0.0f);
}

// Locking is block-size independent and does not click: the angle never jumps by more than the glide allows.
static void testSyncSmoothness()
{
    Parameters p; p.sync = 1.0f; p.speed = 2.0f; p.hornDepth = 0.6f; p.hornDoppler = 1.0f; p.hornAccelS = 0.1f; p.drumAccelS = 0.5f;
    Run ref; ref.playing = true; ref.valid = true; ref.bpm = 133.0; ref.beat0 = 12.25;
    ref.go(p, 3.0f, noise(5), noise(6), {}, 256);
    for (const std::uint32_t block : {1u, 64u, 333u}) {
        Run o; o.playing = true; o.valid = true; o.bpm = 133.0; o.beat0 = 12.25;
        o.go(p, 3.0f, noise(5), noise(6), {}, block);
        // The transport position is read at block starts and advanced per sample inside the block, so block sizes agree to
        // rounding in the double beat position only.
        double worst = 0.0;
        for (std::size_t n = 0; n < ref.l.size(); ++n) worst = std::max(worst, static_cast<double>(std::fabs(o.l[n] - ref.l[n])));
        check("locked rendering does not depend on the block size", worst < 1e-4);
    }
    // A relocation moves the angle by a bounded glide: no sample-to-sample jump in a loud steady tone.
    Parameters q = p; q.hornDoppler = 0.0f;
    Run m; m.playing = true; m.valid = true; m.bpm = 120.0;
    m.go(q, 6.0f, tone(3000.0), tone(3000.0));
    const std::size_t at = m.l.size();
    m.relocate(53.1);
    m.go(q, 3.0f, tone(3000.0), tone(3000.0));
    double worst = 0.0;
    for (std::size_t n = at - 2000; n + 1 < m.l.size(); ++n) worst = std::max(worst, static_cast<double>(std::fabs(m.l[n + 1] - m.l[n])));
    // The amplitude envelope (peak of each 1 ms) is where a jump of the rotor angle shows: a hard jump is a step of about 0.1,
    // the glide changes it by under 0.01 per millisecond.
    const auto env = envelope(m.l, at - 4800, m.l.size(), 48);
    double step = 0.0;
    for (std::size_t k = 0; k + 1 < env.size(); ++k) step = std::max(step, std::fabs(env[k + 1] - env[k]));
    check("a relocation glides: the envelope never steps", step < 0.03);
    check("a relocation glides without a click", worst < 0.5 * 2.0 * kPi * 3000.0 / static_cast<double>(kSr) * 1.2);
}

static void testParameters()
{
    Parameters wild;
    wild.speed = 9.0f; wild.hornSlowHz = 0.0f; wild.hornFastHz = 99.0f; wild.crossoverHz = 5.0f; wild.micAngle = 400.0f;
    wild.mix = -1.0f; wild.outputDb = 99.0f; wild.ccChannel = 40.0f; wild.hornAccelS = 0.0f; wild.drumDepth = 5.0f;
    const Parameters c = clampParameters(wild);
    check("clamp speed and rates", c.speed == 2.0f && c.hornSlowHz == 0.3f && c.hornFastHz == 9.0f);
    check("clamp crossover, angle, mix, output", c.crossoverHz == 300.0f && c.micAngle == 180.0f && c.mix == 0.0f && c.outputDb == 12.0f);
    check("clamp channel, accel, depth", c.ccChannel == 16.0f && c.hornAccelS == 0.1f && c.drumDepth == 1.0f);

    const Parameters d;
    bool ok = true;
    std::set<std::string> symbols;
    for (std::uint32_t i = 0; i < kControlCount; ++i) {
        const auto& s = specs()[i];
        if (field(d, i) != s.defaultValue || s.defaultValue < s.minimum || s.defaultValue > s.maximum || s.output) ok = false;
        symbols.insert(s.symbol);
        Parameters t = d;
        field(t, i) = s.maximum;
        if (clampParameters(t).speed < 0.0f || field(clampParameters(t), i) != s.maximum) ok = false;   // the table range is the clamp range
        field(t, i) = s.minimum;
        if (field(clampParameters(t), i) != s.minimum) ok = false;
    }
    for (std::uint32_t i = kControlCount; i < kParameterCount; ++i) {
        symbols.insert(specs()[i].symbol);
        if (!specs()[i].output) ok = false;
    }
    check("table defaults match the core, ranges equal the clamp ranges", ok);
    check("every parameter has its own symbol", symbols.size() == kParameterCount);
    check("parameter ids are pinned", kSpeed == 0 && kCrossover == 7 && kMicAngle == 12 && kCcChannel == 17 && kSync == 18
          && kSyncPhase == 23 && kStatusHornHz == 24 && kStatusLocked == 28 && kParameterCount == 29 && kControlCount == 24
          && isOutput(kStatusLocked) && !isOutput(kSyncPhase));
    check("defaults: Slow, 90 degree mics, fully wet, Free", d.speed == 1.0f && d.micAngle == 90.0f && d.mix == 1.0f && d.sync == 0.0f);
    Parameters wild2;
    wild2.sync = 5.0f; wild2.hornSlowDiv = 99.0f; wild2.drumFastDiv = -4.0f; wild2.syncPhase = 900.0f;
    const Parameters c2 = clampParameters(wild2);
    check("clamp sync, divisions and phase", c2.sync == 1.0f && c2.hornSlowDiv == 11.0f && c2.drumFastDiv == 0.0f && c2.syncPhase == 360.0f);
}

int main()
{
    testFlatWhenEffectsAreOff();
    testDryPath();
    testLevels();
    testAmplitudeModulation();
    testLobeShapes();
    testDoppler();
    testSpeedChanges();
    testMicrophones();
    testControllers();
    testDeterminism();
    testIdleAndStability();
    testMetersAndReset();
    testSyncRates();
    testSyncLock();
    testSyncSmoothness();
    testParameters();
    std::printf("whirligig core tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
