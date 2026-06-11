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

    for (std::size_t i = 0; i < inL.size(); ++i) {
        assert(std::fabs(outL[i] - inL[i]) < 1e-6f);
        assert(std::fabs(outR[i] - inR[i]) < 1e-6f);
    }
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

}  // namespace

int main()
{
    testClampParameters();
    testSerializationRoundTrip();
    testDryMixPassesInput();
    testDelayAndFeedbackProduceTail();
    testAllChainsStayBounded();
    return 0;
}
