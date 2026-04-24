#include "p_mix_engine.hpp"

#include <cmath>

namespace downspout::pmix {
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

[[nodiscard]] float randFloat(std::uint32_t& state) {
    state = (state * 1664525u) + 1013904223u;
    return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void startFade(EngineState& state,
               float targetGain,
               int granularity,
               double framesPerBar,
               float fadeDurMax) {
    if (framesPerBar <= 0.0) {
        return;
    }

    const float maxFraction = clampf(fadeDurMax, kFadeMinFraction, 1.0f);
    float fraction = kFadeMinFraction;
    if (maxFraction > kFadeMinFraction) {
        fraction = kFadeMinFraction + randFloat(state.rngState) * (maxFraction - kFadeMinFraction);
    }

    const double frames = framesPerBar * static_cast<double>(granularity) * static_cast<double>(fraction);
    const std::uint32_t totalFrames = frames > 1.0 ? static_cast<std::uint32_t>(std::llround(frames)) : 1u;
    state.fadeRemaining = totalFrames;
    state.targetGain = targetGain;
    state.fadeStep = (targetGain - state.currentGain) / static_cast<float>(totalFrames);
}

void applyTransition(EngineState& state,
                     std::int64_t bar,
                     int granularity,
                     double framesPerBar,
                     float maintain,
                     float fade,
                     float cut,
                     float fadeDurMax,
                     float bias) {
    if (granularity <= 0 || (bar % granularity) != 0 || state.fadeRemaining > 0) {
        return;
    }

    float gain = state.currentGain;
    if (gain > 0.001f && gain < 0.999f) {
        gain = (gain < 0.5f) ? 0.0f : 1.0f;
        state.currentGain = gain;
    }

    float pMaintain = maintain;
    float pFade = fade;
    float pCut = cut;
    float sum = pMaintain + pFade + pCut;
    if (sum <= 0.0001f) {
        pMaintain = 1.0f;
        pFade = 0.0f;
        pCut = 0.0f;
        sum = 1.0f;
    }
    pMaintain /= sum;
    pFade /= sum;
    pCut /= sum;

    const float r = randFloat(state.rngState);
    if (r < pMaintain) {
        return;
    }

    const float biasNorm = clampf(bias * 0.01f, 0.0f, 1.0f);
    const float target = (randFloat(state.rngState) < biasNorm) ? 1.0f : 0.0f;

    if (r < (pMaintain + pFade)) {
        startFade(state, target, granularity, framesPerBar, fadeDurMax);
    } else {
        state.currentGain = target;
        state.targetGain = target;
        state.fadeRemaining = 0;
        state.fadeStep = 0.0f;
    }
}

[[nodiscard]] int collectBoundaries(BoundaryList& boundaries,
                                    const TransportSnapshot& transport,
                                    std::uint32_t nframes,
                                    double framesPerBar) {
    if (!(transport.valid && transport.playing && framesPerBar > 0.0)) {
        return 0;
    }

    constexpr double eps = 1e-6;
    int count = 0;
    std::int64_t bar = static_cast<std::int64_t>(std::llround(transport.bar));
    double barBeat = transport.barBeat < 0.0 ? 0.0 : transport.barBeat;

    if (barBeat <= eps && count < static_cast<int>(boundaries.size())) {
        boundaries[count++] = Boundary{0u, bar};
    }

    double framesToNext = (transport.beatsPerBar - barBeat) * (framesPerBar / transport.beatsPerBar);
    if (framesToNext <= eps) {
        framesToNext = framesPerBar;
    }

    double framePos = framesToNext;
    while (framePos < static_cast<double>(nframes) && count < static_cast<int>(boundaries.size())) {
        bar += 1;
        boundaries[count++] = Boundary{static_cast<std::uint32_t>(std::lround(framePos)), bar};
        framePos += framesPerBar;
    }

    return count;
}

}  // namespace

Parameters clampParameters(const Parameters& raw) {
    Parameters parameters = raw;
    parameters.granularity = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.granularity)), 1, 32));
    parameters.maintain = clampf(parameters.maintain, 0.0f, 100.0f);
    parameters.fade = clampf(parameters.fade, 0.0f, 100.0f);
    parameters.cut = clampf(parameters.cut, 0.0f, 100.0f);
    parameters.fadeDurMax = clampf(parameters.fadeDurMax, kFadeMinFraction, 1.0f);
    parameters.bias = clampf(parameters.bias, 0.0f, 100.0f);
    parameters.mute = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.mute)), 0, 1));
    return parameters;
}

void activate(EngineState& state) {
    state.currentGain = 1.0f;
    state.targetGain = 1.0f;
    state.fadeStep = 0.0f;
    state.fadeRemaining = 0;
}

void processBlock(EngineState& state,
                  const Parameters& rawParameters,
                  const TransportSnapshot& transport,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio) {
    if (nframes == 0) {
        return;
    }

    const Parameters parameters = clampParameters(rawParameters);
    const bool muted = parameters.mute >= 0.5f;

    double framesPerBar = 0.0;
    if (transport.valid && transport.playing && transport.bpm > 0.0) {
        const double framesPerBeat = sampleRate * 60.0 / transport.bpm;
        framesPerBar = framesPerBeat * transport.beatsPerBar;
    }

    BoundaryList boundaries {};
    const int boundaryCount = collectBoundaries(boundaries, transport, nframes, framesPerBar);
    int boundaryIndex = 0;

    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        if (boundaryIndex < boundaryCount && boundaries[boundaryIndex].frame == frame) {
            applyTransition(state,
                            boundaries[boundaryIndex].bar,
                            static_cast<int>(parameters.granularity),
                            framesPerBar,
                            parameters.maintain,
                            parameters.fade,
                            parameters.cut,
                            parameters.fadeDurMax,
                            parameters.bias);
            ++boundaryIndex;
        }

        if (state.fadeRemaining > 0) {
            state.currentGain += state.fadeStep;
            state.fadeRemaining -= 1;
            if (state.fadeRemaining == 0) {
                state.currentGain = state.targetGain;
                state.fadeStep = 0.0f;
            }
        }

        const float gain = muted ? 0.0f : state.currentGain;
        const std::uint32_t channelCount = audio.channelCount < kMaxChannels ? audio.channelCount : kMaxChannels;
        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            float* out = audio.outputs[channel];
            if (!out) {
                continue;
            }
            const float* in = audio.inputs[channel];
            const float sample = in ? in[frame] : 0.0f;
            out[frame] = sample * gain;
        }
    }
}

}  // namespace downspout::pmix
