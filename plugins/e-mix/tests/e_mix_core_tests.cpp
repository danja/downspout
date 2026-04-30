#include "e_mix_engine.hpp"
#include "e_mix_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::emix;

namespace {

void testClampParametersRespectsDependentRanges() {
    Parameters parameters;
    parameters.totalBars = 0.0f;
    parameters.division = 600.0f;
    parameters.steps = 999.0f;
    parameters.offset = 999.0f;
    parameters.fadeBars = 9999.0f;

    const Parameters clamped = clampParameters(parameters);
    assert(std::fabs(clamped.totalBars - 1.0f) < 1e-6f);
    assert(std::fabs(clamped.division - 512.0f) < 1e-6f);
    assert(std::fabs(clamped.steps - 512.0f) < 1e-6f);
    assert(std::fabs(clamped.offset - 511.0f) < 1e-6f);
    assert(std::fabs(clamped.fadeBars - 1.0f) < 1e-6f);
}

void testBlockPatternWrapsAndOffsets() {
    Parameters parameters;
    parameters.division = 8.0f;
    parameters.steps = 3.0f;
    parameters.offset = 2.0f;

    const std::array<bool, 8> expected = {false, true, false, false, true, false, true, false};
    for (int index = 0; index < 8; ++index) {
        assert(blockIsActive(parameters, index) == expected[static_cast<std::size_t>(index)]);
    }
    assert(blockIsActive(parameters, 9) == expected[1]);
}

void testStoppedTransportUsesFallbackClock() {
    EngineState state;
    activate(state);

    Parameters parameters;
    parameters.totalBars = 1.0f;
    parameters.division = 4.0f;
    parameters.steps = 1.0f;
    parameters.offset = 0.0f;

    std::array<float, 8> in0 {};
    std::array<float, 8> out0 {};
    in0.fill(1.0f);

    AudioBlock audio;
    audio.inputs[0] = in0.data();
    audio.outputs[0] = out0.data();
    audio.channelCount = 1;

    TransportSnapshot playing;
    playing.valid = true;
    playing.playing = true;
    playing.bar = 0.0;
    playing.barBeat = 0.0;
    playing.beatsPerBar = 4.0;
    playing.bpm = 60.0;

    processBlock(state, parameters, playing, static_cast<std::uint32_t>(out0.size()), 4.0, audio);
    assert(std::fabs(out0[0] - 1.0f) < 1e-6f);
    assert(std::fabs(out0[3] - 1.0f) < 1e-6f);
    assert(std::fabs(out0[4]) < 1e-6f);

    out0.fill(2.0f);
    TransportSnapshot stopped;
    processBlock(state, parameters, stopped, static_cast<std::uint32_t>(out0.size()), 4.0, audio);
    for (float sample : out0) {
        assert(std::fabs(sample) < 1e-6f);
    }
}

void testTransportRestartResetsCycleOrigin() {
    EngineState state;
    activate(state);

    Parameters parameters;
    parameters.totalBars = 2.0f;
    parameters.division = 4.0f;
    parameters.steps = 1.0f;
    parameters.offset = 0.0f;

    std::array<float, 4> in0 {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> out0 {};

    AudioBlock audio;
    audio.inputs[0] = in0.data();
    audio.outputs[0] = out0.data();
    audio.channelCount = 1;

    TransportSnapshot startA;
    startA.valid = true;
    startA.playing = true;
    startA.bar = 8.0;
    startA.barBeat = 0.0;
    startA.beatsPerBar = 4.0;
    startA.bpm = 60.0;

    processBlock(state, parameters, startA, static_cast<std::uint32_t>(out0.size()), 4.0, audio);
    assert(std::fabs(out0[0] - 1.0f) < 1e-6f);

    TransportSnapshot stopped;
    processBlock(state, parameters, stopped, static_cast<std::uint32_t>(out0.size()), 4.0, audio);

    out0.fill(0.0f);
    TransportSnapshot startB = startA;
    startB.bar = 20.0;
    processBlock(state, parameters, startB, static_cast<std::uint32_t>(out0.size()), 4.0, audio);
    assert(std::fabs(out0[0] - 1.0f) < 1e-6f);
}

void testFadeShapesActiveBlock() {
    EngineState state;
    activate(state);

    Parameters parameters;
    parameters.totalBars = 1.0f;
    parameters.division = 1.0f;
    parameters.steps = 1.0f;
    parameters.offset = 0.0f;
    parameters.fadeBars = 1.0f;

    std::array<float, 4> in0 {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> out0 {};

    AudioBlock audio;
    audio.inputs[0] = in0.data();
    audio.outputs[0] = out0.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    processBlock(state, parameters, transport, static_cast<std::uint32_t>(out0.size()), 1.0, audio);
    assert(std::fabs(out0[0]) < 1e-6f);
    assert(std::fabs(out0[1] - 0.25f) < 1e-6f);
    assert(std::fabs(out0[2] - 0.5f) < 1e-6f);
    assert(std::fabs(out0[3] - 0.25f) < 1e-6f);
}

void testSerializationRoundTrip() {
    Parameters parameters;
    parameters.totalBars = 64.0f;
    parameters.division = 24.0f;
    parameters.steps = 11.0f;
    parameters.offset = 3.0f;
    parameters.fadeBars = 2.0f;

    const auto roundTrip = deserializeParameters(serializeParameters(parameters));
    assert(roundTrip.has_value());
    assert(std::fabs(roundTrip->totalBars - 64.0f) < 1e-6f);
    assert(std::fabs(roundTrip->division - 24.0f) < 1e-6f);
    assert(std::fabs(roundTrip->steps - 11.0f) < 1e-6f);
    assert(std::fabs(roundTrip->offset - 3.0f) < 1e-6f);
    assert(std::fabs(roundTrip->fadeBars - 2.0f) < 1e-6f);
}

}  // namespace

int main() {
    testClampParametersRespectsDependentRanges();
    testBlockPatternWrapsAndOffsets();
    testStoppedTransportUsesFallbackClock();
    testTransportRestartResetsCycleOrigin();
    testFadeShapesActiveBlock();
    testSerializationRoundTrip();
    return 0;
}
