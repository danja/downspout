#include "octet_core.hpp"
#include "octet_params.hpp"
#include "octet_spectrum.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::octet;

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

struct Result {
    std::vector<float> l, r;
};

static Result run(const Parameters& p, const std::vector<float>& inL, const std::vector<float>& inR, float sr = 48000.0f,
                  uint32_t block = 128)
{
    EngineState st;
    Result res;
    res.l.resize(inL.size());
    res.r.resize(inL.size());
    for (size_t pos = 0; pos < inL.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, inL.size() - pos));
        const float* ins[2] = {inL.data() + pos, inR.data() + pos};
        float* outs[2] = {res.l.data() + pos, res.r.data() + pos};
        processBlock(st, p, sr, n, ins, outs);
    }
    return res;
}

static std::vector<float> sine(double hz, double amp, double seconds, float sr = 48000.0f)
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

// Measured gain in dB of the left channel for a mono sine, after the filters have settled.
static double gainDb(const Parameters& p, double hz, float sr = 48000.0f)
{
    const double seconds = hz < 100.0 ? 1.6 : 0.8;
    const auto in = sine(hz, 0.05, seconds, sr);
    const Result r = run(p, in, in, sr);
    return dbOf(rms(r.l, in.size() / 2) / rms(in, in.size() / 2));
}

// A parameter set where only band 0 is on, with the given settings.
static Parameters solo(BandType type, double freq, double gain = 0.0, double q = 1.0)
{
    Parameters p;
    for (Band& b : p.bands) b.on = 0.0f;
    Band& b = p.bands[0];
    b.on = 1.0f;
    b.type = static_cast<float>(type);
    b.freqHz = static_cast<float>(freq);
    b.gainDb = static_cast<float>(gain);
    b.q = static_cast<float>(q);
    return p;
}

static void testTransparentByDefault()
{
    const Parameters defaults;
    const auto in = sine(440.0, 0.7, 0.2);
    const Result r = run(defaults, in, in);
    double worst = 0;
    for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(r.l[i] - in[i])));
    check("default settings pass the signal unchanged", worst == 0.0);
    int active = 0;
    for (const Band& b : defaults.bands) active += bandIsActive(b) ? 1 : 0;
    check("no band is active by default", active == 0);
    check("a bell at 0 dB is inactive but a notch is active", !bandIsActive(solo(BandType::Bell, 1000).bands[0]) == true &&
          bandIsActive(solo(BandType::Notch, 1000).bands[0]));
}

// What the panel plots must be what the filter does, for every band type.
static void testPlottedResponseMatchesAudio()
{
    struct Case { BandType t; double f, g, q; };
    const Case cases[] = {
        {BandType::HighPass12, 500, 0, 0.7}, {BandType::HighPass48, 300, 0, 0.7}, {BandType::LowShelf, 200, 9, 0.71},
        {BandType::Bell, 1000, 12, 2.0}, {BandType::Bell, 2000, -9, 0.5}, {BandType::Notch, 3000, 0, 4.0},
        {BandType::HighShelf, 5000, -8, 0.71}, {BandType::LowPass12, 4000, 0, 0.7}, {BandType::LowPass48, 6000, 0, 0.7}};
    for (float sr : {48000.0f, 96000.0f}) {
        for (const Case& c : cases) {
            const Parameters p = solo(c.t, c.f, c.g, c.q);
            double worst = 0;
            for (double hz : {60.0, 150.0, 400.0, 900.0, 1500.0, 2800.0, 5000.0, 9000.0}) {
                if (hz > sr * 0.4) continue;
                worst = std::max(worst, std::fabs(responseDb(p, hz, sr) - gainDb(p, hz, sr)));
            }
            if (worst >= 0.15) std::printf("  type %d f %.0f g %.0f sr %.0f: worst %.3f dB\n", static_cast<int>(c.t), c.f, c.g, sr, worst);
            check("plotted response matches the measured filter", worst < 0.15);
        }
    }
}

