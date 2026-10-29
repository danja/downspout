#include "downspout/test_assert.h"
#include "infra_core.hpp"
#include "infra_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::infra;

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

// Renders `seconds` of a constant input of 1.0 on both channels, in blocks, with the transport advancing in step.
static Result render(const Parameters& p, double bpm, double startBeat, float seconds, bool playing = true, bool valid = true,
                     uint32_t block = 128, float input = 1.0f)
{
    Result res;
    const size_t total = static_cast<size_t>(seconds * kSr);
    res.l.resize(total);
    res.r.resize(total);
    std::vector<float> in(block, input);
    for (size_t pos = 0; pos < total; pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, total - pos));
        Transport t;
        t.playing = playing; t.valid = valid; t.bpm = bpm;
        t.beatPosition = startBeat + static_cast<double>(pos) / kSr * bpm / 60.0;
        const float* ins[2] = {in.data(), in.data()};
        float* outs[2] = {res.l.data() + pos, res.r.data() + pos};
        processBlock(res.state, p, kSr, n, t, ins, outs);
    }
    return res;
}

static double toDb(float g) { return 20.0 * std::log10(std::max(std::fabs(g), 1e-9f)); }

// Independent reference: the multiple nearest to 10 Hz by brute force over every allowed multiple.
static int bruteAuto(double bpm)
{
    int best = -1;
    double bestErr = 1e9;
    for (int m = 1; m <= kMaxMultiple; ++m) {
        const double hz = m * bpm / 60.0;
        if (hz < 1.0 - 1e-9 || hz > 20.0 + 1e-9) continue;
        if (std::fabs(hz - 10.0) < bestErr - 1e-12) { bestErr = std::fabs(hz - 10.0); best = m; }
    }
    return best;
}

static void testDefaults()
{
    const Parameters d;
    check("default waveform is sine", d.waveform == 0.0f);
    check("default multiple is Auto", d.multiple == 0.0f);
    check("both channels default to 90 degrees", d.phaseL == 1.0f && d.phaseR == 1.0f);
    check("default range is 3 dB", d.rangeDb == 3.0f);
    for (std::uint32_t i = 0; i < kControlCount; ++i)
        check("spec default equals the core default", specs()[i].defaultValue == field(d, i));
}

static void testMultipleSelection()
{
    check("120 BPM Auto is 5 cycles per beat (10 Hz)", effectiveMultiple(0.0f, 120.0) == 5);
    check("60 BPM Auto is 10", effectiveMultiple(0.0f, 60.0) == 10);
    check("140 BPM Auto is 4 (9.3 Hz)", effectiveMultiple(0.0f, 140.0) == 4);
    check("no tempo falls back to 120 BPM", effectiveMultiple(0.0f, 0.0) == 5);
    check("a request above 20 Hz is pulled down", effectiveMultiple(40.0f, 120.0) == 10);
    check("a request below 1 Hz is pulled up", effectiveMultiple(1.0f, 30.0) == 2);
    check("an explicit multiple in range is kept", effectiveMultiple(3.0f, 120.0) == 3);

    bool autoOk = true, allInRange = true;
    for (double bpm = 20.0; bpm <= 600.0; bpm += 0.5) {
        // Compare how close each is to 10 Hz, not the multiples: at some tempi two multiples tie exactly.
        if (std::fabs(rateHz(effectiveMultiple(0.0f, bpm), bpm) - 10.0) > std::fabs(rateHz(bruteAuto(bpm), bpm) - 10.0) + 1e-9) autoOk = false;
        for (int req = 0; req <= kMaxMultiple; ++req) {
            const double hz = rateHz(effectiveMultiple(static_cast<float>(req), bpm), bpm);
            if (hz < 1.0 - 1e-6 || hz > 20.0 + 1e-6) allInRange = false;
        }
    }
    check("Auto equals the brute-force nearest-to-10-Hz multiple from 20 to 600 BPM", autoOk);
    check("every multiple lands in 1..20 Hz from 20 to 600 BPM", allInRange);
}

