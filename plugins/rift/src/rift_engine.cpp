#include "rift_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::rift {
namespace {

constexpr double kHalfPi = 1.5707963267948966;

[[nodiscard]] float clampf(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] int clampi(int value, int minValue, int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] std::uint32_t mixU32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

[[nodiscard]] float rand01(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

[[nodiscard]] std::uint32_t seedForPreview(const Parameters& parameters, const std::uint64_t blockIndex) {
    std::uint32_t seed = mixU32(static_cast<std::uint32_t>(blockIndex * 2654435761ull));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.grid * 97.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.density * 53.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.dub * 47.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.damage * 41.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.memoryBars * 29.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround((parameters.drift + 50.0f) * 19.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround((parameters.pitch + 24.0f) * 13.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.mix * 11.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.chop * 7.0f)));
    return seed != 0 ? seed : 1u;
}

[[nodiscard]] float actionWeightTotal(const ActionWeights& weights) {
    return weights.repeat + weights.reverse + weights.skip + weights.smear + weights.slip;
}

[[nodiscard]] ActionType chooseAction(const Parameters& parameters, std::uint32_t& rngState, const bool forceMutation) {
    const float densityNorm = clampf(parameters.density * 0.01f, 0.0f, 1.0f);
    const float dubNorm = clampf(parameters.dub * 0.01f, 0.0f, 1.0f);
    if (dubNorm > 0.0f && rand01(rngState) <= dubNorm) {
        return ActionType::Dub;
    }

    if (!forceMutation && rand01(rngState) > densityNorm) {
        return ActionType::Pass;
    }

    const ActionWeights weights = calculateActionWeights(parameters);
    const float total = actionWeightTotal(weights);
    if (total <= 0.0f) {
        return ActionType::Pass;
    }

    float pick = rand01(rngState) * total;
    if ((pick -= weights.repeat) <= 0.0f) return ActionType::Repeat;
    if ((pick -= weights.reverse) <= 0.0f) return ActionType::Reverse;
    if ((pick -= weights.skip) <= 0.0f) return ActionType::Skip;
    if ((pick -= weights.smear) <= 0.0f) return ActionType::Smear;
    return ActionType::Slip;
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

[[nodiscard]] std::uint32_t sampleFrameCount(const SampleSource& source) {
    if (source.channelCount == 0u) {
        return 0u;
    }

    return static_cast<std::uint32_t>(source.interleaved.size() / static_cast<std::size_t>(source.channelCount));
}

[[nodiscard]] bool nearlyEqual(const double lhs, const double rhs, const double epsilon = 1e-6) {
    return std::fabs(lhs - rhs) <= epsilon;
}

[[nodiscard]] bool blocksEquivalent(const BlockSpec& lhs, const BlockSpec& rhs) {
    return lhs.valid == rhs.valid &&
           lhs.action == rhs.action &&
           lhs.sourceStartFrame == rhs.sourceStartFrame &&
           lhs.sourceLengthFrames == rhs.sourceLengthFrames &&
           lhs.loopBlendFrames == rhs.loopBlendFrames &&
           nearlyEqual(lhs.readPosition, rhs.readPosition) &&
           nearlyEqual(lhs.readRate, rhs.readRate) &&
           nearlyEqual(lhs.wet, rhs.wet) &&
           nearlyEqual(lhs.dry, rhs.dry);
}

[[nodiscard]] std::uint32_t crossfadeFramesFor(const double sampleRate, const std::uint32_t sourceLengthFrames) {
    const std::uint32_t base = static_cast<std::uint32_t>(std::lround(sampleRate * 0.0015));
    const std::uint32_t desired = std::clamp(base, 8u, 96u);
    const std::uint32_t perBlockCap = std::max(1u, sourceLengthFrames / 4u);
    return std::min(desired, perBlockCap);
}

[[nodiscard]] std::uint32_t loopBlendFramesFor(const Parameters& parameters,
                                               const double sampleRate,
                                               const std::uint32_t sourceLengthFrames) {
    const float blendNorm = clampf(parameters.blend * 0.01f, 0.0f, 1.0f);
    if (blendNorm <= 0.0f || sourceLengthFrames <= 2u) {
        return 0u;
    }

    const std::uint32_t maxByTime = static_cast<std::uint32_t>(std::lround(sampleRate * 0.015));
    const std::uint32_t maxByLoop = std::max(1u, sourceLengthFrames / 3u);
    const std::uint32_t cap = std::min(maxByTime, maxByLoop);
    if (cap <= 1u) {
        return 0u;
    }

    return std::clamp(static_cast<std::uint32_t>(std::lround(static_cast<double>(cap) * blendNorm)), 0u, cap);
}

[[nodiscard]] std::uint32_t chopDivisionsFor(const Parameters& parameters) {
    const float chopNorm = clampf(parameters.chop * 0.01f, 0.0f, 1.0f);
    return 1u + static_cast<std::uint32_t>(std::lround(chopNorm * 7.0f));
}

[[nodiscard]] std::uint32_t choppedSourceLength(const Parameters& parameters,
                                                const std::uint32_t blockSourceLength) {
    const std::uint32_t divisions = chopDivisionsFor(parameters);
    if (divisions <= 1u) {
        return blockSourceLength;
    }
    return std::max<std::uint32_t>(1u, blockSourceLength / divisions);
}

void clearTransition(EngineState& state) {
    state.transitionBlock = {};
    state.transitionFramesRemaining = 0;
    state.transitionFramesTotal = 0;
}

[[nodiscard]] BlockSpec makePassBlock() {
    BlockSpec block {};
    block.action = ActionType::Pass;
    block.valid = true;
    block.dry = 1.0f;
    block.wet = 0.0f;
    return block;
}

void applySelectedBlock(EngineState& state,
                        const BlockSpec& block,
                        const std::int64_t blockSerial,
                        const std::int64_t sequenceSerial) {
    const BlockSpec previous = state.activeBlock;

    state.activeBlock = block;
    state.activeBlock.valid = true;
    state.activeBlockSerial = blockSerial;
    state.activeSequenceSerial = sequenceSerial;

    if (!previous.valid || blocksEquivalent(previous, state.activeBlock)) {
        clearTransition(state);
        return;
    }

    const std::uint32_t referenceFrames = std::max(previous.sourceLengthFrames, state.activeBlock.sourceLengthFrames);
    const std::uint32_t transitionFrames = crossfadeFramesFor(state.sampleRate, referenceFrames);
    if (transitionFrames <= 1u) {
        clearTransition(state);
        return;
    }

    state.transitionBlock = previous;
    state.transitionFramesTotal = transitionFrames;
    state.transitionFramesRemaining = transitionFrames;
}

void ensureBuffer(EngineState& state,
                  const Parameters& parameters,
                  const TransportSnapshot& transport,
                  const double sampleRate,
                  const std::uint32_t channelCount) {
    const std::uint32_t safeChannels = std::max<std::uint32_t>(1u, std::min(channelCount, kMaxChannels));
    double requiredFrames = sampleRate * 8.0;

    if (transport.valid && transport.bpm > 0.0 && transport.beatsPerBar > 0.0) {
        const double framesPerBar = sampleRate * 60.0 / transport.bpm * transport.beatsPerBar;
        requiredFrames = std::max(requiredFrames, framesPerBar * std::max(2.0f, parameters.memoryBars * 2.0f));
    } else {
        requiredFrames = std::max(requiredFrames, sampleRate * std::max(2.0f, parameters.memoryBars * 4.0f));
    }

    const double maxFrames = std::max(2048.0, sampleRate * 128.0);
    const std::uint32_t required = static_cast<std::uint32_t>(std::clamp(requiredFrames, 2048.0, maxFrames));
    if (state.bufferFrames == required && state.bufferChannels == safeChannels && std::fabs(state.sampleRate - sampleRate) < 0.001) {
        return;
    }

    state.buffer.assign(static_cast<std::size_t>(required) * static_cast<std::size_t>(safeChannels), 0.0f);
    state.bufferFrames = required;
    state.bufferChannels = safeChannels;
    state.writeHead = 0;
    state.filledFrames = 0;
    state.sampleRate = sampleRate;
    state.activeBlock = {};
    state.activeBlockSerial = -1;
    state.activeSequenceSerial = -1;
    clearTransition(state);
}

void writeInputFrame(EngineState& state, const AudioBlock& audio, const std::uint32_t frame) {
    if (state.bufferFrames == 0 || state.bufferChannels == 0) {
        return;
    }

    for (std::uint32_t channel = 0; channel < state.bufferChannels; ++channel) {
        const float* in = audio.inputs[channel];
        const float sample = in ? in[frame] : 0.0f;
        state.buffer[bufferIndex(state, channel, state.writeHead)] = sample;
    }

    state.writeHead = (state.writeHead + 1u) % state.bufferFrames;
    state.filledFrames = std::min(state.filledFrames + 1u, state.bufferFrames);
}

[[nodiscard]] float dryInputFrame(const AudioBlock& audio, const std::uint32_t channel, const std::uint32_t frame) {
    const float* dry = audio.dryInputs[channel];
    if (dry != nullptr) {
        return dry[frame];
    }

    const float* in = audio.inputs[channel];
    return in ? in[frame] : 0.0f;
}

[[nodiscard]] float readSampleAtFrame(const SampleSource& source,
                                      const std::uint32_t outputChannel,
                                      double position) {
    const std::uint32_t frames = sampleFrameCount(source);
    if (frames == 0u || source.channelCount == 0u) {
        return 0.0f;
    }

    position = std::fmod(position, static_cast<double>(frames));
    if (position < 0.0) {
        position += static_cast<double>(frames);
    }

    const std::int64_t frame0 = static_cast<std::int64_t>(std::floor(position));
    const double frac = position - static_cast<double>(frame0);
    const std::uint32_t index0 = static_cast<std::uint32_t>(frame0) % frames;
    const std::uint32_t index1 = (index0 + 1u) % frames;
    const std::uint32_t sourceChannel = source.channelCount == 1u
        ? 0u
        : std::min(outputChannel, source.channelCount - 1u);

    const std::size_t sample0Index = static_cast<std::size_t>(index0) * source.channelCount + sourceChannel;
    const std::size_t sample1Index = static_cast<std::size_t>(index1) * source.channelCount + sourceChannel;
    const float sample0 = source.interleaved[sample0Index];
    const float sample1 = source.interleaved[sample1Index];
    return sample0 + static_cast<float>(frac) * (sample1 - sample0);
}

[[nodiscard]] bool canRenderSamplePlayback(const SamplePlayback& samplePlayback,
                                           const TransportSnapshot& transport,
                                           const double hostSampleRate) {
    return samplePlayback.source != nullptr &&
           isSampleSourceUsable(*samplePlayback.source) &&
           transport.valid &&
           transport.playing &&
           transport.bpm > 0.0 &&
           transport.beatsPerBar > 0.0 &&
           hostSampleRate > 0.0;
}

[[nodiscard]] float renderSamplePlaybackFrame(const SamplePlayback& samplePlayback,
                                              const TransportSnapshot& transport,
                                              const double hostSampleRate,
                                              const std::uint32_t frame,
                                              const std::uint32_t channel) {
    if (!canRenderSamplePlayback(samplePlayback, transport, hostSampleRate)) {
        return 0.0f;
    }

    const SampleSource& source = *samplePlayback.source;
    const double loopBeats = samplePlayback.loopBeats > 0.0 ? samplePlayback.loopBeats : sampleLoopBeats(source);
    if (loopBeats <= 0.0) {
        return 0.0f;
    }

    const double framesPerHostBeat = hostSampleRate * 60.0 / transport.bpm;
    const double absoluteHostBeats = transport.bar * transport.beatsPerBar +
                                     transport.barBeat +
                                     static_cast<double>(frame) / framesPerHostBeat;
    double sourceBeat = std::fmod(absoluteHostBeats + samplePlayback.beatOffset, loopBeats);
    if (sourceBeat < 0.0) {
        sourceBeat += loopBeats;
    }

    const double sourceFramesPerBeat = static_cast<double>(sampleFrameCount(source)) / loopBeats;
    return readSampleAtFrame(source, channel, sourceBeat * sourceFramesPerBeat) * samplePlayback.gain;
}

[[nodiscard]] float readBufferAtPosition(const EngineState& state,
                                         const std::uint32_t channel,
                                         double position) {
    if (state.bufferFrames == 0 || state.bufferChannels == 0 || channel >= state.bufferChannels) {
        return 0.0f;
    }

    const std::int64_t frame0 = static_cast<std::int64_t>(std::floor(position));
    const double frac = position - static_cast<double>(frame0);
    const std::uint32_t index0 = wrapFrameIndex(state, frame0);
    const std::uint32_t index1 = wrapFrameIndex(state, frame0 + 1);

    const float sample0 = state.buffer[bufferIndex(state, channel, index0)];
    const float sample1 = state.buffer[bufferIndex(state, channel, index1)];
    return sample0 + static_cast<float>(frac) * (sample1 - sample0);
}

[[nodiscard]] float readLoopRelative(const EngineState& state,
                                     const BlockSpec& block,
                                     const std::uint32_t channel,
                                     double relative) {
    const double length = static_cast<double>(std::max<std::uint32_t>(1u, block.sourceLengthFrames));
    relative = std::fmod(relative, length);
    if (relative < 0.0) {
        relative += length;
    }

    return readBufferAtPosition(state, channel, static_cast<double>(block.sourceStartFrame) + relative);
}

[[nodiscard]] float readBuffer(const EngineState& state,
                               const BlockSpec& block,
                               const std::uint32_t channel,
                               double position) {
    const double length = static_cast<double>(std::max<std::uint32_t>(1u, block.sourceLengthFrames));
    double relative = std::fmod(position - static_cast<double>(block.sourceStartFrame), length);
    if (relative < 0.0) {
        relative += length;
    }

    const float primary = readLoopRelative(state, block, channel, relative);
    const std::uint32_t blendFrames = std::min(block.loopBlendFrames, block.sourceLengthFrames > 1u ? block.sourceLengthFrames - 1u : 0u);
    if (blendFrames <= 1u) {
        return primary;
    }

    const double blendLength = static_cast<double>(blendFrames);
    if (block.readRate >= 0.0) {
        const double blendStart = length - blendLength;
        if (relative >= blendStart) {
            const double t = clampf(static_cast<float>((relative - blendStart) / blendLength), 0.0f, 1.0f);
            const float next = readLoopRelative(state, block, channel, relative - blendStart);
            const double phase = t * kHalfPi;
            return primary * static_cast<float>(std::cos(phase)) +
                   next * static_cast<float>(std::sin(phase));
        }
    } else if (relative < blendLength) {
        const double t = clampf(static_cast<float>(relative / blendLength), 0.0f, 1.0f);
        const float next = readLoopRelative(state, block, channel, length - blendLength + relative);
        const double phase = t * kHalfPi;
        return next * static_cast<float>(std::cos(phase)) +
               primary * static_cast<float>(std::sin(phase));
    }

    return primary;
}

[[nodiscard]] double wrapReadPosition(const BlockSpec& block, double position) {
    const double start = static_cast<double>(block.sourceStartFrame);
    const double length = static_cast<double>(std::max<std::uint32_t>(1u, block.sourceLengthFrames));
    double relative = std::fmod(position - start, length);
    if (relative < 0.0) {
        relative += length;
    }
    return start + relative;
}

void armTriggers(EngineState& state, const Triggers& triggers) {
    if (triggers.scatterSerial != 0 && triggers.scatterSerial != state.lastScatterSerial) {
        state.lastScatterSerial = triggers.scatterSerial;
        state.scatterBlocksRemaining = 4;
    }

    if (triggers.recoverSerial != 0 && triggers.recoverSerial != state.lastRecoverSerial) {
        state.lastRecoverSerial = triggers.recoverSerial;
        state.scatterBlocksRemaining = 0;
        state.sequenceBlocksRemaining = 0;
        state.recoverBlocksRemaining = 4;
    }
}

struct SequenceSelection {
    bool active = false;
    ActionType action = ActionType::Pass;
    double sourceBeats = 0.0;
};

[[nodiscard]] bool hasSequenceCells(const SequencePattern& sequence) {
    for (const SequenceCell& cell : sequence.cells) {
        if (cell.kind != SequenceCellKind::Empty) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int sequenceCellIndexForSerial(const std::int64_t blockSerial) {
    std::int64_t wrapped = blockSerial % kSequenceCellCount;
    if (wrapped < 0) {
        wrapped += kSequenceCellCount;
    }
    return static_cast<int>(wrapped);
}

[[nodiscard]] std::int64_t sequenceSerialForTransportBlock(const EngineState& state,
                                                           const std::int64_t transportBlockSerial) {
    if (!state.transportWasPlaying || state.activeSequenceSerial < 0 || state.activeBlockSerial < 0) {
        return transportBlockSerial;
    }

    if (transportBlockSerial > state.activeBlockSerial) {
        return state.activeSequenceSerial + (transportBlockSerial - state.activeBlockSerial);
    }

    if (transportBlockSerial < state.activeBlockSerial) {
        return state.activeSequenceSerial + 1;
    }

    return state.activeSequenceSerial;
}

[[nodiscard]] SequenceSelection sequenceSelectionFor(const SequencePattern& sequence, const std::int64_t blockSerial) {
    if (!hasSequenceCells(sequence)) {
        return {};
    }

    const int wrapped = sequenceCellIndexForSerial(blockSerial);

    SequenceSelection selection;
    selection.active = true;

    const SequenceCell& cell = sequence.cells[static_cast<std::size_t>(wrapped)];
    switch (cell.kind) {
    case SequenceCellKind::Empty:
        selection.action = ActionType::Pass;
        break;
    case SequenceCellKind::Ratchet:
        selection.action = ActionType::Repeat;
        selection.sourceBeats = 0.125;
        break;
    case SequenceCellKind::HalfBeat:
        selection.action = ActionType::Repeat;
        selection.sourceBeats = 0.5;
        break;
    case SequenceCellKind::OneBeat:
        selection.action = ActionType::Repeat;
        selection.sourceBeats = 1.0;
        break;
    case SequenceCellKind::TwoBeat:
        selection.action = ActionType::Repeat;
        selection.sourceBeats = 2.0;
        break;
    case SequenceCellKind::Reverse:
        selection.action = ActionType::Reverse;
        selection.sourceBeats = 1.0;
        break;
    case SequenceCellKind::Smear:
        selection.action = ActionType::Smear;
        selection.sourceBeats = 2.0;
        break;
    case SequenceCellKind::Slip:
        selection.action = ActionType::Slip;
        selection.sourceBeats = 1.0;
        break;
    case SequenceCellKind::Dub:
        selection.action = ActionType::Dub;
        selection.sourceBeats = 1.0;
        break;
    }

    return selection;
}

void selectBlock(EngineState& state,
                 const Parameters& parameters,
                 const Triggers& triggers,
                 const SequencePattern& sequence,
                 const std::int64_t blockSerial,
                 const std::int64_t sequenceSerial,
                 const double blockFrames,
                 const double framesPerBeat,
                 const double framesPerBar) {
    armTriggers(state, triggers);

    if (state.recoverBlocksRemaining > 0) {
        applySelectedBlock(state, makePassBlock(), blockSerial, sequenceSerial);
        state.recoverBlocksRemaining -= 1u;
        state.sequenceBlocksRemaining = 0;
        return;
    }

    if (parameters.hold >= 0.5f && state.activeBlock.valid) {
        state.activeBlockSerial = blockSerial;
        state.activeSequenceSerial = sequenceSerial;
        return;
    }

    if (state.sequenceBlocksRemaining > 0u && state.activeBlock.valid) {
        state.activeBlockSerial = blockSerial;
        state.activeSequenceSerial = sequenceSerial;
        state.sequenceBlocksRemaining -= 1u;
        return;
    }

    bool forceMutation = false;
    if (state.scatterBlocksRemaining > 0) {
        forceMutation = true;
        state.scatterBlocksRemaining -= 1u;
    }

    BlockSpec block {};
    block.valid = true;

    const SequenceSelection sequenceSelection = sequenceSelectionFor(sequence, sequenceSerial);
    const ActionType action = sequenceSelection.active
        ? sequenceSelection.action
        : chooseAction(parameters, state.rngState, forceMutation);
    if (action == ActionType::Pass) {
        applySelectedBlock(state, makePassBlock(), blockSerial, sequenceSerial);
        state.sequenceBlocksRemaining = 0;
        return;
    }

    const std::uint32_t blockSourceLength = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::llround(blockFrames)));
    const std::uint32_t selectedSourceLength = sequenceSelection.sourceBeats > 0.0
        ? std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::llround(framesPerBeat * sequenceSelection.sourceBeats)))
        : blockSourceLength;
    const std::uint32_t sourceLength = sequenceSelection.active
        ? selectedSourceLength
        : (action == ActionType::Dub ? blockSourceLength : choppedSourceLength(parameters, blockSourceLength));
    const std::uint32_t memoryFrames = std::max<std::uint32_t>(
        blockSourceLength + 8u,
        static_cast<std::uint32_t>(std::llround(framesPerBar * std::max(1.0f, parameters.memoryBars))));
    const std::uint32_t availableHistory = std::min(state.filledFrames, memoryFrames);
    if (availableHistory <= sourceLength + 8u) {
        applySelectedBlock(state, makePassBlock(), blockSerial, sequenceSerial);
        return;
    }

