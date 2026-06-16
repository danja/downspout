#include "orchid_engine.hpp"
#include "orchid_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace downspout::orchid;

namespace {

constexpr double kPi = 3.14159265358979323846;

TransportSnapshot playingTransport()
{
    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    return transport;
}

AudioBlock makeStereoBlock(const std::vector<float>& inL,
                           const std::vector<float>& inR,
                           std::vector<float>& outL,
                           std::vector<float>& outR)
{
    AudioBlock audio;
    audio.inputs[0] = inL.data();
    audio.inputs[1] = inR.data();
    audio.outputs[0] = outL.data();
    audio.outputs[1] = outR.data();
    audio.channelCount = 2;
    return audio;
}

void fillSine(std::vector<float>& buffer, const double sampleRate, const double frequency, const float amplitude, const double phase = 0.0)
{
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        buffer[i] = amplitude * static_cast<float>(std::sin(2.0 * kPi * frequency * t + phase));
    }
}

OutputStatus processStereo(EngineState& state,
                           const Parameters& parameters,
                           TransportSnapshot transport,
                           const std::vector<float>& inL,
                           const std::vector<float>& inR,
                           std::vector<float>& outL,
                           std::vector<float>& outR,
                           const double sampleRate)
{
    AudioBlock audio = makeStereoBlock(inL, inR, outL, outR);
    return processBlock(state,
                        parameters,
                        transport,
                        static_cast<std::uint32_t>(inL.size()),
                        sampleRate,
                        audio);
}

void testClampParameters()
{
    Parameters raw;
    raw.sensitivity = -10.0f;
    raw.periodicity = 140.0f;
    raw.stabilityMs = 1.0f;
    raw.pitchLow = 10.0f;
    raw.pitchHigh = 20.0f;
    raw.grid = 99.0f;
    raw.holdUnits = 99.0f;
    raw.releaseMs = -5.0f;
    raw.cooldownMs = -5.0f;
    raw.loopPeriods = 99.0f;
    raw.mix = 140.0f;
    raw.liveUnder = -1.0f;

    const Parameters clamped = clampParameters(raw);
    assert(std::fabs(clamped.sensitivity) < 1e-6f);
    assert(std::fabs(clamped.periodicity - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.stabilityMs - 5.0f) < 1e-6f);
    assert(std::fabs(clamped.pitchLow - 40.0f) < 1e-6f);
    assert(clamped.pitchHigh > clamped.pitchLow);
    assert(std::fabs(clamped.grid - 16.0f) < 1e-6f);
    assert(std::fabs(clamped.holdUnits - 8.0f) < 1e-6f);
    assert(std::fabs(clamped.releaseMs - 2.0f) < 1e-6f);
    assert(std::fabs(clamped.cooldownMs) < 1e-6f);
    assert(std::fabs(clamped.loopPeriods - 16.0f) < 1e-6f);
    assert(std::fabs(clamped.mix - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.liveUnder) < 1e-6f);
}

void testSerializationRoundTrip()
{
    Parameters parameters;
    parameters.sensitivity = 75.0f;
    parameters.periodicity = 81.0f;
    parameters.stabilityMs = 44.0f;
    parameters.pitchLow = 90.0f;
    parameters.pitchHigh = 760.0f;
    parameters.grid = 12.0f;
    parameters.holdUnits = 3.0f;
    parameters.releaseMs = 28.0f;
    parameters.cooldownMs = 91.0f;
    parameters.loopPeriods = 7.0f;
    parameters.mix = 67.0f;
    parameters.liveUnder = 22.0f;

    const std::string text = serializeParameters(parameters);
    const auto decoded = deserializeParameters(text);
    assert(decoded.has_value());
    assert(std::fabs(decoded->sensitivity - parameters.sensitivity) < 1e-6f);
    assert(std::fabs(decoded->periodicity - parameters.periodicity) < 1e-6f);
    assert(std::fabs(decoded->stabilityMs - parameters.stabilityMs) < 1e-6f);
    assert(std::fabs(decoded->pitchLow - parameters.pitchLow) < 1e-6f);
    assert(std::fabs(decoded->pitchHigh - parameters.pitchHigh) < 1e-6f);
    assert(std::fabs(decoded->grid - parameters.grid) < 1e-6f);
    assert(std::fabs(decoded->holdUnits - parameters.holdUnits) < 1e-6f);
    assert(std::fabs(decoded->releaseMs - parameters.releaseMs) < 1e-6f);
    assert(std::fabs(decoded->cooldownMs - parameters.cooldownMs) < 1e-6f);
    assert(std::fabs(decoded->loopPeriods - parameters.loopPeriods) < 1e-6f);
    assert(std::fabs(decoded->mix - parameters.mix) < 1e-6f);
    assert(std::fabs(decoded->liveUnder - parameters.liveUnder) < 1e-6f);
    assert(!deserializeParameters("sensitivity=1\nunknown=2\n").has_value());
}

