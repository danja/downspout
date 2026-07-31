#include "loopdelay_core.hpp"
#include "loopdelay_serialization.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::loopdelay;

namespace {

constexpr float kTolerance = 1.0e-4f;

Transport transportAt(const double bpm = 120.0,
                      const double beatsPerBar = 4.0,
                      const double beatType = 4.0)
{
    Transport transport;
    transport.valid = true;
    transport.playing = true;
    transport.bpm = bpm;
    transport.beatsPerBar = beatsPerBar;
    transport.beatType = beatType;
    return transport;
}

void testDefaultsClampingAndSerialization()
{
    Parameters parameters;
    parameters.values[kMode] = 9.0f;
    parameters.values[kFreeTimeMs] = -50.0f;
    parameters.values[kFeedback] = 4.0f;
    parameters.values[kOutputDb] = -90.0f;
    const Parameters clamped = clampParameters(parameters);
    assert(clamped.values[kMode] == 1.0f);
    assert(clamped.values[kFreeTimeMs] == 20.0f);
    assert(clamped.values[kFeedback] == 1.0f);
    assert(clamped.values[kOutputDb] == -12.0f);
    const auto decoded = deserializeParameters(serializeParameters(clamped));
    assert(decoded.has_value());
    assert(decoded->values[kMode] == 1.0f);
    assert(decoded->values[kFeedback] == 1.0f);
    assert(!deserializeParameters("unknown=1\n").has_value());
}

void testBbtSynchronization()
{
    Parameters parameters;
    State state;
    parameters.values[kTimeMode] = 1.0f;
    parameters.values[kSyncLength] = 2.0f;
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt()) - 500.0f) < kTolerance);
    parameters.values[kSyncLength] = 4.0f;
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt()) - 2000.0f) < kTolerance);
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt(120.0, 6.0, 8.0)) - 1500.0f) < kTolerance);
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt(60.0, 3.0, 4.0)) - 3000.0f) < kTolerance);

    Transport stopped = transportAt(100.0);
    stopped.playing = false;
    assert(effectiveDelayMilliseconds(state, parameters, stopped) > 0.0f);
    Transport invalid;
    assert(effectiveDelayMilliseconds(state, parameters, invalid) > 0.0f);

    parameters.values[kSyncLength] = 2.0f;
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt(60.0)) - 1000.0f) < kTolerance);
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt(180.0)) - 1000.0f / 3.0f) < 0.01f);

    const std::uint8_t shortest[] {0xb0, kTimeController, 0};
    const std::uint8_t longest[] {0xb0, kTimeController, 127};
    assert(handleMidi(state, shortest, 3, true));
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt()) - 125.0f) < kTolerance);
    assert(handleMidi(state, longest, 3, true));
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt()) - 8000.0f) < kTolerance);
}

void testFreeDelayProducesARepeat()
{
    State state;
    prepare(state, 1000.0);
    Parameters parameters;
    parameters.values[kTimeMode] = 0.0f;
    parameters.values[kFreeTimeMs] = 20.0f;
    parameters.values[kFeedback] = 0.0f;
    parameters.values[kMix] = 1.0f;
    parameters.values[kTone] = 1.0f;
    std::array<float, 64> leftIn {};
    std::array<float, 64> rightIn {};
    std::array<float, 64> leftOut {};
    std::array<float, 64> rightOut {};
    leftIn[0] = 0.5f;
    rightIn[0] = -0.5f;
    const AudioBlock audio {leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data()};
    const OutputStatus status = process(state, parameters, transportAt(), 64, audio);
    assert(std::fabs(leftOut[20]) > 0.3f);
    assert(std::fabs(rightOut[20]) > 0.3f);
    assert(std::fabs(leftOut[0]) < kTolerance);
    assert(std::fabs(status.delayMs - 20.0f) < kTolerance);
}

void testMidiTimeAndFeedbackContract()
{
    State state;
    prepare(state, 1000.0);
    Parameters parameters;
    parameters.values[kTimeMode] = 0.0f;
    const std::uint8_t time[] {0xbf, kTimeController, 127};
    const std::uint8_t feedback[] {0xb4, kFeedbackController, 64};
    const std::uint8_t unrelated[] {0xb0, 29, 1};
    assert(!handleMidi(state, unrelated, 3, true));
    assert(handleMidi(state, time, 3, true));
    assert(handleMidi(state, feedback, 3, true));
    assert(std::fabs(effectiveDelayMilliseconds(state, parameters, transportAt()) - 4000.0f) < 0.1f);
    assert(std::fabs(effectiveFeedback(state, parameters) - 64.0f / 127.0f) < kTolerance);
    releaseMidiTakeover(state);
    assert(!state.timeMidiActive && !state.feedbackMidiActive);
    assert(!handleMidi(state, time, 3, false));
}

