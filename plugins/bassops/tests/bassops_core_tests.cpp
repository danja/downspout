#include "bassops_engine.hpp"
#include "bassops_serialization.hpp"

#include <array>
#include "downspout/test_assert.h"
#include <cmath>
#include <complex>
#include <initializer_list>
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
    return 0;
}
