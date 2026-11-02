#include "bassops_engine.hpp"
#include "bassops_serialization.hpp"

#include <array>
#include "downspout/test_assert.h"
#include <cmath>
#include <complex>
#include <functional>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <vector>

using namespace downspout::bassops;

namespace {

// Allow for FIR ramp-up latency in output checks
constexpr std::uint32_t kFirDelay = kLatencyFrames;

void testClampParameters()
{
    Parameters raw;
    raw.duckDepth = -10.0f;
    raw.attackMs  = 0.0f;
    raw.releaseMs = 5.0f;
    raw.cutoffHz  = 10000.0f;

    const Parameters p = clampParameters(raw);
    assert(p.duckDepth == 0.0f);
    assert(p.attackMs  == 0.0f);
    assert(p.releaseMs == 5.0f);
    assert(p.cutoffHz  == 5000.0f);
}

void testSerializationRoundTrip()
{
    Parameters original;
    original.duckDepth = 60.0f;
    original.attackMs  = 25.0f;
    original.releaseMs = 300.0f;
    original.cutoffHz  = 400.0f;

    const std::string text = serializeParameters(original);
    const auto result = deserializeParameters(text);
    assert(result.has_value());
    assert(std::fabs(result->duckDepth - 60.0f)  < 1e-3f);
    assert(std::fabs(result->attackMs  - 25.0f)  < 1e-3f);
    assert(std::fabs(result->releaseMs - 300.0f) < 1e-3f);
    assert(std::fabs(result->cutoffHz  - 400.0f) < 1e-3f);
}

void testDeserializeToleratesUnknownKeys()
{
    // Unknown keys are skipped for forward compatibility
    const auto result = deserializeParameters("version=1\nunknownKey=1.0\n");
    assert(result.has_value());
    assert(std::fabs(result->duckDepth - 80.0f) < 1e-3f);  // default preserved
}

// Silence in => silence out (after FIR ramp-up)
void testSilencePassThrough()
{
    EngineState state;
    activate(state);

    constexpr std::uint32_t kFrames = kFirDelay + 64;
    std::array<float, kFrames> zero {};
    zero.fill(0.0f);
    std::array<float, kFrames> outL {}, outR {};

    AudioBlock audio;
    audio.mainL    = zero.data();
    audio.mainR    = zero.data();
    audio.controlL = zero.data();
    audio.controlR = zero.data();
    audio.outL     = outL.data();
    audio.outR     = outR.data();

    processBlock(state, Parameters{}, BusParameters{}, kFrames, 48000.0, audio);

    for (std::uint32_t i = 0; i < kFrames; ++i) {
        assert(std::fabs(outL[i]) < 1e-6f);
        assert(std::fabs(outR[i]) < 1e-6f);
    }
}

// No sidechain signal => no ducking; with cutoff very high, mid dominates
void testNoDuckingWithNoSidechain()
{
    EngineState state;
    activate(state);

    // With no sidechain and duck=100%, output should equal input (through EQ)
    // We just verify output is non-zero when input is non-zero (no ducking)
    constexpr std::uint32_t kFrames = kFirDelay + 128;
    std::array<float, kFrames> mainL {}, mainR {}, zero {};
    mainL.fill(1.0f);
    mainR.fill(1.0f);
    zero.fill(0.0f);
    std::array<float, kFrames> outL {}, outR {};

    AudioBlock audio;
    audio.mainL    = mainL.data();
    audio.mainR    = mainR.data();
    audio.controlL = zero.data();
    audio.controlR = zero.data();
    audio.outL     = outL.data();
    audio.outR     = outR.data();

    Parameters p;
    p.duckDepth = 100.0f;
    p.cutoffHz  = 5000.0f;  // high cutoff: most signal passes through mid

    processBlock(state, p, BusParameters{}, kFrames, 48000.0, audio);

    // After FIR settles, output should be non-zero (no sidechain = no ducking)
    // For L==R, side=0 and mid=L; with high cutoff, LP(mid) ≈ mid
    float sumOut = 0.0f;
    for (std::uint32_t i = kFirDelay; i < kFrames; ++i) {
        sumOut += std::fabs(outL[i]);
    }
    assert(sumOut > 1.0f);  // significantly non-zero
}

// Full sidechain + full duck reduces the signal
void testDuckingReducesLevel()
{
    EngineState state;
    activate(state);

    constexpr std::uint32_t kFrames = kFirDelay + 256;
    std::array<float, kFrames> mainL {}, mainR {};
    std::array<float, kFrames> sidechain {};
    std::array<float, kFrames> outL {}, outR {};

    mainL.fill(0.5f);
    mainR.fill(0.5f);
    sidechain.fill(1.0f);  // full amplitude sidechain

    AudioBlock audio;
    audio.mainL    = mainL.data();
    audio.mainR    = mainR.data();
    audio.controlL = sidechain.data();
    audio.controlR = sidechain.data();
    audio.outL     = outL.data();
    audio.outR     = outR.data();

    Parameters p;
    p.duckDepth = 100.0f;
    p.attackMs  = 1.0f;    // fast attack so envelope reaches 1.0 quickly
    p.releaseMs = 2000.0f;
    p.cutoffHz  = 5000.0f;

    processBlock(state, p, BusParameters{}, kFrames, 48000.0, audio);

    // After FIR and envelope settle, output should be significantly below input (0.5)
    float sumLate = 0.0f;
    constexpr std::uint32_t kSettle = kFirDelay + 128;
    for (std::uint32_t i = kSettle; i < kFrames; ++i) {
        sumLate += std::fabs(outL[i]);
    }
    const float avgLate = sumLate / static_cast<float>(kFrames - kSettle);
    assert(avgLate < 0.4f);  // appreciably less than 0.5 (ducked)
}

// RMS (relative to the input RMS) of one output channel for a sine, after the FIR has filled.
double sineGainDb(const double hz, const bool monoOnLeft, const Parameters& p, const bool right,
                  const bool useDiff = false)
{
    constexpr std::uint32_t kN = 48000;
    std::vector<float> l(kN), r(kN), zero(kN, 0.0f), oL(kN), oR(kN);
    for (std::uint32_t i = 0; i < kN; ++i) {
        l[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * i / 48000.0));
        r[i] = monoOnLeft ? 0.0f : l[i];
    }
    EngineState state;
    activate(state);
    AudioBlock audio;
    audio.mainL = l.data();
    audio.mainR = r.data();
    audio.controlL = zero.data();
    audio.controlR = zero.data();
    audio.outL = oL.data();
    audio.outR = oR.data();
    processBlock(state, p, BusParameters{}, kN, 48000.0, audio);
    double ss = 0.0;
    for (std::uint32_t i = kN / 2; i < kN; ++i) {
        const double v = useDiff ? 0.5 * (oL[i] - oR[i]) : (right ? oR[i] : oL[i]);
        ss += v * v;
    }
    return 20.0 * std::log10(std::sqrt(ss / (kN / 2)) / (0.5 / std::sqrt(2.0)) + 1.0e-12);
}