static void testBandShapes()
{
    // Bell: the set gain at the centre; unity far away.
    const Parameters bell = solo(BandType::Bell, 1000, 12.0, 2.0);
    check("bell reaches its gain at the centre", std::fabs(responseDb(bell, 1000.0, 48000.0) - 12.0) < 0.01);
    check("bell is unity far below", std::fabs(responseDb(bell, 30.0, 48000.0)) < 0.2);
    check("bell is unity far above", std::fabs(responseDb(bell, 16000.0, 48000.0)) < 0.5);
    // Higher Q is narrower: the response 1/2 octave away is smaller.
    const double wide = responseDb(solo(BandType::Bell, 1000, 12, 0.5), 1414.0, 48000.0);
    const double narrow = responseDb(solo(BandType::Bell, 1000, 12, 8.0), 1414.0, 48000.0);
    check("higher Q gives a narrower bell", narrow < wide - 3.0);

    const Parameters ls = solo(BandType::LowShelf, 1000, 12.0, 0.71);
    check("low shelf reaches its gain well below", std::fabs(responseDb(ls, 30.0, 48000.0) - 12.0) < 0.3);
    check("low shelf is unity well above", std::fabs(responseDb(ls, 15000.0, 48000.0)) < 0.3);
    const Parameters hs = solo(BandType::HighShelf, 1000, -12.0, 0.71);
    check("high shelf reaches its gain well above", std::fabs(responseDb(hs, 15000.0, 48000.0) + 12.0) < 0.5);
    check("high shelf is unity well below", std::fabs(responseDb(hs, 30.0, 48000.0)) < 0.3);

    const Parameters notch = solo(BandType::Notch, 1000, 0, 4.0);
    check("notch removes its centre frequency (> 40 dB)", gainDb(notch, 1000.0) < -40.0);
    check("notch leaves a distant frequency alone", std::fabs(gainDb(notch, 100.0)) < 0.2);
}

// A 48 dB cut is an 8th-order Butterworth: -3 dB at the cutoff and 48 dB per octave beyond it.
static void testCutSlopes()
{
    for (BandType t : {BandType::HighPass48, BandType::LowPass48}) {
        const bool hp = t == BandType::HighPass48;
        const Parameters p = solo(t, 1000.0);
        check("48 dB cut is -3 dB at the cutoff", std::fabs(responseDb(p, 1000.0, 96000.0) + 3.01) < 0.05);
        const double oct = hp ? 500.0 : 2000.0;
        const double twoOct = hp ? 250.0 : 4000.0;
        const double d1 = responseDb(p, oct, 96000.0), d2 = responseDb(p, twoOct, 96000.0);
        if (std::fabs((d2 - d1) + 48.0) >= 2.0) std::printf("  slope %.2f dB/oct (%.2f -> %.2f)\n", d2 - d1, d1, d2);
        check("48 dB cut falls about 48 dB per octave", std::fabs((d2 - d1) + 48.0) < 2.0);
        // Measured on audio too, not only on the formula.
        check("48 dB cut on audio, one octave out", std::fabs(gainDb(p, oct, 96000.0) - d1) < 0.5);
    }
    for (BandType t : {BandType::HighPass12, BandType::LowPass12}) {
        const Parameters p = solo(t, 1000.0);
        check("12 dB cut is -3 dB at the cutoff", std::fabs(responseDb(p, 1000.0, 96000.0) + 3.01) < 0.05);
        const bool hp = t == BandType::HighPass12;
        const double d1 = responseDb(p, hp ? 500.0 : 2000.0, 96000.0), d2 = responseDb(p, hp ? 250.0 : 4000.0, 96000.0);
        check("12 dB cut falls about 12 dB per octave", std::fabs((d2 - d1) + 12.0) < 1.0);
    }
    // Low cut at 20 Hz must still work at high sample rates (the poles sit near z = 1).
    const Parameters low = solo(BandType::HighPass48, 20.0);
    check("a 20 Hz 48 dB high-pass is -3 dB at 20 Hz at 192 kHz", std::fabs(gainDb(low, 20.0, 192000.0) + 3.01) < 0.3);
}