    const std::uint32_t maxExtra = availableHistory > sourceLength + 1u ? availableHistory - sourceLength - 1u : 0u;
    std::uint32_t extra = maxExtra > 0u ? state.rngState % (maxExtra + 1u) : 0u;
    const float driftNorm = clampf(parameters.drift * 0.01f, 0.0f, 1.0f);
    if (maxExtra > 0u) {
        const std::uint32_t driftPush = static_cast<std::uint32_t>(std::lround(driftNorm * static_cast<float>(maxExtra) * rand01(state.rngState)));
        extra = std::min(maxExtra, extra + driftPush);
    }

    const std::uint32_t lookback = sourceLength + extra;
    block.sourceStartFrame = wrapFrameIndex(state, static_cast<std::int64_t>(state.writeHead) - static_cast<std::int64_t>(lookback));
    block.sourceLengthFrames = sourceLength;
    block.loopBlendFrames = loopBlendFramesFor(parameters, state.sampleRate, sourceLength);
    block.action = action;
    block.wet = clampf(parameters.mix * 0.01f, 0.0f, 1.0f);
    block.dry = 1.0f - block.wet;

    switch (action) {
    case ActionType::Repeat:
        block.readPosition = static_cast<double>(block.sourceStartFrame);
        block.readRate = 1.0;
        break;
    case ActionType::Reverse:
        block.readPosition = static_cast<double>(block.sourceStartFrame + block.sourceLengthFrames - 1u);
        block.readRate = -1.0;
        break;
    case ActionType::Skip:
        block.readPosition = static_cast<double>(block.sourceStartFrame);
        block.readRate = 0.0;
        block.wet = 0.0f;
        break;
    case ActionType::Smear:
        block.readPosition = static_cast<double>(block.sourceStartFrame);
        block.readRate = 0.12 + static_cast<double>(rand01(state.rngState)) * (0.18 + (1.0 - driftNorm) * 0.18);
        break;
    case ActionType::Slip: {
        float semitones = parameters.pitch;
        if (std::fabs(semitones) < 0.5f) {
            const float magnitude = 3.0f + rand01(state.rngState) * 4.0f;
            const float direction = rand01(state.rngState) < 0.5f ? -1.0f : 1.0f;
            semitones = magnitude * direction;
        }
        semitones += (rand01(state.rngState) - 0.5f) * driftNorm * 8.0f;
        block.readPosition = static_cast<double>(block.sourceStartFrame);
        block.readRate = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        break;
    }
    case ActionType::Dub:
        block.readPosition = static_cast<double>(block.sourceStartFrame);
        block.readRate = 1.0;
        break;
    case ActionType::Pass:
        break;
    }

