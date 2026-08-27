#include "orchid_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::orchid {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kTargetAnalysisRate = 12000.0;

// ── Scale interval tables (chromatic semitone offsets from root) ───────────────

constexpr int kScaleIntervals[23][12] = {
    {0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1},   // Major (7)
    {0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1},   // Ionian (7)
    {0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1},   // Minor (7)
    {0, 2, 3, 5, 7, 8, 11, -1, -1, -1, -1, -1},   // Harmonic Minor (7)
    {0, 2, 3, 5, 7, 9, 11, -1, -1, -1, -1, -1},   // Melodic Minor (7)
    {0, 2, 3, 5, 7, 9, 10, -1, -1, -1, -1, -1},   // Dorian (7)
    {0, 1, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1},   // Phrygian (7)
    {0, 2, 4, 6, 7, 9, 11, -1, -1, -1, -1, -1},   // Lydian (7)
    {0, 2, 4, 5, 7, 9, 10, -1, -1, -1, -1, -1},   // Mixolydian (7)
    {0, 1, 3, 5, 6, 8, 10, -1, -1, -1, -1, -1},   // Locrian (7)
    {0, 1, 4, 5, 7, 8, 10, -1, -1, -1, -1, -1},   // Phrygian Dominant (7)
    {0, 1, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1},   // Neo Major (7)
    {0, 1, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1},   // Neo Minor (7)
    {0, 2, 4, 7, 9, -1, -1, -1, -1, -1, -1, -1},  // Pent Major (5)
    {0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1}, // Pent Minor (5)
    {0, 3, 5, 6, 7, 10, -1, -1, -1, -1, -1, -1},  // Blues (6)
    {0, 2, 4, 6, 8, 10, -1, -1, -1, -1, -1, -1},  // Whole Tone (6)
    {0, 1, 3, 4, 6, 8, 10, -1, -1, -1, -1, -1},   // Altered (7)
    {0, 1, 3, 4, 6, 7, 9, 10, -1, -1, -1, -1},    // Half-Whole Diminished (8)
    {0, 2, 3, 5, 6, 8, 9, 11, -1, -1, -1, -1},    // Whole-Half Diminished (8)
    {0, 2, 4, 5, 7, 9, 10, 11, -1, -1, -1, -1},   // Bebop Dominant (8)
    {0, 2, 4, 5, 7, 8, 9, 11, -1, -1, -1, -1},    // Bebop Major (8)
    {0, 2, 3, 4, 5, 7, 9, 10, -1, -1, -1, -1},    // Bebop Minor (8)
};

constexpr int kScaleIntervalCounts[23] = {
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 7, 8, 8, 8, 8, 8
};

// Quantize a semitone offset to the nearest in-scale pitch.
// scaleIndex is 0-22 (already offset by -1 from the parameter value 1-23).
[[nodiscard]] int quantizeSemitone(const int semitone, const int scaleIndex) {
    if (scaleIndex < 0 || scaleIndex >= 23) return semitone;
    const int count = kScaleIntervalCounts[scaleIndex];
    int best = semitone;
    int bestDist = 1000;
    for (int oct = -4; oct <= 4; ++oct) {
        for (int i = 0; i < count; ++i) {
            const int candidate = oct * 12 + kScaleIntervals[scaleIndex][i];
            const int dist = std::abs(candidate - semitone);
            if (dist < bestDist) {
                bestDist = dist;
                best = candidate;
            }
        }
    }
    return best;
}

[[nodiscard]] double freqToMidi(const double freqHz) {
    if (freqHz <= 0.0) return 69.0;
    return 69.0 + 12.0 * std::log2(freqHz / 440.0);
}