// Regression: on the defaults a bass note must come out of both channels at its
// input level. The right channel used to be LP - HP of a short, un-normalised
// FIR, which removed most of everything below the cutoff.
void testDefaultsKeepBass()
{
    for (const double hz : {30.0, 50.0, 80.0, 120.0, 200.0, 1000.0}) {
        for (const bool monoOnLeft : {false, true}) {
            assert(std::fabs(sineGainDb(hz, monoOnLeft, Parameters{}, false)) < 0.1);
            assert(std::fabs(sineGainDb(hz, monoOnLeft, Parameters{}, true)) < 0.1);
        }
    }
}

// With the waveshaper on, the bass is still clean mono in both channels (the added
// harmonics live in the side), and distorted material above the cutoff goes to the side.
void testShapeKeepsBassMonoAndWidensHighs()
{
    Parameters p;
    p.sideShape = 100.0f;
    p.cutoffHz = 200.0f;
    // A bass sine is not in the side channel at all.
    assert(sineGainDb(60.0, false, p, false, true) < -20.0);
    // A loud midrange sine is clipped, so the side carries its harmonics.
    assert(sineGainDb(1000.0, false, p, false, true) > -12.0);
    // At shape 0 there is no side at all: the outputs are identical.
    assert(sineGainDb(1000.0, false, Parameters{}, false, true) < -100.0);
}