    applySelectedBlock(state, block, blockSerial, sequenceSerial);
    if (sequenceSelection.active && sequenceSelection.sourceBeats > 0.0 && framesPerBeat > 0.0 && blockFrames > 0.0) {
        const double beatsPerBlock = blockFrames / framesPerBeat;
        const std::uint32_t durationBlocks = static_cast<std::uint32_t>(
            std::max(1.0, std::ceil(sequenceSelection.sourceBeats / beatsPerBlock)));
        state.sequenceBlocksRemaining = durationBlocks > 0u ? durationBlocks - 1u : 0u;
    } else {
        state.sequenceBlocksRemaining = 0;
    }
}

[[nodiscard]] float renderBlockSample(const EngineState& state,
                                      const BlockSpec& block,
                                      const std::uint32_t channel,
                                      const float live) {
    switch (block.action) {
    case ActionType::Pass:
        return live;
    case ActionType::Skip:
        return live * block.dry;
    case ActionType::Repeat:
    case ActionType::Reverse:
    case ActionType::Smear:
    case ActionType::Slip: {
        const float effected = readBuffer(state, block, channel, block.readPosition);
        return live * block.dry + effected * block.wet;
    }
    case ActionType::Dub: {
        const double length = static_cast<double>(std::max<std::uint32_t>(1u, block.sourceLengthFrames));
        const std::uint32_t wideChannel = state.bufferChannels > 1u
            ? (channel + 1u) % state.bufferChannels
            : channel;
        const float head = readBuffer(state, block, channel, block.readPosition);
        const float tapA = readBuffer(state, block, channel, block.readPosition - length * 0.375);
        const float tapB = readBuffer(state, block, wideChannel, block.readPosition - length * 0.625);
        const float tapC = readBuffer(state, block, channel, block.readPosition - length * 0.875);
        const float tapD = readBuffer(state, block, wideChannel, block.readPosition - length * 0.1875);
        const float throwInput = live * 0.72f +
                                 head * 1.10f +
                                 tapA * 0.82f +
                                 tapB * 0.66f +
                                 tapC * 0.48f +
                                 tapD * 0.36f;
        const float effected = clampf(std::tanh(throwInput * 1.85f) * 1.12f, -1.0f, 1.0f);
        return live * block.dry + effected * block.wet;
    }
    }

    return live;
}

