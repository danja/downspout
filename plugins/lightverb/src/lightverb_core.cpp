#include "lightverb_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::lightverb {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr std::array<std::uint32_t, kDelayLineCount> kBaseFrames {{1493, 1601, 1747, 1867}};

float pv(const Parameters& parameters, const Parameter parameter) noexcept
{
    return downspout::generative::clampParam(parameters.values[parameter],
                                             kParameterSpecs[parameter]);
}

float safe(const float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}

float bounded(const float value) noexcept
{
    return std::tanh(std::clamp(safe(value), -4.0f, 4.0f));
}

float gainFromDb(const float db) noexcept { return std::pow(10.0f, db / 20.0f); }
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
        result.values[index] = downspout::generative::clampParam(raw.values[index],
                                                                 kParameterSpecs[index]);
    return result;
}

void prepare(State& state, double sampleRate)
{
    sampleRate = std::clamp(sampleRate, 8000.0, 384000.0);
    if (std::fabs(state.sampleRate - sampleRate) < 0.001 && !state.delays[0].empty())
        return;
    state.sampleRate = sampleRate;
    const double scale = sampleRate / 48000.0;
    for (std::size_t index = 0; index < kDelayLineCount; ++index) {
        const std::size_t capacity = static_cast<std::size_t>(
            std::ceil(kBaseFrames[index] * scale * 1.24)) + 4u;
        state.delays[index].assign(std::max<std::size_t>(capacity, 8u), 0.0f);
    }
    const std::size_t preDelayCapacity = static_cast<std::size_t>(
        std::ceil(sampleRate * 0.100)) + 2u;
    state.preDelayLeft.assign(preDelayCapacity, 0.0f);
    state.preDelayRight.assign(preDelayCapacity, 0.0f);
    reset(state);
}

void reset(State& state) noexcept
{
    for (auto& delay : state.delays)
        std::fill(delay.begin(), delay.end(), 0.0f);
    std::fill(state.preDelayLeft.begin(), state.preDelayLeft.end(), 0.0f);
    std::fill(state.preDelayRight.begin(), state.preDelayRight.end(), 0.0f);
    state.writeHeads.fill(0);
    state.damped.fill(0.0f);
    state.preDelayWrite = 0;
    state.smoothingInitialized = false;
    state.spaceMidiActive = false;
    state.mixMidiActive = false;
    state.spaceMidiValue = 0.0f;
    state.mixMidiValue = 0.0f;
}

void releaseMidiTakeover(State& state) noexcept
{
    state.spaceMidiActive = false;
    state.mixMidiActive = false;
}

bool handleMidi(State& state, const std::uint8_t* data, const std::uint32_t size,
                const bool enabled) noexcept
{
    if (!enabled || data == nullptr || size < 3 || (data[0] & 0xf0u) != 0xb0u)
        return false;
    const float value = static_cast<float>(data[2] & 0x7fu) / 127.0f;
    if ((data[1] & 0x7fu) == kMixController) {
        state.mixMidiValue = value;
        state.mixMidiActive = true;
        return true;
    }
    if ((data[1] & 0x7fu) == kSpaceController) {
        state.spaceMidiValue = value;
        state.spaceMidiActive = true;
        return true;
    }
    return false;
}

float effectiveSpace(const State& state, const Parameters& parameters) noexcept
{
    return state.spaceMidiActive ? state.spaceMidiValue : pv(parameters, kSpace);
}

float effectiveMix(const State& state, const Parameters& parameters) noexcept
{
    return state.mixMidiActive ? state.mixMidiValue : pv(parameters, kMix);
}

