#include "t_mix_engine.hpp"
#include "t_mix_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::tmix;

namespace {

constexpr float kTolerance = 1.0e-5f;

void testClampAndSerialization()
{
    Parameters parameters;
    parameters.channels[0].levelDb = 99.0f;
    parameters.channels[1].levelDb = -99.0f;
    parameters.channels[2].pan = 4.0f;
    parameters.channels[3].pan = -4.0f;
    parameters.channels[4].mute = 0.7f;
    parameters.channels[5].solo = 0.2f;
    parameters.masterDb = 30.0f;

    const Parameters clamped = clampParameters(parameters);
    assert(clamped.channels[0].levelDb == kMaximumLevelDb);
    assert(clamped.channels[1].levelDb == kMinimumLevelDb);
    assert(clamped.channels[2].pan == 1.0f);
    assert(clamped.channels[3].pan == -1.0f);
    assert(clamped.channels[4].mute == 1.0f);
    assert(clamped.channels[5].solo == 0.0f);
    assert(clamped.masterDb == kMaximumLevelDb);

    const auto decoded = deserializeParameters(serializeParameters(clamped));
    assert(decoded.has_value());
    assert(decoded->channels[0].levelDb == kMaximumLevelDb);
    assert(decoded->channels[2].pan == 1.0f);
    assert(decoded->channels[4].mute == 1.0f);
    assert(decoded->masterDb == kMaximumLevelDb);
    assert(!deserializeParameters("channel0.unknown=1\n").has_value());
}

void testPanAndMasterGain()
{
    EngineState state;
    activate(state);
    Parameters parameters;
    parameters.channels[0].pan = -1.0f;
    parameters.channels[1].pan = 1.0f;
    parameters.masterDb = -6.0f;

    std::array<float, 3> input0 {1.0f, 0.5f, -0.25f};
    std::array<float, 3> input1 {0.5f, -1.0f, 0.25f};
    std::array<float, 3> left {};
    std::array<float, 3> right {};
    AudioBlock audio;
    audio.inputs[0] = input0.data();
    audio.inputs[1] = input1.data();
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();

    (void)processBlock(state, parameters, 3, 48000.0, audio);
    const float master = decibelsToGain(-6.0f);
    for (std::size_t i = 0; i < left.size(); ++i) {
        assert(std::fabs(left[i] - input0[i] * master) < kTolerance);
        assert(std::fabs(right[i] - input1[i] * master) < kTolerance);
    }
}

void testCenteredPanIsConstantPower()
{
    EngineState state;
    activate(state);
    Parameters parameters;
    std::array<float, 1> input {1.0f};
    std::array<float, 1> left {};
    std::array<float, 1> right {};
    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();

    (void)processBlock(state, parameters, 1, 48000.0, audio);
    const float expected = std::sqrt(0.5f);
    assert(std::fabs(left[0] - expected) < kTolerance);
    assert(std::fabs(right[0] - expected) < kTolerance);
}

void testMuteAndSoloRules()
{
    EngineState state;
    activate(state);
    Parameters parameters;
    parameters.channels[0].pan = -1.0f;
    parameters.channels[1].pan = -1.0f;
    parameters.channels[1].solo = 1.0f;
    parameters.channels[2].pan = -1.0f;
    parameters.channels[2].solo = 1.0f;
    parameters.channels[2].mute = 1.0f;

    std::array<float, 1> one {1.0f};
    std::array<float, 1> two {2.0f};
    std::array<float, 1> four {4.0f};
    std::array<float, 1> left {};
    std::array<float, 1> right {};
    AudioBlock audio;
    audio.inputs[0] = one.data();
    audio.inputs[1] = two.data();
    audio.inputs[2] = four.data();
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();

    (void)processBlock(state, parameters, 1, 48000.0, audio);
    assert(std::fabs(left[0] - 2.0f) < kTolerance);
    assert(std::fabs(right[0]) < kTolerance);
}

void testMetersArePreFaderAndDecay()
{
    EngineState state;
    activate(state);
    Parameters parameters;
    parameters.channels[0].mute = 1.0f;
    std::array<float, 2> input {0.25f, -0.8f};
    std::array<float, 2> left {};
    std::array<float, 2> right {};
    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();

    OutputStatus status = processBlock(state, parameters, 2, 1000.0, audio);
    assert(std::fabs(status.meters[0] - 0.8f) < kTolerance);
    assert(std::fabs(left[0]) < kTolerance);

    std::array<float, 300> silence {};
    std::array<float, 300> longLeft {};
    std::array<float, 300> longRight {};
    audio.inputs[0] = silence.data();
    audio.outputs[0] = longLeft.data();
    audio.outputs[1] = longRight.data();
    status = processBlock(state, parameters, 300, 1000.0, audio);
    assert(status.meters[0] > 0.29f && status.meters[0] < 0.30f);
}

}  // namespace

int main()
{
    testClampAndSerialization();
    testPanAndMasterGain();
    testCenteredPanIsConstantPower();
    testMuteAndSoloRules();
    testMetersArePreFaderAndDecay();
    return 0;
}
