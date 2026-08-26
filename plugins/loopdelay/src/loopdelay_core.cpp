#include "loopdelay_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::loopdelay {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float clampValue(const float value, const std::uint32_t index) noexcept
{
    return downspout::generative::clampParam(value, kParameterSpecs[index]);
}

float readInterpolated(const std::vector<float>& buffer,
                       const std::uint32_t writeHead,
                       const float delayFrames) noexcept
{
    if (buffer.empty())
        return 0.0f;
    float read = static_cast<float>(writeHead) - delayFrames;
    const float size = static_cast<float>(buffer.size());
    while (read < 0.0f)
        read += size;
    while (read >= size)
        read -= size;
    const std::uint32_t first = static_cast<std::uint32_t>(std::floor(read));
    const std::uint32_t second = (first + 1u) % static_cast<std::uint32_t>(buffer.size());
    const float fraction = read - static_cast<float>(first);
    return buffer[first] + (buffer[second] - buffer[first]) * fraction;
}

float sanitize(const float value) noexcept
{
    return std::tanh(std::clamp(std::isfinite(value) ? value : 0.0f, -4.0f, 4.0f));
}

float transparentOutput(const float value) noexcept
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, -1.0f, 1.0f);
}

float dcBlock(const float input, float& previousInput, float& previousOutput) noexcept
{
    const float output = input - previousInput + 0.995f * previousOutput;
    previousInput = input;
    previousOutput = std::isfinite(output) ? output : 0.0f;
    return previousOutput;
}

float outputGain(const float decibels) noexcept
{
    return std::pow(10.0f, decibels / 20.0f);
}

double beatQuarters(const Transport& transport) noexcept
{
    return transport.beatType > 0.0 ? 4.0 / transport.beatType : 1.0;
}

double syncQuarters(const int choice, const Transport& transport) noexcept
{
    const double beat = beatQuarters(transport);
    const double bar = beat * std::max(1.0, transport.beatsPerBar);
    switch (std::clamp(choice, 0, kSyncChoiceCount - 1)) {
    case 0: return 0.25;       // 1 sixteenth note — fixed regardless of beat type
    case 1: return beat * 0.5;
    case 2: return beat;
    case 3: return beat * 2.0;
    case 4: return bar;
    case 5: return bar * 2.0;
    default: return bar * 4.0;
    }
}

void beginLoopCapture(State& state, const std::uint32_t length) noexcept
{
    state.loopLength = std::clamp(length, 1u, std::max(1u, state.bufferFrames - 1u));
    state.configuredLoopLength = state.loopLength;
    state.loopPosition = 0;
    state.loopCapturing = true;
    state.loopCaptureRequested = false;
}

// Trim loopLength so the loop ends at the nearest zero-crossing within
// windowFrames of the original end.  Uses the left channel as the reference.
// Returns the original length if no crossing is found in the window.
std::uint32_t snapLoopEnd(const std::vector<float>& buffer,
                           const std::uint32_t loopLength,
                           const std::uint32_t windowFrames) noexcept
{
    if (loopLength < 4u || buffer.size() < loopLength)
        return loopLength;
    const std::uint32_t hi = loopLength - 1u;
    const std::uint32_t lo = windowFrames < hi ? hi - windowFrames : 1u;

    std::uint32_t best = loopLength;
    std::uint32_t bestDist = windowFrames + 1u;

    for (std::uint32_t f = lo; f < hi; ++f) {
        const bool crossing = (buffer[f] <= 0.0f && buffer[f + 1u] > 0.0f)
                           || (buffer[f] >= 0.0f && buffer[f + 1u] < 0.0f);
        if (crossing) {
            const std::uint32_t dist = hi - f;
            if (dist < bestDist) {
                bestDist = dist;
                best = f + 1u;
            }
        }
    }
    return best;
}

} // namespace

Parameters::Parameters()
{
    for (std::size_t index = 0; index < values.size(); ++index)
        values[index] = kParameterSpecs[index].defaultValue;
}