void advanceBlockReadPosition(BlockSpec& block) {
    if (block.action == ActionType::Pass || block.action == ActionType::Skip) {
        return;
    }

    block.readPosition = wrapReadPosition(block, block.readPosition + block.readRate);
}

}  // namespace

Parameters clampParameters(const Parameters& raw) {
    Parameters parameters = raw;
    parameters.grid = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.grid)), 1, 16));
    parameters.density = clampf(parameters.density, 0.0f, 100.0f);
    parameters.dub = clampf(parameters.dub, 0.0f, 100.0f);
    parameters.damage = clampf(parameters.damage, 0.0f, 100.0f);
    parameters.memoryBars = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.memoryBars)), 1, 8));
    parameters.drift = clampf(parameters.drift, 0.0f, 100.0f);
    parameters.pitch = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.pitch)), -12, 12));
    parameters.mix = clampf(parameters.mix, 0.0f, 100.0f);
    parameters.blend = clampf(parameters.blend, 0.0f, 100.0f);
    parameters.hold = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.hold)), 0, 1));
    parameters.sourceMode = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.sourceMode)), 0, 2));
    parameters.sampleBeats = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.sampleBeats)), 1, 32));
    parameters.chop = clampf(parameters.chop, 0.0f, 100.0f);
    return parameters;
}

