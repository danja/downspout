#include "octet_core.hpp"
#include "octet_params.hpp"

#include <algorithm>
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

static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    check("fifty parameters", kParameterCount == 50);
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = specs()[i];
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
    check("band parameter indices are contiguous per band", bandParameter(0, kOn) == 2 && bandParameter(7, kChannel) == kParameterCount - 1);
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
    testSwitchingABandOffClearsItsMemory();
    testSilentDecayLeavesNoDenormals();
    testResetClearsMemoryOnly();
    testParameterTableMatchesCore();
    std::printf("octet_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
