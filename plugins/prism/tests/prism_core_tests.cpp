#include "downspout/test_assert.h"

#include "prism_engine.hpp"
#include "prism_params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace downspout::prism;

namespace {

constexpr double kPi = 3.14159265358979323846;
int g_failures = 0;
int g_checks = 0;

void check(const bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkNear(const double got, const double want, const double tol, const std::string& what)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), " (got %.4f, want %.4f +-%.4f)", got, want, tol);
    check(std::fabs(got - want) <= tol, what + buf);
}

struct Stereo {
    std::vector<float> l, r;
};

// A fresh engine, patch set so the effect is a plain delay unless a test changes it.
struct Rig {
    double sr = 48000.0;
    Engine engine;
    Parameters p;
    double bpm = 120.0;

    explicit Rig(const double sampleRate = 48000.0) : sr(sampleRate)
    {
        activate(engine, sr);
        for (std::size_t b = 0; b < kMaxBands; ++b)
        {
            p.band(b, kDelayL) = 0.0f;
            p.band(b, kDelayR) = 0.0f;
            p.band(b, kFeedback) = 0.0f;
            p.band(b, kSolo) = 0.0f;
        }
        p[kMix] = 1.0f;
        p[kLink] = 0.0f;
    }

    void setAll(const float delayLms, const float delayRms, const float feedbackPct)
    {
        for (std::size_t b = 0; b < kMaxBands; ++b)
        {
            p.band(b, kDelayL) = delayLms;
            p.band(b, kDelayR) = delayRms;
            p.band(b, kFeedback) = feedbackPct;
        }
    }

    Stereo run(const Stereo& in, const std::size_t block = 512)
    {
        Stereo out;
        out.l.assign(in.l.size(), 0.0f);
        out.r.assign(in.l.size(), 0.0f);
        setTempo(engine, bpm);
        for (std::size_t at = 0; at < in.l.size(); at += block)
        {
            const std::size_t n = std::min(block, in.l.size() - at);
            processBlock(engine, p, in.l.data() + at, in.r.data() + at, out.l.data() + at, out.r.data() + at, n);
        }
        return out;
    }
};

Stereo silence(const std::size_t n)
{
    return {std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f)};
}

std::size_t peakIndex(const std::vector<float>& x)
{
    std::size_t best = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        if (std::fabs(x[i]) > std::fabs(x[best]))
            best = i;
    return best;
}

// Energy-weighted centre of a signal, in samples.
double centroid(const std::vector<float>& x)
{
    double num = 0.0, den = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        num += static_cast<double>(i) * x[i] * x[i];
        den += static_cast<double>(x[i]) * x[i];
    }
    return den > 0.0 ? num / den : -1.0;
}

double energy(const std::vector<float>& x)
{
    double e = 0.0;
    for (const float v : x)
        e += static_cast<double>(v) * v;
    return e;
}

// Hann-windowed sine burst, `length` samples long, starting at `at`.
void addBurst(std::vector<float>& x, const std::size_t at, const std::size_t length, const double hz, const double sr,
              const double amp = 0.5)
{
    for (std::size_t i = 0; i < length && at + i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(length));
        x[at + i] += static_cast<float>(amp * w * std::sin(2.0 * kPi * hz * static_cast<double>(i) / sr));
    }
}

Stereo noise(const std::size_t n, const unsigned seed = 1)
{
    Stereo s = silence(n);
    std::srand(seed);
    for (std::size_t i = 0; i < n; ++i)
    {
        s.l[i] = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f;
        s.r[i] = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f;
    }
    return s;
}

// ── Parameters ─────────────────────────────────────────────────────────────