ActionWeights calculateActionWeights(const Parameters& rawParameters) {
    const Parameters parameters = clampParameters(rawParameters);
    const float damageNorm = parameters.damage * 0.01f;
    const float driftNorm = parameters.drift * 0.01f;
    const float pitchNorm = std::fabs(parameters.pitch) / 12.0f;
    const float chopNorm = parameters.chop * 0.01f;

    ActionWeights weights;
    weights.repeat = 1.20f + chopNorm * 1.80f;
    weights.reverse = 0.35f + damageNorm * 1.10f + chopNorm * 0.25f;
    weights.skip = (0.15f + damageNorm * 0.90f) * (1.0f - chopNorm * 0.45f);
    weights.smear = 0.35f + ((damageNorm + driftNorm) * 0.75f) + chopNorm * 0.25f;
    weights.slip = 0.25f + pitchNorm * 0.90f + driftNorm * 0.75f + chopNorm * 0.20f;
    return weights;
}

ActionType previewActionForBlock(const Parameters& rawParameters, const std::uint64_t blockIndex) {
    const Parameters parameters = clampParameters(rawParameters);
    std::uint32_t rngState = seedForPreview(parameters, blockIndex);
    return chooseAction(parameters, rngState, false);
}

bool isSampleSourceUsable(const SampleSource& source) {
    return source.channelCount > 0u &&
           source.channelCount <= kMaxChannels &&
           source.sampleRate > 0.0 &&
           sampleFrameCount(source) > 0u;
}

