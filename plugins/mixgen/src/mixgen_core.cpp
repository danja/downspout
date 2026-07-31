#include "mixgen_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::mixgen {
namespace {

float parameterValue(const std::array<float, kParameterCount>& parameters,
                     const std::uint32_t index) noexcept
{
    return downspout::generative::clampParam(parameters[index], kParameterSpecs[index]);
}

int wrapStep(const std::int64_t step, const int length) noexcept
{
    const std::int64_t wrapped = step % length;
    return static_cast<int>(wrapped < 0 ? wrapped + length : wrapped);
}

float fractional(const double value) noexcept
{
    return static_cast<float>(value - std::floor(value));
}

bool euclideanHit(const int step, const int pulses, const int rotation, const int length) noexcept
{
    if (pulses <= 0)
        return false;
    if (pulses >= length)
        return true;
    const int shifted = (step + rotation) % length;
    return ((shifted * pulses) % length) < pulses;
}

} // namespace

void reset(State& state) noexcept
{
    state = {};
    state.lastStep = -1;
    state.gains.fill(1.0f);
}

float gainForStep(const std::array<float, kParameterCount>& parameters,
                  const int rawLane,
                  const std::int64_t rawStep) noexcept
{
    const int lane = std::clamp(rawLane, 0, kLaneCount - 1);
    const int length = static_cast<int>(std::lround(parameterValue(parameters, kSteps)));
    const int step = wrapStep(rawStep, length);
    const int mode = static_cast<int>(std::lround(parameterValue(parameters, kMode)));
    const float density = parameterValue(parameters, kDensity);
    const float depth = parameterValue(parameters, kDepth);
    const float variation = parameterValue(parameters, kVariation);
    const float spread = parameterValue(parameters, kSpread);
    const std::uint64_t seed = static_cast<std::uint64_t>(std::lround(parameterValue(parameters, kSeed)));
    const int rotation = static_cast<int>(std::lround(
        spread * static_cast<float>(lane) * static_cast<float>(length) / kLaneCount));
    const std::uint64_t index = static_cast<std::uint64_t>(step * kLaneCount + lane);

    bool active = false;
    if (mode == 0) {
        active = downspout::generative::randomUnit(seed + lane * 101u, index) < density;
    } else if (mode == 1) {
        constexpr double golden = 0.6180339887498948482;
        const float sequence = fractional((static_cast<double>(step + rotation + 1) * golden)
            + static_cast<double>(lane) * 0.1375035237
            + static_cast<double>(seed % 997u) / 997.0);
        active = sequence < density;
    } else {
        const int pulses = std::clamp(static_cast<int>(std::lround(density * length)), 0, length);
        active = euclideanHit(step, pulses, rotation, length);
    }

    const float low = 1.0f - depth;
    if (!active)
        return low;
    const float accentRandom = downspout::generative::randomUnit(seed + 0x9e37u, index + 0x51u);
    return std::clamp(1.0f - variation * depth * 0.35f * accentRandom, low, 1.0f);
}

float fxValueForStep(const std::array<float, kParameterCount>& parameters,
                     const int rawFxLane,
                     const std::int64_t step) noexcept
{
    const int fxLane = std::clamp(rawFxLane, 0, kFxLaneCount - 1);
    const int source = std::clamp(static_cast<int>(std::lround(
        parameterValue(parameters, kFxSourceBase + fxLane))) - 1, 0, kLaneCount - 1);
    float sourceValue = gainForStep(parameters, source, step);
    if (parameterValue(parameters, kFxInvertBase + fxLane) >= 0.5f)
        sourceValue = 1.0f - sourceValue;
    const float minimum = parameterValue(parameters, kFxMinimumBase + fxLane);
    const float maximum = parameterValue(parameters, kFxMaximumBase + fxLane);
    return std::clamp(minimum + sourceValue * (maximum - minimum), 0.0f, 1.0f);
}

