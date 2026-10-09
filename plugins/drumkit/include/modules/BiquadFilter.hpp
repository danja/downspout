// BiquadFilter.hpp
// Versatile biquad filter supporting bandpass and highpass modes
// Used for snare resonators, hi-hat filtering, crash shaping

#ifndef BIQUAD_FILTER_HPP
#define BIQUAD_FILTER_HPP

#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class BiquadFilter {
public:
    enum class Type {
        Bandpass,
        Highpass
    };

private:
    static constexpr float TWO_PI = 6.28318530718f;

    float sampleRate;
    Type filterType;
    float frequency;     // Cutoff/center frequency (Hz)
    float Q;             // Quality factor

    // Biquad coefficients
    float a0, a1, a2;
    float b1, b2;

    // Filter state (delay line)
    float x1, x2;        // Input history
    float y1, y2;        // Output history

    /**
     * Update biquad coefficients based on current parameters
     */
    void updateCoefficients() {
        const float omega = TWO_PI * std::clamp(frequency, 20.0f, sampleRate * 0.49f) / sampleRate;
        const float sn = std::sin(omega);
        const float cs = std::cos(omega);
        const float alpha = sn / (2.0f * std::max(0.5f, Q));

        float a0_raw, a1_raw, a2_raw, b0, b1_raw, b2_raw;

        switch (filterType) {
            case Type::Bandpass:
            default:
                // Bandpass (constant 0 dB peak gain)
                a0_raw = 1.0f + alpha;
                a1_raw = -2.0f * cs;
                a2_raw = 1.0f - alpha;
                b0 = Q * alpha;
                b1_raw = 0.0f;
                b2_raw = -Q * alpha;
                break;

            case Type::Highpass:
                // Highpass (constant 0 dB passband gain)
                a0_raw = 1.0f + alpha;
                a1_raw = -2.0f * cs;
                a2_raw = 1.0f - alpha;
                b0 = (1.0f + cs) / 2.0f;
                b1_raw = -(1.0f + cs);
                b2_raw = (1.0f + cs) / 2.0f;
                break;
        }

        // Normalize coefficients
        a0 = b0 / a0_raw;
        a1 = b1_raw / a0_raw;
        a2 = b2_raw / a0_raw;
        b1 = a1_raw / a0_raw;
        b2 = a2_raw / a0_raw;
    }

public:
    explicit BiquadFilter(float sampleRate = 48000.0f, Type type = Type::Bandpass)
        : sampleRate(sampleRate)
        , filterType(type)
        , frequency(1000.0f)
        , Q(1.0f)
        , a0(1.0f), a1(0.0f), a2(0.0f)
        , b1(0.0f), b2(0.0f)
        , x1(0.0f), x2(0.0f)
        , y1(0.0f), y2(0.0f)
    {
        updateCoefficients();
    }

    /**
     * Set filter type
     */
    void setType(Type type) {
        filterType = type;
        updateCoefficients();
    }

    /**
     * Set center/cutoff frequency
     */
    void setFrequency(float freq) {
        frequency = freq;
        updateCoefficients();
    }

    /**
     * Set Q (quality factor)
     * Higher Q = narrower bandwidth (for BP) or steeper slope (for HP)
     */
    void setQ(float q) {
        Q = q;
        updateCoefficients();
    }

    /**
     * Set frequency and Q together (more efficient)
     */
    void setParameters(float freq, float q) {
        frequency = freq;
        Q = q;
        updateCoefficients();
    }

    /**
     * Reset filter state (clear delay line)
     */
    void reset() {
        x1 = x2 = 0.0f;
        y1 = y2 = 0.0f;
    }

    /**
     * Process one sample through the filter
     */
    float process(float input) {
        // Biquad difference equation:
        // y[n] = a0*x[n] + a1*x[n-1] + a2*x[n-2] - b1*y[n-1] - b2*y[n-2]
        const float output = a0 * input + a1 * x1 + a2 * x2 - b1 * y1 - b2 * y2;

        // Update delay line
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;

        // Stability check (prevent NaN/Inf)
        if (!std::isfinite(output)) {
            reset();
            return 0.0f;
        }

        return output;
    }
};

} // namespace downspout::drumkit

#endif // BIQUAD_FILTER_HPP
