#pragma once

#include <array>
#include <cstdint>

namespace downspout::silken {

// A vocal clean-up and polish strip, in this order:
//   rumble filter -> plosive guard -> gate -> harshness tamer -> de-esser -> leveller -> voice EQ (mud, warmth, presence, air) -> drive -> sparkle -> output.
// Every stage is neutral when it is off or at zero, and the whole strip is then the input, sample for sample. No latency.
//
// Original design; the filters are the standard RBJ biquads. Levels and times are typical values chosen by ear.
//
// - Rumble filter: 2nd-order Butterworth high-pass.
// - Plosive guard: a low-pass at kPlosiveHz is the detector (ratio kPlosiveRatio, fast attack, 60 ms release); when it fires the signal is
//   cross-faded towards a 2nd-order high-pass at the same frequency, so the thump of a "p" goes and speech is left alone.
// - De-esser: a detector (two cascaded band-passes of Q kEssQ, so it ignores everything away from the frequency) around the chosen frequency, linked across both channels, drives a downward gain of ratio kEssRatio
//   limited to Range. Band mode takes that gain off the band-pass component only (a dynamic bell cut at the frequency: the rest of the voice
//   is untouched); Wide mode turns the whole signal down. Listen plays the detector.
// - Gate: a 2:1 downward expander on the louder channel (80 ms envelope release, 40 ms close, 2 ms open) that closes by at most Range.
// - Harshness tamer: the de-esser's mechanism (detector, linked, Band cut) at a lower, adjustable frequency, 3:1, with Amount as its range.
// - Leveller: a feed-forward compressor (peak detector, gain smoothed with 8 ms attack and 150 ms release, soft knee), automatic make-up of half the
//   reduction at full scale, and a parallel Mix.
// - Voice EQ: a bell for mud, a low shelf, a peaking bell and a high shelf (RBJ), 0 dB meaning no filter.
// - Drive: a soft saturator, tanh(a x) / a with a = 1 + 2 Drive: unity gain for small signals, rounding the peaks.
// - Sparkle: the band above kSparkleFromHz is saturated and only the new harmonics above kSparkleHarmonicsHz are added back.

inline constexpr double kPlosiveHz = 150.0;
inline constexpr double kPlosiveRatio = 6.0;
inline constexpr double kPlosiveAttackS = 0.001, kPlosiveReleaseS = 0.06;
inline constexpr double kEssRatio = 4.0;
inline constexpr double kEssQ = 1.5;
inline constexpr double kEssAttackS = 0.0003, kEssReleaseS = 0.02;
inline constexpr double kKneeDb = 6.0;
inline constexpr double kWarmthHz = 200.0, kPresenceHz = 3200.0, kPresenceQ = 0.9, kAirHz = 10000.0;
inline constexpr double kSparkleFromHz = 5000.0, kSparkleHarmonicsHz = 8000.0, kSparkleDrive = 6.0, kSparkleGain = 1.5;
inline constexpr double kGateRatio = 2.0, kGateAttackS = 0.001, kGateReleaseS = 0.08, kGateOpenS = 0.002, kGateCloseS = 0.04;
inline constexpr double kTameRatio = 3.0;
inline constexpr double kLevelAttackS = 0.008, kLevelReleaseS = 0.15, kLevelDetectAttackS = 0.0005, kLevelDetectReleaseS = 0.005, kLevelMakeup = 0.5;
inline constexpr double kLevelMaxDb = 30.0;
inline constexpr double kMudHz = 300.0, kMudQ = 1.0;
inline constexpr double kSmoothSeconds = 0.02;
inline constexpr double kGainSmoothSeconds = 0.001;   // the de-esser and plosive gains, at most this fast

inline constexpr float kHpfMin = 20.0f, kHpfMax = 300.0f;
inline constexpr float kPlosiveThrMin = -40.0f, kPlosiveThrMax = 0.0f;
inline constexpr float kEssFreqMin = 2000.0f, kEssFreqMax = 12000.0f;
inline constexpr float kEssThrMin = -60.0f, kEssThrMax = 0.0f;
inline constexpr float kEssRangeMax = 24.0f;
inline constexpr float kToneMaxDb = 12.0f;
inline constexpr float kGateThrMin = -80.0f, kGateThrMax = -20.0f, kGateRangeMax = 40.0f;
inline constexpr float kTameFreqMin = 1500.0f, kTameFreqMax = 5000.0f, kTameThrMin = -60.0f, kTameThrMax = 0.0f, kTameAmountMax = 12.0f;
inline constexpr float kLevelThrMin = -40.0f, kLevelThrMax = 0.0f, kLevelRatioMin = 1.5f, kLevelRatioMax = 10.0f;
inline constexpr float kOutputMinDb = -24.0f, kOutputMaxDb = 12.0f;

enum class EssMode : int { Band = 0, Wide = 1 };

struct Parameters {
    float hpfOn = 1.0f;
    float hpfHz = 80.0f;
    float plosiveOn = 1.0f;
    float plosiveThrDb = -20.0f;
    float essOn = 1.0f;
    float essFreqHz = 6500.0f;
    float essThrDb = -30.0f;
    float essRangeDb = 10.0f;
    float essMode = 0.0f;       // 0 Band, 1 Wide
    float essListen = 0.0f;
    float warmthDb = 0.0f;
    float presenceDb = 0.0f;
    float airDb = 0.0f;
    float sparkle = 0.0f;       // 0..1
    float outputDb = 0.0f;
    // Added later (parameter ids 18 and up).
    float gateOn = 0.0f;
    float gateThrDb = -50.0f;
    float gateRangeDb = 12.0f;
    float tameFreqHz = 3000.0f;
    float tameThrDb = -30.0f;
    float tameAmountDb = 0.0f;   // the most it will take off; 0 is off
    float levelOn = 0.0f;
    float levelThrDb = -20.0f;
    float levelRatio = 3.0f;
    float levelMix = 1.0f;
    float mudDb = 0.0f;
    float drive = 0.0f;          // 0..1
};

// Read by the panel.
struct Meters {
    float essReductionDb = 0.0f;
    float plosiveReductionDb = 0.0f;
    float gateReductionDb = 0.0f;
    float levelReductionDb = 0.0f;
    float detectorDb = -60.0f;   // level at the de-esser's detector
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
};

// A detector and a band cut: used by the de-esser and the harshness tamer.
struct DynBand {
    std::array<Biquad, 2> cut, detect {};   // `cut` is the band-pass the gain is taken from, `detect` follows it
    double env = 0.0, gain = 1.0;
    double freqHz = 6500.0, made = -1.0;
};

struct EngineState {
    std::array<Biquad, 2> hpf, plosiveLow, plosiveHigh, mud, warmth, presence, air, sparkleLow, sparkleHarm {};
    DynBand ess, tame;
    double plosiveEnv = 0.0, plosiveGain = 1.0;
    double gateEnv = 0.0, gateGain = 1.0;
    double levelEnv = 0.0, levelGr = 0.0;
    // Smoothed values.
    double hpfHz = 80.0, warmthDb = 0.0, presenceDb = 0.0, airDb = 0.0, mudDb = 0.0, sparkle = 0.0, drive = 0.0, outputGain = 1.0;
    double tameRange = 0.0, levelAmount = 0.0, levelMakeupDb = 0.0, levelMix = 1.0;
    // The values the filter coefficients were last made for.
    double madeHpf = -1.0, madeWarmth = 1e9, madePresence = 1e9, madeAir = 1e9, madeMud = 1e9;
    unsigned counter = 0;
    bool started = false, fixedMade = false;
    Meters meters {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// The gain reduction (dB, >= 0) for a detector level `levelDb` over `thresholdDb`, at `ratio`, with a soft knee.
[[nodiscard]] double reductionDb(double levelDb, double thresholdDb, double ratio) noexcept;

// The gain reduction (dB, >= 0) of a downward expander: `levelDb` below `thresholdDb`, at `ratio`, with the same soft knee.
[[nodiscard]] double expansionDb(double levelDb, double thresholdDb, double ratio) noexcept;

// Clears the running state (filters, detectors). There is no patch in the state.
void resetState(EngineState& state) noexcept;

void processBlock(EngineState& state, const Parameters& params, float sampleRate, std::uint32_t frames, const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight) noexcept;

}  // namespace downspout::silken