static void testStereoModes()
{
    const auto tone = sine(1000.0, 0.1, 0.5);
    const auto minus = [&] { auto v = tone; for (float& x : v) x = -x; return v; }();

    Parameters boost = solo(BandType::Bell, 1000, 12.0, 1.0);
    // Stereo mode: every band acts on both channels whatever its Channel says.
    boost.mode = static_cast<float>(StereoMode::Stereo);
    boost.bands[0].channel = static_cast<float>(Channel::B);
    Result r = run(boost, tone, tone);
    check("Stereo mode ignores the channel assignment", std::fabs(dbOf(rms(r.l, tone.size() / 2) / rms(tone, tone.size() / 2)) - 12.0) < 0.5 &&
          std::fabs(dbOf(rms(r.r, tone.size() / 2) / rms(tone, tone.size() / 2)) - 12.0) < 0.5);

    // L/R: A is left, B is right.
    boost.mode = static_cast<float>(StereoMode::LeftRight);
    boost.bands[0].channel = static_cast<float>(Channel::A);
    r = run(boost, tone, tone);
    const double lg = dbOf(rms(r.l, tone.size() / 2) / rms(tone, tone.size() / 2));
    const double rg = dbOf(rms(r.r, tone.size() / 2) / rms(tone, tone.size() / 2));
    check("L/R channel A boosts only the left", std::fabs(lg - 12.0) < 0.5 && std::fabs(rg) < 0.05);
    boost.bands[0].channel = static_cast<float>(Channel::Both);
    r = run(boost, tone, tone);
    check("L/R channel Both boosts both", std::fabs(dbOf(rms(r.r, tone.size() / 2) / rms(tone, tone.size() / 2)) - 12.0) < 0.5);

    // M/S: a band on Mid changes a mono signal; the same band on Side leaves it alone and acts on L = -R.
    boost.mode = static_cast<float>(StereoMode::MidSide);
    boost.bands[0].channel = static_cast<float>(Channel::A);
    r = run(boost, tone, tone);
    check("M/S: a Mid band boosts a mono signal", std::fabs(dbOf(rms(r.l, tone.size() / 2) / rms(tone, tone.size() / 2)) - 12.0) < 0.5);
    r = run(boost, tone, minus);
    check("M/S: a Mid band leaves a pure-side signal alone", std::fabs(dbOf(rms(r.l, tone.size() / 2) / rms(tone, tone.size() / 2))) < 0.05);
    boost.bands[0].channel = static_cast<float>(Channel::B);
    r = run(boost, tone, minus);
    check("M/S: a Side band boosts a pure-side signal", std::fabs(dbOf(rms(r.l, tone.size() / 2) / rms(tone, tone.size() / 2)) - 12.0) < 0.5);
    r = run(boost, tone, tone);
    check("M/S: a Side band leaves a mono signal alone", std::fabs(dbOf(rms(r.l, tone.size() / 2) / rms(tone, tone.size() / 2))) < 0.05);
    check("M/S keeps the mono signal mono", [&] {
        double d = 0;
        for (size_t i = 0; i < r.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(r.l[i] - r.r[i])));
        return d < 1e-6;
    }());
}

static void testBandsCombine()
{
    Parameters p;
    for (Band& b : p.bands) b.on = 0.0f;
    p.bands[2].on = 1.0f; p.bands[2].gainDb = 6.0f; p.bands[2].freqHz = 500.0f;
    p.bands[3].on = 1.0f; p.bands[3].gainDb = 6.0f; p.bands[3].freqHz = 500.0f;
    check("two identical bells add their gains", std::fabs(responseDb(p, 500.0, 48000.0) - 12.0) < 0.01);
    check("two identical bells add their gains in the audio", std::fabs(gainDb(p, 500.0) - 12.0) < 0.2);
    p.bands[3].on = 0.0f;
    check("turning a band off removes its effect", std::fabs(responseDb(p, 500.0, 48000.0) - 6.0) < 0.01);
}

static void testOutputGain()
{
    Parameters p;
    p.outputDb = -6.0f;
    check("output gain applies even with every band off", std::fabs(gainDb(p, 1000.0) + 6.0) < 0.05);
}

