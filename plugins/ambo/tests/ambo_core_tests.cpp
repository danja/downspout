#include "ambo_engine.hpp"
#include "ambo_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <string>

using namespace downspout::ambo;

namespace {

void testClampParameters()
{
    Parameters raw;
    raw.chain = 99.0f;
    raw.time = -1.0f;
    raw.spectral = 2.0f;
    raw.tape = -0.2f;
    raw.shimmer = 1.8f;
    raw.delay = -4.0f;
    raw.drive = 3.0f;
    raw.feedback = 2.0f;
    raw.mix = -1.0f;
    raw.output = 48.0f;

    const Parameters clamped = clampParameters(raw);
    assert(std::fabs(clamped.chain - 3.0f) < 1e-6f);
    assert(std::fabs(clamped.time) < 1e-6f);
    assert(std::fabs(clamped.spectral - 1.0f) < 1e-6f);
    assert(std::fabs(clamped.tape) < 1e-6f);
    assert(std::fabs(clamped.shimmer - 1.0f) < 1e-6f);
    assert(std::fabs(clamped.delay) < 1e-6f);
    assert(std::fabs(clamped.drive - 1.0f) < 1e-6f);
    assert(std::fabs(clamped.feedback - 0.96f) < 1e-6f);
    assert(std::fabs(clamped.mix) < 1e-6f);
    assert(std::fabs(clamped.output - 12.0f) < 1e-6f);
}

void testSerializationRoundTrip()
{
    Parameters parameters;
    parameters.chain = 2.0f;
    parameters.time = 0.61f;
    parameters.spectral = 0.73f;
    parameters.tape = 0.11f;
    parameters.shimmer = 0.82f;
    parameters.delay = 0.37f;
    parameters.drive = 0.21f;
    parameters.feedback = 0.44f;
    parameters.mix = 0.67f;
    parameters.output = -3.5f;
    parameters.bypass = 1.0f;

    const std::string text = serializeParameters(parameters);
    const auto decoded = deserializeParameters(text);
    assert(decoded.has_value());
    assert(std::fabs(decoded->chain - parameters.chain) < 1e-6f);
    assert(std::fabs(decoded->time - parameters.time) < 1e-6f);
    assert(std::fabs(decoded->spectral - parameters.spectral) < 1e-6f);
    assert(std::fabs(decoded->tape - parameters.tape) < 1e-6f);
    assert(std::fabs(decoded->shimmer - parameters.shimmer) < 1e-6f);
    assert(std::fabs(decoded->delay - parameters.delay) < 1e-6f);
    assert(std::fabs(decoded->drive - parameters.drive) < 1e-6f);
    assert(std::fabs(decoded->feedback - parameters.feedback) < 1e-6f);
    assert(std::fabs(decoded->mix - parameters.mix) < 1e-6f);
    assert(std::fabs(decoded->output - parameters.output) < 1e-6f);
    assert(std::fabs(decoded->bypass - parameters.bypass) < 1e-6f);
    assert(!deserializeParameters("time=0.5\nunknown=1\n").has_value());
}

void testDryMixPassesInput()
{
    EngineState state;
    activate(state, 48000.0);

    Parameters parameters;
    parameters.time = 1.0f;
    parameters.spectral = 1.0f;
    parameters.tape = 1.0f;
    parameters.shimmer = 1.0f;
    parameters.delay = 1.0f;
    parameters.drive = 1.0f;
    parameters.feedback = 0.9f;
    parameters.mix = 0.0f;

    std::array<float, 16> inL {};
    std::array<float, 16> inR {};
    std::array<float, 16> outL {};
    std::array<float, 16> outR {};
    for (std::size_t i = 0; i < inL.size(); ++i) {
        inL[i] = static_cast<float>(i) * 0.03f - 0.2f;
        inR[i] = 0.3f - static_cast<float>(i) * 0.02f;
    }

    AudioBlock audio;
    audio.inputs[0] = inL.data();
    audio.inputs[1] = inR.data();
    audio.outputs[0] = outL.data();
    audio.outputs[1] = outR.data();
    audio.channelCount = 2;

    const OutputStatus status = processBlock(state, parameters, static_cast<std::uint32_t>(inL.size()), 48000.0, audio);
    assert(status.wetEnergy >= 0.0f);
    assert(status.wetEnergy == 0.0f);

    for (std::size_t i = 0; i < inL.size(); ++i) {
        assert(std::fabs(outL[i] - inL[i]) < 1e-6f);
        assert(std::fabs(outR[i] - inR[i]) < 1e-6f);
    }
}

void testActivityStatusesHaveDistinctSemantics()
{
    EngineState state;
    activate(state, 1000.0);

    Parameters parameters;
    parameters.time = 0.0f;
    parameters.spectral = 0.0f;
    parameters.tape = 0.0f;
    parameters.shimmer = 0.0f;
    parameters.delay = 0.0f;
    parameters.drive = 0.0f;
    parameters.feedback = 0.0f;
    parameters.mix = 1.0f;

    std::array<float, 64> input {};
    std::array<float, 64> outputL {};
    std::array<float, 64> outputR {};
    input[0] = 1.0f;
    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.inputs[1] = input.data();
    audio.outputs[0] = outputL.data();
    audio.outputs[1] = outputR.data();
    audio.channelCount = 2;

    const OutputStatus noReturn = processBlock(state, parameters, 64, 1000.0, audio);
    assert(noReturn.wetEnergy > 0.0f);
    assert(noReturn.feedbackEnergy == 0.0f);

    parameters.feedback = 0.8f;
    input.fill(0.25f);
    const OutputStatus withReturn = processBlock(state, parameters, 64, 1000.0, audio);
    assert(withReturn.feedbackEnergy > 0.0f);
    assert(std::fabs(withReturn.wetEnergy - withReturn.feedbackEnergy) > 1e-4f);
}

void testChainChangeUsesDryTransition()
{
    EngineState state;
    activate(state, 1000.0);

    Parameters parameters;
    parameters.mix = 1.0f;
    parameters.feedback = 0.0f;
    std::array<float, 32> input {};
    std::array<float, 32> outputL {};
    std::array<float, 32> outputR {};
    input.fill(0.2f);
    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.inputs[1] = input.data();
    audio.outputs[0] = outputL.data();
    audio.outputs[1] = outputR.data();
    audio.channelCount = 2;

    (void)processBlock(state, parameters, 32, 1000.0, audio);
    parameters.chain = 3.0f;
    (void)processBlock(state, parameters, 32, 1000.0, audio);

    assert(state.activeChain == 3);
    assert(state.chainTransition == 1.0f);
    for (std::size_t i = 1; i < outputL.size(); ++i)
        assert(std::fabs(outputL[i] - outputL[i - 1]) < 0.5f);
}

void testRapidParameterChangesAreSmoothedAndBounded()
{
    EngineState state;
    activate(state, 48000.0);
    Parameters parameters;
    parameters.mix = 1.0f;

    std::array<float, 64> input {};
    std::array<float, 64> outputL {};
    std::array<float, 64> outputR {};
    input.fill(0.2f);
    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.inputs[1] = input.data();
    audio.outputs[0] = outputL.data();
    audio.outputs[1] = outputR.data();
    audio.channelCount = 2;

    (void)processBlock(state, parameters, 64, 48000.0, audio);
    for (int change = 0; change < 20; ++change) {
        const float value = (change % 2) == 0 ? 1.0f : 0.0f;
        parameters.time = value;
        parameters.shimmer = value;
        parameters.tape = value;
        parameters.drive = value;
        (void)processBlock(state, parameters, 64, 48000.0, audio);
        for (std::size_t i = 0; i < outputL.size(); ++i) {
            assert(std::isfinite(outputL[i]));
            assert(std::isfinite(outputR[i]));
            assert(std::fabs(outputL[i]) <= 1.5f);
            assert(std::fabs(outputR[i]) <= 1.5f);
        }
    }
}

void testBypassSettlesToDrySignal()
{
    EngineState state;
    activate(state, 1000.0);
    Parameters parameters;
    parameters.mix = 1.0f;
    parameters.output = 12.0f;

    std::array<float, 128> input {};
    std::array<float, 128> outputL {};
    std::array<float, 128> outputR {};
    input.fill(0.23f);
    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.inputs[1] = input.data();
    audio.outputs[0] = outputL.data();
    audio.outputs[1] = outputR.data();
    audio.channelCount = 2;

    (void)processBlock(state, parameters, 128, 1000.0, audio);
    parameters.bypass = 1.0f;
    (void)processBlock(state, parameters, 128, 1000.0, audio);
    assert(std::fabs(outputL.back() - input.back()) < 1e-4f);
    assert(std::fabs(outputR.back() - input.back()) < 1e-4f);
}

void testDelayAndFeedbackProduceTail()
{
    EngineState state;
    activate(state, 1000.0);

    Parameters parameters;
    parameters.time = 0.0f;
    parameters.spectral = 0.0f;
    parameters.tape = 0.0f;
    parameters.shimmer = 0.0f;
    parameters.delay = 0.12f;
    parameters.drive = 0.0f;
    parameters.feedback = 0.65f;
    parameters.mix = 1.0f;

    std::array<float, 520> inL {};
    std::array<float, 520> inR {};
    std::array<float, 520> outL {};
    std::array<float, 520> outR {};
    inL[0] = 1.0f;
    inR[0] = -1.0f;

    AudioBlock audio;
    audio.inputs[0] = inL.data();
    audio.inputs[1] = inR.data();
    audio.outputs[0] = outL.data();
    audio.outputs[1] = outR.data();
    audio.channelCount = 2;

    const OutputStatus status = processBlock(state, parameters, static_cast<std::uint32_t>(inL.size()), 1000.0, audio);

    bool foundTail = false;
    for (std::size_t i = 170; i < outL.size(); ++i) {
        assert(std::isfinite(outL[i]));
        assert(std::isfinite(outR[i]));
        assert(std::fabs(outL[i]) <= 1.5f);
        assert(std::fabs(outR[i]) <= 1.5f);
        if (std::fabs(outL[i]) > 0.0001f || std::fabs(outR[i]) > 0.0001f)
            foundTail = true;
    }

    assert(foundTail);
    assert(status.feedbackEnergy > 0.0f);
}

void testAllChainsStayBounded()
{
    for (int chain = 0; chain < static_cast<int>(kChainCount); ++chain) {
        EngineState state;
        activate(state, 2000.0);

        Parameters parameters;
        parameters.chain = static_cast<float>(chain);
        parameters.time = 0.7f;
        parameters.spectral = 0.8f;
        parameters.tape = 0.6f;
        parameters.shimmer = 0.7f;
        parameters.delay = 0.5f;
        parameters.drive = 0.4f;
        parameters.feedback = 0.5f;
        parameters.mix = 1.0f;

        std::array<float, 256> inL {};
        std::array<float, 256> inR {};
        std::array<float, 256> outL {};
        std::array<float, 256> outR {};
        for (std::size_t i = 0; i < inL.size(); ++i) {
            inL[i] = std::sin(static_cast<float>(i) * 0.04f) * 0.35f;
            inR[i] = std::cos(static_cast<float>(i) * 0.031f) * 0.35f;
        }

        AudioBlock audio;
        audio.inputs[0] = inL.data();
        audio.inputs[1] = inR.data();
        audio.outputs[0] = outL.data();
        audio.outputs[1] = outR.data();
        audio.channelCount = 2;

        const OutputStatus status = processBlock(state, parameters, static_cast<std::uint32_t>(inL.size()), 2000.0, audio);
        assert(status.wetEnergy >= 0.0f);
        for (std::size_t i = 0; i < outL.size(); ++i) {
            assert(std::isfinite(outL[i]));
            assert(std::isfinite(outR[i]));
            assert(std::fabs(outL[i]) <= 1.5f);
            assert(std::fabs(outR[i]) <= 1.5f);
        }
    }
}

void testThirtySecondFeedbackTailRejectsDcAndPreservesAc()
{
    constexpr double sampleRate = 48000.0;
    constexpr std::uint32_t blockFrames = 1024;
    constexpr std::uint64_t totalFrames = static_cast<std::uint64_t>(sampleRate * 30.0);
    constexpr std::uint64_t analysisStart = static_cast<std::uint64_t>(sampleRate * 25.0);

    EngineState state;
    activate(state, sampleRate);

    Parameters parameters;
    parameters.chain = 3.0f;
    parameters.time = 0.72f;
    parameters.spectral = 0.74f;
    parameters.tape = 0.64f;
    parameters.shimmer = 0.88f;
    parameters.delay = 0.82f;
    parameters.drive = 0.42f;
    parameters.feedback = 0.90f;
    parameters.mix = 1.0f;

    std::array<float, blockFrames> inputL {};
    std::array<float, blockFrames> inputR {};
    std::array<float, blockFrames> outputL {};
    std::array<float, blockFrames> outputR {};
    AudioBlock audio;
    audio.inputs[0] = inputL.data();
    audio.inputs[1] = inputR.data();
    audio.outputs[0] = outputL.data();
    audio.outputs[1] = outputR.data();
    audio.channelCount = 2;

    double sum = 0.0;
    double sumSquares = 0.0;
    std::uint64_t sampleCount = 0;
    for (std::uint64_t blockStart = 0; blockStart < totalFrames; blockStart += blockFrames) {
        inputL.fill(0.0f);
        inputR.fill(0.0f);
        if (blockStart == 0) {
            inputL[0] = 0.8f;
            inputR[0] = -0.6f;
        }
        const auto frames = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(blockFrames, totalFrames - blockStart));
        (void)processBlock(state, parameters, frames, sampleRate, audio);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            assert(std::isfinite(outputL[frame]));
            assert(std::isfinite(outputR[frame]));
            assert(std::isfinite(state.feedback[0]));
            assert(std::isfinite(state.feedback[1]));
            assert(std::fabs(state.feedback[0]) <= 4.0f);
            assert(std::fabs(state.feedback[1]) <= 4.0f);
            if (blockStart + frame >= analysisStart) {
                const double mono = 0.5 * (outputL[frame] + outputR[frame]);
                sum += mono;
                sumSquares += mono * mono;
                ++sampleCount;
            }
        }
    }

    const double mean = sum / static_cast<double>(sampleCount);
    const double acRms = std::sqrt(std::max(
        0.0, sumSquares / static_cast<double>(sampleCount) - mean * mean));
    assert(std::fabs(mean) < 0.002);
    assert(acRms > 1.0e-5);
}

}  // namespace

int main()
{
    testClampParameters();
    testSerializationRoundTrip();
    testDryMixPassesInput();
    testActivityStatusesHaveDistinctSemantics();
    testDelayAndFeedbackProduceTail();
    testAllChainsStayBounded();
    testChainChangeUsesDryTransition();
    testRapidParameterChangesAreSmoothedAndBounded();
    testBypassSettlesToDrySignal();
    testThirtySecondFeedbackTailRejectsDcAndPreservesAc();
    return 0;
}
