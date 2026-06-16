#include "orchid_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::orchid {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 1.5707963267948966;

[[nodiscard]] float clampf(float value, const float minValue, const float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] int clampi(int value, const int minValue, const int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] bool transportUsable(const TransportSnapshot& transport, const double sampleRate) {
    return transport.valid &&
           transport.playing &&
           transport.bpm > 0.0 &&
           transport.beatsPerBar > 0.0 &&
           sampleRate > 0.0;
}

[[nodiscard]] double absoluteBeat(const TransportSnapshot& transport) {
    return transport.bar * transport.beatsPerBar + transport.barBeat;
}

[[nodiscard]] std::uint32_t wrapFrameIndex(const EngineState& state, const std::int64_t frame) {
    if (state.bufferFrames == 0) {
        return 0;
    }

    std::int64_t wrapped = frame % static_cast<std::int64_t>(state.bufferFrames);
    if (wrapped < 0) {
        wrapped += static_cast<std::int64_t>(state.bufferFrames);
    }
    return static_cast<std::uint32_t>(wrapped);
}

[[nodiscard]] std::size_t bufferIndex(const EngineState& state, const std::uint32_t channel, const std::uint32_t frame) {
    return static_cast<std::size_t>(frame) * static_cast<std::size_t>(state.bufferChannels) + static_cast<std::size_t>(channel);
}

void clearActiveHold(EngineState& state) {
    state.processorState = ProcessorState::Pass;
    state.loop = {};
    state.holdFramesRemaining = 0;
    state.holdFramesTotal = 0;
    state.releaseFramesRemaining = 0;
    state.releaseFramesTotal = 0;
    state.cooldownFramesRemaining = 0;
    state.loopReadPosition = 0.0;
}

void clearDetectionHistory(EngineState& state) {
    clearActiveHold(state);
    state.detector = {};
    state.previousStableFrequencyHz = 0.0f;
    state.framesUntilAnalysis = std::max<std::uint32_t>(1u, state.analysisHopFrames);
    state.writeHead = 0;
    state.filledFrames = 0;
}

void ensureBuffer(EngineState& state,
                  const Parameters& parameters,
                  const double sampleRate,
                  const std::uint32_t channelCount) {
    const std::uint32_t safeChannels = std::max<std::uint32_t>(1u, std::min(channelCount, kMaxChannels));
    const std::uint32_t windowFrames = static_cast<std::uint32_t>(
        std::clamp(std::lround(sampleRate * 0.050), 512l, 4096l));
    const std::uint32_t hopFrames = static_cast<std::uint32_t>(
        std::clamp(std::lround(sampleRate * 0.008), 64l, 1024l));
    const double lowPitch = std::max(20.0f, parameters.pitchLow);
    const std::uint32_t minHistory = static_cast<std::uint32_t>(
        std::ceil(sampleRate * std::max(0.25, 12.0 / lowPitch)));
    const std::uint32_t requiredFrames = std::max<std::uint32_t>(
        std::max(windowFrames * 4u, minHistory),
        static_cast<std::uint32_t>(std::lround(sampleRate * 2.0)));

    if (state.bufferFrames == requiredFrames &&
        state.bufferChannels == safeChannels &&
        state.analysisWindowFrames == windowFrames &&
        state.analysisHopFrames == hopFrames &&
        std::fabs(state.sampleRate - sampleRate) < 0.001) {
        return;
    }

    state.buffer.assign(static_cast<std::size_t>(requiredFrames) * safeChannels, 0.0f);
    state.analysisWindow.assign(windowFrames, 0.0f);
    state.bufferFrames = requiredFrames;
    state.bufferChannels = safeChannels;
    state.writeHead = 0;
    state.filledFrames = 0;
    state.sampleRate = sampleRate;
    state.analysisWindowFrames = windowFrames;
    state.analysisHopFrames = hopFrames;
    state.framesUntilAnalysis = hopFrames;
    state.detector = {};
    state.previousStableFrequencyHz = 0.0f;
    clearActiveHold(state);
}