void testParameterTable()
{
    std::printf("parameters\n");
    const auto& specs = parameterSpecs();
    check(kParameterCount == 58 && kInputParameterCount == 56, "8 global + 8 bands x 6 + 2 outputs");
    const Parameters defaults;
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
    {
        check(defaults[i] == specs[i].defaultValue, std::string("default matches spec: ") + specs[i].symbol);
        check(specs[i].defaultValue >= specs[i].minimum && specs[i].defaultValue <= specs[i].maximum,
              std::string("default inside range: ") + specs[i].symbol);
    }
    for (std::size_t i = 0; i < kParameterCount; ++i)
        for (std::size_t j = i + 1; j < kParameterCount; ++j)
            check(std::string(specs[i].symbol) != specs[j].symbol, "symbols are unique");
    check(specs[kOutLatency].output && specs[kOutTempo].output && !specs[kMix].output && !specs[kSyncL].output,
          "latency and tempo are the outputs");
    check(std::string(specs[bandParam(2, kDelayR)].symbol) == "b3_delay_r", "band symbols are 1-based");
    check(std::string(specs[bandParam(7, kDivR)].symbol) == "b8_div_r" && std::string(specs[kSyncL].symbol) == "sync_l",
          "sync symbols");
    checkNear(divisionMs(6, 120.0), 250.0, 1e-3, "1/8 at 120 BPM is 250 ms");
    checkNear(divisionMs(9, 90.0), 666.667, 1e-2, "1/4 at 90 BPM");
    checkNear(divisionMs(5, 120.0), 166.667, 1e-2, "1/8T at 120 BPM");
    checkNear(divisionMs(7, 120.0), 375.0, 1e-3, "1/8D at 120 BPM");
    checkNear(divisionMs(12, 120.0), 2000.0, 1e-3, "1/1 at 120 BPM");
    checkNear(divisionMs(6, 0.0), 250.0, 1e-3, "no tempo means 120 BPM");
    // Grouped by note value (triplet, plain, dotted), so lengths are not monotonic: they must be distinct.
    for (std::size_t i = 0; i < kDivisionCount; ++i)
        for (std::size_t j = i + 1; j < kDivisionCount; ++j)
            check(std::fabs(kDivisionBeats[i] - kDivisionBeats[j]) > 1e-6, "divisions are distinct");

    Parameters wild;
    wild[kBands] = 99.0f;
    wild[kMix] = std::nanf("");
    wild[bandParam(0, kDelayL)] = -5.0f;
    wild[bandParam(7, kFeedback)] = 500.0f;
    wild[kSize] = 2.4f;
    const Parameters c = clampParameters(wild);
    check(c[kBands] == 8.0f && c[kMix] == 0.5f && c.band(0, kDelayL) == 0.0f && c.band(7, kFeedback) == 95.0f &&
              c[kSize] == 2.0f,
          "clampParameters bounds, rounds integers and replaces NaN with the default");
}

void testBandGeometry()
{
    std::printf("band geometry\n");
    checkNear(bandCentreHz(0, 8), 31.25, 1e-3, "first centre 31.25 Hz");
    checkNear(bandCentreHz(7, 8), 16000.0, 1.0, "last centre 16 kHz");
    for (int n = 2; n <= 8; ++n)
        for (int b = 1; b < n; ++b)
            check(bandCentreHz(b, n) > bandCentreHz(b - 1, n), "centres rise");

    for (int n = 2; n <= 8; ++n)
    {
        for (int b = 0; b < n; ++b)
        {
            const auto pos = bandPosition(bandCentreHz(b, n), n);
            const float exact = static_cast<float>(pos.band) + pos.frac;
            checkNear(exact, static_cast<double>(b), 1e-3, "a band centre lands on its band");
        }
        // Halfway (in log frequency) between two centres is a 50/50 blend.
        const float mid = std::sqrt(bandCentreHz(0, n) * bandCentreHz(1, n));
        checkNear(bandPosition(mid, n).frac, 0.5, 1e-3, "midpoint blends 50/50");
        // No step anywhere: position is continuous.
        double previous = 0.0;
        for (double hz = 20.0; hz < 20000.0; hz *= 1.01)
        {
            const auto pos = bandPosition(hz, n);
            const double now = pos.band + pos.frac;
            check(now >= previous - 1e-9 && now - previous < 0.2, "position is continuous and non-decreasing");
            previous = now;
        }
    }
    check(bandPosition(5.0, 8).band == 0 && bandPosition(5.0, 8).frac == 0.0f, "below the first centre holds band 0");
    check(bandPosition(20000.0, 8).band == 6 && bandPosition(20000.0, 8).frac == 1.0f,
          "above the last centre holds the top band");
    check(bandPosition(1234.0, 1).band == 0 && bandPosition(1234.0, 1).frac == 0.0f, "one band covers everything");

    check(lowCutWeight(100.0, 20.0f) == 1.0f && highCutWeight(100.0, 20000.0f) == 1.0f, "cuts at their ends are off");
    checkNear(lowCutWeight(1000.0, 1000.0f), 1.0, 1e-6, "low cut: full at the cutoff");
    checkNear(lowCutWeight(1000.0 / std::cbrt(2.0), 1000.0f), 0.0, 1e-6, "low cut: zero a third of an octave below");
    checkNear(highCutWeight(1000.0, 1000.0f), 1.0, 1e-6, "high cut: full at the cutoff");
    checkNear(highCutWeight(1000.0 * std::cbrt(2.0), 1000.0f), 0.0, 1e-6, "high cut: zero a third of an octave above");
}

