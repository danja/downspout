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
    parameters.producerSlewMs = 900.0f;

    const Parameters clamped = clampParameters(parameters);
    assert(clamped.channels[0].levelDb == kMaximumLevelDb);
    assert(clamped.channels[1].levelDb == kMinimumLevelDb);
    assert(clamped.channels[2].pan == 1.0f);
    assert(clamped.channels[3].pan == -1.0f);
    assert(clamped.channels[4].mute == 1.0f);
    assert(clamped.channels[5].solo == 0.0f);
    assert(clamped.masterDb == kMaximumLevelDb);
    assert(clamped.producerSlewMs == 500.0f);

    const auto decoded = deserializeParameters(serializeParameters(clamped));
    assert(decoded.has_value());
    assert(decoded->channels[0].levelDb == kMaximumLevelDb);
    assert(decoded->channels[2].pan == 1.0f);
    assert(decoded->channels[4].mute == 1.0f);
    assert(decoded->masterDb == kMaximumLevelDb);
    assert(decoded->producerSlewMs == 500.0f);
    assert(!deserializeParameters("channel0.unknown=1\n").has_value());

    const auto legacy = deserializeParameters("version=1\nmaster_db=-3\n");
    assert(legacy.has_value());
    assert(legacy->producerSlewMs == kDefaultProducerSlewMs);
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

void testProducerCcControlsTheMatchingChannelAtItsFrame()
{
    EngineState state;
    activate(state);
    Parameters parameters;
    parameters.producerSlewMs = 0.0f;

    std::array<float, 4> input {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> left {};
    std::array<float, 4> right {};
    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();

    MidiControlEvent event;
    event.frame = 2;
    event.data = {0xb0, kProducerCcBase, 0};
    const OutputStatus status = processBlock(state, parameters, 4, 48000.0, audio, &event, 1);
    const float centered = std::sqrt(0.5f);
    assert(std::fabs(left[0] - centered) < kTolerance);
    assert(std::fabs(left[1] - centered) < kTolerance);
    assert(std::fabs(left[2]) < kTolerance);
    assert(std::fabs(left[3]) < kTolerance);
    assert(status.producerGains[0] == 0.0f);
    assert(status.producerGains[1] == 1.0f);
}

void testProducerCcContractAndSmoothing()
{
    EngineState state;
    activate(state);
    const std::uint8_t unrelated[] {0xb0, 19, 0};
    const std::uint8_t channelEight[] {0xbf, static_cast<std::uint8_t>(kProducerCcBase + 7), 64};
    assert(!handleMidi(state, unrelated, 3));
    assert(handleMidi(state, channelEight, 3));
    assert(std::fabs(state.producerTargets[7] - 64.0f / 127.0f) < kTolerance);

    Parameters parameters;
    parameters.producerSlewMs = 25.0f;
    std::array<float, 64> silence {};
    AudioBlock audio;
    audio.inputs[7] = silence.data();
    (void)processBlock(state, parameters, 64, 48000.0, audio);
    assert(state.producerGains[7] < 1.0f);
    assert(state.producerGains[7] > state.producerTargets[7]);
}

}  // namespace

int main()
{
    testClampAndSerialization();
    testPanAndMasterGain();
    testCenteredPanIsConstantPower();
    testMuteAndSoloRules();
    testMetersArePreFaderAndDecay();
    testProducerCcControlsTheMatchingChannelAtItsFrame();
    testProducerCcContractAndSmoothing();
    return 0;
}