void writeInputFrame(EngineState& state, const AudioBlock& audio, const std::uint32_t frame) {
    if (state.bufferFrames == 0 || state.bufferChannels == 0) {
        return;
    }

    for (std::uint32_t channel = 0; channel < state.bufferChannels; ++channel) {
        const float* input = audio.inputs[channel];
        state.buffer[bufferIndex(state, channel, state.writeHead)] = input != nullptr ? input[frame] : 0.0f;
    }

    state.writeHead = (state.writeHead + 1u) % state.bufferFrames;
    state.filledFrames = std::min(state.filledFrames + 1u, state.bufferFrames);
}

[[nodiscard]] float readBufferFrame(const EngineState& state, const std::uint32_t channel, const std::int64_t frame) {
    if (state.bufferFrames == 0 || state.bufferChannels == 0 || channel >= state.bufferChannels) {
        return 0.0f;
    }
    return state.buffer[bufferIndex(state, channel, wrapFrameIndex(state, frame))];
}

[[nodiscard]] float readBufferAt(const EngineState& state, const std::uint32_t channel, double frame) {
    const std::int64_t frame0 = static_cast<std::int64_t>(std::floor(frame));
    const double frac = frame - static_cast<double>(frame0);
    const float sample0 = readBufferFrame(state, channel, frame0);
    const float sample1 = readBufferFrame(state, channel, frame0 + 1);
    return sample0 + static_cast<float>(frac) * (sample1 - sample0);
}

[[nodiscard]] float readMonoFrame(const EngineState& state, const std::int64_t frame) {
    if (state.bufferChannels == 0) {
        return 0.0f;
    }

    float sum = 0.0f;
    for (std::uint32_t channel = 0; channel < state.bufferChannels; ++channel) {
        sum += readBufferFrame(state, channel, frame);
    }
    return sum / static_cast<float>(state.bufferChannels);
}

[[nodiscard]] float rmsThresholdFor(const Parameters& parameters) {
    const float sensitivity = clampf(parameters.sensitivity * 0.01f, 0.0f, 1.0f);
    return 0.0025f + (1.0f - sensitivity) * 0.040f;
}