static void testEveryControlMatters()
{
    const auto in = sine(900.0, 0.3, 0.3);
    Parameters base = solo(BandType::Bell, 900, 6.0, 1.0);
    base.bands[1].on = 1.0f; base.bands[1].type = static_cast<float>(BandType::LowShelf);
    base.bands[1].gainDb = 3.0f; base.bands[1].freqHz = 1500.0f;
    const Result ref = run(base, in, in);
    auto differs = [&](const Parameters& q) {
        const Result o = run(q, in, in);
        double d = 0;
        for (size_t i = 0; i < in.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(o.l[i] - ref.l[i])));
        return d > 1e-4;
    };
    Parameters q = base; q.bands[0].on = 0.0f;             check("band on/off has an effect", differs(q));
    q = base; q.bands[0].type = static_cast<float>(BandType::Notch); check("band type has an effect", differs(q));
    q = base; q.bands[0].freqHz = 1200.0f;                 check("band frequency has an effect", differs(q));
    q = base; q.bands[0].gainDb = -6.0f;                   check("band gain has an effect", differs(q));
    q = base; q.bands[0].q = 6.0f;                         check("band Q has an effect", differs(q));
    q = base; q.mode = 2.0f; q.bands[0].channel = 2.0f;    check("mode and channel have an effect", differs(q));
    q = base; q.outputDb = 3.0f;                           check("output has an effect", differs(q));
    // Gain and Q are ignored by the types that do not use them, and that must be a documented fact.
    check("cuts do not use gain or Q", !bandUsesGain(solo(BandType::HighPass48, 100).bands[0]) && !bandUsesQ(solo(BandType::LowPass12, 100).bands[0]));
    check("notch uses Q but not gain", bandUsesQ(solo(BandType::Notch, 100).bands[0]) && !bandUsesGain(solo(BandType::Notch, 100).bands[0]));
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
            p.mode = static_cast<float>(blk % 3);
            for (int b = 0; b < kBands; ++b) {
                Band& band = p.bands[static_cast<size_t>(b)];
                band.on = 1.0f;
                band.type = static_cast<float>((blk + b) % kBandTypeCount);
                band.freqHz = 20.0f * std::pow(1000.0f, static_cast<float>((blk * 7 + b * 13) % 100) / 99.0f);
                band.gainDb = -18.0f + static_cast<float>((blk * 5 + b) % 37);
                band.q = 0.1f + static_cast<float>((blk * 3 + b) % 179) * 0.1f;
                band.channel = static_cast<float>((blk + b) % 3);
            }
            for (size_t i = 0; i < in.size(); ++i) in[i] = ((blk + i) % 7 == 0) ? 0.9f : -0.2f;
            const float* ins[2] = {in.data(), in.data()};
            float* outs[2] = {l.data(), r.data()};
            processBlock(st, p, sr, 256, ins, outs);
            for (float v : l) { if (!std::isfinite(v)) bad = true; mx = std::max(mx, static_cast<double>(std::fabs(v))); }
        }
        check("no NaN or inf under random parameters", !bad);
        (void)mx;
    }
    // Stability is that the ringing dies away, not that the gain is small: stacked boosts can
    // legitimately amplify by well over 100 dB. Impulse response over many random settings.
    for (float sr : {44100.0f, 96000.0f, 192000.0f}) {
        bool decays = true;
        for (int s = 0; s < 40; ++s) {
            Parameters p;
            p.mode = static_cast<float>(s % 3);
            for (int b = 0; b < kBands; ++b) {
                Band& band = p.bands[static_cast<size_t>(b)];
                band.on = 1.0f;
                band.type = static_cast<float>((s * 3 + b) % kBandTypeCount);
                band.freqHz = 20.0f * std::pow(1000.0f, static_cast<float>((s * 11 + b * 17) % 100) / 99.0f);
                band.gainDb = -18.0f + static_cast<float>((s * 7 + b * 5) % 37);
                band.q = 0.1f + static_cast<float>((s * 13 + b) % 179) * 0.1f;
                band.channel = static_cast<float>((s + b) % 3);
            }
            const size_t n = static_cast<size_t>(sr * 4.0f);
            std::vector<float> in(n, 0.0f);
            in[0] = 1.0f;
            const Result r = run(p, in, in, sr, 512);
            double early = 0, late = 0;
            for (size_t i = 0; i < static_cast<size_t>(sr * 0.5f); ++i) early = std::max(early, static_cast<double>(std::fabs(r.l[i])));
            for (size_t i = n - static_cast<size_t>(sr * 0.2f); i < n; ++i) late = std::max(late, static_cast<double>(std::fabs(r.l[i])));
            if (!std::isfinite(early) || !(late <= 1e-3 * early + 1e-9)) decays = false;
        }
        check("impulse response decays for random settings", decays);
    }
    EngineState st;
    std::vector<float> in(64, std::nanf("")), l(64), r(64);
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {l.data(), r.data()};
    Parameters p = solo(BandType::Bell, 1000, 6.0);
    processBlock(st, p, 48000.0f, 64, ins, outs);
    bool finite = true;
    for (float v : l) if (!std::isfinite(v)) finite = false;
    check("NaN input does not poison the output", finite);
}

static void testBlockSizeIndependence()
{
    Parameters p = solo(BandType::HighPass48, 200.0);
    p.bands[2].on = 1.0f; p.bands[2].gainDb = 8.0f; p.bands[2].freqHz = 700.0f;
    p.mode = 2.0f;
    const auto in = sine(440.0, 0.4, 0.3);
    const Result a = run(p, in, in, 48000.0f, 16);
    const Result b = run(p, in, in, 48000.0f, 480);
    double worst = 0;
    for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
    check("block size does not change the output", worst < 1e-6);
}