static void testWaveforms()
{
    check("sine peaks a quarter cycle in", std::fabs(lfoValue(Waveform::Sine, 0.25) - 1.0f) < 1e-6f);
    check("sine troughs at three quarters", std::fabs(lfoValue(Waveform::Sine, 0.75)) < 1e-6f);
    check("sine starts mid-way", std::fabs(lfoValue(Waveform::Sine, 0.0) - 0.5f) < 1e-6f);
    check("triangle peaks at a quarter", std::fabs(lfoValue(Waveform::Triangle, 0.25) - 1.0f) < 1e-6f);
    check("triangle is linear between", std::fabs(lfoValue(Waveform::Triangle, 0.125) - 0.75f) < 1e-5f);
    check("ramp up rises", lfoValue(Waveform::RampUp, 0.1) < lfoValue(Waveform::RampUp, 0.9));
    check("ramp down falls", lfoValue(Waveform::RampDown, 0.1) > lfoValue(Waveform::RampDown, 0.9));
    check("square is high then low", lfoValue(Waveform::Square, 0.1) == 1.0f && lfoValue(Waveform::Square, 0.6) == 0.0f);
    check("soft square sits between the sine and the square", lfoValue(Waveform::SoftSquare, 0.1) > lfoValue(Waveform::Sine, 0.1)
                                                              && lfoValue(Waveform::SoftSquare, 0.1) < 1.0f);
    check("sample & hold is steady within a cycle", lfoValue(Waveform::SampleHold, 3.1) == lfoValue(Waveform::SampleHold, 3.9));
    check("sample & hold changes between cycles", lfoValue(Waveform::SampleHold, 3.5) != lfoValue(Waveform::SampleHold, 4.5));
    check("sample & hold repeats for the same cycle", lfoValue(Waveform::SampleHold, 7.3) == lfoValue(Waveform::SampleHold, 7.3));
    check("smooth random is continuous across a cycle edge",
          std::fabs(lfoValue(Waveform::SmoothRandom, 5.0 - 1e-7) - lfoValue(Waveform::SmoothRandom, 5.0)) < 1e-4f);

    bool bounded = true;
    for (int w = 0; w < kWaveformCount; ++w)
        for (double x = -3.0; x < 40.0; x += 0.0137) {
            const float v = lfoValue(static_cast<Waveform>(w), x);
            if (!(v >= 0.0f && v <= 1.0f)) bounded = false;
        }
    check("every waveform stays within 0..1", bounded);
}

static void testVcaRange()
{
    const Result r = render(Parameters{}, 120.0, 0.0, 2.0f);
    double lo = 1e9, hi = -1e9;
    for (size_t i = r.l.size() / 2; i < r.l.size(); ++i) { lo = std::min(lo, toDb(r.l[i])); hi = std::max(hi, toDb(r.l[i])); }
    check("default VCA tops out at 0 dB", std::fabs(hi) < 0.02);
    check("default VCA bottoms out at -3 dB", std::fabs(lo + 3.0) < 0.05);

    Parameters flat; flat.rangeDb = 0.0f;
    const Result t = render(flat, 120.0, 0.0, 0.5f);
    bool same = true;
    for (float v : t.l) if (std::fabs(v - 1.0f) > 1e-6f) same = false;
    check("a range of 0 dB is transparent", same);

    for (float range : {1.5f, 6.0f, 12.0f, 24.0f}) {
        Parameters p; p.rangeDb = range;
        const Result s = render(p, 120.0, 0.0, 1.0f);
        double mn = 1e9, mx = -1e9;
        for (size_t i = s.l.size() / 2; i < s.l.size(); ++i) { mn = std::min(mn, toDb(s.l[i])); mx = std::max(mx, toDb(s.l[i])); }
        check("the swing equals Range across the whole range", std::fabs((mx - mn) - range) < 0.08 * std::max(range, 1.0f));
    }
}