// Complex amplitude of a sine at hz in the second half of x (single-bin DFT).
std::complex<double> binAt(const std::vector<float>& x, const double hz)
{
    std::complex<double> acc = 0.0;
    const std::size_t n0 = x.size() / 2;
    for (std::size_t i = n0; i < x.size(); ++i)
        acc += static_cast<double>(x[i])
               * std::polar(1.0, -2.0 * 3.14159265358979 * hz * static_cast<double>(i) / 48000.0);
    return acc * (2.0 / static_cast<double>(x.size() - n0));
}

struct BassRender { std::vector<float> l, r; };

BassRender renderBass(const double hz, const Parameters& p, EngineState* keep = nullptr)
{
    constexpr std::uint32_t kN = 24000;
    std::vector<float> in(kN), zero(kN, 0.0f);
    BassRender o { std::vector<float>(kN), std::vector<float>(kN) };
    for (std::uint32_t i = 0; i < kN; ++i)
        in[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * i / 48000.0));
    EngineState local;
    EngineState& state = keep ? *keep : local;
    activate(state);
    AudioBlock audio;
    audio.mainL = in.data();
    audio.mainR = in.data();
    audio.controlL = zero.data();
    audio.controlR = zero.data();
    audio.outL = o.l.data();
    audio.outR = o.r.data();
    processBlock(state, p, BusParameters{}, kN, 48000.0, audio);
    return o;
}

// Default is off: both channels carry identical bass, and the quadrature network is
// never run or touched.
void testBassPhaseDefaultsOffAndIdle()
{
    assert(Parameters{}.bassPhase == 0);
    EngineState state;
    const BassRender o = renderBass(100.0, Parameters{}, &state);
    assert(!state.quad.running);
    for (std::size_t i = 0; i < o.l.size(); ++i)
        assert(o.l[i] == o.r[i]);
    for (const auto& s : state.quad.pathA) assert(s.x1 == 0.0f && s.y1 == 0.0f);
    for (const auto& s : state.quad.pathB) assert(s.x1 == 0.0f && s.y1 == 0.0f);
}

// On: the bass in the two channels is 90 degrees apart at unchanged level, for either
// choice of channel, across the bass range. Swapping the channel flips the sign.
void testBassPhaseQuadrature()
{
    const double pi = 3.14159265358979;
    for (const double hz : {40.0, 60.0, 100.0, 150.0}) {
        Parameters p;
        p.cutoffHz  = 1000.0f;
        p.bassPhase = 1;
        const BassRender a = renderBass(hz, p);
        const auto aL = binAt(a.l, hz), aR = binAt(a.r, hz);
        assert(std::fabs(std::abs(aL) - 0.5) < 0.03);
        assert(std::fabs(std::abs(aR) - 0.5) < 0.03);
        const double dPhase = std::arg(aL / aR) * 180.0 / pi;
        assert(std::fabs(std::fabs(dPhase) - 90.0) < 3.0);

        p.bassPhase = 2;
        const BassRender b = renderBass(hz, p);
        const double dPhase2 = std::arg(binAt(b.l, hz) / binAt(b.r, hz)) * 180.0 / pi;
        assert(std::fabs(std::fabs(dPhase2) - 90.0) < 3.0);
        assert(dPhase * dPhase2 < 0.0);
    }
}

