#pragma once

#include "fathom_dsp.hpp"

#include <array>
#include <cstdint>

namespace downspout::fathom {

struct ProcessorControls {
    int kind = 0;        // 0 metallic, 1 diffuse
    float time = 0.4f;   // 0..1, see timeToSeconds
    float form = 0.6f;   // 0 echoes .. 1 reverb-like density and modulation
    float feed = 0.5f;   // 0..1 feedback
    float shift = 0.0f;  // -1..1, see shiftToHertz
};

// Mono in, stereo wet out. Two structures sharing one idea: a delay network with
// a frequency shifter inside the feedback path, so every repeat climbs or falls
// in pitch. Metallic is a four-line feedback delay network; Diffuse is a stereo
// ping-pong delay running through allpass diffusers.
class Processor {
public:
    // Allocates; call off the audio thread.
    void prepare(double sampleRate);
    void clear() noexcept;

    void setControls(const ProcessorControls& controls) noexcept { target_ = controls; }

    void process(float input, float& wetLeft, float& wetRight) noexcept;

private:
    void processMetallic(float input, float cosine, float sine, bool shifting, float& left, float& right) noexcept;
    void processDiffuse(float input, float cosine, float sine, bool shifting, float& left, float& right) noexcept;

    double sampleRate_ = 48000.0;
    ProcessorControls target_ {};

    // Smoothed controls.
    float time_ = 0.4f;
    float form_ = 0.6f;
    float feed_ = 0.5f;
    float controlCoefficient_ = 0.001f;
    float timeCoefficient_ = 0.0002f;

    // Shifter rotation.
    double shiftPhase_ = 0.0;

    // Metallic.
    std::array<DelayLine, 4> lines_ {};
    std::array<FrequencyShifter, 4> shifters_ {};
    std::array<float, 4> damping_ {};
    std::array<double, 4> lfoPhase_ {};
    std::array<Allpass, 2> inputDiffusers_ {};

    // Diffuse.
    std::array<DelayLine, 2> pingLines_ {};
    std::array<FrequencyShifter, 2> pingShifters_ {};
    std::array<float, 2> pingDamping_ {};
    std::array<Allpass, 4> diffusers_ {};
    std::array<Allpass, 2> loopDiffusers_ {};
};

}  // namespace downspout::fathom