[[nodiscard]] double computePlaybackRate(const EngineState& state, const Parameters& parameters) {
    if (state.activeMidiNote < 0 || !state.loop.valid || state.loop.frequencyHz <= 0.0f)
        return 1.0;

    const double rootMidi = freqToMidi(static_cast<double>(state.loop.frequencyHz));
    int semitoneShift = state.activeMidiNote - static_cast<int>(std::lround(rootMidi));

    const int scaleParam = static_cast<int>(std::lround(parameters.scale));
    if (scaleParam >= 1 && scaleParam <= 23)
        semitoneShift = quantizeSemitone(semitoneShift, scaleParam - 1);

    return std::pow(2.0, static_cast<double>(semitoneShift) / 12.0);
}

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
    state.pendingLoop = {};
    state.holdFramesRemaining = 0;
    state.holdFramesTotal = 0;
    state.releaseFramesRemaining = 0;
    state.releaseFramesTotal = 0;
    state.cooldownFramesRemaining = 0;
    state.loopReadPosition = 0.0;
    state.armedGridSerial = -1.0;
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
    const std::uint32_t decimation = std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>(std::floor(sampleRate / kTargetAnalysisRate)));
    const double analysisRate = sampleRate / static_cast<double>(decimation);
    const std::uint32_t windowFrames = static_cast<std::uint32_t>(
        std::clamp(std::lround(analysisRate * 0.050), 256l, 1024l));
    const std::uint32_t hopFrames = static_cast<std::uint32_t>(
        std::clamp(std::lround(sampleRate * 0.016), 128l, 2048l));
    const std::uint32_t sourceWindowFrames = windowFrames * decimation;
    const double lowPitch = std::max(20.0f, parameters.pitchLow);
    const std::uint32_t minHistory = static_cast<std::uint32_t>(
        std::ceil(sampleRate * std::max(0.25, 12.0 / lowPitch)));
    const std::uint32_t requiredFrames = std::max<std::uint32_t>(
        std::max(sourceWindowFrames * 4u, minHistory),
        static_cast<std::uint32_t>(std::lround(sampleRate * 2.0)));

    if (state.bufferFrames == requiredFrames &&
        state.bufferChannels == safeChannels &&
        state.analysisWindowFrames == windowFrames &&
        state.analysisHopFrames == hopFrames &&
        state.analysisDecimation == decimation &&
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
    state.analysisDecimation = decimation;
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
    const std::uint32_t decimation = std::max<std::uint32_t>(1u, state.analysisDecimation);
    const std::uint32_t sourceWindowFrames = windowFrames * decimation;
    const double analysisRate = state.sampleRate / static_cast<double>(decimation);
    if (windowFrames < 32u || state.filledFrames < sourceWindowFrames || state.sampleRate <= 0.0 || analysisRate <= 0.0) {
        return status;
    }

    double mean = 0.0;
    const std::int64_t startFrame = static_cast<std::int64_t>(state.writeHead) - static_cast<std::int64_t>(sourceWindowFrames);
    for (std::uint32_t i = 0; i < windowFrames; ++i) {
        const float sample = readMonoFrame(state, startFrame + static_cast<std::int64_t>(i * decimation));
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
    const float high = std::min(std::max(low + 1.0f, parameters.pitchHigh), static_cast<float>(analysisRate * 0.45));
    if (high <= low) {
        return status;
    }

    const std::uint32_t minLag = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::floor(analysisRate / high)));
    const std::uint32_t maxLag = std::min<std::uint32_t>(
        windowFrames - 8u,
        static_cast<std::uint32_t>(std::ceil(analysisRate / low)));

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

        const float lowerFrequency = static_cast<float>(analysisRate / static_cast<double>(lowerLag));
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
    status.frequencyHz = static_cast<float>(analysisRate / static_cast<double>(bestLag));

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

[[nodiscard]] double gridSerialForBeat(const Parameters& parameters,
                                       const TransportSnapshot& transport,
                                       const double beat) {
    const double beatsPerBar = std::max(1.0, transport.beatsPerBar);
    const double grid = std::max(1.0f, parameters.grid);
    const double gridBeats = beatsPerBar / grid;
    return std::floor((beat / gridBeats) + 1.0e-9);
}

[[nodiscard]] double nextGridSerialForBeat(const Parameters& parameters,
                                           const TransportSnapshot& transport,
                                           const double beat) {
    const double serial = gridSerialForBeat(parameters, transport, beat);
    const double beatsPerBar = std::max(1.0, transport.beatsPerBar);
    const double grid = std::max(1.0f, parameters.grid);
    const double gridBeats = beatsPerBar / grid;
    const double boundary = serial * gridBeats;
    return beat <= boundary + 1.0e-6 ? serial : serial + 1.0;
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
        // Prefer loop boundaries near zero so the hard splice is phase-coherent.
        cost += 0.5f * (std::fabs(first) + std::fabs(last));
    }
    return cost;
}