// ── Delay ──────────────────────────────────────────────────────────────────

void testLatencyAndDryAlignment()
{
    std::printf("latency and dry path\n");
    for (std::size_t s = 0; s < kFftSizeCount; ++s)
    {
        Rig rig;
        rig.p[kSize] = static_cast<float>(s);
        rig.p[kMix] = 0.0f;
        auto in = silence(20000);
        in.l[100] = 1.0f;
        in.r[300] = -1.0f;
        const auto out = rig.run(in);
        const auto n = static_cast<std::size_t>(kFftSizes[s]);
        check(latencySamples(rig.engine) == kFftSizes[s], "latency is the FFT size");
        check(peakIndex(out.l) == 100 + n && peakIndex(out.r) == 300 + n, "mix 0 is the input, N samples late");
        checkNear(out.l[100 + n], 1.0, 1e-6, "dry path is exact");
    }
}

void testEqualDelayIsADelay()
{
    std::printf("equal delay on every band\n");
    for (std::size_t s = 0; s < kFftSizeCount; ++s)
    {
        for (const double ms : {0.0, 3.0, 37.5, 100.0, 713.25, 1999.0})
        {
            Rig rig;
            rig.p[kSize] = static_cast<float>(s);
            rig.setAll(static_cast<float>(ms), static_cast<float>(ms), 0.0f);
            const auto n = static_cast<std::size_t>(kFftSizes[s]);
            const std::size_t length = n + static_cast<std::size_t>(ms * 48.0) + 20000;
            auto in = silence(length);
            in.l[500] = 1.0f;
            in.r[500] = 1.0f;
            const auto out = rig.run(in);
            const double want = 500.0 + static_cast<double>(n) + ms * 48.0;
            const double got = static_cast<double>(peakIndex(out.l));
            char label[96];
            std::snprintf(label, sizeof(label), "N=%zu %.1f ms: impulse arrives on time", n, ms);
            checkNear(got, want, 1.5, label);
            const double peak = std::fabs(out.l[peakIndex(out.l)]);
            std::snprintf(label, sizeof(label), "N=%zu %.1f ms: impulse keeps its level", n, ms);
            checkNear(peak, 1.0, 0.12, label);
        }
    }
}

void testDelayedSineKeepsItsLevel()
{
    std::printf("level through fractional delays\n");
    // The rotation that supplies the sub-hop part of a delay costs level through the
    // windows; the compensation must put it back at any residual.
    for (const double ms : {10.0, 21.0, 42.7, 64.0, 77.7})
    {
        Rig rig;
        rig.setAll(static_cast<float>(ms), static_cast<float>(ms), 0.0f);
        auto in = silence(48000);
        addBurst(in.l, 2000, 24000, 1000.0, 48000.0);
        addBurst(in.r, 2000, 24000, 1000.0, 48000.0);
        const auto out = rig.run(in);
        const double ratio = std::sqrt(energy(out.l) / energy(in.l));
        char label[64];
        std::snprintf(label, sizeof(label), "1 kHz burst, %.1f ms: level within 0.3 dB", ms);
        checkNear(20.0 * std::log10(ratio), 0.0, 0.3, label);
    }
}

void testBandsDelayIndependently()
{
    std::printf("bands delay independently\n");
    Rig rig;
    for (std::size_t b = 0; b < kMaxBands; ++b)
    {
        const float ms = b < 4 ? 0.0f : 500.0f;  // 31 Hz - 250 Hz now, 500 Hz up in half a second
        rig.p.band(b, kDelayL) = ms;
        rig.p.band(b, kDelayR) = ms;
        rig.p.band(b, kFeedback) = 0.0f;
    }
    auto in = silence(48000 * 2);
    // 100 Hz sits in the early bands; 8 kHz in the late ones.
    addBurst(in.l, 4800, 9600, 100.0, 48000.0);
    addBurst(in.l, 4800, 9600, 8000.0, 48000.0);
    in.r = in.l;
    const auto out = rig.run(in);
    // Split the output by time: the early half holds only the low tone's energy.
    const std::size_t n = 2048;
    std::vector<float> early(out.l.begin() + 4800 + n - 2000, out.l.begin() + 4800 + n + 11600);
    std::vector<float> late(out.l.begin() + 4800 + n + 24000 - 2000, out.l.begin() + 4800 + n + 24000 + 11600);
    // Each tone's energy: Goertzel at its own frequency.
    const auto tone = [](const std::vector<float>& x, const double hz) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            re += x[i] * std::cos(2.0 * kPi * hz * static_cast<double>(i) / 48000.0);
            im += x[i] * std::sin(2.0 * kPi * hz * static_cast<double>(i) / 48000.0);
        }
        return std::hypot(re, im) / static_cast<double>(x.size());
    };
    const double lowEarly = tone(early, 100.0), highEarly = tone(early, 8000.0);
    const double lowLate = tone(late, 100.0), highLate = tone(late, 8000.0);
    std::printf("  early: low %.4f high %.4f   late: low %.4f high %.4f\n", lowEarly, highEarly, lowLate, highLate);
    check(lowEarly > 20.0 * highEarly, "the low band arrives at once, the high band does not");
    check(highLate > 20.0 * lowLate, "the high band arrives half a second later");
    check(lowEarly > 0.05 && highLate > 0.05, "both arrive at a useful level");
}