// Content above the cutoff is not touched by the bass shift.
void testBassPhaseLeavesHighsAlone()
{
    Parameters p;
    p.cutoffHz  = 200.0f;
    p.bassPhase = 1;
    const BassRender o = renderBass(3000.0, p);
    const auto l = binAt(o.l, 3000.0), r = binAt(o.r, 3000.0);
    assert(std::abs(l - r) < 0.01);
}

void testBassPhaseSerializationAndClamp()
{
    Parameters p;
    p.bassPhase = 2;
    const auto q = deserializeParameters(serializeParameters(p));
    assert(q.has_value() && q->bassPhase == 2);
    Parameters raw;
    raw.bassPhase = 7;
    assert(clampParameters(raw).bassPhase == 2);
    raw.bassPhase = -1;
    assert(clampParameters(raw).bassPhase == 0);
    // Old projects have no key: stays off.
    assert(deserializeParameters("version=1\nwet=50\n")->bassPhase == 0);
}

// Main sine x sidechain sine; returns the output spectrum at the given frequencies.
std::vector<double> ringRender(const Parameters& p, std::initializer_list<double> hzs)
{
    constexpr std::uint32_t kN = 24000;
    std::vector<float> in(kN), sc(kN), oL(kN), oR(kN);
    for (std::uint32_t i = 0; i < kN; ++i) {
        in[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 48000.0));
        sc[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 300.0 * i / 48000.0));
    }
    EngineState state;
    activate(state);
    AudioBlock audio;
    audio.mainL = in.data(); audio.mainR = in.data();
    audio.controlL = sc.data(); audio.controlR = sc.data();
    audio.outL = oL.data(); audio.outR = oR.data();
    processBlock(state, p, BusParameters{}, kN, 48000.0, audio);
    std::vector<double> mags;
    for (const double hz : hzs) mags.push_back(std::abs(binAt(oL, hz)));
    return mags;
}

// Attack = release = 0 turns the ducker into a ring mod: sum and difference tones, no carrier
// and no modulator. At any other setting the main tone survives.
void testZeroAttackReleaseIsRingMod()
{
    Parameters p;
    p.duckDepth = 100.0f;
    p.attackMs  = 0.0f;
    p.releaseMs = 0.0f;
    p.cutoffHz  = 5000.0f;
    const auto rm = ringRender(p, {700.0, 1300.0, 1000.0, 300.0});
    assert(std::fabs(rm[0] - 0.125) < 0.01);   // 0.5 * 0.5 / 2
    assert(std::fabs(rm[1] - 0.125) < 0.01);
    assert(rm[2] < 0.01);
    assert(rm[3] < 0.01);

    p.attackMs = 1.0f;  // one control off zero: back to the ducker
    assert(ringRender(p, {1000.0})[0] > 0.05);
    p.attackMs = 0.0f;
    p.releaseMs = 10.0f;
    assert(ringRender(p, {1000.0})[0] > 0.05);

    // Depth 0 is dry even in ring mod.
    p.releaseMs = 0.0f;
    p.duckDepth = 0.0f;
    assert(std::fabs(ringRender(p, {1000.0})[0] - 0.5) < 0.02);
}

// ── Input delay ────────────────────────────────────────────────────────────

constexpr double kSr = 48000.0;

// Renders `in` (both main channels) with the sidechain `sc` in blocks of `block`, asking for the
// parameters and tempo afresh at the start of each block, so settings can change mid-stream.
std::vector<float> renderDelay(EngineState& state,
                               const std::function<Parameters(std::size_t)>& paramsAt,
                               const std::vector<float>& in, const std::vector<float>& sc,
                               double sr = kSr, double bpm = 120.0, std::size_t block = 256)
{
    std::vector<float> out(in.size(), 0.0f), outR(in.size(), 0.0f);
    std::vector<float> silent(in.size(), 0.0f);
    const std::vector<float>& ctl = sc.empty() ? silent : sc;
    for (std::size_t at = 0; at < in.size(); at += block) {
        const std::size_t n = std::min(block, in.size() - at);
        AudioBlock audio;
        audio.mainL = in.data() + at;  audio.mainR = in.data() + at;
        audio.controlL = ctl.data() + at;  audio.controlR = ctl.data() + at;
        audio.outL = out.data() + at;  audio.outR = outR.data() + at;
        Transport tr;
        tr.bpm = bpm;
        processBlock(state, paramsAt(at), BusParameters {}, static_cast<std::uint32_t>(n), sr, audio, tr);
    }
    return out;
}

