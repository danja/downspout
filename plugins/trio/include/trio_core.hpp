#pragma once

#include <array>
#include <cstdint>

namespace downspout::trio {

// Three-band dynamics: each band compresses downward above a threshold and lifts quiet signals
// upward below another. Inspired by the Multiband Dynamics in oxide-studio's oxide-fx, but a
// redesign (see docs/design.md): the source shares one envelope between the two channels of a
// band and updates it twice per sample.

inline constexpr int kBands = 3;

struct Band {
    float on          = 1.0f;     // 0 off (the band passes through), 1 on
    float downThreshDb = -24.0f;  // -60..0 dB: compress above this
    float downRatio    = 4.0f;    // 1..20 : 1
    float upThreshDb   = -36.0f;  // -80..0 dB: lift below this
    float upRatio      = 2.5f;    // 1..10 : 1 (1 = no lift)
    float attackMs     = 10.0f;   // 0.5..200 ms
    float releaseMs    = 100.0f;  // 5..2000 ms
    float gainDb       = 0.0f;    // -18..+18 dB band trim
};

struct Parameters {
    float inputDb     = 0.0f;     // -12..+24 dB into the dynamics (the dry path is untouched)
    float outputDb    = 0.0f;     // -24..+12 dB
    float amount      = 100.0f;   // 0..100 % wet
    float time        = 1.0f;     // 0.1..10, scales every attack and release
    float upLimitDb   = 18.0f;    // 0..24 dB cap on the upward lift
    float crossLowHz  = 250.0f;   // 40..1000
    float crossHighHz = 2500.0f;  // 1000..12000 (kept at least 1.25 x the low crossover)
    std::array<Band, kBands> bands {};
    Parameters();                 // the three default bands
};

// Measured from the audio; the panel reads these.
struct Meters {
    std::array<float, kBands> gainDb {};                  // dynamic gain of each band, - is reduction, + is lift
    std::array<float, kBands> levelDb {-120.0f, -120.0f, -120.0f};  // band level
};

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct Cell {
    double z1 = 0.0, z2 = 0.0;
};

// Fourth-order Linkwitz-Riley: two identical Butterworth cells.
struct Lr4 {
    std::array<Cell, 2> cell {};
};

struct ChannelState {
    Lr4 lowLp, lowHp;          // split at the low crossover
    Lr4 midLp, highHp;         // split the rest at the high crossover
    Lr4 lowApLp, lowApHp;      // all-pass (LP + HP) of the high crossover, to keep the low band in phase
};

struct EngineState {
    std::array<ChannelState, 2> channels {};
    Biquad lowLp, lowHp, highLp, highHp;   // coefficients in use
    float lastLow = 0.0f, lastHigh = 0.0f;
    std::array<float, kBands> envelopeDb {};
    std::array<float, kBands> detector {};   // peak-hold level of each band (linear)
    float inputLinear = 1.0f, outputLinear = 1.0f, amount = 1.0f;
    bool primed = false;
    Meters meters {};
};

[[nodiscard]] Band clampBand(const Band& b) noexcept;
[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Target dynamic gain in dB for a band level in dB: downward reduction above the down
// threshold, upward lift (capped at upLimitDb) below the up threshold, nothing in between.
// The tests hold the audio to this and the panel does not need to guess.
[[nodiscard]] float bandTargetDb(const Band& b, float levelDb, float upLimitDb) noexcept;

// Clears detector and filter memory only; never touches the patch.
void resetState(EngineState& state) noexcept;

void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  uint32_t          frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept;

}  // namespace downspout::trio