void testLeftAndRightAreIndependent()
{
    std::printf("left and right\n");
    Rig rig;
    rig.setAll(50.0f, 120.0f, 0.0f);
    auto in = silence(30000);
    in.l[100] = 1.0f;
    in.r[100] = 1.0f;
    const auto out = rig.run(in);
    checkNear(static_cast<double>(peakIndex(out.l)), 100.0 + 2048.0 + 50.0 * 48.0, 1.5, "left uses the L delay");
    checkNear(static_cast<double>(peakIndex(out.r)), 100.0 + 2048.0 + 120.0 * 48.0, 1.5, "right uses the R delay");

    Rig linked;
    linked.setAll(50.0f, 120.0f, 0.0f);
    linked.p[kLink] = 1.0f;
    const auto both = linked.run(in);
    checkNear(static_cast<double>(peakIndex(both.r)), 100.0 + 2048.0 + 50.0 * 48.0, 1.5, "linked: right follows the L delay");
}

void testTempoSync()
{
    std::printf("tempo sync\n");
    const auto peakAfter = [](Rig& rig, const std::size_t channel) {
        auto in = silence(48000 * 3);
        in.l[500] = 1.0f;
        in.r[500] = 1.0f;
        const auto out = rig.run(in);
        return static_cast<double>(peakIndex(channel == 0 ? out.l : out.r)) - 500.0 - 2048.0;
    };
    // 1/8 at 120 BPM = 250 ms; 1/4 at 90 BPM = 666.67 ms; 1/8T at 120 BPM = 166.67 ms.
    struct Case { int division; double bpm; double ms; };
    for (const Case c : {Case {6, 120.0, 250.0}, Case {9, 90.0, 666.0 + 2.0 / 3.0}, Case {5, 120.0, 166.0 + 2.0 / 3.0},
                         Case {3, 140.0, 107.142857}, Case {11, 60.0, 2000.0}})
    {
        Rig rig;
        rig.bpm = c.bpm;
        rig.setAll(10.0f, 10.0f, 0.0f);  // the millisecond values must be ignored
        rig.p[kSyncL] = 1.0f;
        rig.p[kSyncR] = 1.0f;
        for (std::size_t b = 0; b < kMaxBands; ++b)
        {
            rig.p.band(b, kDivL) = static_cast<float>(c.division);
            rig.p.band(b, kDivR) = static_cast<float>(c.division);
        }
        char label[96];
        std::snprintf(label, sizeof(label), "division %s at %.0f BPM = %.1f ms", kDivisionNames[static_cast<std::size_t>(c.division)],
                      c.bpm, c.ms);
        checkNear(peakAfter(rig, 0), c.ms * 48.0, 2.5, label);
    }

    // The side that is not synced keeps its milliseconds.
    Rig mixed;
    mixed.setAll(100.0f, 100.0f, 0.0f);
    mixed.p[kSyncL] = 1.0f;
    for (std::size_t b = 0; b < kMaxBands; ++b)
        mixed.p.band(b, kDivL) = 6.0f;  // 250 ms
    auto in = silence(48000 * 2);
    in.l[500] = 1.0f;
    in.r[500] = 1.0f;
    const auto out = mixed.run(in);
    checkNear(static_cast<double>(peakIndex(out.l)) - 2548.0, 250.0 * 48.0, 2.5, "synced left follows the tempo");
    checkNear(static_cast<double>(peakIndex(out.r)) - 2548.0, 100.0 * 48.0, 2.5, "unsynced right keeps its milliseconds");

    // Linked, the right side uses the left's mode and division.
    Rig linked;
    linked.setAll(100.0f, 400.0f, 0.0f);
    linked.p[kLink] = 1.0f;
    linked.p[kSyncL] = 1.0f;
    linked.p[kSyncR] = 0.0f;
    for (std::size_t b = 0; b < kMaxBands; ++b)
    {
        linked.p.band(b, kDivL) = 6.0f;
        linked.p.band(b, kDivR) = 12.0f;
    }
    const auto both = linked.run(in);
    checkNear(static_cast<double>(peakIndex(both.r)) - 2548.0, 250.0 * 48.0, 2.5, "linked: right takes the left's sync and division");

    // A division longer than the longest delay is limited to it.
    Rig tooLong;
    tooLong.bpm = 60.0;  // 1/1 is 4 s
    tooLong.setAll(0.0f, 0.0f, 0.0f);
    tooLong.p[kSyncL] = 1.0f;
    tooLong.p[kSyncR] = 1.0f;
    for (std::size_t b = 0; b < kMaxBands; ++b)
    {
        tooLong.p.band(b, kDivL) = 12.0f;
        tooLong.p.band(b, kDivR) = 12.0f;
    }
    check(std::fabs(peakAfter(tooLong, 0) - 2000.0 * 48.0) < 2.5, "a division beyond 2 s is limited to 2 s");

    // Tempo changes while playing: the delay follows (after the smoothing), and nothing blows up.
    Rig moving;
    moving.setAll(0.0f, 0.0f, 40.0f);
    moving.p[kSyncL] = 1.0f;
    moving.p[kSyncR] = 1.0f;
    for (std::size_t b = 0; b < kMaxBands; ++b)
    {
        moving.p.band(b, kDivL) = 9.0f;  // 1/4
        moving.p.band(b, kDivR) = 9.0f;
    }
    auto burst = silence(48000 * 8);
    for (std::size_t i = 0; i < 3; ++i)
    {
        burst.l[48000 * (1 + 2 * i)] = 1.0f;
        burst.r[48000 * (1 + 2 * i)] = 1.0f;
    }
    Stereo first = silence(48000 * 4), second = silence(48000 * 4);
    std::copy(burst.l.begin(), burst.l.begin() + 48000 * 4, first.l.begin());
    std::copy(burst.r.begin(), burst.r.begin() + 48000 * 4, first.r.begin());
    std::copy(burst.l.begin() + 48000 * 4, burst.l.end(), second.l.begin());
    std::copy(burst.r.begin() + 48000 * 4, burst.r.end(), second.r.begin());
    moving.bpm = 120.0;
    const auto a = moving.run(first);
    moving.bpm = 60.0;
    const auto b = moving.run(second);
    bool finite = true;
    for (const float v : a.l) finite = finite && std::isfinite(v);
    for (const float v : b.l) finite = finite && std::isfinite(v);
    check(finite, "changing tempo mid-play stays finite");
    // The click at 5 s (second block, 1 s in) arrives a quarter at 60 BPM = 1 s later, once settled.
    const std::size_t at = 48000 + 2048 + 48000;
    double late = 0.0;
    for (std::size_t i = at - 400; i < at + 400; ++i)
        late = std::max(late, static_cast<double>(std::fabs(b.l[i])));
    check(late > 0.3, "after a tempo change the delay is the new quarter note");
}