static void testOutputGainIsSmoothed()
{
    EngineState st;
    Parameters p;
    const auto in = sine(200.0, 0.3, 0.05);
    std::vector<float> l(in.size()), r(in.size());
    const uint32_t half = static_cast<uint32_t>(in.size() / 2);
    const float* i1[2] = {in.data(), in.data()};
    float* o1[2] = {l.data(), r.data()};
    processBlock(st, p, 48000.0f, half, i1, o1);
    p.outputDb = -12.0f;
    const float* i2[2] = {in.data() + half, in.data() + half};
    float* o2[2] = {l.data() + half, r.data() + half};
    processBlock(st, p, 48000.0f, half, i2, o2);
    double worst = 0;
    for (size_t i = half - 2; i < half + 2; ++i) worst = std::max(worst, static_cast<double>(std::fabs(l[i + 1] - l[i])));
    check("output gain changes do not step at the block edge", worst < 0.05);
}

static void testBandMovesGlide()
{
    // A narrow +18 dB bell on a 1 kHz tone is moved 50 Hz away in one step: the band's coefficients glide there over a few
    // milliseconds instead of jumping, and settle exactly on the target.
    Parameters p = solo(BandType::Bell, 1000.0, 18.0, 18.0);
    const auto in = sine(1000.0, 0.05, 0.4);
    EngineState st;
    std::vector<float> out(in.size()), outR(in.size());
    const uint32_t split = 9600;
    const float* i1[2] = {in.data(), in.data()};
    float* o1[2] = {out.data(), outR.data()};
    processBlock(st, p, 48000.0f, split, i1, o1);
    p.bands[0].freqHz = 1050.0f;
    const uint32_t rest = static_cast<uint32_t>(in.size()) - split;
    const float* i2[2] = {in.data() + split, in.data() + split};
    float* o2[2] = {out.data() + split, outR.data() + split};
    processBlock(st, p, 48000.0f, rest, i2, o2);
    const double oldDb = responseDb(solo(BandType::Bell, 1000.0, 18.0, 18.0), 1000.0, 48000.0);
    const double newDb = responseDb(p, 1000.0, 48000.0);
    std::vector<float> early(out.begin() + split, out.begin() + split + 96);
    std::vector<float> src(in.begin() + split, in.begin() + split + 96);
    const double earlyDb = dbOf(rms(early, 0) / rms(src, 0));
    check("the level has barely left the old response a couple of cycles after the move", earlyDb > 0.5 * (oldDb + newDb));
    const std::vector<float> tail(out.end() - 4800, out.end());
    const std::vector<float> tailIn(in.end() - 4800, in.end());
    check("... and settles on the new response", std::fabs(dbOf(rms(tail, 0) / rms(tailIn, 0)) - newDb) < 0.1);
}

static void testSwitchingABandOffClearsItsMemory()
{
    EngineState st;
    Parameters p = solo(BandType::Bell, 1000, 12.0, 4.0);
    std::vector<float> loud = sine(1000.0, 0.5, 0.1), silent(4800, 0.0f), l(loud.size()), r(loud.size());
    const float* li[2] = {loud.data(), loud.data()};
    float* lo[2] = {l.data(), r.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(loud.size()), li, lo);
    p.bands[0].on = 0.0f;
    std::vector<float> sl(silent.size()), sr(silent.size());
    const float* si[2] = {silent.data(), silent.data()};
    float* so[2] = {sl.data(), sr.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(silent.size()), si, so);
    p.bands[0].on = 1.0f;
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(silent.size()), si, so);
    double mx = 0;
    for (float v : sl) mx = std::max(mx, static_cast<double>(std::fabs(v)));
    check("re-enabling a band does not replay old memory as a click", mx == 0.0);
}

static void testSilentDecayLeavesNoDenormals()
{
    EngineState st;
    Parameters p = solo(BandType::HighPass48, 40.0);
    p.bands[1].on = 1.0f; p.bands[1].gainDb = 6.0f;
    std::vector<float> loud = sine(300.0, 0.8, 0.2), silent(480, 0.0f), l(loud.size()), r(loud.size());
    const float* li[2] = {loud.data(), loud.data()};
    float* lo[2] = {l.data(), r.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(loud.size()), li, lo);
    for (int blk = 0; blk < 6000; ++blk) {
        const float* si[2] = {silent.data(), silent.data()};
        processBlock(st, p, 48000.0f, 480, si, lo);
    }
    bool denormal = false;
    for (const BandRuntime& br : st.bands)
        for (const auto& ch : br.state)
            for (const SectionState& s : ch)
                for (double v : {s.z1, s.z2})
                    if (v != 0.0 && std::fabs(v) < 2.2250738585072014e-308) denormal = true;
    check("states are flushed to zero, never left denormal", !denormal);
}

