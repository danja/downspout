#include "bassops_engine.hpp"
#include "bassops_serialization.hpp"

#include <array>
#include "downspout/test_assert.h"
#include <cmath>
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
    assert(p.attackMs  == 1.0f);
    assert(p.releaseMs == 10.0f);
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
    return 0;
}