static void testRateIsLockedToTheBeat()
{
    // 120 BPM Auto: 10 Hz, so 10 cycles a second; count upward crossings of the middle gain.
    auto cycles = [](double bpm, float multiple) {
        Parameters p; p.multiple = multiple; p.rangeDb = 12.0f;
        const Result r = render(p, bpm, 0.0, 2.0f);
        const float mid = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
        int up = 0;
        for (size_t i = r.l.size() / 4 + 1; i < r.l.size(); ++i) if (r.l[i - 1] < mid && r.l[i] >= mid) ++up;
        return up / 1.5;   // over the last 1.5 s
    };
    check("120 BPM Auto runs at 10 Hz", std::fabs(cycles(120.0, 0.0f) - 10.0) < 0.7);
    check("90 BPM Auto (7 x 1.5 Hz = 10.5 Hz)", std::fabs(cycles(90.0, 0.0f) - 10.5) < 0.7);
    check("3 cycles per beat at 120 BPM is 6 Hz", std::fabs(cycles(120.0, 3.0f) - 6.0) < 0.5);
    check("a multiple past 20 Hz is held to 20 Hz", std::fabs(cycles(120.0, 40.0f) - 20.0) < 1.0);

    const Result m = render(Parameters{}, 100.0, 0.0, 0.1f);
    check("the meter reports the Auto multiple and rate", m.state.meters.multiple == 6.0f
          && std::fabs(m.state.meters.rateHz - 10.0f) < 1e-4f);
}

static void testPhaseSelectors()
{
    // At a beat boundary the default 90 degrees puts both channels on the LFO peak: 0 dB.
    Parameters d;
    const Result r = render(d, 120.0, 4.0, 0.02f);
    check("the default 90 degrees starts a beat at 0 dB on both channels", std::fabs(toDb(r.l[0])) < 0.01 && std::fabs(toDb(r.r[0])) < 0.01);

    // 0 and 180 degrees on a sine are mirror images: their dB gains sum to -Range.
    Parameters p; p.phaseL = 0.0f; p.phaseR = 2.0f; p.rangeDb = 12.0f;
    const Result a = render(p, 120.0, 0.0, 0.5f);
    double worst = 0.0;
    for (size_t i = 2000; i < a.l.size(); ++i) worst = std::max(worst, std::fabs(toDb(a.l[i]) + toDb(a.r[i]) + 12.0));
    check("0 and 180 degrees run in antiphase", worst < 0.35);

    bool differs = false;
    for (int pl = 0; pl < 4 && !differs; ++pl) {
        Parameters q; q.phaseL = static_cast<float>(pl); q.phaseR = 0.0f; q.rangeDb = 12.0f;
        if (pl == 0) continue;
        const Result b = render(q, 120.0, 0.0, 0.3f);
        double diff = 0.0;
        for (size_t i = 0; i < b.l.size(); ++i) diff = std::max(diff, std::fabs(static_cast<double>(b.l[i] - b.r[i])));
        check("each phase step makes the channels differ", diff > 0.1);
        differs = false;
    }
    Parameters same; same.phaseL = 3.0f; same.phaseR = 3.0f;
    const Result e = render(same, 120.0, 0.0, 0.3f);
    bool equal = true;
    for (size_t i = 0; i < e.l.size(); ++i) if (e.l[i] != e.r[i]) equal = false;
    check("equal phases give identical channels", equal);
}

static void testEveryControlHasAnEffect()
{
    Parameters base; base.rangeDb = 12.0f;
    const Result ref = render(base, 120.0, 0.0, 0.5f);
    auto differs = [&](const Parameters& q, double bpm = 120.0) {
        const Result o = render(q, bpm, 0.0, 0.5f);
        double diff = 0.0;
        for (size_t i = 0; i < o.l.size(); ++i) diff = std::max(diff, std::fabs(static_cast<double>(o.l[i] - ref.l[i])));
        return diff > 0.01;
    };
    Parameters q;
    for (int w = 1; w < kWaveformCount; ++w) { q = base; q.waveform = static_cast<float>(w); check("each waveform sounds different from sine", differs(q)); }
    q = base; q.multiple = 2.0f; check("multiple has an effect", differs(q));
    q = base; q.phaseL = 2.0f; check("left phase has an effect", differs(q));
    q = base; q.rangeDb = 24.0f; check("range has an effect", differs(q));
    q = base; check("tempo has an effect", differs(q, 90.0));
}

