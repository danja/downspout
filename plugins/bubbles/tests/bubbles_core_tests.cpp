#include "bubbles_engine.hpp"
#include "bubbles_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace downspout::bubbles;

namespace {

constexpr double kSR = 48000.0;
constexpr uint32_t kBlockSize = 256;

void testActivateProducesCleanState()
{
    EngineState state;
    activate(state, kSR);
    assert(state.gateAmp == 1.0f);
    for (float s : state.delayL) assert(s == 0.0f);
    for (float s : state.delayR) assert(s == 0.0f);
    for (const auto& v : state.bubbles) assert(!v.active);
    for (const auto& v : state.drips)   assert(!v.active);
    std::puts("PASS: testActivateProducesCleanState");
}

void testDefaultParametersProduceAudio()
{
    EngineState state;
    activate(state, kSR);

    std::array<float, kBlockSize> L {}, R {};
    Parameters params;
    TransportSnapshot transport;

    processBlock(state, params, transport, kBlockSize, kSR,
                 L.data(), R.data(), nullptr, 0);

    // After one block the engine should produce non-zero output
    float energy = 0.0f;
    for (float s : L) energy += s * s;
    assert(energy > 0.0f);
    std::puts("PASS: testDefaultParametersProduceAudio");
}

void testAllModesProduceAudio()
{
    for (int m = 0; m < static_cast<int>(WaterMode::Count); ++m) {
        EngineState state;
        activate(state, kSR);

        Parameters params;
        params.mode = static_cast<float>(m);
        params.density = 0.8f;
        TransportSnapshot transport;

        std::array<float, kBlockSize * 4> L {}, R {};
        processBlock(state, params, transport,
                     static_cast<uint32_t>(L.size()), kSR,
                     L.data(), R.data(), nullptr, 0);

        float energy = 0.0f;
        for (float s : L) energy += s * s;
        assert(energy > 0.0f);
    }
    std::puts("PASS: testAllModesProduceAudio");
}

void testZeroOutputIsFinite()
{
    EngineState state;
    activate(state, kSR);

    Parameters params;
    params.output = 0.0f;
    TransportSnapshot transport;

    std::array<float, kBlockSize> L {}, R {};
    processBlock(state, params, transport, kBlockSize, kSR,
                 L.data(), R.data(), nullptr, 0);

    for (float s : L) assert(std::isfinite(s));
    for (float s : R) assert(std::isfinite(s));
    std::puts("PASS: testZeroOutputIsFinite");
}

void testMidiNoteGatesCycles()
{
    EngineState state;
    activate(state, kSR);

    // Send note-off: gate should start fading
    InputMidiEvent noteOff;
    noteOff.frame  = 0;
    noteOff.data[0] = 0x80;  // note-off ch1
    noteOff.data[1] = 60;
    noteOff.data[2] = 0;

    Parameters params;
    TransportSnapshot transport;
    std::array<float, kBlockSize> L {}, R {};
    processBlock(state, params, transport, kBlockSize, kSR,
                 L.data(), R.data(), &noteOff, 1);

    // gateOn should be false after note-off
    assert(!state.gateOn);

    // After a long note-off period gateAmp should be < 1
    for (int blk = 0; blk < 10; ++blk) {
        processBlock(state, params, transport, kBlockSize, kSR,
                     L.data(), R.data(), nullptr, 0);
    }
    assert(state.gateAmp < 1.0f);
    std::puts("PASS: testMidiNoteGatesCycles");
}

void testClampParametersEnforcesRange()
{
    Parameters raw;
    raw.flow = 5.0f;
    raw.depth = -1.0f;
    raw.mode = 99.0f;
    Parameters clamped = clampParameters(raw);
    assert(clamped.flow  == 1.0f);
    assert(clamped.depth == 0.0f);
    assert(clamped.mode  == 6.0f);
    std::puts("PASS: testClampParametersEnforcesRange");
}

void testSerializeRoundtrip()
{
    Parameters p;
    p.mode = 2.0f; p.flow = 0.77f; p.turbulence = 0.33f;
    p.size = 0.5f; p.heat = 0.8f;

    const std::string text = serializeParameters(p);
    auto loaded = deserializeParameters(text);
    assert(loaded.has_value());
    assert(std::fabs(loaded->mode - p.mode)       < 1e-4f);
    assert(std::fabs(loaded->flow - p.flow)       < 1e-4f);
    assert(std::fabs(loaded->turbulence - p.turbulence) < 1e-4f);
    assert(std::fabs(loaded->heat - p.heat)       < 1e-4f);
    std::puts("PASS: testSerializeRoundtrip");
}

void testOutputIsFiniteUnderAllParams()
{
    // Extreme parameter values must not produce NaN/Inf
    EngineState state;
    activate(state, kSR);

    Parameters params;
    params.flow = 1.0f; params.turbulence = 1.0f; params.density = 1.0f;
    params.heat = 1.0f; params.drive = 1.0f; params.resonance = 1.0f;
    params.space = 1.0f; params.output = 1.0f;
    TransportSnapshot transport;

    std::array<float, kBlockSize * 8> L {}, R {};
    processBlock(state, params, transport,
                 static_cast<uint32_t>(L.size()), kSR,
                 L.data(), R.data(), nullptr, 0);

    for (std::size_t i = 0; i < L.size(); ++i) {
        assert(std::isfinite(L[i]));
        assert(std::isfinite(R[i]));
    }
    std::puts("PASS: testOutputIsFiniteUnderAllParams");
}

}  // namespace

int main()
{
    testActivateProducesCleanState();
    testDefaultParametersProduceAudio();
    testAllModesProduceAudio();
    testZeroOutputIsFinite();
    testMidiNoteGatesCycles();
    testClampParametersEnforcesRange();
    testSerializeRoundtrip();
    testOutputIsFiniteUnderAllParams();
    std::puts("All Bubbles core tests passed.");
    return 0;
}
