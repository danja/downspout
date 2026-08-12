#pragma once

#include <algorithm>
#include <cmath>

namespace flues::pm {

class FilterModule {
public:
    explicit FilterModule(float sampleRate = 44100.0f)
        : sampleRate(sampleRate),
          frequency(1000.0f),
          q(1.0f),
          shape(0.0f),
          low(0.0f),
          band(0.0f),
          high(0.0f) {}

    void setFrequency(float value) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        const float newFreq = 20.0f * std::pow(1000.0f, clamped);
        if (newFreq != frequency) {
            frequency = newFreq;
            coeffDirty = true;
        }
    }

    void setQ(float value) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        const float newQ = 0.5f * std::pow(40.0f, clamped);
        if (newQ != q) {
            q = newQ;
            coeffDirty = true;
        }
    }

    void setShape(float value) {
        shape = std::clamp(value, 0.0f, 1.0f);
    }

    float process(float input) {
        if (std::fabs(input) < 1.0e-18f) {
            input = 0.0f;
        }

        if (coeffDirty) {
            cachedF = 2.0f * std::sin(static_cast<float>(M_PI) * frequency / sampleRate);
            cachedQInv = 1.0f / std::max(0.5f, q);
            coeffDirty = false;
        }
        const float f = cachedF;
        const float qInv = cachedQInv;

        low += f * band;
        high = input - low - qInv * band;
        band = f * high + band;

        if (!std::isfinite(low)) low = 0.0f;
        if (!std::isfinite(band)) band = 0.0f;
        if (!std::isfinite(high)) high = 0.0f;
        if (std::fabs(low) < 1.0e-18f) low = 0.0f;
        if (std::fabs(band) < 1.0e-18f) band = 0.0f;
        if (std::fabs(high) < 1.0e-18f) high = 0.0f;

        float output = 0.0f;
        if (shape < 0.5f) {
            const float mix = shape * 2.0f;
            output = low * (1.0f - mix) + band * mix;
        } else {
            const float mix = (shape - 0.5f) * 2.0f;
            output = band * (1.0f - mix) + high * mix;
        }

        return std::fabs(output) < 1.0e-18f ? 0.0f : output;
    }

    void reset() {
        low = 0.0f;
        band = 0.0f;
        high = 0.0f;
    }

private:
    float sampleRate;
    float frequency;
    float q;
    float shape;
    float low;
    float band;
    float high;
    float cachedF = 0.0f;
    float cachedQInv = 2.0f;
    bool coeffDirty = true;
};

} // namespace flues::pm