Parameters clampParameters(const Parameters& raw) noexcept
{
    Parameters result = raw;
    for (std::uint32_t index = 0; index < kParameterCount; ++index)
        result.values[index] = clampValue(raw.values[index], index);
    return result;
}

void prepare(State& state, double sampleRate)
{
    sampleRate = std::clamp(sampleRate, 1000.0, 384000.0);
    const std::uint32_t frames = std::max<std::uint32_t>(2u,
        static_cast<std::uint32_t>(std::ceil(sampleRate * kMaximumDelaySeconds)) + 2u);
    if (state.bufferFrames == frames && std::fabs(state.sampleRate - sampleRate) < 0.001)
        return;
    state.sampleRate = sampleRate;
    state.bufferFrames = frames;
    state.leftBuffer.assign(frames, 0.0f);
    state.rightBuffer.assign(frames, 0.0f);
    reset(state);
}

void reset(State& state) noexcept
{
    std::fill(state.leftBuffer.begin(), state.leftBuffer.end(), 0.0f);
    std::fill(state.rightBuffer.begin(), state.rightBuffer.end(), 0.0f);
    state.writeHead = 0;
    state.smoothedDelayFrames = 1.0f;
    state.smoothedFeedback = 0.0f;
    state.smoothingInitialized = false;
    state.lowpassLeft = state.lowpassRight = 0.0f;
    state.dcInputLeft = state.dcInputRight = 0.0f;
    state.dcOutputLeft = state.dcOutputRight = 0.0f;
    state.previousMode = -1;
    state.loopCaptureRequested = false;
    state.loopCapturing = false;
    state.loopLength = 1;
    state.loopPosition = 0;
    state.configuredLoopLength = 0;
    state.timeMidiActive = false;
    state.feedbackMidiActive = false;
    state.timeMidiValue = 0.0f;
    state.feedbackMidiValue = 0.0f;
    state.producerActive = false;
}

void requestClear(State& state) noexcept
{
    state.loopCaptureRequested = true;
}

void releaseMidiTakeover(State& state) noexcept
{
    state.timeMidiActive = false;
    state.feedbackMidiActive = false;
    state.producerActive = false;
    state.loopCaptureRequested = true;
}

bool handleMidi(State& state, const std::uint8_t* data, const std::uint32_t size,
                const bool midiEnabled, const int controlChannel,
                const bool requireProducerGate) noexcept
{
    if (!midiEnabled || data == nullptr || size < 3 || (data[0] & 0xf0u) != 0xb0u)
        return false;
    const std::uint8_t controller = data[1] & 0x7fu;
    const int messageChannel = (data[0] & 0x0fu) + 1;
    if (controlChannel != 0 && std::clamp(controlChannel, 1, 16) != messageChannel)
        return false;
    if (controller == kProducerLifecycleController) {
        state.producerActive = (data[2] & 0x7fu) >= 64;
        if (!state.producerActive)
            releaseMidiTakeover(state);
        return true;
    }
    if (requireProducerGate && !state.producerActive)
        return false;
    const float value = static_cast<float>(data[2] & 0x7fu) / 127.0f;
    if (controller == kTimeController) {
        state.timeMidiValue = value;
        state.timeMidiActive = true;
        state.loopCaptureRequested = true;
        state.producerActive = true;
        return true;
    }
    if (controller == kFeedbackController) {
        state.feedbackMidiValue = value;
        state.feedbackMidiActive = true;
        state.producerActive = true;
        return true;
    }
    return false;
}

float effectiveDelayMilliseconds(const State& state,
                                 const Parameters& rawParameters,
                                 const Transport& transport) noexcept
{
    const Parameters parameters = clampParameters(rawParameters);
    const bool sync = parameters.values[kTimeMode] >= 0.5f;
    if (!sync) {
        if (!state.timeMidiActive)
            return parameters.values[kFreeTimeMs];
        constexpr float minimum = 20.0f;
        constexpr float maximum = 4000.0f;
        return minimum * std::pow(maximum / minimum, state.timeMidiValue);
    }

    const int choice = state.timeMidiActive
        ? static_cast<int>(std::lround(state.timeMidiValue * (kSyncChoiceCount - 1)))
        : static_cast<int>(std::lround(parameters.values[kSyncLength]));
    const double bpm = transport.valid && transport.bpm > 0.0 ? transport.bpm : 120.0;
    const double seconds = syncQuarters(choice, transport) * 60.0 / bpm;
    return std::clamp(static_cast<float>(seconds * 1000.0), 1.0f,
                      kMaximumDelaySeconds * 1000.0f);
}