void testFeedbackRepeats()
{
    std::printf("feedback\n");
    Rig rig;
    rig.setAll(100.0f, 100.0f, 50.0f);
    auto in = silence(48000 * 2);
    in.l[1000] = 1.0f;
    in.r[1000] = 1.0f;
    const auto out = rig.run(in);
    double previous = 0.0;
    for (int k = 1; k <= 4; ++k)
    {
        const auto centre = static_cast<std::size_t>(1000 + 2048 + k * 4800);
        const std::vector<float> window(out.l.begin() + static_cast<long>(centre) - 40, out.l.begin() + static_cast<long>(centre) + 40);
        const double level = std::fabs(window[peakIndex(window)]);
        const std::size_t where = centre - 40 + peakIndex(window);
        char label[96];
        std::snprintf(label, sizeof(label), "repeat %d arrives on the beat (off by %.1f samples)", k,
                      static_cast<double>(where) - static_cast<double>(centre));
        checkNear(static_cast<double>(where), static_cast<double>(centre), 2.0, label);
        if (k > 1)
        {
            std::snprintf(label, sizeof(label), "repeat %d is half the last (%.3f / %.3f)", k, level, previous);
            checkNear(level / previous, 0.5, 0.08, label);
        }
        previous = level;
    }
}

// Each trip round the loop rotates a frame by the sub-hop part of the delay, which costs a
// little level through the windows, more with each repeat. Sweep delays (so the residual
// covers its whole range) and hold the documented bound on that.
void testFeedbackLevelAcrossDelays()
{
    std::printf("feedback level across delays\n");
    double worst[6] = {};
    for (const std::size_t size : {std::size_t {0}, std::size_t {2}})
    {
        for (double ms = 41.0; ms <= 420.0; ms += 37.0)
        {
            Rig rig;
            rig.p[kSize] = static_cast<float>(size);
            rig.setAll(static_cast<float>(ms), static_cast<float>(ms), 70.0f);
            auto in = silence(48000 * 3);
            in.l[1000] = 1.0f;
            in.r[1000] = 1.0f;
            const auto out = rig.run(in);
            for (int k = 1; k <= 4; ++k)
            {
                const auto centre = static_cast<long>(std::lround(1000.0 + kFftSizes[size] + k * ms * 48.0));
                double e = 0.0;
                for (long i = centre - 40; i <= centre + 40; ++i)
                    e += static_cast<double>(out.l[static_cast<std::size_t>(i)]) * out.l[static_cast<std::size_t>(i)];
                // Energy, not the peak: a delay that is not a whole number of samples spreads the peak.
                const double db = 20.0 * std::log10(std::sqrt(e) / std::pow(0.7, k - 1));
                worst[k] = std::max(worst[k], std::fabs(db));
            }
        }
    }
    std::printf("  worst error, repeats 1-4: %.2f %.2f %.2f %.2f dB\n", worst[1], worst[2], worst[3], worst[4]);
    check(worst[1] < 0.3, "first repeat is at level");
    check(worst[2] < 0.8, "second repeat within 0.8 dB of the ideal");
    check(worst[3] < 1.6, "third repeat within 1.6 dB");
    check(worst[4] < 2.8, "fourth repeat within 2.8 dB");
}