[[nodiscard]] LoopRegion makeLoopRegion(EngineState& state,
                                        const Parameters& parameters,
                                        const DetectorStatus& detector) {
    LoopRegion region {};
    const std::uint32_t length = loopLengthFor(parameters, detector, state.sampleRate);
    if (state.filledFrames <= length + 4u) {
        return region;
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

    region.valid = true;
    region.startFrame = wrapFrameIndex(state, bestStart);
    region.lengthFrames = length;
    region.frequencyHz = detector.frequencyHz;
    region.confidence = detector.confidence;
    return region;
}

void startHoldFromLoop(EngineState& state,
                       const Parameters& parameters,
                       const TransportSnapshot& transport,
                       const LoopRegion& loop) {
    if (!loop.valid) {
        return;
    }

    state.loop = loop;
    state.pendingLoop = {};
    state.loopReadPosition = 0.0;
    state.armedGridSerial = -1.0;

    state.processorState = ProcessorState::Held;
    state.holdFramesTotal = holdFramesFor(parameters, transport, state.sampleRate);
    state.holdFramesRemaining = state.holdFramesTotal;
    state.releaseFramesTotal = releaseFramesFor(parameters, state.sampleRate);
    state.releaseFramesRemaining = state.releaseFramesTotal;
    state.cooldownFramesRemaining = cooldownFramesFor(parameters, state.sampleRate);
}

[[nodiscard]] bool shouldReplaceLoop(const Parameters& parameters,
                                     const LoopRegion& current,
                                     const LoopRegion& candidate) {
    if (!candidate.valid) {
        return false;
    }
    if (!current.valid) {
        return true;
    }

    const float retriggerNorm = clampf(parameters.retrigger * 0.01f, 0.0f, 1.0f);
    if (retriggerNorm <= 0.0f) {
        return false;
    }

    const float requiredImprovement = 0.18f - retriggerNorm * 0.15f;
    return candidate.confidence >= current.confidence + requiredImprovement;
}

void armOrCaptureLoop(EngineState& state,
                      const Parameters& parameters,
                      const TransportSnapshot& transport,
                      const DetectorStatus& detector) {
    const LoopRegion loop = makeLoopRegion(state, parameters, detector);
    if (!loop.valid) {
        return;
    }

    if (parameters.captureTiming < 0.5f) {
        if ((state.processorState == ProcessorState::Held || state.processorState == ProcessorState::Release) &&
            !shouldReplaceLoop(parameters, state.loop, loop)) {
            return;
        }
        startHoldFromLoop(state, parameters, transport, loop);
        return;
    }

    const LoopRegion current = state.pendingLoop.valid ? state.pendingLoop : state.loop;
    if ((state.processorState == ProcessorState::Armed ||
         state.processorState == ProcessorState::Held ||
         state.processorState == ProcessorState::Release) &&
        !shouldReplaceLoop(parameters, current, loop)) {
        return;
    }

    state.pendingLoop = loop;
    if (state.processorState != ProcessorState::Held && state.processorState != ProcessorState::Release) {
        state.processorState = ProcessorState::Armed;
    }
    state.armedGridSerial = nextGridSerialForBeat(parameters, transport, absoluteBeat(transport));
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
        state.loopReadPosition += state.playbackRate;
        if (state.loop.valid && state.loop.lengthFrames > 0u) {
            const double len = static_cast<double>(state.loop.lengthFrames);
            while (state.loopReadPosition >= len) state.loopReadPosition -= len;
            while (state.loopReadPosition < 0.0)  state.loopReadPosition += len;
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
    if ((state.processorState == ProcessorState::Held || state.processorState == ProcessorState::Release) &&
        parameters.retrigger <= 0.0f &&
        !state.pendingLoop.valid) {
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
        armOrCaptureLoop(state, parameters, transport, state.detector);
    } else if (state.processorState == ProcessorState::Pass) {
        state.processorState = ProcessorState::Armed;
    }
}

void updateArmedGridCapture(EngineState& state,
                            const Parameters& parameters,
                            const TransportSnapshot& transport,
                            const double frameAbsoluteBeat) {
    if (parameters.captureTiming < 0.5f ||
        (state.processorState != ProcessorState::Armed && state.processorState != ProcessorState::Held && state.processorState != ProcessorState::Release) ||
        !state.pendingLoop.valid ||
        state.armedGridSerial < 0.0) {
        return;
    }

    const double currentSerial = gridSerialForBeat(parameters, transport, frameAbsoluteBeat);
    if (currentSerial >= state.armedGridSerial) {
        startHoldFromLoop(state, parameters, transport, state.pendingLoop);
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
    parameters.captureTiming = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.captureTiming)), 0, 1));
    parameters.retrigger = clampf(parameters.retrigger, 0.0f, 100.0f);
    parameters.scale = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.scale)), 0, kOrchidScaleCount - 1));
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
                          const AudioBlock& audio,
                          const MidiInputEvent* midiEvents,
                          const std::uint32_t midiEventCount) {
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

    // Process MIDI note-on/off for pitch shift
    for (std::uint32_t e = 0; e < midiEventCount; ++e) {
        const MidiInputEvent& msg = midiEvents[e];
        if (msg.size < 2) continue;
        const std::uint8_t status = msg.data[0] & 0xF0;
        const std::uint8_t note   = msg.data[1];
        const std::uint8_t vel    = msg.size >= 3 ? msg.data[2] : 0;
        if (status == 0x90 && vel > 0) {
            state.activeMidiNote = note;
            state.playbackRate = computePlaybackRate(state, parameters);
        } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
            if (state.activeMidiNote == static_cast<int>(note)) {
                state.activeMidiNote = -1;
                state.playbackRate = 1.0;
            }
        }
    }

    // Recompute playback rate when loop changes (loop.frequencyHz may have been updated)
    if (state.activeMidiNote >= 0)
        state.playbackRate = computePlaybackRate(state, parameters);

    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        writeInputFrame(state, audio, frame);
        updateDetectorAndCapture(state, parameters, transport, usableTransport);
        if (usableTransport && state.pendingLoop.valid) {
            const double framesPerBeat = sampleRate * 60.0 / transport.bpm;
            const double frameBeat = currentBeat + static_cast<double>(frame) / framesPerBeat;
            updateArmedGridCapture(state, parameters, transport, frameBeat);
        }

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