void testStoppedTransportPassesThrough()
{
    constexpr double sampleRate = 48000.0;
    EngineState state;
    activate(state, sampleRate, 2);

    Parameters parameters;
    std::vector<float> inL(2048);
    std::vector<float> inR(2048);
    std::vector<float> outL(inL.size());
    std::vector<float> outR(inR.size());

    for (std::size_t i = 0; i < inL.size(); ++i) {
        inL[i] = std::sin(static_cast<float>(i) * 0.07f) * 0.31f;
        inR[i] = std::cos(static_cast<float>(i) * 0.05f) * 0.27f;
    }

    const OutputStatus status = processStereo(state, parameters, TransportSnapshot {}, inL, inR, outL, outR, sampleRate);
    assert(status.state == ProcessorState::Pass);
    assert(status.transportUsable < 0.5f);

    for (std::size_t i = 0; i < inL.size(); ++i) {
        assert(std::fabs(outL[i] - inL[i]) < 1e-6f);
        assert(std::fabs(outR[i] - inR[i]) < 1e-6f);
    }
}

void testSteadySineTriggersHold()
{
    constexpr double sampleRate = 48000.0;
    EngineState state;
    activate(state, sampleRate, 2);

    Parameters parameters;
    parameters.sensitivity = 95.0f;
    parameters.periodicity = 60.0f;
    parameters.stabilityMs = 30.0f;
    parameters.mix = 100.0f;
    parameters.liveUnder = 0.0f;

    std::vector<float> inL(12288);
    std::vector<float> inR(inL.size());
    std::vector<float> outL(inL.size());
    std::vector<float> outR(inR.size());
    fillSine(inL, sampleRate, 220.0, 0.45f);
    fillSine(inR, sampleRate, 220.0, 0.35f, 0.4);

    const OutputStatus status = processStereo(state, parameters, playingTransport(), inL, inR, outL, outR, sampleRate);
    assert(status.transportUsable > 0.5f);
    assert(status.detectorConfidence >= 0.60f);
    assert(std::fabs(status.detectedPitchHz - 220.0f) < 8.0f);
    assert(status.state == ProcessorState::Held || status.state == ProcessorState::Release);

    bool outputChanged = false;
    for (std::size_t i = 8000; i < outL.size(); ++i) {
        assert(std::isfinite(outL[i]));
        assert(std::isfinite(outR[i]));
        assert(std::fabs(outL[i]) <= 1.25f);
        assert(std::fabs(outR[i]) <= 1.25f);
        if (std::fabs(outL[i] - inL[i]) > 1e-3f) {
            outputChanged = true;
        }
    }
    assert(outputChanged);
}

void testPitchRangeRejectsLowSine()
{
    constexpr double sampleRate = 48000.0;
    EngineState state;
    activate(state, sampleRate, 2);

    Parameters parameters;
    parameters.sensitivity = 95.0f;
    parameters.periodicity = 70.0f;
    parameters.stabilityMs = 20.0f;
    parameters.pitchLow = 300.0f;
    parameters.pitchHigh = 900.0f;

    std::vector<float> inL(12288);
    std::vector<float> inR(inL.size());
    std::vector<float> outL(inL.size());
    std::vector<float> outR(inR.size());
    fillSine(inL, sampleRate, 110.0, 0.45f);
    fillSine(inR, sampleRate, 110.0, 0.45f);

    const OutputStatus status = processStereo(state, parameters, playingTransport(), inL, inR, outL, outR, sampleRate);
    assert(status.state != ProcessorState::Held);
    assert(status.detectedPitchHz < 1.0f || status.detectedPitchHz >= parameters.pitchLow);
}

