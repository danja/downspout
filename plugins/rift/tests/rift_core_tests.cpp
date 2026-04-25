#include "rift_engine.hpp"
#include "rift_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::rift;

namespace {

void testClampParameters() {
    Parameters parameters;
    parameters.grid = 42.0f;
    parameters.density = -5.0f;
    parameters.damage = 120.0f;
    parameters.memoryBars = 99.0f;
    parameters.drift = -1.0f;
    parameters.pitch = 99.0f;
    parameters.mix = 140.0f;
    parameters.hold = 7.0f;

    const Parameters clamped = clampParameters(parameters);
    assert(std::fabs(clamped.grid - 16.0f) < 1e-6f);
    assert(std::fabs(clamped.density) < 1e-6f);
    assert(std::fabs(clamped.damage - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.memoryBars - 8.0f) < 1e-6f);
    assert(std::fabs(clamped.drift) < 1e-6f);
    assert(std::fabs(clamped.pitch - 12.0f) < 1e-6f);
    assert(std::fabs(clamped.mix - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.hold - 1.0f) < 1e-6f);
}

void testPreviewActionHonorsZeroDensity() {
    Parameters parameters;
    parameters.density = 0.0f;
    for (std::uint64_t index = 0; index < 32; ++index) {
        assert(previewActionForBlock(parameters, index) == ActionType::Pass);
    }
}

void testStoppedTransportPassesThrough() {
    EngineState state;
    activate(state, 8.0, 2);

    std::array<float, 8> inL {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    std::array<float, 8> inR {0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f};
    std::array<float, 8> outL {};
    std::array<float, 8> outR {};

    AudioBlock audio;
    audio.inputs[0] = inL.data();
    audio.inputs[1] = inR.data();
    audio.outputs[0] = outL.data();
    audio.outputs[1] = outR.data();
    audio.channelCount = 2;

    const OutputStatus status = processBlock(state,
                                             Parameters {},
                                             Triggers {},
                                             TransportSnapshot {},
                                             static_cast<std::uint32_t>(inL.size()),
                                             8.0,
                                             audio);

    assert(status.action == ActionType::Pass);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        assert(std::fabs(outL[i] - inL[i]) < 1e-6f);
        assert(std::fabs(outR[i] - inR[i]) < 1e-6f);
    }
}

void testScatterMutatesAndRecoverReturnsDry() {
    EngineState state;
    activate(state, 8.0, 1);

    Parameters parameters;
    parameters.grid = 1.0f;
    parameters.density = 0.0f;
    parameters.damage = 100.0f;
    parameters.memoryBars = 2.0f;
    parameters.drift = 80.0f;
    parameters.pitch = 7.0f;
    parameters.mix = 100.0f;

    std::array<float, 32> firstIn {};
    std::array<float, 32> secondIn {};
    std::array<float, 32> thirdIn {};
    for (std::size_t i = 0; i < firstIn.size(); ++i) {
        firstIn[i] = static_cast<float>(i) * 0.05f;
        secondIn[i] = 10.0f + static_cast<float>(i) * 0.07f;
        thirdIn[i] = 20.0f + static_cast<float>(i) * 0.09f;
    }

    std::array<float, 32> firstOut {};
    std::array<float, 32> secondOut {};
    std::array<float, 32> thirdOut {};

    AudioBlock audio;
    audio.inputs[0] = firstIn.data();
    audio.outputs[0] = firstOut.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    const OutputStatus firstStatus = processBlock(state,
                                                  parameters,
                                                  Triggers {},
                                                  transport,
                                                  static_cast<std::uint32_t>(firstIn.size()),
                                                  8.0,
                                                  audio);
    assert(firstStatus.action == ActionType::Pass);

    audio.inputs[0] = secondIn.data();
    audio.outputs[0] = secondOut.data();
    transport.bar = 1.0;
    const OutputStatus mutated = processBlock(state,
                                              parameters,
                                              Triggers {.scatterSerial = 1},
                                              transport,
                                              static_cast<std::uint32_t>(secondIn.size()),
                                              8.0,
                                              audio);

    assert(mutated.action != ActionType::Pass);
    bool changed = false;
    for (std::size_t i = 0; i < secondIn.size(); ++i) {
        if (std::fabs(secondOut[i] - secondIn[i]) > 1e-4f) {
            changed = true;
            break;
        }
    }
    assert(changed);

    audio.inputs[0] = thirdIn.data();
    audio.outputs[0] = thirdOut.data();
    transport.bar = 2.0;
    const OutputStatus recovered = processBlock(state,
                                                parameters,
                                                Triggers {.scatterSerial = 1, .recoverSerial = 1},
                                                transport,
                                                static_cast<std::uint32_t>(thirdIn.size()),
                                                8.0,
                                                audio);

    assert(recovered.action == ActionType::Pass);
    for (std::size_t i = 0; i < thirdIn.size(); ++i) {
        assert(std::fabs(thirdOut[i] - thirdIn[i]) < 1e-6f);
    }
}

void testSerializationRoundTrip() {
    Parameters parameters;
    parameters.grid = 5.0f;
    parameters.density = 62.0f;
    parameters.damage = 71.0f;
    parameters.memoryBars = 4.0f;
    parameters.drift = 55.0f;
    parameters.pitch = -7.0f;
    parameters.mix = 84.0f;
    parameters.hold = 1.0f;

    const auto roundTrip = deserializeParameters(serializeParameters(parameters));
    assert(roundTrip.has_value());
    assert(std::fabs(roundTrip->grid - 5.0f) < 1e-6f);
    assert(std::fabs(roundTrip->density - 62.0f) < 1e-6f);
    assert(std::fabs(roundTrip->damage - 71.0f) < 1e-6f);
    assert(std::fabs(roundTrip->memoryBars - 4.0f) < 1e-6f);
    assert(std::fabs(roundTrip->drift - 55.0f) < 1e-6f);
    assert(std::fabs(roundTrip->pitch + 7.0f) < 1e-6f);
    assert(std::fabs(roundTrip->mix - 84.0f) < 1e-6f);
    assert(std::fabs(roundTrip->hold - 1.0f) < 1e-6f);
}

}  // namespace

int main() {
    testClampParameters();
    testPreviewActionHonorsZeroDensity();
    testStoppedTransportPassesThrough();
    testScatterMutatesAndRecoverReturnsDry();
    testSerializationRoundTrip();
    return 0;
}