std::vector<float> renderDelay(const Parameters& p, const std::vector<float>& in,
                               double sr = kSr, double bpm = 120.0, std::size_t block = 256)
{
    EngineState state;
    activate(state);
    return renderDelay(state, [&](std::size_t) { return p; }, in, {}, sr, bpm, block);
}

std::vector<float> impulseAt(std::size_t where, std::size_t length)
{
    std::vector<float> x(length, 0.0f);
    x[where] = 1.0f;
    return x;
}

std::size_t argMax(const std::vector<float>& x)
{
    std::size_t best = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        if (std::fabs(x[i]) > std::fabs(x[best])) best = i;
    return best;
}

std::vector<float> sine(std::size_t length, double hz, double amp = 0.5, double sr = kSr)
{
    std::vector<float> x(length);
    for (std::size_t i = 0; i < length; ++i)
        x[i] = static_cast<float>(amp * std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / sr));
    return x;
}

double rmsOver(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    double s = 0.0;
    for (std::size_t i = begin; i < end && i < x.size(); ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(end - begin));
}

// Defaults are inert: an impulse comes out after the FIR latency and no later, at unity.
void testDelayDefaultsAreInert()
{
    const Parameters p;
    assert(p.delayBeat == 0 && p.delayMs == 0.0f);
    const auto out = renderDelay(p, impulseAt(100, 2000));
    assert(argMax(out) == 100 + kFirDelay);
    assert(std::fabs(out[100 + kFirDelay] - 1.0f) < 1e-4f);
    assert(delaySeconds(p, 120.0) == 0.0);
}

// The ms slider is an exact shift: the impulse lands delayMs later, whole samples, no smearing.
void testMsDelayShiftsExactly()
{
    for (const float ms : {1.0f, 10.0f, 37.5f, 200.0f}) {
        Parameters p;
        p.delayMs = ms;
        const auto expected = static_cast<std::size_t>(std::lround(ms * 0.001 * kSr));
        const auto out = renderDelay(p, impulseAt(100, 12000));
        assert(argMax(out) == 100 + kFirDelay + expected);
        assert(std::fabs(out[argMax(out)] - 1.0f) < 1e-4f);                     // unity, not filtered
        double elsewhere = 0.0;                                                 // and nothing else is left over
        for (std::size_t i = 0; i < out.size(); ++i)
            if (i != argMax(out)) elsewhere = std::max(elsewhere, static_cast<double>(std::fabs(out[i])));
        assert(elsewhere < 1e-4);
    }
}

// The beat slider is locked to the host tempo, in fractions of a beat. The expected beats are
// written out here, not read from the table under test.
void testBeatDelayFollowsTempo()
{
    const double beats[12] = {0.0, 1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 12, 1.0 / 8,
                              1.0 / 6, 1.0 / 4, 1.0 / 3, 1.0 / 2, 3.0 / 4, 1.0};
    for (std::size_t step = 0; step < 12; ++step)
        assert(kBeatFractions[step] == beats[step]);

    for (const double bpm : {60.0, 120.0, 174.0}) {
        for (int step = 0; step < 12; ++step) {
            Parameters p;
            p.delayBeat = step;
            const auto expected = static_cast<std::size_t>(std::lround(beats[step] * 60.0 / bpm * kSr));
            const auto out = renderDelay(p, impulseAt(50, 60000), kSr, bpm);
            assert(argMax(out) == 50 + kFirDelay + expected);
        }
    }
    // The documented anchor points: a quarter of a beat is 125 ms at 120 bpm, 250 ms at 60.
    Parameters q;
    q.delayBeat = 7;
    assert(std::fabs(delaySeconds(q, 120.0) - 0.125) < 1e-12);
    assert(std::fabs(delaySeconds(q, 60.0) - 0.25) < 1e-12);
}