float effectiveFeedback(const State& state, const Parameters& rawParameters) noexcept
{
    const Parameters parameters = clampParameters(rawParameters);
    return state.feedbackMidiActive ? state.feedbackMidiValue : parameters.values[kFeedback];
}

OutputStatus process(State& state,
                     const Parameters& rawParameters,
                     const Transport& transport,
                     const std::uint32_t frames,
                     const AudioBlock& audio,
                     const MidiControlEvent* midiEvents,
                     const std::uint32_t midiEventCount) noexcept
{
    OutputStatus status;
    if (frames == 0 || state.bufferFrames < 2)
        return status;
    const Parameters parameters = clampParameters(rawParameters);
    const int mode = static_cast<int>(std::lround(parameters.values[kMode]));
    if (state.previousMode != mode) {
        state.previousMode = mode;
        if (mode == 1)
            state.loopCaptureRequested = true;
        else
            state.loopCapturing = false;
    }

    const float mix = parameters.values[kMix];
    const float tone = parameters.values[kTone];
    const float pingPong = parameters.values[kPingPong];
    const float overdub = parameters.values[kOverdub];
    const float trim = outputGain(parameters.values[kOutputDb]);
    const bool midiEnabled = parameters.values[kMidiEnabled] >= 0.5f;
    const int controlChannel = static_cast<int>(std::lround(parameters.values[kControlChannel]));
    const bool requireProducerGate = parameters.values[kRequireProducerGate] >= 0.5f;
    const float delaySmoothing = 1.0f - std::exp(-1.0f / static_cast<float>(state.sampleRate * 0.040));
    const float feedbackSmoothing = 1.0f - std::exp(-1.0f / static_cast<float>(state.sampleRate * 0.020));
    const float cutoff = 450.0f + tone * tone * 17500.0f;
    const float safeCutoff = std::min(cutoff, static_cast<float>(state.sampleRate * 0.45));
    const float filterCoefficient = 1.0f - std::exp(-2.0f * kPi * safeCutoff / static_cast<float>(state.sampleRate));
    std::uint32_t eventIndex = 0;

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (midiEvents != nullptr && eventIndex < midiEventCount
               && midiEvents[eventIndex].frame <= frame) {
            (void)handleMidi(state, midiEvents[eventIndex].data.data(),
                             midiEvents[eventIndex].size, midiEnabled,
                             controlChannel, requireProducerGate);
            ++eventIndex;
        }

        const float delayMs = effectiveDelayMilliseconds(state, parameters, transport);
        const float targetDelayFrames = std::clamp(delayMs * 0.001f * static_cast<float>(state.sampleRate),
                                                   1.0f, static_cast<float>(state.bufferFrames - 2u));
        const float targetFeedback = effectiveFeedback(state, parameters);
        if (!state.smoothingInitialized) {
            state.smoothedDelayFrames = targetDelayFrames;
            state.smoothedFeedback = targetFeedback;
            state.smoothingInitialized = true;
        }
        state.smoothedDelayFrames += (targetDelayFrames - state.smoothedDelayFrames) * delaySmoothing;
        state.smoothedFeedback += (targetFeedback - state.smoothedFeedback) * feedbackSmoothing;

        const float inputLeft = audio.leftIn != nullptr ? audio.leftIn[frame] : 0.0f;
        const float inputRight = audio.rightIn != nullptr ? audio.rightIn[frame] : 0.0f;
        status.inputPeak = std::max(status.inputPeak,
            std::max(std::fabs(inputLeft), std::fabs(inputRight)));
        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        if (mode == 0) {
            wetLeft = readInterpolated(state.leftBuffer, state.writeHead, state.smoothedDelayFrames);
            wetRight = readInterpolated(state.rightBuffer, state.writeHead, state.smoothedDelayFrames);
            state.lowpassLeft += (wetLeft - state.lowpassLeft) * filterCoefficient;
            state.lowpassRight += (wetRight - state.lowpassRight) * filterCoefficient;
            wetLeft = state.lowpassLeft;
            wetRight = state.lowpassRight;
            const float returnLeft = wetLeft * (1.0f - pingPong) + wetRight * pingPong;
            const float returnRight = wetRight * (1.0f - pingPong) + wetLeft * pingPong;
            state.leftBuffer[state.writeHead] = sanitize(inputLeft + returnLeft * std::min(0.985f, state.smoothedFeedback));
            state.rightBuffer[state.writeHead] = sanitize(inputRight + returnRight * std::min(0.985f, state.smoothedFeedback));
            state.writeHead = (state.writeHead + 1u) % state.bufferFrames;
            status.state = 0;
        } else {
            const std::uint32_t desiredLength = std::clamp(
                static_cast<std::uint32_t>(std::lround(targetDelayFrames)), 1u, state.bufferFrames - 2u);
            if (state.loopCaptureRequested || state.configuredLoopLength != desiredLength)
                beginLoopCapture(state, desiredLength);
            if (state.loopCapturing) {
                state.leftBuffer[state.loopPosition] = sanitize(inputLeft);
                state.rightBuffer[state.loopPosition] = sanitize(inputRight);
                wetLeft = inputLeft;
                wetRight = inputRight;
                ++state.loopPosition;
                if (state.loopPosition >= state.loopLength) {
                    const std::uint32_t zcWindow =
                        static_cast<std::uint32_t>(state.sampleRate * 0.003);
                    state.loopLength = snapLoopEnd(state.leftBuffer, state.loopLength, zcWindow);
                    state.loopPosition = 0;
                    state.loopCapturing = false;
                }
                status.state = 1;
            } else {
                wetLeft = state.leftBuffer[state.loopPosition];
                wetRight = state.rightBuffer[state.loopPosition];
                state.lowpassLeft += (wetLeft - state.lowpassLeft) * filterCoefficient;
                state.lowpassRight += (wetRight - state.lowpassRight) * filterCoefficient;
                wetLeft = state.lowpassLeft;
                wetRight = state.lowpassRight;
                const float memory = std::clamp(state.smoothedFeedback, 0.0f, 1.0f);
                state.leftBuffer[state.loopPosition] = sanitize(wetLeft * memory + inputLeft * overdub);
                state.rightBuffer[state.loopPosition] = sanitize(wetRight * memory + inputRight * overdub);
                state.loopPosition = (state.loopPosition + 1u) % state.loopLength;
                status.state = 2;
            }
            status.loopProgress = static_cast<float>(state.loopPosition) /
                static_cast<float>(std::max(1u, state.loopLength));
        }

        wetLeft = dcBlock(wetLeft, state.dcInputLeft, state.dcOutputLeft);
        wetRight = dcBlock(wetRight, state.dcInputRight, state.dcOutputRight);
        const float outputLeft = transparentOutput((inputLeft * (1.0f - mix) + wetLeft * mix) * trim);
        const float outputRight = transparentOutput((inputRight * (1.0f - mix) + wetRight * mix) * trim);
        if (audio.leftOut != nullptr)
            audio.leftOut[frame] = outputLeft;
        if (audio.rightOut != nullptr)
            audio.rightOut[frame] = outputRight;
        status.outputPeak = std::max(status.outputPeak,
            std::max(std::fabs(outputLeft), std::fabs(outputRight)));
        status.delayMs = delayMs;
        status.feedback = targetFeedback;
        status.timeMidi = state.timeMidiActive;
        status.feedbackMidi = state.feedbackMidiActive;
        status.producerActive = state.producerActive;
    }
    return status;
}

} // namespace downspout::loopdelay