double sampleLoopBeats(const SampleSource& source) {
    if (!isSampleSourceUsable(source)) {
        return 0.0;
    }

    if (source.loopBeats > 0.0) {
        return source.loopBeats;
    }

    if (source.sourceBpm <= 0.0) {
        return 0.0;
    }

    return static_cast<double>(sampleFrameCount(source)) / source.sampleRate * source.sourceBpm / 60.0;
}

void activate(EngineState& state, const double sampleRate, const std::uint32_t channelCount) {
    state = {};
    ensureBuffer(state, Parameters {}, TransportSnapshot {}, sampleRate, channelCount);
}

void resetHistory(EngineState& state) {
    std::fill(state.buffer.begin(), state.buffer.end(), 0.0f);
    std::fill(state.sourceInputScratch.begin(), state.sourceInputScratch.end(), 0.0f);
    state.writeHead = 0;
    state.filledFrames = 0;
    state.transportWasPlaying = false;
    state.activeBlock = {};
    state.activeBlockSerial = -1;
    state.activeSequenceSerial = -1;
    state.sequenceBlocksRemaining = 0;
    state.scatterBlocksRemaining = 0;
    state.recoverBlocksRemaining = 0;
    clearTransition(state);
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& rawParameters,
                          const Triggers& triggers,
                          const TransportSnapshot& transport,
                          const std::uint32_t nframes,
                          const double sampleRate,
                          const AudioBlock& audio) {
    return processBlock(state, rawParameters, triggers, SequencePattern {}, transport, nframes, sampleRate, audio);
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& rawParameters,
                          const Triggers& triggers,
                          const SequencePattern& sequence,
                          const TransportSnapshot& transport,
                          const std::uint32_t nframes,
                          const double sampleRate,
                          const AudioBlock& audio) {
    const Parameters parameters = clampParameters(rawParameters);
    const std::uint32_t channelCount = std::max<std::uint32_t>(1u, std::min(audio.channelCount, kMaxChannels));
    ensureBuffer(state, parameters, transport, sampleRate, channelCount);

    if (nframes == 0) {
        return {};
    }

    const bool hostPlaying = transport.valid &&
                             transport.playing &&
                             transport.bpm > 0.0 &&
                             transport.beatsPerBar > 0.0 &&
                             sampleRate > 0.0;

    if (!hostPlaying) {
        state.transportWasPlaying = false;
        state.activeBlock = makePassBlock();
        state.activeBlockSerial = -1;
        state.activeSequenceSerial = -1;
        state.sequenceBlocksRemaining = 0;
        clearTransition(state);

        for (std::uint32_t frame = 0; frame < nframes; ++frame) {
            for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
                float* out = audio.outputs[channel];
                if (!out) {
                    continue;
                }
                out[frame] = dryInputFrame(audio, channel, frame);
            }
            writeInputFrame(state, audio, frame);
        }

        return {};
    }

    const double framesPerBeat = sampleRate * 60.0 / transport.bpm;
    const double beatsPerBlock = transport.beatsPerBar / static_cast<double>(parameters.grid);
    const double blockFrames = std::max(1.0, framesPerBeat * beatsPerBlock);
    const double framesPerBar = framesPerBeat * transport.beatsPerBar;

    const double absBeatsStart = transport.bar * transport.beatsPerBar + transport.barBeat;
    const std::int64_t currentBlockSerial = static_cast<std::int64_t>(std::floor(absBeatsStart / beatsPerBlock + 1e-9));
    if (!state.transportWasPlaying || currentBlockSerial != state.activeBlockSerial || !state.activeBlock.valid) {
        const std::int64_t currentSequenceSerial = sequenceSerialForTransportBlock(state, currentBlockSerial);
        selectBlock(state,
                    parameters,
                    triggers,
                    sequence,
                    currentBlockSerial,
                    currentSequenceSerial,
                    blockFrames,
                    framesPerBeat,
                    framesPerBar);
    } else {
        armTriggers(state, triggers);
    }

    double positionInBlock = std::fmod(absBeatsStart, beatsPerBlock);
    if (positionInBlock < 0.0) {
        positionInBlock += beatsPerBlock;
    }

    double framesToNextBoundary = (beatsPerBlock - positionInBlock) * framesPerBeat;
    if (framesToNextBoundary <= 1e-6) {
        framesToNextBoundary = blockFrames;
    }

    struct Boundary {
        std::uint32_t frame = 0;
        std::int64_t blockSerial = 0;
    };

    std::array<Boundary, 64> boundaries {};
    std::size_t boundaryCount = 0;
    std::int64_t nextSerial = currentBlockSerial + 1;
    double framePos = framesToNextBoundary;
    while (framePos < static_cast<double>(nframes) && boundaryCount < boundaries.size()) {
        const std::uint32_t frame = static_cast<std::uint32_t>(std::max(0.0, std::floor(framePos + 1e-6)));
        if (frame > 0u && (boundaryCount == 0 || frame > boundaries[boundaryCount - 1].frame)) {
            boundaries[boundaryCount++] = Boundary {frame, nextSerial++};
        }
        framePos += blockFrames;
    }

    std::size_t boundaryIndex = 0;
    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        if (boundaryIndex < boundaryCount && frame == boundaries[boundaryIndex].frame) {
            const std::int64_t boundarySequenceSerial =
                sequenceSerialForTransportBlock(state, boundaries[boundaryIndex].blockSerial);
            selectBlock(state,
                        parameters,
                        triggers,
                        sequence,
                        boundaries[boundaryIndex].blockSerial,
                        boundarySequenceSerial,
                        blockFrames,
                        framesPerBeat,
                        framesPerBar);
            ++boundaryIndex;
        }

        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            float* out = audio.outputs[channel];
            if (!out) {
                continue;
            }

            const float live = dryInputFrame(audio, channel, frame);
            const float currentOutput = renderBlockSample(state, state.activeBlock, channel, live);

            if (state.transitionFramesRemaining > 0u && state.transitionBlock.valid) {
                const float previousOutput = renderBlockSample(state, state.transitionBlock, channel, live);
                const std::uint32_t transitionIndex = state.transitionFramesTotal - state.transitionFramesRemaining + 1u;
                const double t = clampf(static_cast<float>(transitionIndex) /
                                            static_cast<float>(std::max(1u, state.transitionFramesTotal)),
                                        0.0f,
                                        1.0f);
                const double phase = t * kHalfPi;
                const float previousGain = static_cast<float>(std::cos(phase));
                const float currentGain = static_cast<float>(std::sin(phase));
                out[frame] = previousOutput * previousGain + currentOutput * currentGain;
            } else {
                out[frame] = currentOutput;
            }
        }

        advanceBlockReadPosition(state.activeBlock);

        if (state.transitionFramesRemaining > 0u && state.transitionBlock.valid) {
            advanceBlockReadPosition(state.transitionBlock);
            state.transitionFramesRemaining -= 1u;
            if (state.transitionFramesRemaining == 0u) {
                clearTransition(state);
            }
        }

        writeInputFrame(state, audio, frame);
    }

    state.transportWasPlaying = true;

    OutputStatus status;
    status.action = state.activeBlock.action;
    status.activity = (state.activeBlock.action == ActionType::Pass) ? 0.0f : state.activeBlock.wet;
    status.sequenceCell = sequenceCellIndexForSerial(state.activeSequenceSerial);
    return status;
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& parameters,
                          const Triggers& triggers,
                          const TransportSnapshot& transport,
                          const std::uint32_t nframes,
                          const double sampleRate,
                          const AudioBlock& audio,
                          const SamplePlayback& samplePlayback) {
    return processBlock(state, parameters, triggers, SequencePattern {}, transport, nframes, sampleRate, audio, samplePlayback);
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& parameters,
                          const Triggers& triggers,
                          const SequencePattern& sequence,
                          const TransportSnapshot& transport,
                          const std::uint32_t nframes,
                          const double sampleRate,
                          const AudioBlock& audio,
                          const SamplePlayback& samplePlayback) {
    if (samplePlayback.mode == InputSourceMode::LiveInput) {
        return processBlock(state, parameters, triggers, sequence, transport, nframes, sampleRate, audio);
    }

    const std::uint32_t channelCount = std::max<std::uint32_t>(1u, std::min(audio.channelCount, kMaxChannels));
    // Allocate one extra nframes block of zeros at the end of the scratch buffer to serve as a
    // silent dry-input pointer for channels that have no live audio in Sample mode.
    const std::size_t scratchFrames = static_cast<std::size_t>(nframes) * channelCount;
    state.sourceInputScratch.assign(scratchFrames + static_cast<std::size_t>(nframes), 0.0f);
    const float* const silentDry = state.sourceInputScratch.data() + scratchFrames;

    for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
        for (std::uint32_t frame = 0; frame < nframes; ++frame) {
            float value = renderSamplePlaybackFrame(samplePlayback, transport, sampleRate, frame, channel);
            if (samplePlayback.mode == InputSourceMode::LiveAndSample) {
                const float* in = audio.inputs[channel];
                value += in ? in[frame] : 0.0f;
            }
            state.sourceInputScratch[static_cast<std::size_t>(channel) * nframes + frame] = value;
        }
    }

    AudioBlock sourcedAudio;
    sourcedAudio.outputs = audio.outputs;
    sourcedAudio.channelCount = channelCount;
    for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
        sourcedAudio.inputs[channel] = state.sourceInputScratch.data() + static_cast<std::size_t>(channel) * nframes;
        // Use the caller's live audio as the dry pass-through; fall back to the silent buffer
        // so that dryInputFrame does not pick up the sample scratch as the dry signal.
        sourcedAudio.dryInputs[channel] = audio.inputs[channel] ? audio.inputs[channel] : silentDry;
    }

    return processBlock(state, parameters, triggers, sequence, transport, nframes, sampleRate, sourcedAudio);
}

}  // namespace downspout::rift