static void testResetClearsMemoryOnly()
{
    EngineState st;
    Parameters p = solo(BandType::Bell, 800, 9.0);
    const auto in = sine(800.0, 0.4, 0.1);
    std::vector<float> l(in.size()), r(in.size());
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {l.data(), r.data()};
    processBlock(st, p, 48000.0f, static_cast<uint32_t>(in.size()), ins, outs);
    check("state is non-trivial after audio", st.bands[0].state[0][0].z1 != 0.0);
    resetState(st);
    check("reset clears the filter memory", st.bands[0].state[0][0].z1 == 0.0 && !st.bands[0].active);
    check("reset leaves the parameters alone", p.bands[0].gainDb == 9.0f);
}

// Drift's CC 1-4 on a channel drive one band's Frequency, Gain and Q, and the Output.
static void testMidiControl()
{
    check("CC 1 spans 20 Hz to 20 kHz logarithmically", std::fabs(ccToValue(0, 0) - 20.0f) < 0.01f && std::fabs(ccToValue(0, 127) - 20000.0f) < 1.0f &&
          std::fabs(ccToValue(0, 64) / 20.0f - std::pow(1000.0f, 64.0f / 127.0f)) < 0.5f);
    check("CC 2 is gain, +-18 dB", ccToValue(1, 0) == -18.0f && ccToValue(1, 127) == 18.0f);
    check("CC 3 is Q, 0.1 to 18 logarithmically", std::fabs(ccToValue(2, 0) - 0.1f) < 1e-4f && std::fabs(ccToValue(2, 127) - 18.0f) < 1e-3f);
    check("CC 4 is output, +-12 dB", ccToValue(3, 0) == -12.0f && ccToValue(3, 127) == 12.0f);
    check("a controller matches only its channel, and 0 ignores MIDI", ccChannelMatches(0xB0, 1.0f) && !ccChannelMatches(0xB1, 1.0f) &&
          ccChannelMatches(0xBF, 16.0f) && !ccChannelMatches(0xB0, 0.0f) && !ccChannelMatches(0x90, 1.0f));
    LiveControl live;
    handleController(live, 5, 100);
    handleController(live, 1, 64);
    check("only CC 1-4 are taken", live.active[0] && !live.active[1] && !live.active[2] && !live.active[3]);
    handleController(live, 2, 127);
    handleController(live, 3, 127);
    handleController(live, 4, 0);
    Parameters p;
    p.ccBand = 6.0f;
    const Parameters o = withLive(p, live);
    const Band& d = o.bands[5];
    check("the chosen band takes frequency, gain and Q", d.freqHz == live.value[0] && d.gainDb == 18.0f && std::fabs(d.q - 18.0f) < 1e-3f);
    check("... and the output follows CC 4", o.outputDb == -12.0f);
    bool others = true;
    for (int b = 0; b < kBands; ++b)
        if (b != 5 && (o.bands[static_cast<size_t>(b)].freqHz != p.bands[static_cast<size_t>(b)].freqHz || o.bands[static_cast<size_t>(b)].gainDb != p.bands[static_cast<size_t>(b)].gainDb)) others = false;
    check("the other bands are untouched", others);
    check("the panel releases exactly the controls the CCs hold", liveSlot(bandParameter(5, kFreq), 6) == 0 && liveSlot(bandParameter(5, kGain), 6) == 1 &&
          liveSlot(bandParameter(5, kQ), 6) == 2 && liveSlot(kOutput, 6) == 3 && liveSlot(bandParameter(4, kFreq), 6) == -1 && liveSlot(kCcBand, 6) == -1);
    // On audio: CC 2 lifting a bell at 1 kHz changes the level of a 1 kHz tone.
    Parameters q = solo(BandType::Bell, 1000.0, 0.0, 1.0);
    q.ccBand = 1.0f;
    LiveControl l2;
    handleController(l2, 2, 127);
    const auto in = sine(1000.0, 0.05, 0.8);
    EngineState st;
    std::vector<float> out(in.size()), outR(in.size());
    const Parameters e = withLive(q, l2);
    for (size_t pos = 0; pos < in.size(); pos += 128) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(128, in.size() - pos));
        const float* ins[2] = {in.data() + pos, in.data() + pos};
        float* outs[2] = {out.data() + pos, outR.data() + pos};
        processBlock(st, e, 48000.0f, n, ins, outs);
    }
    check("CC 2 at full lifts the chosen band by 18 dB", std::fabs(dbOf(rms(out, in.size() / 2) / rms(in, in.size() / 2)) - 18.0) < 0.3);
}