void testUnstablePitchDoesNotCapture()
{
    constexpr double sampleRate = 48000.0;
    EngineState state;
    activate(state, sampleRate, 2);

    Parameters parameters;
    parameters.sensitivity = 95.0f;
    parameters.periodicity = 72.0f;
    parameters.stabilityMs = 180.0f;

    std::vector<float> inL(12288);
    std::vector<float> inR(inL.size());
    std::vector<float> outL(inL.size());
    std::vector<float> outR(inR.size());
    double phase = 0.0;
    for (std::size_t i = 0; i < inL.size(); ++i) {
        const double frequency = ((i / 512u) % 2u) == 0u ? 190.0 : 310.0;
        phase += 2.0 * kPi * frequency / sampleRate;
        const float sample = 0.42f * static_cast<float>(std::sin(phase));
        inL[i] = sample;
        inR[i] = sample;
    }

    const OutputStatus status = processStereo(state, parameters, playingTransport(), inL, inR, outL, outR, sampleRate);
    assert(status.state != ProcessorState::Held);
}

void testStereoCapturePreservesChannelDifference()
{
    constexpr double sampleRate = 48000.0;
    EngineState state;
    activate(state, sampleRate, 2);

    Parameters parameters;
    parameters.sensitivity = 95.0f;
    parameters.periodicity = 60.0f;
    parameters.stabilityMs = 30.0f;
    parameters.mix = 100.0f;
    parameters.liveUnder = 0.0f;

    std::vector<float> inL(12288);
    std::vector<float> inR(inL.size());
    std::vector<float> outL(inL.size());
    std::vector<float> outR(inR.size());
    fillSine(inL, sampleRate, 246.0, 0.50f);
    fillSine(inR, sampleRate, 246.0, 0.18f);

    const OutputStatus status = processStereo(state, parameters, playingTransport(), inL, inR, outL, outR, sampleRate);
    assert(status.state == ProcessorState::Held || status.state == ProcessorState::Release);

    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    for (std::size_t i = 8000; i < outL.size(); ++i) {
        leftEnergy += std::fabs(outL[i]);
        rightEnergy += std::fabs(outR[i]);
    }
    assert(leftEnergy > rightEnergy * 1.8);
}

void testRewindClearsHeldState()
{
    constexpr double sampleRate = 48000.0;
    EngineState state;
    activate(state, sampleRate, 2);

    Parameters parameters;
    parameters.sensitivity = 95.0f;
    parameters.periodicity = 60.0f;
    parameters.stabilityMs = 30.0f;

    std::vector<float> inL(12288);
    std::vector<float> inR(inL.size());
    std::vector<float> outL(inL.size());
    std::vector<float> outR(inR.size());
    fillSine(inL, sampleRate, 220.0, 0.45f);
    fillSine(inR, sampleRate, 220.0, 0.45f);

    TransportSnapshot transport = playingTransport();
    transport.bar = 4.0;
    const OutputStatus held = processStereo(state, parameters, transport, inL, inR, outL, outR, sampleRate);
    assert(held.state == ProcessorState::Held || held.state == ProcessorState::Release);

    transport.bar = 1.0;
    std::vector<float> shortInL(256, 0.1f);
    std::vector<float> shortInR(256, -0.1f);
    std::vector<float> shortOutL(shortInL.size());
    std::vector<float> shortOutR(shortInR.size());
    const OutputStatus rewound = processStereo(state, parameters, transport, shortInL, shortInR, shortOutL, shortOutR, sampleRate);
    assert(rewound.state == ProcessorState::Pass || rewound.state == ProcessorState::Armed);
}

}  // namespace

int main()
{
    testClampParameters();
    testSerializationRoundTrip();
    testStoppedTransportPassesThrough();
    testSteadySineTriggersHold();
    testPitchRangeRejectsLowSine();
    testUnstablePitchDoesNotCapture();
    testStereoCapturePreservesChannelDifference();
    testRewindClearsHeldState();
    return 0;
}
