#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::skream {

// ADAA2-based anti-aliased tanh state (doubles required for numerical precision)
struct TanhAdaa2 {
    double x1 = 0.0, x2 = 0.0;
    double d2 = 0.0;
    double ad2x0 = 0.0, ad2x1 = 0.0;
};

struct ChannelState {
    TanhAdaa2 tanh1{};       // feedforward distortion
    TanhAdaa2 tanh2{};       // feedback distortion
    float fbYn1    = 0.0f;   // feedback delay (1 sample)
    float peakXn1  = 0.0f;   // peak detector for feedback gate
    float lpState[2] = {};   // SVF lowpass state
    float hpState[2] = {};   // SVF highpass state
};

struct EngineState {
    std::array<ChannelState, 2> channels{};
};

struct Parameters {
    float inputGain  =  0.0f;   // -24 to +24 dB
    float cutoff     = 85.0f;   // 0-100 % (LP frequency)
    float scream     = 46.5f;   // 0-100 % (HP feedback cutoff)
    float resonance  = 100.0f;  // 0-100 % (feedback gain + HP Q)
    float mix        = 100.0f;  // 0-100 % dry/wet
    float outputGain = -6.0f;   // -24 to 0 dB
    float ccCutoff   =  0.0f;   // 0-127 CC# for cutoff (0 = off)
    float ccScream   =  0.0f;   // 0-127 CC# for scream (0 = off)
    float ccChannel  =  1.0f;   // 1-16 MIDI channel
};

struct AudioBlock {
    std::array<const float*, 2> inputs{};
    std::array<float*, 2>       outputs{};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

void processBlock(EngineState&      state,
                  const Parameters& params,
                  uint32_t          frames,
                  double            sampleRate,
                  const AudioBlock& audio,
                  float             effectiveCutoff,
                  float             effectiveScream) noexcept;

[[nodiscard]] std::string               serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::skream