void testFeedbackStaysStable()
{
    std::printf("feedback stability\n");
    Rig rig;
    rig.setAll(30.0f, 45.0f, 95.0f);
    rig.p[kMix] = 1.0f;
    auto in = noise(48000 * 3);
    // Noise for a second, then nothing: the tail must decay.
    for (std::size_t i = 48000; i < in.l.size(); ++i)
        in.l[i] = in.r[i] = 0.0f;
    const auto out = rig.run(in);
    float peak = 0.0f;
    bool finite = true;
    for (std::size_t i = 0; i < out.l.size(); ++i)
    {
        finite = finite && std::isfinite(out.l[i]) && std::isfinite(out.r[i]);
        peak = std::max(peak, std::max(std::fabs(out.l[i]), std::fabs(out.r[i])));
    }
    check(finite, "output stays finite at 95 % feedback");
    std::printf("  peak %.2f\n", static_cast<double>(peak));
    check(peak < 60.0f, "feedback does not run away");
    const std::vector<float> early(out.l.begin() + 48000 + 4096, out.l.begin() + 48000 + 4096 + 12000);
    const std::vector<float> late(out.l.end() - 12000, out.l.end());
    check(energy(late) < energy(early), "the tail decays once the input stops");
}

void testSolo()
{
    std::printf("solo\n");
    Rig rig;
    rig.p[kMix] = 0.0f;  // solo must override this: only the soloed band is heard
    rig.p.band(7, kSolo) = 1.0f;
    auto in = silence(48000);
    addBurst(in.l, 2000, 24000, 100.0, 48000.0);
    addBurst(in.l, 2000, 24000, 12000.0, 48000.0);
    in.r = in.l;
    const auto out = rig.run(in);
    const auto tone = [](const std::vector<float>& x, const double hz) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            re += x[i] * std::cos(2.0 * kPi * hz * static_cast<double>(i) / 48000.0);
            im += x[i] * std::sin(2.0 * kPi * hz * static_cast<double>(i) / 48000.0);
        }
        return std::hypot(re, im);
    };
    const double low = tone(out.l, 100.0), high = tone(out.l, 12000.0);
    std::printf("  soloed top band: low %.2f high %.2f\n", low, high);
    check(high > 30.0 * low, "soloing the top band silences the low one");
    check(high > 1.0, "and the top band is heard at mix 0");
}

