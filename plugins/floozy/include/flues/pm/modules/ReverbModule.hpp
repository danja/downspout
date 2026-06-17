#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <array>

namespace flues::pm {

class ReverbModule {
public:
    explicit ReverbModule(float sampleRate = 44100.0f)
        : sampleRate(sampleRate),
          size(0.5f),
          level(0.3f),
          combDelays{
              static_cast<std::size_t>(0.0297f * sampleRate),
              static_cast<std::size_t>(0.0371f * sampleRate),
              static_cast<std::size_t>(0.0411f * sampleRate),
              static_cast<std::size_t>(0.0437f * sampleRate)
          },
          allpassDelays{
              static_cast<std::size_t>(0.005f * sampleRate),
              static_cast<std::size_t>(0.0017f * sampleRate)
          },
          combBuffers{
              std::vector<float>(combDelays[0], 0.0f),
              std::vector<float>(combDelays[1], 0.0f),
              std::vector<float>(combDelays[2], 0.0f),
              std::vector<float>(combDelays[3], 0.0f)
          },
          combIndices{0, 0, 0, 0},
          allpassBuffers{
              std::vector<float>(allpassDelays[0], 0.0f),
              std::vector<float>(allpassDelays[1], 0.0f)
          },
          allpassIndices{0, 0} {}

    void setSize(float value) {
        size = std::clamp(value, 0.0f, 1.0f);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.0f);
    }

    float process(float input) {
        if (std::fabs(input) < 1.0e-18f) {
            input = 0.0f;
        }

        float combSum = 0.0f;
        const float feedback = 0.7f + size * 0.28f;

        for (std::size_t i = 0; i < combBuffers.size(); ++i) {
            auto& buffer = combBuffers[i];
            const std::size_t index = combIndices[i];
            const std::size_t delay = combDelays[i];

            const float delayed = buffer[index];
            const float write = input + delayed * feedback;
            buffer[index] = std::fabs(write) < 1.0e-18f ? 0.0f : write;
            combSum += delayed;

            combIndices[i] = (index + 1) % delay;
        }

        float output = combSum / static_cast<float>(combBuffers.size());

        for (std::size_t i = 0; i < allpassBuffers.size(); ++i) {
            auto& buffer = allpassBuffers[i];
            const std::size_t index = allpassIndices[i];
            const std::size_t delay = allpassDelays[i];

            const float delayed = buffer[index];
            const float g = 0.5f;
            const float newOutput = -output * g + delayed;
            const float write = output + delayed * g;
            buffer[index] = std::fabs(write) < 1.0e-18f ? 0.0f : write;
            output = newOutput;

            allpassIndices[i] = (index + 1) % delay;
        }

        const float mixed = input * (1.0f - level) + output * level;
        return std::fabs(mixed) < 1.0e-18f ? 0.0f : mixed;
    }

    void reset() {
        for (auto& buffer : combBuffers) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        for (auto& buffer : allpassBuffers) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        std::fill(combIndices.begin(), combIndices.end(), 0);
        std::fill(allpassIndices.begin(), allpassIndices.end(), 0);
    }

private:
    float sampleRate;
    float size;
    float level;

    std::array<std::size_t, 4> combDelays;
    std::array<std::size_t, 2> allpassDelays;
    std::array<std::vector<float>, 4> combBuffers;
    std::array<std::size_t, 4> combIndices;
    std::array<std::vector<float>, 2> allpassBuffers;
    std::array<std::size_t, 2> allpassIndices;
};

} // namespace flues::pm