MidiBlock process(State& state,
                  const std::array<float, kParameterCount>& parameters,
                  const Transport& transport,
                  const std::uint32_t frames,
                  const double sampleRate) noexcept
{
    MidiBlock result;
    state.statusEvents = 0;
    if (frames == 0 || sampleRate <= 0.0)
        return result;
    const int midiChannel = static_cast<int>(std::lround(parameterValue(parameters, kMidiChannel)));
    const bool enabled = parameterValue(parameters, kEnabled) >= 0.5f;
    const auto emitRelease = [&](const int channel) {
        result.push(0, downspout::generative::ccStatus(channel), kProducerLifecycleCc, 0);
        for (int lane = 0; lane < kLaneCount; ++lane)
            result.push(0, downspout::generative::ccStatus(channel),
                        static_cast<std::uint8_t>(kTargetCcBase + lane), 127);
    };
    if (!transport.valid || !transport.playing || !enabled) {
        if (state.busActive) {
            emitRelease(state.activeChannel);
            state.statusEvents = static_cast<int>(result.count);
            state.busActive = false;
            state.gains.fill(1.0f);
        }
        state.havePosition = false;
        return result;
    }

    const double quartersPerFrame = std::clamp(transport.bpm, 1.0, 999.0) / (60.0 * sampleRate);
    const double start = downspout::generative::absoluteQuarter(transport);
    const double end = start + static_cast<double>(frames) * quartersPerFrame;
    if (downspout::generative::isDiscontinuity(state.havePosition, state.previousEnd, start))
        reset(state);

    const double rate = parameterValue(parameters, kRate);
    if (state.busActive && state.activeChannel != midiChannel) {
        emitRelease(state.activeChannel);
        state.busActive = false;
    }
    if (!state.busActive) {
        result.push(0, downspout::generative::ccStatus(midiChannel), kProducerLifecycleCc, 127);
        state.busActive = true;
        state.activeChannel = midiChannel;
    }
    const int profile = static_cast<int>(std::lround(parameterValue(parameters, kRoutingProfile)));
    const bool sendMix = profile != 1;
    const bool sendFx = profile != 0;
    std::int64_t step = static_cast<std::int64_t>(std::floor((start + 1.0e-8) / rate));
    if (step <= state.lastStep)
        step = state.lastStep + 1;

    for (;; ++step) {
        const double boundary = static_cast<double>(step) * rate;
        if (boundary >= end - 1.0e-10)
            break;
        state.lastStep = step;
        state.statusStep = wrapStep(step, static_cast<int>(std::lround(parameterValue(parameters, kSteps))));
        if (sendMix) for (int lane = 0; lane < kLaneCount; ++lane) {
            const float gain = gainForStep(parameters, lane, step);
            state.gains[static_cast<std::size_t>(lane)] = gain;
            const int value = std::clamp(static_cast<int>(std::lround(gain * 127.0f)), 0, 127);
            result.push(downspout::generative::frameAt(std::max(start, boundary), start,
                                                       quartersPerFrame, frames),
                        downspout::generative::ccStatus(midiChannel),
                        static_cast<std::uint8_t>(kTargetCcBase + lane),
                        static_cast<std::uint8_t>(value));
            ++state.statusEvents;
        }
        if (sendFx) for (int lane = 0; lane < kFxLaneCount; ++lane) {
            const float value = fxValueForStep(parameters, lane, step);
            state.fxValues[static_cast<std::size_t>(lane)] = value;
            result.push(downspout::generative::frameAt(std::max(start, boundary), start,
                                                       quartersPerFrame, frames),
                        downspout::generative::ccStatus(midiChannel),
                        static_cast<std::uint8_t>(std::lround(
                            parameterValue(parameters, kFxCcBase + lane))),
                        static_cast<std::uint8_t>(std::lround(value * 127.0f)));
            ++state.statusEvents;
        }
    }

    state.havePosition = true;
    state.previousEnd = end;
    state.statusEvents = static_cast<int>(result.count);
    return result;
}

} // namespace downspout::mixgen