void testCutsLeaveTheirRegionUndelayed()
{
    std::printf("cuts\n");
    Rig rig;
    rig.setAll(300.0f, 300.0f, 0.0f);
    rig.p[kLowCut] = 1000.0f;
    auto in = silence(48000 * 2);
    addBurst(in.l, 4800, 9600, 200.0, 48000.0);
    addBurst(in.l, 4800, 9600, 4000.0, 48000.0);
    in.r = in.l;
    const auto out = rig.run(in);
    const auto tone = [](const std::vector<float>& x, const double hz) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            re += x[i] * std::cos(2.0 * kPi * hz * static_cast<double>(i) / 48000.0);
            im += x[i] * std::sin(2.0 * kPi * hz * static_cast<double>(i) / 48000.0);
        }
        return std::hypot(re, im);
    };
    const std::size_t n = 2048;
    const std::vector<float> early(out.l.begin() + 4800 + n - 500, out.l.begin() + 4800 + n + 10100);
    const std::vector<float> late(out.l.begin() + 4800 + n + 14400 - 500, out.l.begin() + 4800 + n + 14400 + 10100);
    check(tone(early, 200.0) > 10.0 * tone(early, 4000.0), "below the low cut the sound passes undelayed");
    check(tone(late, 4000.0) > 10.0 * tone(late, 200.0), "above it the sound is delayed");

    Rig high;
    high.setAll(300.0f, 300.0f, 0.0f);
    high.p[kHighCut] = 1000.0f;
    const auto out2 = high.run(in);
    const std::vector<float> early2(out2.l.begin() + 4800 + n - 500, out2.l.begin() + 4800 + n + 10100);
    const std::vector<float> late2(out2.l.begin() + 4800 + n + 14400 - 500, out2.l.begin() + 4800 + n + 14400 + 10100);
    check(tone(early2, 4000.0) > 10.0 * tone(early2, 200.0), "above the high cut the sound passes undelayed");
    check(tone(late2, 200.0) > 10.0 * tone(late2, 4000.0), "below it the sound is delayed");
}

void testOneBand()
{
    std::printf("band count\n");
    Rig rig;
    rig.p[kBands] = 1.0f;
    rig.p.band(0, kDelayL) = 80.0f;
    rig.p.band(0, kDelayR) = 80.0f;
    rig.p.band(5, kDelayL) = 900.0f;  // inactive band: must not matter
    auto in = silence(30000);
    in.l[200] = 1.0f;
    in.r[200] = 1.0f;
    const auto out = rig.run(in);
    checkNear(static_cast<double>(peakIndex(out.l)), 200.0 + 2048.0 + 80.0 * 48.0, 1.5, "one band delays every frequency alike");

    // Two bands: the delay glides from one centre to the other in log frequency. Beyond the top
    // centre it holds exactly; between the centres it follows the interpolation, but only
    // approximately, since each bin's delay is a phase slope that itself varies with frequency.
    const auto delayOf = [](const double hz) {
        Rig two;
        two.p[kBands] = 2.0f;
        for (std::size_t b = 0; b < 2; ++b)
        {
            two.p.band(b, kDelayL) = b == 0 ? 0.0f : 200.0f;
            two.p.band(b, kDelayR) = b == 0 ? 0.0f : 200.0f;
        }
        auto burst = silence(48000 * 2);
        addBurst(burst.l, 4800, 9600, hz, 48000.0);
        burst.r = burst.l;
        const auto out = two.run(burst);
        const std::vector<float> after(out.l.begin() + 2048, out.l.end());
        return (centroid(after) - centroid(burst.l)) / 48.0;  // ms
    };
    const double top = delayOf(17000.0);
    checkNear(top, 200.0, 1.0, "above the top centre the top band's delay holds exactly");
    double previous = -1.0;
    for (const double hz : {300.0, 707.0, 1500.0, 4000.0})
    {
        const double want = 200.0 * std::log2(hz / 31.25) / std::log2(16000.0 / 31.25);
        const double got = delayOf(hz);
        char label[96];
        std::snprintf(label, sizeof(label), "%.0f Hz between two centres: %.1f ms (interpolated %.1f)", hz, got, want);
        checkNear(got, want, 20.0, label);
        check(got > previous, "delay rises with frequency across the gap");
        previous = got;
    }
}

// ── Plumbing ───────────────────────────────────────────────────────────────

void testBlockSizeDoesNotMatter()
{
    std::printf("block size\n");
    const auto in = noise(40000, 7);
    Rig a, b, c;
    for (Rig* r : {&a, &b, &c})
    {
        r->p[kMix] = 0.7f;
        r->p.band(3, kSolo) = 0.0f;
    }
    const auto x = a.run(in, 1);
    const auto y = b.run(in, 37);
    const auto z = c.run(in, 4096);
    check(x.l == y.l && x.r == y.r && x.l == z.l, "output is identical at any host block size");
}