DetectorStatus analyzeRecentWindow(EngineState& state, const Parameters& parameters) {
    DetectorStatus status {};
    const std::uint32_t windowFrames = state.analysisWindowFrames;
    if (windowFrames < 32u || state.filledFrames < windowFrames || state.sampleRate <= 0.0) {
        return status;
    }

    double mean = 0.0;
    const std::int64_t startFrame = static_cast<std::int64_t>(state.writeHead) - static_cast<std::int64_t>(windowFrames);
    for (std::uint32_t i = 0; i < windowFrames; ++i) {
        const float sample = readMonoFrame(state, startFrame + static_cast<std::int64_t>(i));
        state.analysisWindow[i] = sample;
        mean += sample;
    }
    mean /= static_cast<double>(windowFrames);

    double energy = 0.0;
    for (std::uint32_t i = 0; i < windowFrames; ++i) {
        const double phase = static_cast<double>(i) / static_cast<double>(windowFrames - 1u);
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * phase);
        const float sample = static_cast<float>((static_cast<double>(state.analysisWindow[i]) - mean) * hann);
        state.analysisWindow[i] = sample;
        energy += static_cast<double>(sample) * sample;
    }

    status.rms = static_cast<float>(std::sqrt(energy / static_cast<double>(windowFrames)));
    if (status.rms < rmsThresholdFor(parameters)) {
        state.previousStableFrequencyHz = 0.0f;
        return status;
    }

    const float low = std::max(20.0f, std::min(parameters.pitchLow, parameters.pitchHigh - 1.0f));
    const float high = std::max(low + 1.0f, parameters.pitchHigh);
    const std::uint32_t minLag = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::floor(state.sampleRate / high)));
    const std::uint32_t maxLag = std::min<std::uint32_t>(
        windowFrames - 8u,
        static_cast<std::uint32_t>(std::ceil(state.sampleRate / low)));

    if (minLag >= maxLag || maxLag >= windowFrames) {
        return status;
    }

    const auto correlationAtLag = [&](const std::uint32_t lag) -> float {
        double xy = 0.0;
        double xx = 0.0;
        double yy = 0.0;
        const std::uint32_t count = windowFrames - lag;
        for (std::uint32_t i = 0; i < count; ++i) {
            const double x = state.analysisWindow[i];
            const double y = state.analysisWindow[i + lag];
            xy += x * y;
            xx += x * x;
            yy += y * y;
        }

        const double denom = std::sqrt(xx * yy);
        if (denom <= 1.0e-12) {
            return 0.0f;
        }

        return static_cast<float>(xy / denom);
    };

    float bestCorrelation = 0.0f;
    std::uint32_t bestLag = 0;
    for (std::uint32_t lag = minLag; lag <= maxLag; ++lag) {
        const float corr = correlationAtLag(lag);
        if (corr > bestCorrelation) {
            bestCorrelation = corr;
            bestLag = lag;
        }
    }

    status.confidence = clampf(bestCorrelation, 0.0f, 1.0f);
    const float requiredConfidence = clampf(parameters.periodicity * 0.01f, 0.0f, 1.0f);
    if (bestLag == 0u || status.confidence < requiredConfidence) {
        state.previousStableFrequencyHz = 0.0f;
        return status;
    }

    for (std::uint32_t multiple = 2u; multiple <= 16u; ++multiple) {
        const std::uint32_t lowerLag = bestLag * multiple;
        if (lowerLag >= windowFrames - 8u) {
            break;
        }

        const float lowerFrequency = static_cast<float>(state.sampleRate / static_cast<double>(lowerLag));
        if (lowerFrequency >= low) {
            continue;
        }

        const float lowerCorrelation = correlationAtLag(lowerLag);
        if (lowerCorrelation >= status.confidence * 0.85f) {
            state.previousStableFrequencyHz = 0.0f;
            status.confidence = std::max(status.confidence, lowerCorrelation);
            return status;
        }
    }

    status.voiced = true;
    status.frequencyHz = static_cast<float>(state.sampleRate / static_cast<double>(bestLag));

    if (state.previousStableFrequencyHz > 0.0f) {
        const float relativeChange = std::fabs(status.frequencyHz - state.previousStableFrequencyHz) /
                                     std::max(1.0f, state.previousStableFrequencyHz);
        if (relativeChange <= 0.04f) {
            status.stableFrames = state.detector.stableFrames + state.analysisHopFrames;
        } else {
            status.stableFrames = state.analysisHopFrames;
        }
    } else {
        status.stableFrames = state.analysisHopFrames;
    }

    state.previousStableFrequencyHz = status.frequencyHz;
    return status;
}

[[nodiscard]] std::uint32_t releaseFramesFor(const Parameters& parameters, const double sampleRate) {
    return std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(sampleRate * parameters.releaseMs * 0.001f)));
}

[[nodiscard]] std::uint32_t cooldownFramesFor(const Parameters& parameters, const double sampleRate) {
    return static_cast<std::uint32_t>(std::lround(sampleRate * parameters.cooldownMs * 0.001f));
}

[[nodiscard]] std::uint32_t holdFramesFor(const Parameters& parameters,
                                          const TransportSnapshot& transport,
                                          const double sampleRate) {
    const double framesPerBeat = sampleRate * 60.0 / transport.bpm;
    const double framesPerBar = framesPerBeat * transport.beatsPerBar;
    const double gridFrames = framesPerBar / std::max(1.0f, parameters.grid);
    return std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>(std::lround(gridFrames * std::max(1.0f, parameters.holdUnits))));
}

