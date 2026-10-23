#pragma once

#include <array>
#include <cstdint>

namespace downspout::ladder {

// Non-linear zero-delay-feedback 4-pole low-pass ladder (Mystran's 2x-oversampled
// topology, via oxide-studio's oxide-dsp MystranZdfLadder). Probed against the
// analytic response before porting; see docs/design.md.

struct Parameters {
    float cutoffHz   = 1000.0f;  // 20-18000 Hz
    float resonance  = 20.0f;    // 0-100 %; taper is roughly linear in peak dB, self-oscillates near 85 %
    float driveDb    = 0.0f;     // 0-24 dB gain into the first saturator
    float bassComp   = 50.0f;    // 0-100 %; how much of the resonance bass loss is restored
    float mix        = 100.0f;   // 0-100 % wet
    float outputDb   = 0.0f;     // -24 to +12 dB
};

// Feedback amount k and the prewarped integrator gain f; both are interpolated
// across a block so cutoff and resonance sweeps do not zipper.
struct Coefficients {
    float f = 0.0f;
    float k = 0.0f;
};

struct ChannelState {
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
    float prevInput = 0.0f;
};

// Linear gains interpolated across each block, like the coefficients.
struct Gains {
    float drive = 1.0f, out = 1.0f, wet = 1.0f, comp = 0.0f;
};

struct EngineState {
    std::array<ChannelState, 2> channels {};
    Coefficients current {};
    Gains gains {};
    bool primed = false;  // first block snaps to its target instead of gliding from zero
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Resonance control (0-1) to feedback amount k. k >= 4 self-oscillates.
[[nodiscard]] float resonanceToFeedback(float resonance01) noexcept;
[[nodiscard]] Coefficients makeCoefficients(float cutoffHz, float resonance01, float sampleRate) noexcept;

// Small-signal magnitude response in dB at `hz`, from the linear model (four one-poles in a
// feedback loop, bass compensation, mix and output). Drive is left out because it is
// non-linear. This is what the panel plots; tests hold it against the measured filter.
[[nodiscard]] double responseDb(const Parameters& p, double hz) noexcept;

// Clears the filter memory only; never touches the patch.
void resetState(EngineState& state) noexcept;

// inputs and outputs are stereo; a null right input is treated as the left.
void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  uint32_t          frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept;

}  // namespace downspout::ladder