static void testTransportLock()
{
    // The LFO is a function of the transport position: starting fresh at a position gives what a long run reaches there.
    Parameters p; p.rangeDb = 12.0f; p.waveform = 6.0f;   // sample & hold: the hardest case
    // 113 BPM, so the cycle edges of the random waveform do not land exactly on sample boundaries, where rounding decides.
    const double bpm = 113.0;
    const Result longRun = render(p, bpm, 0.0, 6.0f);
    const size_t startSample = static_cast<size_t>(std::llround(4.3 * 60.0 / bpm * kSr));
    const double startBeat = static_cast<double>(startSample) / kSr * bpm / 60.0;   // exactly on a sample
    const Result fresh = render(p, bpm, startBeat, 1.0f);
    const size_t n = fresh.l.size();
    double worst = 0.0;
    for (size_t i = 3000; i < n; ++i) worst = std::max(worst, std::fabs(static_cast<double>(fresh.l[i] - longRun.l[startSample + i])));
    check("the output at a beat position does not depend on history", worst < 1e-4);

    // Block size must not matter.
    const Result a = render(p, bpm, 0.0, 1.0f, true, true, 64);
    const Result b = render(p, bpm, 0.0, 1.0f, true, true, 1000);
    double d = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i) d = std::max(d, std::fabs(static_cast<double>(a.l[i] - b.l[i])));
    check("block size does not change the sound", d < 1e-3);
}

static void testStoppedAndUnsyncedTransport()
{
    Parameters p; p.rangeDb = 12.0f;
    const Result stopped = render(p, 120.0, 8.0, 0.5f, false, true);
    double lo = 1e9, hi = -1e9;
    for (float v : stopped.l) { lo = std::min<double>(lo, v); hi = std::max<double>(hi, v); }
    check("a stopped transport still modulates (free-running)", hi - lo > 0.3);
    const Result noHost = render(p, 0.0, 0.0, 0.5f, false, false);
    lo = 1e9; hi = -1e9;
    for (float v : noHost.l) { lo = std::min<double>(lo, v); hi = std::max<double>(hi, v); }
    check("without any host tempo it free-runs at 120 BPM", hi - lo > 0.3 && std::fabs(noHost.state.meters.rateHz - 10.0f) < 1e-4f);
}

static void testRobustness()
{
    Parameters wild;
    wild.waveform = 99.0f; wild.multiple = -4.0f; wild.phaseL = 12.0f; wild.phaseR = -3.0f; wild.rangeDb = 1e9f;
    const Parameters c = clampParameters(wild);
    check("parameters clamp", c.waveform == 7.0f && c.multiple == 0.0f && c.phaseL == 3.0f && c.phaseR == 0.0f && c.rangeDb == 40.0f);
    const Parameters nan = clampParameters([] { Parameters q; q.rangeDb = NAN; q.waveform = NAN; return q; }());
    check("NaN parameters clamp to a lower bound", nan.rangeDb == 0.0f && nan.waveform == 0.0f);

    // Bad input and in-place processing.
    std::vector<float> buf(256, 0.5f);
    buf[10] = NAN; buf[11] = INFINITY;
    EngineState s;
    Transport t; t.playing = true; t.valid = true; t.bpm = 120.0;
    float* io[2] = {buf.data(), buf.data()};
    const float* in[2] = {buf.data(), buf.data()};
    processBlock(s, Parameters{}, kSr, 256, t, in, io);
    bool finite = true;
    for (float v : buf) if (!std::isfinite(v)) finite = false;
    check("non-finite input gives finite output, in place", finite);

    const Result r = render(Parameters{}, 120.0, 0.0, 0.2f, true, true, 128, 0.0f);
    bool silent = true;
    for (float v : r.l) if (v != 0.0f) silent = false;
    check("silence in is silence out", silent);
}

static void testResetKeepsNothingButState()
{
    Result r = render(Parameters{}, 120.0, 0.0, 0.1f);
    resetState(r.state);
    check("reset clears the smoother and meters", !r.state.primed && r.state.meters.rateHz == 0.0f && r.state.freePhase == 0.0);
}

int main()
{
    testDefaults();
    testMultipleSelection();
    testWaveforms();
    testVcaRange();
    testRateIsLockedToTheBeat();
    testPhaseSelectors();
    testEveryControlHasAnEffect();
    testTransportLock();
    testStoppedAndUnsyncedTransport();
    testRobustness();
    testResetKeepsNothingButState();
    std::printf("%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