[[nodiscard]] std::uint32_t loopLengthFor(const Parameters& parameters,
                                          const DetectorStatus& detector,
                                          const double sampleRate) {
    if (detector.frequencyHz <= 0.0f || sampleRate <= 0.0) {
        return 1u;
    }

    const double periodFrames = sampleRate / detector.frequencyHz;
    const int periods = clampi(static_cast<int>(std::lround(parameters.loopPeriods)), 2, 16);
    std::uint32_t length = static_cast<std::uint32_t>(std::lround(periodFrames * static_cast<double>(periods)));
    const std::uint32_t minLength = static_cast<std::uint32_t>(std::lround(sampleRate * 0.020));
    const std::uint32_t maxLength = static_cast<std::uint32_t>(std::lround(sampleRate * 0.160));

    if (length < minLength) {
        const int adjustedPeriods = std::max(1, static_cast<int>(std::ceil(static_cast<double>(minLength) / periodFrames)));
        length = static_cast<std::uint32_t>(std::lround(periodFrames * static_cast<double>(adjustedPeriods)));
    }
    if (length > maxLength) {
        const int adjustedPeriods = std::max(1, static_cast<int>(std::floor(static_cast<double>(maxLength) / periodFrames)));
        length = static_cast<std::uint32_t>(std::lround(periodFrames * static_cast<double>(adjustedPeriods)));
    }

    return std::max<std::uint32_t>(8u, length);
}

[[nodiscard]] float loopJoinCost(const EngineState& state, const std::int64_t start, const std::uint32_t length) {
    float cost = 0.0f;
    for (std::uint32_t channel = 0; channel < state.bufferChannels; ++channel) {
        const float first = readBufferFrame(state, channel, start);
        const float last = readBufferFrame(state, channel, start + static_cast<std::int64_t>(length) - 1);
        const float after = readBufferFrame(state, channel, start + static_cast<std::int64_t>(length));
        cost += std::fabs(last - first) + 0.35f * std::fabs(after - first);
    }
    return cost;
}

void captureLoop(EngineState& state,
                 const Parameters& parameters,
                 const TransportSnapshot& transport,
                 const DetectorStatus& detector) {
    const std::uint32_t length = loopLengthFor(parameters, detector, state.sampleRate);
    if (state.filledFrames <= length + 4u) {
        return;
    }

    const std::int64_t nominalStart = static_cast<std::int64_t>(state.writeHead) - static_cast<std::int64_t>(length) - 1;
    const std::int32_t searchRadius = static_cast<std::int32_t>(
        std::min<std::uint32_t>(32u, std::max<std::uint32_t>(2u, length / 8u)));
    std::int64_t bestStart = nominalStart;
    float bestCost = loopJoinCost(state, bestStart, length);

    for (std::int32_t delta = -searchRadius; delta <= searchRadius; ++delta) {
        const std::int64_t candidate = nominalStart + delta;
        const float cost = loopJoinCost(state, candidate, length);
        if (cost < bestCost) {
            bestCost = cost;
            bestStart = candidate;
        }
    }

    state.loop.valid = true;
    state.loop.startFrame = wrapFrameIndex(state, bestStart);
    state.loop.lengthFrames = length;
    state.loop.frequencyHz = detector.frequencyHz;
    state.loop.confidence = detector.confidence;
    state.loopReadPosition = 0.0;

    state.processorState = ProcessorState::Held;
    state.holdFramesTotal = holdFramesFor(parameters, transport, state.sampleRate);
    state.holdFramesRemaining = state.holdFramesTotal;
    state.releaseFramesTotal = releaseFramesFor(parameters, state.sampleRate);
    state.releaseFramesRemaining = state.releaseFramesTotal;
    state.cooldownFramesRemaining = cooldownFramesFor(parameters, state.sampleRate);
}

[[nodiscard]] float readLoopSample(const EngineState& state, const std::uint32_t channel) {
    if (!state.loop.valid || state.loop.lengthFrames == 0u) {
        return 0.0f;
    }

    double relative = std::fmod(state.loopReadPosition, static_cast<double>(state.loop.lengthFrames));
    if (relative < 0.0) {
        relative += static_cast<double>(state.loop.lengthFrames);
    }

    const double absolute = static_cast<double>(state.loop.startFrame) + relative;
    const float primary = readBufferAt(state, channel, absolute);
    const std::uint32_t blendFrames = std::min<std::uint32_t>(96u, std::max<std::uint32_t>(1u, state.loop.lengthFrames / 4u));
    if (blendFrames <= 1u || relative < static_cast<double>(state.loop.lengthFrames - blendFrames)) {
        return primary;
    }

    const double t = (relative - static_cast<double>(state.loop.lengthFrames - blendFrames)) /
                     static_cast<double>(blendFrames);
    const float wrapped = readBufferAt(state, channel, static_cast<double>(state.loop.startFrame) +
                                                   relative - static_cast<double>(state.loop.lengthFrames - blendFrames));
    const double phase = std::clamp(t, 0.0, 1.0) * kHalfPi;
    return primary * static_cast<float>(std::cos(phase)) + wrapped * static_cast<float>(std::sin(phase));
}