static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    check("117 parameters (CC channel and band, spectrum source and 64 spectrum outputs are appended after the band controls)",
          kParameterCount == 117 && kCcChannel == 50 && kCcBand == 51 && kSpectrumSource == 52 && kSpectrumFirst == 53);
    int outputs = 0;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = specs()[i];
        if (s.output) {   // measured from the audio: not part of the patch, so the core neither stores nor clamps them
            ++outputs;
            check("outputs are exactly the spectrum bins", isSpectrumBin(i));
            check("a spectrum output runs from the floor up to +6 dB, resting at the floor",
                  s.minimum == kSpectrumFloorDb && s.maximum == 6.0f && s.defaultValue == kSpectrumFloorDb);
            check("symbol and name are set", s.symbol[0] != '\0' && s.name[0] != '\0');
            continue;
        }
        check("table default equals the core default", field(defaults, i) == s.defaultValue);
        check("default is inside its range", s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        field(probe, i) = s.minimum - 1000.0f;
        check("core clamps to the table minimum", field(clampParameters(probe), i) == s.minimum);
        field(probe, i) = s.maximum + 1000.0f;
        check("core clamps to the table maximum", field(clampParameters(probe), i) == s.maximum);
        field(probe, i) = s.defaultValue;
        check("a logarithmic range has a positive minimum", !s.logarithmic || s.minimum > 0.0f);
        check("symbol and name are set", s.symbol[0] != '\0' && s.name[0] != '\0');
    }
    bool unique = true;
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        for (std::uint32_t j = i + 1; j < kParameterCount; ++j)
            if (std::string(specs()[i].symbol) == specs()[j].symbol) unique = false;
    check("symbols are unique", unique);
    check("64 outputs", outputs == kSpectrumBins);
    check("the spectrum shows the post-EQ signal by default", defaults.spectrumSource == 2.0f);
    check("band parameter indices are contiguous per band", bandParameter(0, kOn) == 2 && bandParameter(7, kChannel) == kBandControlEnd - 1);
}

// A fixed-frequency sine, run through the analyser in blocks of `block`.
static SpectrumState analyse(const std::vector<float>& sig, float sr = 48000.0f, uint32_t block = 128)
{
    SpectrumState st;
    resetSpectrum(st);
    for (size_t pos = 0; pos < sig.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, sig.size() - pos));
        analyseBlock(st, sr, n, sig.data() + pos, sig.data() + pos);
    }
    return st;
}

static int bandOf(double hz)
{
    for (int k = 0; k < kSpectrumBins; ++k)
        if (hz >= spectrumBandEdgeHz(k) && hz < spectrumBandEdgeHz(k + 1)) return k;
    return -1;
}

