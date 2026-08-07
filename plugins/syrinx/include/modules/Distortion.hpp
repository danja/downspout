// Distortion.hpp
// Tanh soft clipping for harmonic saturation
// Used for kick drive and master distortion

#ifndef SYRINX_DISTORTION_HPP
#define SYRINX_DISTORTION_HPP

#include <algorithm>
#include <cmath>

namespace downspout::syrinx {

class Distortion {
private:
    float drive;         // Gain applied before clipping (1.0-10.0)
    float outputGain;    // Compensating output gain

public:
    Distortion()
        : drive(1.0f)
        , outputGain(1.0f)
    {}

    /**
     * Set drive amount
     * @param d - Drive gain (1.0 = clean, 10.0 = heavy saturation)
     */
    void setDrive(float d) {
        drive = std::clamp(d, 1.0f, 10.0f);
        // Compensate output gain to maintain perceived loudness
        outputGain = 1.0f / std::tanh(drive);
    }

    /**
     * Process one sample through tanh waveshaping
     */
    float process(float input) {
        if (drive <= 1.001f) {
            return input;  // Bypass if drive is minimal
        }

        // Apply drive, soft clip with tanh, compensate gain
        return std::tanh(input * drive) * outputGain;
    }
};

} // namespace downspout::syrinx

#endif // SYRINX_DISTORTION_HPP