void advanceLoop(EngineState& state) {
    if (state.processorState == ProcessorState::Held || state.processorState == ProcessorState::Release) {
        state.loopReadPosition += 1.0;
        if (state.loop.valid && state.loop.lengthFrames > 0u && state.loopReadPosition >= static_cast<double>(state.loop.lengthFrames)) {
            state.loopReadPosition -= static_cast<double>(state.loop.lengthFrames);
        }
    }
}

void updateDetectorAndCapture(EngineState& state,
                              const Parameters& parameters,
                              const TransportSnapshot& transport,
                              const bool usableTransport) {
    if (state.framesUntilAnalysis > 0u) {
        --state.framesUntilAnalysis;
        return;
    }
    state.framesUntilAnalysis = std::max<std::uint32_t>(1u, state.analysisHopFrames);

    state.detector = analyzeRecentWindow(state, parameters);
    if (!usableTransport) {
        return;
    }

    if (state.processorState == ProcessorState::Cooldown && state.cooldownFramesRemaining > 0u) {
        return;
    }
    if (state.processorState == ProcessorState::Held || state.processorState == ProcessorState::Release) {
        return;
    }

    if (!state.detector.voiced) {
        if (state.processorState == ProcessorState::Armed) {
            state.processorState = ProcessorState::Pass;
        }
        return;
    }

    const std::uint32_t requiredStableFrames = std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>(std::lround(state.sampleRate * parameters.stabilityMs * 0.001f)));
    if (state.detector.stableFrames >= requiredStableFrames) {
        captureLoop(state, parameters, transport, state.detector);
    } else if (state.processorState == ProcessorState::Pass) {
        state.processorState = ProcessorState::Armed;
    }
}

[[nodiscard]] float holdEnvelope(const EngineState& state) {
    if (state.processorState == ProcessorState::Held) {
        return 1.0f;
    }
    if (state.processorState == ProcessorState::Release && state.releaseFramesTotal > 0u) {
        return clampf(static_cast<float>(state.releaseFramesRemaining) /
                          static_cast<float>(state.releaseFramesTotal),
                      0.0f,
                      1.0f);
    }
    return 0.0f;
}

void advanceStateAfterFrame(EngineState& state) {
    if (state.processorState == ProcessorState::Held) {
        if (state.holdFramesRemaining > 0u) {
            --state.holdFramesRemaining;
        }
        if (state.holdFramesRemaining == 0u) {
            state.processorState = ProcessorState::Release;
            state.releaseFramesRemaining = state.releaseFramesTotal;
        }
        return;
    }

    if (state.processorState == ProcessorState::Release) {
        if (state.releaseFramesRemaining > 0u) {
            --state.releaseFramesRemaining;
        }
        if (state.releaseFramesRemaining == 0u) {
            state.processorState = ProcessorState::Cooldown;
            state.loop = {};
            state.loopReadPosition = 0.0;
        }
        return;
    }

    if (state.processorState == ProcessorState::Cooldown) {
        if (state.cooldownFramesRemaining > 0u) {
            --state.cooldownFramesRemaining;
        }
        if (state.cooldownFramesRemaining == 0u) {
            state.processorState = ProcessorState::Pass;
        }
    }
}

