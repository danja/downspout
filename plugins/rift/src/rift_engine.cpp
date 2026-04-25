#include "rift_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::rift {
namespace {

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
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.damage * 41.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.memoryBars * 29.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround((parameters.drift + 50.0f) * 19.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround((parameters.pitch + 24.0f) * 13.0f)));
    seed ^= mixU32(static_cast<std::uint32_t>(std::lround(parameters.mix * 11.0f)));
    return seed != 0 ? seed : 1u;
}

[[nodiscard]] float actionWeightTotal(const ActionWeights& weights) {
    return weights.repeat + weights.reverse + weights.skip + weights.smear + weights.slip;
}

[[nodiscard]] ActionType chooseAction(const Parameters& parameters, std::uint32_t& rngState, const bool forceMutation) {
    const float densityNorm = clampf(parameters.density * 0.01f, 0.0f, 1.0f);
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

    const std::uint32_t required = static_cast<std::uint32_t>(std::clamp(requiredFrames, 2048.0, sampleRate * 128.0));
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

[[nodiscard]] float readBuffer(const EngineState& state,
                               const BlockSpec& block,
                               const std::uint32_t channel,
                               double position) {
    if (state.bufferFrames == 0 || state.bufferChannels == 0 || channel >= state.bufferChannels) {
        return 0.0f;
    }

    const double start = static_cast<double>(block.sourceStartFrame);
    const double length = static_cast<double>(std::max<std::uint32_t>(1u, block.sourceLengthFrames));
    position = std::fmod(position - start, length);
    if (position < 0.0) {
        position += length;
    }
    position += start;

    const std::int64_t frame0 = static_cast<std::int64_t>(std::floor(position));
    const double frac = position - static_cast<double>(frame0);
    const std::uint32_t index0 = wrapFrameIndex(state, frame0);
    const std::uint32_t index1 = wrapFrameIndex(state, frame0 + 1);

    const float sample0 = state.buffer[bufferIndex(state, channel, index0)];
    const float sample1 = state.buffer[bufferIndex(state, channel, index1)];
    return sample0 + static_cast<float>(frac) * (sample1 - sample0);
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
        state.recoverBlocksRemaining = 4;
    }
}

void setPassBlock(EngineState& state, const std::int64_t blockSerial) {
    state.activeBlock = {};
    state.activeBlock.action = ActionType::Pass;
    state.activeBlock.valid = true;
    state.activeBlockSerial = blockSerial;
}

void selectBlock(EngineState& state,
                 const Parameters& parameters,
                 const Triggers& triggers,
                 const std::int64_t blockSerial,
                 const double blockFrames,
                 const double framesPerBar) {
    armTriggers(state, triggers);

    if (state.recoverBlocksRemaining > 0) {
        setPassBlock(state, blockSerial);
        state.recoverBlocksRemaining -= 1u;
        return;
    }

    if (parameters.hold >= 0.5f && state.activeBlock.valid) {
        state.activeBlockSerial = blockSerial;
        return;
    }

    bool forceMutation = false;
    if (state.scatterBlocksRemaining > 0) {
        forceMutation = true;
        state.scatterBlocksRemaining -= 1u;
    }

    BlockSpec block {};
    block.valid = true;

    const ActionType action = chooseAction(parameters, state.rngState, forceMutation);
    if (action == ActionType::Pass) {
        setPassBlock(state, blockSerial);
        return;
    }

    const std::uint32_t sourceLength = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::llround(blockFrames)));
    const std::uint32_t memoryFrames = std::max<std::uint32_t>(
        sourceLength + 8u,
        static_cast<std::uint32_t>(std::llround(framesPerBar * std::max(1.0f, parameters.memoryBars))));
    const std::uint32_t availableHistory = std::min(state.filledFrames, memoryFrames);
    if (availableHistory <= sourceLength + 8u) {
        setPassBlock(state, blockSerial);
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
    case ActionType::Pass:
        break;
    }

    state.activeBlock = block;
    state.activeBlockSerial = blockSerial;
}

}  // namespace