OutputStatus process(State& state, const Parameters& raw, const std::uint32_t frames,
                     const AudioBlock& audio, const MidiControlEvent* midiEvents,
                     const std::uint32_t midiEventCount) noexcept
{
    OutputStatus status;
    const Parameters parameters = clampParameters(raw);
    if (frames == 0)
        return status;
    if (state.delays[0].empty() || state.preDelayLeft.empty()) {
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            if (audio.leftOut) audio.leftOut[frame] = audio.leftIn ? safe(audio.leftIn[frame]) : 0.0f;
            if (audio.rightOut) audio.rightOut[frame] = audio.rightIn ? safe(audio.rightIn[frame]) : 0.0f;
        }
        return status;
    }

    const float decay = pv(parameters, kDecaySeconds);
    const float damping = pv(parameters, kDamping);
    const float width = pv(parameters, kWidth);
    const float trim = gainFromDb(pv(parameters, kOutputDb));
    const bool midiEnabled = pv(parameters, kMidiEnabled) >= 0.5f;
    const float controlSmoothing = 1.0f - std::exp(-1.0f / static_cast<float>(state.sampleRate * 0.080));
    const float cutoff = 1100.0f + (1.0f - damping) * (1.0f - damping) * 16900.0f;
    const float safeCutoff = std::min(cutoff, static_cast<float>(state.sampleRate * 0.45));
    const float dampingCoefficient = 1.0f - std::exp(-2.0f * kPi * safeCutoff
                                                     / static_cast<float>(state.sampleRate));
    const double rateScale = state.sampleRate / 48000.0;
    std::uint32_t eventIndex = 0;

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (midiEvents && eventIndex < midiEventCount && midiEvents[eventIndex].frame <= frame) {
            (void)handleMidi(state, midiEvents[eventIndex].data.data(),
                             midiEvents[eventIndex].size, midiEnabled);
            ++eventIndex;
        }
        const float targetSpace = effectiveSpace(state, parameters);
        const float targetMix = effectiveMix(state, parameters);
        const float targetPreDelay = pv(parameters, kPreDelayMs) * 0.001f
            * static_cast<float>(state.sampleRate);
        if (!state.smoothingInitialized) {
            state.smoothedSpace = targetSpace;
            state.smoothedMix = targetMix;
            state.smoothedPreDelayFrames = targetPreDelay;
            state.smoothingInitialized = true;
        }
        state.smoothedSpace += (targetSpace - state.smoothedSpace) * controlSmoothing;
        state.smoothedMix += (targetMix - state.smoothedMix) * controlSmoothing;
        state.smoothedPreDelayFrames += (targetPreDelay - state.smoothedPreDelayFrames) * controlSmoothing;

        const float inputLeft = audio.leftIn ? safe(audio.leftIn[frame]) : 0.0f;
        const float inputRight = audio.rightIn ? safe(audio.rightIn[frame]) : 0.0f;
        status.inputPeak = std::max(status.inputPeak,
            std::max(std::fabs(inputLeft), std::fabs(inputRight)));
        state.preDelayLeft[state.preDelayWrite] = inputLeft;
        state.preDelayRight[state.preDelayWrite] = inputRight;
        const std::uint32_t preFrames = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(std::lround(state.smoothedPreDelayFrames)),
            static_cast<std::uint32_t>(state.preDelayLeft.size() - 1u));
        const std::uint32_t preRead = (state.preDelayWrite + state.preDelayLeft.size() - preFrames)
            % static_cast<std::uint32_t>(state.preDelayLeft.size());
        const float preLeft = state.preDelayLeft[preRead];
        const float preRight = state.preDelayRight[preRead];
        state.preDelayWrite = (state.preDelayWrite + 1u)
            % static_cast<std::uint32_t>(state.preDelayLeft.size());

        std::array<float, kDelayLineCount> taps {};
        std::array<std::uint32_t, kDelayLineCount> lengths {};
        for (std::size_t index = 0; index < kDelayLineCount; ++index) {
            lengths[index] = std::clamp<std::uint32_t>(
                static_cast<std::uint32_t>(std::lround(kBaseFrames[index] * rateScale
                    * (0.72f + 0.50f * state.smoothedSpace))),
                2u, static_cast<std::uint32_t>(state.delays[index].size() - 1u));
            const std::uint32_t read = (state.writeHeads[index] + state.delays[index].size()
                                        - lengths[index])
                % static_cast<std::uint32_t>(state.delays[index].size());
            const float rawTap = state.delays[index][read];
            state.damped[index] += (rawTap - state.damped[index]) * dampingCoefficient;
            taps[index] = state.damped[index];
        }

        const std::array<float, 4> matrix {{
            0.5f * (taps[0] + taps[1] + taps[2] + taps[3]),
            0.5f * (taps[0] - taps[1] + taps[2] - taps[3]),
            0.5f * (taps[0] + taps[1] - taps[2] - taps[3]),
            0.5f * (taps[0] - taps[1] - taps[2] + taps[3]),
        }};
        const float mid = (preLeft + preRight) * 0.5f;
        const float side = (preLeft - preRight) * 0.5f;
        const std::array<float, 4> injection {{preLeft, preRight, mid + side * width, mid - side * width}};
        for (std::size_t index = 0; index < kDelayLineCount; ++index) {
            const float lineSeconds = static_cast<float>(lengths[index] / state.sampleRate);
            const float feedback = std::min(0.985f, std::pow(10.0f, -3.0f * lineSeconds / decay));
            state.delays[index][state.writeHeads[index]] = bounded(injection[index] * 0.42f
                                                                   + matrix[index] * feedback);
            state.writeHeads[index] = (state.writeHeads[index] + 1u)
                % static_cast<std::uint32_t>(state.delays[index].size());
        }

        float wetLeft = 0.5f * (taps[0] + taps[2]);
        float wetRight = 0.5f * (taps[1] - taps[3]);
        const float wetMid = (wetLeft + wetRight) * 0.5f;
        const float wetSide = (wetLeft - wetRight) * 0.5f * width;
        wetLeft = wetMid + wetSide;
        wetRight = wetMid - wetSide;
        const float dryGain = std::cos(state.smoothedMix * kPi * 0.5f);
        const float wetGain = std::sin(state.smoothedMix * kPi * 0.5f);
        const float outputLeft = std::clamp((inputLeft * dryGain + wetLeft * wetGain) * trim, -1.0f, 1.0f);
        const float outputRight = std::clamp((inputRight * dryGain + wetRight * wetGain) * trim, -1.0f, 1.0f);
        if (audio.leftOut) audio.leftOut[frame] = outputLeft;
        if (audio.rightOut) audio.rightOut[frame] = outputRight;
        status.outputPeak = std::max(status.outputPeak,
            std::max(std::fabs(outputLeft), std::fabs(outputRight)));
        status.tail = std::max(status.tail,
            std::min(1.0f, 0.5f * (std::fabs(wetLeft) + std::fabs(wetRight))));
        status.space = targetSpace;
        status.mix = targetMix;
        status.spaceMidi = state.spaceMidiActive;
        status.mixMidi = state.mixMidiActive;
    }
    return status;
}

} // namespace downspout::lightverb