static void testSpectrum()
{
    check("the bands run from 20 Hz to 20 kHz", std::fabs(spectrumBandEdgeHz(0) - 20.0) < 1e-9 && std::fabs(spectrumBandEdgeHz(kSpectrumBins) - 20000.0) < 1e-6);

    const SpectrumState silent = analyse(std::vector<float>(48000, 0.0f));
    bool floor = true;
    for (float v : silent.levelDb) floor = floor && v == kSpectrumFloorDb;
    check("silence rests on the floor", floor);

    // A tone reads at its level in its own band and far below elsewhere, over the range of the display.
    for (const double hz : {55.0, 220.0, 1000.0, 4400.0, 12000.0}) {
        const SpectrumState st = analyse(sine(hz, 0.5, 0.5));
        const int band = bandOf(hz);
        float best = -200.0f;
        int bestBand = -1;
        for (int k = 0; k < kSpectrumBins; ++k)
            if (st.levelDb[static_cast<size_t>(k)] > best) { best = st.levelDb[static_cast<size_t>(k)]; bestBand = k; }
        char name[96];
        std::snprintf(name, sizeof(name), "%.0f Hz peaks in its own band (+-1)", hz);
        check(name, std::abs(bestBand - band) <= 1);
        std::snprintf(name, sizeof(name), "%.0f Hz at -6 dBFS reads about -6 dB (got %.1f)", hz, static_cast<double>(best));
        check(name, std::fabs(static_cast<double>(best) + 6.02) < 3.0);
        const int far = (band + kSpectrumBins / 2) % kSpectrumBins;   // 4+ octaves away
        std::snprintf(name, sizeof(name), "%.0f Hz leaves a band 4 octaves away more than 40 dB down", hz);
        check(name, st.levelDb[static_cast<size_t>(far)] < best - 40.0f);
    }

    // The block size makes no difference.
    const auto tone = sine(777.0, 0.4, 0.4);
    const SpectrumState a = analyse(tone, 48000.0f, 37), b = analyse(tone, 48000.0f, 4096);
    check("block size does not change the spectrum", a.levelDb == b.levelDb);

    // Falls slowly once the tone stops, and ends on the floor.
    std::vector<float> sig = sine(1000.0, 0.5, 0.5);
    const size_t toneEnd = sig.size();
    sig.resize(toneEnd + 12000, 0.0f);   // 0.25 s of silence
    const SpectrumState tail = analyse(sig);
    const int band = bandOf(1000.0);
    const float held = analyse(sine(1000.0, 0.5, 0.5)).levelDb[static_cast<size_t>(band)];
    check("a stopped tone fades no faster than the fall rate",
          tail.levelDb[static_cast<size_t>(band)] > held - kSpectrumFallDbPerSecond * 0.25f - 4.0f);
    check("a stopped tone has begun to fall", tail.levelDb[static_cast<size_t>(band)] < held - 3.0f);
    sig.resize(toneEnd + 48000 * 4, 0.0f);
    check("a stopped tone reaches the floor", analyse(sig).levelDb[static_cast<size_t>(band)] == kSpectrumFloorDb);

    // Bands above Nyquist stay on the floor at a low sample rate.
    const SpectrumState low = analyse(sine(1000.0, 0.5, 0.5, 22050.0f), 22050.0f);
    check("bands above Nyquist stay on the floor", low.levelDb[kSpectrumBins - 1] == kSpectrumFloorDb && low.levelDb[static_cast<size_t>(bandOf(1000.0))] > -12.0f);

    // Stereo is analysed as the mono sum: opposite channels cancel.
    {
        const auto l = sine(1000.0, 0.5, 0.3);
        std::vector<float> r(l.size());
        for (size_t i = 0; i < l.size(); ++i) r[i] = -l[i];
        SpectrumState st;
        resetSpectrum(st);
        analyseBlock(st, 48000.0f, static_cast<uint32_t>(l.size()), l.data(), r.data());
        check("out-of-phase channels cancel in the sum", st.levelDb[static_cast<size_t>(bandOf(1000.0))] == kSpectrumFloorDb);
    }

    // The tap matters: an EQ boost shows in the post signal, not the pre signal.
    {
        const auto in = sine(1000.0, 0.1, 0.5);
        const Result boosted = run(solo(BandType::Bell, 1000, 12.0, 1.0), in, in);
        const double pre = analyse(in).levelDb[static_cast<size_t>(bandOf(1000.0))];
        const double post = analyse(boosted.l).levelDb[static_cast<size_t>(bandOf(1000.0))];
        check("a +12 dB bell reads about 12 dB higher after the EQ than before", std::fabs((post - pre) - 12.0) < 1.5);
    }

    // Finite, and cheap: ten seconds of noise must take far less than real time.
    {
        std::vector<float> noise(480000);
        unsigned r = 1u;
        for (auto& x : noise) { r = r * 1664525u + 1013904223u; x = (static_cast<float>(r >> 8) / 8388608.0f - 1.0f) * 0.5f; }
        const auto t0 = std::chrono::steady_clock::now();
        const SpectrumState st = analyse(noise);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool finite = true;
        for (float v : st.levelDb) finite = finite && std::isfinite(v) && v >= kSpectrumFloorDb && v <= 6.0f;
        check("noise gives finite levels inside the range", finite);
        check("ten seconds of audio analyse in under a tenth of the time", sec < 1.0);
    }

    // Parameter: clamped, and a source that is Off is the only silent choice.
    Parameters p;
    p.spectrumSource = 9.0f;
    check("the spectrum source clamps to 0..2", clampParameters(p).spectrumSource == 2.0f);
    p.spectrumSource = -3.0f;
    check("the spectrum source clamps at 0", clampParameters(p).spectrumSource == 0.0f);
}

int main()
{
    testTransparentByDefault();
    testPlottedResponseMatchesAudio();
    testBandShapes();
    testCutSlopes();
    testStereoModes();
    testBandsCombine();
    testOutputGain();
    testEveryControlMatters();
    testStabilityAndBounds();
    testBlockSizeIndependence();
    testOutputGainIsSmoothed();
    testBandMovesGlide();
    testSwitchingABandOffClearsItsMemory();
    testSilentDecayLeavesNoDenormals();
    testResetClearsMemoryOnly();
    testMidiControl();
    testParameterTableMatchesCore();
    testSpectrum();
    std::printf("octet_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
