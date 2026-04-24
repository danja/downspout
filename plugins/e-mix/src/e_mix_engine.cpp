#include "e_mix_engine.hpp"

#include <cmath>

namespace downspout::emix {
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

[[nodiscard]] bool euclidHit(int step, int pulses, int offset, int length) {
    if (length <= 0 || pulses <= 0) {
        return false;
    }
    if (pulses >= length) {
        return true;
    }

    const int base = (step + (offset % length)) % length;
    return ((base * pulses) % length) < pulses;
}

}  // namespace

Parameters clampParameters(const Parameters& raw) {
    Parameters parameters = raw;
    parameters.totalBars = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.totalBars)), 1, 4096));
    parameters.division = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.division)), 1, 512));

    const int division = static_cast<int>(parameters.division);
    parameters.steps = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.steps)), 0, division));
    parameters.offset = static_cast<float>(clampi(static_cast<int>(std::lround(parameters.offset)), 0, division - 1));
    parameters.fadeBars = clampf(parameters.fadeBars, 0.0f, parameters.totalBars);
    return parameters;
}

bool blockIsActive(const Parameters& rawParameters, int blockIndex) {
    const Parameters parameters = clampParameters(rawParameters);
    const int division = static_cast<int>(parameters.division);
    if (division <= 0) {
        return false;
    }

    const int wrappedIndex = ((blockIndex % division) + division) % division;
    return euclidHit(wrappedIndex,
                     static_cast<int>(parameters.steps),
                     static_cast<int>(parameters.offset),
                     division);
}

void activate(EngineState& state) {
    state.transportWasPlaying = false;
    state.cycleOriginBar = 0.0;
    state.fallbackBarPos = 0.0;
    state.fallbackBpm = 120.0;
    state.fallbackBeatsPerBar = 4.0;
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
    const int totalBars = static_cast<int>(parameters.totalBars);
    const int division = static_cast<int>(parameters.division);
    const int steps = static_cast<int>(parameters.steps);
    const int offset = static_cast<int>(parameters.offset);
    const float fadeBars = parameters.fadeBars;

    double barPos = 0.0;
    double barStep = 0.0;

    const bool hostPlaying = transport.valid &&
                             transport.playing &&
                             transport.bpm > 0.0 &&
                             transport.beatsPerBar > 0.0 &&
                             sampleRate > 0.0;
    if (hostPlaying) {
        barPos = transport.bar + (transport.barBeat / transport.beatsPerBar);
        barStep = transport.bpm / (60.0 * sampleRate * transport.beatsPerBar);
        state.fallbackBpm = transport.bpm;
        state.fallbackBeatsPerBar = transport.beatsPerBar;
        state.fallbackBarPos = barPos;

        if (!state.transportWasPlaying) {
            state.cycleOriginBar = barPos;
        }
        state.transportWasPlaying = true;
    } else {
        state.transportWasPlaying = false;
        barPos = state.fallbackBarPos;
        if (sampleRate > 0.0 && state.fallbackBeatsPerBar > 0.0) {
            barStep = state.fallbackBpm / (60.0 * sampleRate * state.fallbackBeatsPerBar);
        }
    }

    const double cycleBars = static_cast<double>(totalBars);
    const double blockBars = cycleBars / static_cast<double>(division);
    const std::uint32_t channelCount = audio.channelCount < kMaxChannels ? audio.channelCount : kMaxChannels;

    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        float gain = 1.0f;

        const double relBar = barPos - state.cycleOriginBar;
        double cyclePos = std::fmod(relBar, cycleBars);
        if (cyclePos < 0.0) {
            cyclePos += cycleBars;
        }

        int blockIndex = static_cast<int>(std::floor(cyclePos / blockBars));
        blockIndex = clampi(blockIndex, 0, division - 1);

        const bool active = euclidHit(blockIndex, steps, offset, division);
        if (!active) {
            gain = 0.0f;
        } else if (fadeBars > 0.0f) {
            const double blockStart = static_cast<double>(blockIndex) * blockBars;
            const double localBar = cyclePos - blockStart;
            const double fade = static_cast<double>(fadeBars);
            const float inGain = clampf(static_cast<float>(localBar / fade), 0.0f, 1.0f);
            const float outGain = clampf(static_cast<float>((blockBars - localBar) / fade), 0.0f, 1.0f);
            gain = inGain < outGain ? inGain : outGain;
        }

        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            float* out = audio.outputs[channel];
            if (!out) {
                continue;
            }
            const float* in = audio.inputs[channel];
            const float sample = in ? in[frame] : 0.0f;
            out[frame] = sample * gain;
        }

        barPos += barStep;
    }

    state.fallbackBarPos = barPos;
}

}  // namespace downspout::emix