// The two controls add.
void testBeatAndMsAdd()
{
    Parameters p;
    p.delayBeat = 5;     // 1/8 beat = 3000 samples at 120 bpm
    p.delayMs   = 10.0f; // 480 samples
    const auto out = renderDelay(p, impulseAt(100, 8000));
    assert(argMax(out) == 100 + kFirDelay + 3000 + 480);
    assert(std::fabs(delaySeconds(p, 120.0) - 0.0725) < 1e-12);   // 62.5 ms + 10 ms
}

// A host with no usable tempo is read as 120 bpm; the delay never goes past the line.
void testBadTempoAndTheLongestDelay()
{
    Parameters p;
    p.delayBeat = 7;
    for (const double bad : {0.0, -5.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        assert(delaySeconds(p, bad) == delaySeconds(p, 120.0));

    Parameters slow;
    slow.delayBeat = 11;  // one whole beat at 5 bpm is 12 s: cut to the 2 s limit
    slow.delayMs   = 200.0f;
    assert(delaySeconds(slow, 5.0) == kMaxDelaySeconds);
    const auto out = renderDelay(slow, impulseAt(100, 100 * 1000 + 3000), kSr, 5.0);
    assert(argMax(out) == 100 + kFirDelay + static_cast<std::size_t>(kMaxDelaySeconds * kSr));
    for (const float v : out) assert(std::isfinite(v));
}

// Delay applies to the dry path too: Wet 0 is still the shifted input.
void testDelayAppliesToTheDryPath()
{
    Parameters p;
    p.wet = 0.0f;
    p.delayMs = 10.0f;
    const auto out = renderDelay(p, impulseAt(100, 4000));
    assert(argMax(out) == 100 + kFirDelay + 480);
}

// Changing the delay while audio plays is a smooth blend: no jump, and once it settles the output
// is exactly the input shifted (so a settled delay does not filter the sound).
void testDelayChangesAreGlitchFree()
{
    const auto in = sine(30000, 200.0);
    const double maxStep = 2.0 * 3.14159265358979 * 200.0 / kSr * 0.5;  // steepest slope of the input
    EngineState state;
    activate(state);
    const auto out = renderDelay(state,
        [](std::size_t at) { Parameters p; p.delayMs = at >= 4800 ? 5.0f : 0.0f; return p; }, in, {}, kSr, 120.0, 64);

    double worst = 0.0;
    for (std::size_t i = 1; i < out.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(out[i] - out[i - 1])));
    assert(worst < 2.5 * maxStep);          // a hard switch to 5 ms is a jump of roughly 0.4

    for (std::size_t i = 4800 + 600 + kFirDelay; i < out.size(); ++i)
        assert(std::fabs(out[i] - in[i - kFirDelay - 240]) < 1e-5f);
    for (std::size_t i = 2000; i < 4800; ++i)                       // before the change: undelayed
        assert(std::fabs(out[i] - in[i - kFirDelay]) < 1e-5f);

    // A second change while the first is still fading waits its turn, then lands exactly.
    EngineState again;
    activate(again);
    const auto out2 = renderDelay(again,
        [](std::size_t at) { Parameters p; p.delayMs = at >= 4800 + 128 ? 10.0f : (at >= 4800 ? 5.0f : 0.0f); return p; },
        in, {}, kSr, 120.0, 64);
    double worst2 = 0.0;
    for (std::size_t i = 1; i < out2.size(); ++i)
        worst2 = std::max(worst2, static_cast<double>(std::fabs(out2[i] - out2[i - 1])));
    assert(worst2 < 2.5 * maxStep);
    for (std::size_t i = 4800 + 1500 + kFirDelay; i < out2.size(); ++i)
        assert(std::fabs(out2[i] - in[i - kFirDelay - 480]) < 1e-5f);
}

// A tempo change moves the beat delay (after its crossfade) without touching the ms part.
void testTempoChangeRetimesTheBeatDelay()
{
    Parameters p;
    p.delayBeat = 7;     // 1/4 beat
    EngineState state;
    activate(state);
    std::vector<float> in = sine(60000, 200.0);
    std::vector<float> out(in.size(), 0.0f), outR(in.size(), 0.0f), zero(in.size(), 0.0f);
    for (std::size_t at = 0; at < in.size(); at += 256) {
        AudioBlock a;
        a.mainL = in.data() + at; a.mainR = in.data() + at;
        a.controlL = zero.data(); a.controlR = zero.data();
        a.outL = out.data() + at; a.outR = outR.data() + at;
        Transport tr;
        tr.bpm = at < 20000 ? 120.0 : 60.0;
        processBlock(state, p, BusParameters {}, static_cast<std::uint32_t>(std::min<std::size_t>(256, in.size() - at)), kSr, a, tr);
    }
    assert(std::fabs(state.meters.delayMs - 250.0f) < 1e-3f);                           // 1/4 beat at 60 bpm
    assert(std::fabs(out[19000] - in[19000 - kFirDelay - 6000]) < 1e-5f);                // 125 ms before
    assert(std::fabs(out[59000] - in[59000 - kFirDelay - 12000]) < 1e-5f);               // 250 ms after
}

// The point of it: a bass note that starts with the kick is held back, so the kick is heard alone
// first. Observed at the output, with ducking off so the delay is the only thing at work.
void testDelayHoldsTheBassBehindTheKick()
{
    const std::size_t t0 = 4800;
    std::vector<float> bass(20000, 0.0f), kick(20000, 0.0f);
    const auto tone = sine(20000, 90.0, 0.5);
    for (std::size_t i = t0; i < bass.size(); ++i) bass[i] = tone[i - t0];
    for (std::size_t i = t0; i < t0 + 40; ++i) kick[i] = 1.0f;

    Parameters off;
    off.duckDepth = 0.0f;
    Parameters held = off;
    held.delayMs = 20.0f;

    EngineState a, b;
    activate(a);
    activate(b);
    const auto plain = renderDelay(a, [&](std::size_t) { return off; }, bass, kick);
    const auto late  = renderDelay(b, [&](std::size_t) { return held; }, bass, kick);

    const std::size_t from = t0 + kFirDelay;
    const double startPlain = rmsOver(plain, from + 20, from + 700);       // the first 15 ms of the note
    const double startLate  = rmsOver(late,  from + 20, from + 700);
    assert(startPlain > 0.15);
    assert(startLate < 0.02 * startPlain);                                  // held back: silent through the 20 ms
    const double tailPlain = rmsOver(plain, from + 4000, from + 8000);
    const double tailLate  = rmsOver(late,  from + 4000, from + 8000);
    assert(std::fabs(tailLate - tailPlain) < 0.02 * tailPlain);             // and then the bass is all there
}

// The panel's read-out of the total delay.
void testDelayStatusReportsTheTotal()
{
    Parameters p;
    p.delayBeat = 5;
    p.delayMs = 10.0f;
    EngineState state;
    activate(state);
    (void)renderDelay(state, [&](std::size_t) { return p; }, std::vector<float>(512, 0.0f), {}, kSr, 100.0);
    assert(std::fabs(state.meters.delayMs - (0.125f * 60.0f / 100.0f * 1000.0f + 10.0f)) < 1e-3f);  // 1/8 beat at 100 bpm
    assert(std::fabs(state.meters.delayMs - 85.0f) < 1e-3f);
}

// The line is sized up front; nothing is reallocated while the delay moves around, and a sample-rate
// change scales the shift.
void testDelayLineIsPreparedAndFollowsSampleRate()
{
    EngineState state;
    prepare(state, kSr);
    activate(state);
    const float* l = state.delay.bufL.data();
    const std::size_t cap = state.delay.bufL.capacity();
    Parameters p;
    (void)renderDelay(state, [&](std::size_t at) { Parameters q; q.delayMs = static_cast<float>((at / 256) % 190); return q; },
                      sine(40000, 150.0), {}, kSr, 97.0, 100);
    assert(state.delay.bufL.data() == l && state.delay.bufL.capacity() == cap);

    // Same state, new sample rate, no prepare(): the host did not tell us, the engine copes.
    p.delayMs = 10.0f;
    const auto out = renderDelay(state, [&](std::size_t) { return p; }, impulseAt(100, 6000), {}, 96000.0);
    assert(argMax(out) == 100 + kFirDelay + 960);
}

// Activation forgets the audio but not the setting.
void testActivationClearsTheDelayLineOnly()
{
    Parameters p;
    p.delayMs = 50.0f;
    EngineState state;
    activate(state);
    (void)renderDelay(state, [&](std::size_t) { return p; }, sine(8000, 300.0), {});
    activate(state);
    const auto out = renderDelay(state, [&](std::size_t) { return p; }, std::vector<float>(8000, 0.0f), {});
    for (const float v : out) assert(std::fabs(v) < 1e-6f);     // nothing left over to replay
    // And the first block after activation starts settled rather than fading in from zero.
    activate(state);
    const auto again = renderDelay(state, [&](std::size_t) { return p; }, impulseAt(10, 6000), {}, kSr);
    assert(argMax(again) == 10 + kFirDelay + 2400);
}

void testDelaySerializationAndClamp()
{
    Parameters p;
    p.delayBeat = 7;
    p.delayMs   = 12.5f;
    const auto q = deserializeParameters(serializeParameters(p));
    assert(q.has_value() && q->delayBeat == 7 && std::fabs(q->delayMs - 12.5f) < 1e-3f);

    Parameters raw;
    raw.delayBeat = 99;  raw.delayMs = 999.0f;
    assert(clampParameters(raw).delayBeat == 11 && clampParameters(raw).delayMs == 200.0f);
    raw.delayBeat = -3;  raw.delayMs = -5.0f;
    assert(clampParameters(raw).delayBeat == 0 && clampParameters(raw).delayMs == 0.0f);

    // Projects saved before the delay existed load with it off.
    const auto old = deserializeParameters("version=1\nduckDepth=50\nbassPhase=1\n");
    assert(old.has_value() && old->delayBeat == 0 && old->delayMs == 0.0f);
}

}  // namespace

int main()
{
    testClampParameters();
    testSerializationRoundTrip();
    testDeserializeToleratesUnknownKeys();
    testSilencePassThrough();
    testNoDuckingWithNoSidechain();
    testDuckingReducesLevel();
    testDefaultsKeepBass();
    testShapeKeepsBassMonoAndWidensHighs();
    testBassPhaseDefaultsOffAndIdle();
    testBassPhaseQuadrature();
    testBassPhaseLeavesHighsAlone();
    testBassPhaseSerializationAndClamp();
    testZeroAttackReleaseIsRingMod();
    testDelayDefaultsAreInert();
    testMsDelayShiftsExactly();
    testBeatDelayFollowsTempo();
    testBeatAndMsAdd();
    testBadTempoAndTheLongestDelay();
    testDelayAppliesToTheDryPath();
    testDelayChangesAreGlitchFree();
    testTempoChangeRetimesTheBeatDelay();
    testDelayHoldsTheBassBehindTheKick();
    testDelayStatusReportsTheTotal();
    testDelayLineIsPreparedAndFollowsSampleRate();
    testActivationClearsTheDelayLineOnly();
    testDelaySerializationAndClamp();
    std::puts("All Bassops core tests passed.");
    return 0;
}
