#include "t_mix_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::tmix {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr double kMeterReleaseSeconds = 0.3;

[[nodiscard]] float clampf(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] bool enabled(float value)
{
    return value >= 0.5f;
}

}  // namespace

Parameters clampParameters(const Parameters& raw)
{
    Parameters parameters = raw;
    for (ChannelParameters& channel : parameters.channels) {
        channel.levelDb = clampf(channel.levelDb, kMinimumLevelDb, kMaximumLevelDb);
        channel.pan = clampf(channel.pan, -1.0f, 1.0f);
        channel.mute = enabled(channel.mute) ? 1.0f : 0.0f;
        channel.solo = enabled(channel.solo) ? 1.0f : 0.0f;
    }
    parameters.masterDb = clampf(parameters.masterDb, kMinimumLevelDb, kMaximumLevelDb);
    return parameters;
}

float decibelsToGain(float decibels)
{
    if (decibels <= kMinimumLevelDb)
        return 0.0f;
    return std::pow(10.0f, decibels / 20.0f);
}

void activate(EngineState& state)
{
    state.meters.fill(0.0f);
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& rawParameters,
                          std::uint32_t frameCount,
                          double sampleRate,
                          const AudioBlock& audio)
{
    const Parameters parameters = clampParameters(rawParameters);
    const bool anySolo = std::any_of(parameters.channels.begin(),
                                     parameters.channels.end(),
                                     [](const ChannelParameters& channel) {
                                         return enabled(channel.solo);
                                     });

    std::array<float, kInputChannelCount> leftGains {};
    std::array<float, kInputChannelCount> rightGains {};
    for (std::uint32_t channel = 0; channel < kInputChannelCount; ++channel) {
        const ChannelParameters& strip = parameters.channels[channel];
        if (enabled(strip.mute) || (anySolo && !enabled(strip.solo)))
            continue;

        const float angle = (strip.pan + 1.0f) * (kPi * 0.25f);
        const float level = decibelsToGain(strip.levelDb);
        leftGains[channel] = level * std::cos(angle);
        rightGains[channel] = level * std::sin(angle);
    }

    std::array<float, kInputChannelCount> peaks {};
    const float masterGain = decibelsToGain(parameters.masterDb);
    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        for (std::uint32_t channel = 0; channel < kInputChannelCount; ++channel) {
            const float* input = audio.inputs[channel];
            const float sample = input != nullptr ? input[frame] : 0.0f;
            peaks[channel] = std::max(peaks[channel], std::fabs(sample));
            left += sample * leftGains[channel];
            right += sample * rightGains[channel];
        }
        if (audio.outputs[0] != nullptr)
            audio.outputs[0][frame] = left * masterGain;
        if (audio.outputs[1] != nullptr)
            audio.outputs[1][frame] = right * masterGain;
    }

    const float release = sampleRate > 0.0
        ? static_cast<float>(std::exp(-static_cast<double>(frameCount)
                                      / (sampleRate * kMeterReleaseSeconds)))
        : 0.0f;
    for (std::uint32_t channel = 0; channel < kInputChannelCount; ++channel) {
        state.meters[channel] = std::min(1.0f,
                                         std::max(peaks[channel],
                                                  state.meters[channel] * release));
    }

    return {state.meters};
}

}  // namespace downspout::tmix