Parameters clampParameters(const Parameters& raw) {
    Parameters parameters = raw;
    parameters.grid = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.grid)), 1, 16));
    parameters.density = clampf(parameters.density, 0.0f, 100.0f);
    parameters.damage = clampf(parameters.damage, 0.0f, 100.0f);
    parameters.memoryBars = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.memoryBars)), 1, 8));
    parameters.drift = clampf(parameters.drift, 0.0f, 100.0f);
    parameters.pitch = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.pitch)), -12, 12));
    parameters.mix = clampf(parameters.mix, 0.0f, 100.0f);
    parameters.hold = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.hold)), 0, 1));
    return parameters;
}

ActionWeights calculateActionWeights(const Parameters& rawParameters) {
    const Parameters parameters = clampParameters(rawParameters);
    const float damageNorm = parameters.damage * 0.01f;
    const float driftNorm = parameters.drift * 0.01f;
    const float pitchNorm = std::fabs(parameters.pitch) / 12.0f;

    ActionWeights weights;
    weights.repeat = 1.20f;
    weights.reverse = 0.35f + damageNorm * 1.10f;
    weights.skip = 0.15f + damageNorm * 0.90f;
    weights.smear = 0.35f + ((damageNorm + driftNorm) * 0.75f);
    weights.slip = 0.25f + pitchNorm * 0.90f + driftNorm * 0.75f;
    return weights;
}

ActionType previewActionForBlock(const Parameters& rawParameters, const std::uint64_t blockIndex) {
    const Parameters parameters = clampParameters(rawParameters);
    std::uint32_t rngState = seedForPreview(parameters, blockIndex);
    return chooseAction(parameters, rngState, false);
}

void activate(EngineState& state, const double sampleRate, const std::uint32_t channelCount) {
    state = {};
    ensureBuffer(state, Parameters {}, TransportSnapshot {}, sampleRate, channelCount);
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& rawParameters,
                          const Triggers& triggers,
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
        setPassBlock(state, -1);

        for (std::uint32_t frame = 0; frame < nframes; ++frame) {
            for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
                float* out = audio.outputs[channel];
                if (!out) {
                    continue;
                }
                const float* in = audio.inputs[channel];
                out[frame] = in ? in[frame] : 0.0f;
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
        selectBlock(state, parameters, triggers, currentBlockSerial, blockFrames, framesPerBar);
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
            selectBlock(state,
                        parameters,
                        triggers,
                        boundaries[boundaryIndex].blockSerial,
                        blockFrames,
                        framesPerBar);
            ++boundaryIndex;
        }

        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            float* out = audio.outputs[channel];
            if (!out) {
                continue;
            }

            const float* in = audio.inputs[channel];
            const float live = in ? in[frame] : 0.0f;
            float effected = 0.0f;

            switch (state.activeBlock.action) {
            case ActionType::Pass:
                out[frame] = live;
                continue;
            case ActionType::Skip:
                effected = 0.0f;
                break;
            case ActionType::Repeat:
            case ActionType::Reverse:
            case ActionType::Smear:
            case ActionType::Slip:
                effected = readBuffer(state, state.activeBlock, channel, state.activeBlock.readPosition);
                break;
            }

            out[frame] = live * state.activeBlock.dry + effected * state.activeBlock.wet;
        }

        if (state.activeBlock.action != ActionType::Pass && state.activeBlock.action != ActionType::Skip) {
            state.activeBlock.readPosition = wrapReadPosition(state.activeBlock,
                                                              state.activeBlock.readPosition + state.activeBlock.readRate);
        }

        writeInputFrame(state, audio, frame);
    }

    state.transportWasPlaying = true;

    OutputStatus status;
    status.action = state.activeBlock.action;
    status.activity = (state.activeBlock.action == ActionType::Pass) ? 0.0f : state.activeBlock.wet;
    return status;
}

}  // namespace downspout::rift
