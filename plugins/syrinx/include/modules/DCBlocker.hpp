// DCBlocker.hpp
// High-pass filter to remove DC offset
// Critical for feedback loops and distortion stages

#ifndef SYRINX_DC_BLOCKER_HPP
#define SYRINX_DC_BLOCKER_HPP

namespace downspout::syrinx {

class DCBlocker {
private:
    float R;             // Pole location (0.995-0.999 typical)
    float x1;            // Previous input
    float y1;            // Previous output

public:
    explicit DCBlocker(float R = 0.999f)
        : R(R)
        , x1(0.0f)
        , y1(0.0f)
    {}

    /**
     * Set the pole location
     * @param r - Pole location (0.995-0.999), higher = lower cutoff frequency
     */
    void setPole(float r) {
        R = r;
    }

    /**
     * Reset filter state
     */
    void reset() {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    /**
     * Process one sample
     * Implements: y[n] = x[n] - x[n-1] + R * y[n-1]
     */
    float process(float input) {
        const float output = input - x1 + R * y1;
        x1 = input;
        y1 = output;
        return output;
    }
};

} // namespace downspout::syrinx

#endif // SYRINX_DC_BLOCKER_HPP
