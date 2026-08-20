#include "bassops_engine.hpp"
#include "bassops_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>

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

void testDeserializeInvalidReturnsNullopt()
{
    const auto result = deserializeParameters("version=1\nunknownKey=1.0\n");
    assert(!result.has_value());
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

    processBlock(state, Parameters{}, kFrames, 48000.0, audio);

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

    processBlock(state, p, kFrames, 48000.0, audio);

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

    processBlock(state, p, kFrames, 48000.0, audio);

    // After FIR and envelope settle, output should be significantly below input (0.5)
    float sumLate = 0.0f;
    constexpr std::uint32_t kSettle = kFirDelay + 128;
    for (std::uint32_t i = kSettle; i < kFrames; ++i) {
        sumLate += std::fabs(outL[i]);
    }
    const float avgLate = sumLate / static_cast<float>(kFrames - kSettle);
    assert(avgLate < 0.4f);  // appreciably less than 0.5 (ducked)
}

}  // namespace

int main()
{
    testClampParameters();
    testSerializationRoundTrip();
    testDeserializeInvalidReturnsNullopt();
    testSilencePassThrough();
    testNoDuckingWithNoSidechain();
    testDuckingReducesLevel();
    return 0;
}
