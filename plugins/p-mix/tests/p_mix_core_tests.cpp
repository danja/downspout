#include "p_mix_engine.hpp"
#include "p_mix_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::pmix;

namespace {

void testStoppedTransportPassThrough() {
    EngineState state;
    activate(state);

    std::array<float, 8> in0 {};
    std::array<float, 8> out0 {};
    in0.fill(0.5f);

    AudioBlock audio;
    audio.inputs[0] = in0.data();
    audio.outputs[0] = out0.data();

    TransportSnapshot transport;
    transport.valid = false;

    processBlock(state, Parameters{}, transport, static_cast<std::uint32_t>(in0.size()), 48000.0, audio);
    for (float sample : out0) {
        assert(std::fabs(sample - 0.5f) < 1e-6f);
    }
}

void testBoundaryCutToSilence() {
    EngineState state;
    activate(state);

    std::array<float, 4> in0 {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> out0 {};

    AudioBlock audio;
    audio.inputs[0] = in0.data();
    audio.outputs[0] = out0.data();

    Parameters parameters;
    parameters.maintain = 0.0f;
    parameters.fade = 0.0f;
    parameters.cut = 100.0f;
    parameters.bias = 0.0f;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    processBlock(state, parameters, transport, static_cast<std::uint32_t>(in0.size()), 48000.0, audio);
    for (float sample : out0) {
        assert(std::fabs(sample) < 1e-6f);
    }
    assert(std::fabs(state.currentGain) < 1e-6f);
}

void testFadeProgression() {
    EngineState state;
    activate(state);

    constexpr std::size_t frameCount = 128;
    std::array<float, frameCount> in0 {};
    std::array<float, frameCount> out0 {};
    in0.fill(1.0f);

    AudioBlock audio;
    audio.inputs[0] = in0.data();
    audio.outputs[0] = out0.data();

    Parameters parameters;
    parameters.granularity = 1.0f;
    parameters.maintain = 0.0f;
    parameters.fade = 100.0f;
    parameters.cut = 0.0f;
    parameters.bias = 0.0f;
    parameters.fadeDurMax = kFadeMinFraction;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 8.0;
    transport.bpm = 60.0;

    processBlock(state, parameters, transport, static_cast<std::uint32_t>(frameCount), 64.0, audio);
    assert(out0[0] < 1.0f);
    assert(out0[0] > out0[32]);
    assert(out0[32] > out0[127]);
    assert(out0[127] < 0.5f);
    assert(state.currentGain < 0.5f);
}

void testSerializationRoundTrip() {
    Parameters parameters;
    parameters.granularity = 7.0f;
    parameters.maintain = 10.0f;
    parameters.fade = 20.0f;
    parameters.cut = 70.0f;
    parameters.fadeDurMax = 0.5f;
    parameters.bias = 35.0f;

    const auto roundTrip = deserializeParameters(serializeParameters(parameters));
    assert(roundTrip.has_value());
    assert(std::fabs(roundTrip->granularity - 7.0f) < 1e-6f);
    assert(std::fabs(roundTrip->maintain - 10.0f) < 1e-6f);
    assert(std::fabs(roundTrip->fade - 20.0f) < 1e-6f);
    assert(std::fabs(roundTrip->cut - 70.0f) < 1e-6f);
    assert(std::fabs(roundTrip->fadeDurMax - 0.5f) < 1e-6f);
    assert(std::fabs(roundTrip->bias - 35.0f) < 1e-6f);
}

}  // namespace

int main() {
    testStoppedTransportPassThrough();
    testBoundaryCutToSilence();
    testFadeProgression();
    testSerializationRoundTrip();
    return 0;
}