void testMidiEventIsAppliedAtItsFrame()
{
    State state;
    prepare(state, 1000.0);
    Parameters parameters;
    parameters.values[kTimeMode] = 0.0f;
    parameters.values[kFreeTimeMs] = 100.0f;
    std::array<float, 8> silence {};
    std::array<float, 8> leftOutput {};
    std::array<float, 8> rightOutput {};
    MidiControlEvent event;
    event.frame = 4;
    event.data = {0xb0, kFeedbackController, 127};
    const AudioBlock audio {silence.data(), silence.data(), leftOutput.data(), rightOutput.data()};
    const OutputStatus status = process(state, parameters, transportAt(), 8, audio, &event, 1);
    assert(status.feedbackMidi);
    assert(status.feedback == 1.0f);
}

void testTransportChangesAndRewindRemainStable()
{
    State state;
    prepare(state, 1000.0);
    Parameters parameters;
    parameters.values[kMode] = 1.0f;
    parameters.values[kTimeMode] = 1.0f;
    parameters.values[kSyncLength] = 0.0f;
    parameters.values[kMix] = 1.0f;
    std::array<float, 128> silence {};
    std::array<float, 128> left {};
    std::array<float, 128> right {};
    const AudioBlock audio {silence.data(), silence.data(), left.data(), right.data()};

    Transport moving = transportAt(120.0, 4.0, 4.0);
    moving.bar = 8.0;
    moving.barBeat = 3.75;
    OutputStatus status = process(state, parameters, moving, 128, audio);
    assert(std::fabs(status.delayMs - 125.0f) < kTolerance);

    Transport loopBoundary = transportAt(90.0, 7.0, 8.0);
    loopBoundary.bar = 2.0;
    loopBoundary.barBeat = 0.0;
    status = process(state, parameters, loopBoundary, 128, audio);
    assert(std::fabs(status.delayMs - 1000.0f / 6.0f) < 0.01f);

    Transport rewind = loopBoundary;
    rewind.bar = 0.0;
    rewind.barBeat = 0.0;
    status = process(state, parameters, rewind, 128, audio);
    assert(std::isfinite(status.outputPeak));
    assert(state.loopLength > 0);
}

void testDryPathIsTransparent()
{
    State state;
    prepare(state, 48000.0);
    Parameters parameters;
    parameters.values[kMix] = 0.0f;
    std::array<float, 4> input {0.5f, -0.5f, 0.25f, -0.25f};
    std::array<float, 4> left {};
    std::array<float, 4> right {};
    const AudioBlock audio {input.data(), input.data(), left.data(), right.data()};
    (void)process(state, parameters, transportAt(), input.size(), audio);
    for (std::size_t index = 0; index < input.size(); ++index) {
        assert(left[index] == input[index]);
        assert(right[index] == input[index]);
    }
}

void testLoopCapturesAndPlays()
{
    State state;
    prepare(state, 1000.0);
    Parameters parameters;
    parameters.values[kMode] = 1.0f;
    parameters.values[kTimeMode] = 0.0f;
    parameters.values[kFreeTimeMs] = 20.0f;
    parameters.values[kFeedback] = 1.0f;
    parameters.values[kMix] = 1.0f;
    parameters.values[kTone] = 1.0f;
    parameters.values[kOverdub] = 0.0f;
    std::array<float, 45> input {};
    std::array<float, 45> output {};
    input[0] = 0.4f;
    const AudioBlock audio {input.data(), input.data(), output.data(), output.data()};
    const OutputStatus status = process(state, parameters, transportAt(), 45, audio);
    assert(!state.loopCapturing);
    assert(status.state == 2);
    assert(std::fabs(output[20]) > 0.2f);
    requestClear(state);
    (void)process(state, parameters, transportAt(), 1, audio);
    assert(state.loopCapturing);
}

void testDenseFeedbackRemainsFiniteAndBounded()
{
    State state;
    prepare(state, 48000.0);
    Parameters parameters;
    parameters.values[kTimeMode] = 0.0f;
    parameters.values[kFreeTimeMs] = 20.0f;
    parameters.values[kFeedback] = 1.0f;
    parameters.values[kMix] = 1.0f;
    std::array<float, 4096> input {};
    std::array<float, 4096> outputLeft {};
    std::array<float, 4096> outputRight {};
    input[0] = 4.0f;
    const AudioBlock audio {input.data(), input.data(), outputLeft.data(), outputRight.data()};
    (void)process(state, parameters, transportAt(), input.size(), audio);
    for (const float sample : outputLeft) {
        assert(std::isfinite(sample));
        assert(std::fabs(sample) <= 1.0f);
    }
}

} // namespace

int main()
{
    testDefaultsClampingAndSerialization();
    testBbtSynchronization();
    testFreeDelayProducesARepeat();
    testMidiTimeAndFeedbackContract();
    testMidiEventIsAppliedAtItsFrame();
    testTransportChangesAndRewindRemainStable();
    testDryPathIsTransparent();
    testLoopCapturesAndPlays();
    testDenseFeedbackRemainsFiniteAndBounded();
    return 0;
}