[[nodiscard]] OutputStatus makeStatus(const EngineState& state, const bool usableTransport) {
    OutputStatus status;
    status.state = state.processorState;
    status.detectorConfidence = state.detector.confidence;
    status.detectedPitchHz = state.detector.frequencyHz;
    status.inputRms = state.detector.rms;
    status.transportUsable = usableTransport ? 1.0f : 0.0f;
    if (state.holdFramesTotal > 0u && state.processorState == ProcessorState::Held) {
        status.holdProgress = 1.0f - clampf(static_cast<float>(state.holdFramesRemaining) /
                                                static_cast<float>(state.holdFramesTotal),
                                            0.0f,
                                            1.0f);
    } else if (state.processorState == ProcessorState::Release) {
        status.holdProgress = 1.0f;
    }
    return status;
}

}  // namespace

Parameters clampParameters(const Parameters& raw) {
    Parameters parameters = raw;
    parameters.sensitivity = clampf(parameters.sensitivity, 0.0f, 100.0f);
    parameters.periodicity = clampf(parameters.periodicity, 0.0f, 100.0f);
    parameters.stabilityMs = clampf(parameters.stabilityMs, 5.0f, 250.0f);
    parameters.pitchLow = clampf(parameters.pitchLow, 40.0f, 800.0f);
    parameters.pitchHigh = clampf(parameters.pitchHigh, 120.0f, 2000.0f);
    if (parameters.pitchHigh <= parameters.pitchLow + 10.0f) {
        parameters.pitchHigh = parameters.pitchLow + 10.0f;
    }
    parameters.grid = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.grid)), 1, 16));
    parameters.holdUnits = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.holdUnits)), 1, 8));
    parameters.releaseMs = clampf(parameters.releaseMs, 2.0f, 500.0f);
    parameters.cooldownMs = clampf(parameters.cooldownMs, 0.0f, 1000.0f);
    parameters.loopPeriods = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.loopPeriods)), 2, 16));
    parameters.mix = clampf(parameters.mix, 0.0f, 100.0f);
    parameters.liveUnder = clampf(parameters.liveUnder, 0.0f, 100.0f);
    return parameters;
}

void activate(EngineState& state, const double sampleRate, const std::uint32_t channelCount) {
    state = {};
    ensureBuffer(state, Parameters {}, sampleRate, channelCount);
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& rawParameters,
                          const TransportSnapshot& transport,
                          const std::uint32_t nframes,
                          const double sampleRate,
                          const AudioBlock& audio) {
    const Parameters parameters = clampParameters(rawParameters);
    const std::uint32_t channelCount = std::max<std::uint32_t>(1u, std::min(audio.channelCount, kMaxChannels));
    ensureBuffer(state, parameters, sampleRate, channelCount);

    const bool usableTransport = transportUsable(transport, sampleRate);
    const double currentBeat = usableTransport ? absoluteBeat(transport) : state.lastAbsoluteBeat;
    if (!usableTransport) {
        clearDetectionHistory(state);
        state.transportWasUsable = false;
    } else if (state.transportWasUsable && currentBeat + 0.0001 < state.lastAbsoluteBeat) {
        clearDetectionHistory(state);
    }
    state.transportWasUsable = usableTransport;
    state.lastAbsoluteBeat = currentBeat;

    if (nframes == 0) {
        return makeStatus(state, usableTransport);
    }

    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        writeInputFrame(state, audio, frame);
        updateDetectorAndCapture(state, parameters, transport, usableTransport);

        const float envelope = usableTransport ? holdEnvelope(state) : 0.0f;
        const float wet = clampf(parameters.mix * 0.01f, 0.0f, 1.0f) * envelope;
        const float dryDuringHold = 1.0f - wet + wet * clampf(parameters.liveUnder * 0.01f, 0.0f, 1.0f);

        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            float* output = audio.outputs[channel];
            if (output == nullptr) {
                continue;
            }

            const float* input = audio.inputs[channel];
            const float live = input != nullptr ? input[frame] : 0.0f;
            const float held = wet > 0.0f ? readLoopSample(state, channel) : 0.0f;
            output[frame] = live * dryDuringHold + held * wet;
        }

        advanceLoop(state);
        advanceStateAfterFrame(state);
    }

    return makeStatus(state, usableTransport);
}

}  // namespace downspout::orchid