void testResetClearsAudioOnly()
{
    std::printf("reset\n");
    Rig rig;
    rig.setAll(100.0f, 100.0f, 80.0f);
    const auto noisy = noise(20000);
    rig.run(noisy);
    const Parameters before = rig.p;
    reset(rig.engine);
    const auto out = rig.run(silence(20000));
    check(energy(out.l) == 0.0 && energy(out.r) == 0.0, "reset silences the tail");
    check(rig.p.v == before.v, "and leaves the patch alone");
}

void testResolutionSwitch()
{
    std::printf("resolution switch\n");
    Rig rig;
    rig.setAll(40.0f, 60.0f, 60.0f);
    const auto in = noise(60000, 3);
    std::vector<float> all;
    bool finite = true;
    for (int s : {2, 0, 3, 1, 2})
    {
        rig.p[kSize] = static_cast<float>(s);
        const auto out = rig.run(Stereo {std::vector<float>(in.l.begin(), in.l.begin() + 9000),
                                         std::vector<float>(in.r.begin(), in.r.begin() + 9000)});
        for (const float v : out.l)
            finite = finite && std::isfinite(v);
        check(latencySamples(rig.engine) == kFftSizes[static_cast<std::size_t>(s)], "latency follows the resolution");
    }
    check(finite, "switching resolution stays finite");
}

void testEveryControlHasAnEffect()
{
    std::printf("every control\n");
    const auto in = noise(30000, 11);
    const auto& specs = parameterSpecs();
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
    {
        Rig rig;
        Rig ref;  // the same patch, untouched
        ref.setAll(60.0f, 90.0f, 40.0f);
        ref.p[kMix] = 0.5f;
        rig.setAll(60.0f, 90.0f, 40.0f);
        rig.p[kMix] = 0.5f;
        const ParamSpec& spec = specs[i];
        // Divisions only mean something once their side is synced.
        const bool divisionParam = i >= kGlobalCount && ((i - kGlobalCount) % kFieldsPerBand == kDivL ||
                                                          (i - kGlobalCount) % kFieldsPerBand == kDivR);
        if (divisionParam)
        {
            const bool leftSide = (i - kGlobalCount) % kFieldsPerBand == kDivL;
            rig.p[leftSide ? kSyncL : kSyncR] = 1.0f;
            ref.p[leftSide ? kSyncL : kSyncR] = 1.0f;
        }
        // Move to the far end from where it sits; for the cuts and size, the other end of the range.
        const float current = rig.p[i];
        const float target = std::fabs(current - spec.maximum) > std::fabs(current - spec.minimum) ? spec.maximum : spec.minimum;
        if (target == current)
            continue;
        rig.p[i] = target;
        const auto a = ref.run(in);
        const auto b = rig.run(in);
        check(a.l != b.l || a.r != b.r, std::string("control changes the sound: ") + spec.symbol);
    }
}

void testSpeed()
{
    std::printf("speed\n");
    // Worst case for per-frame work: smallest hop, all bands, high rate. An upper bound when
    // the machine is busy.
    Rig rig(96000.0);
    rig.p[kSize] = 0.0f;
    rig.setAll(120.0f, 150.0f, 70.0f);
    const auto in = noise(96000 * 2, 5);
    const auto start = std::chrono::steady_clock::now();
    rig.run(in);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  512 at 96 kHz: %.1f %% of real time\n", 100.0 * seconds / 2.0);
    check(seconds < 2.0, "renders faster than real time");
    Rig big;
    big.p[kSize] = 3.0f;
    const auto start2 = std::chrono::steady_clock::now();
    big.run(noise(48000 * 2, 5));
    std::printf("  4096 at 48 kHz: %.1f %% of real time\n",
                100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start2).count() / 2.0);
}

}  // namespace

int main()
{
    testParameterTable();
    testBandGeometry();
    testLatencyAndDryAlignment();
    testEqualDelayIsADelay();
    testDelayedSineKeepsItsLevel();
    testBandsDelayIndependently();
    testLeftAndRightAreIndependent();
    testTempoSync();
    testFeedbackRepeats();
    testFeedbackLevelAcrossDelays();
    testFeedbackStaysStable();
    testSolo();
    testCutsLeaveTheirRegionUndelayed();
    testOneBand();
    testBlockSizeDoesNotMatter();
    testResetClearsAudioOnly();
    testResolutionSwitch();
    testEveryControlHasAnEffect();
    testSpeed();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
