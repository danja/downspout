#pragma once

#include "voxmod_params.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::voxmod {

struct Parameters {
    // Mix
    float mix = 50.0f;         // %, vocoder / ring blend
    float bypass = 0.0f;       // 0 active, 1 bypassed (DSP keeps running)
    float seed = 1.0f;

    // Ring modulator
    float ringFreq = 220.0f;    // Hz
    float ringRatio = 2.0f;     // ratio to the modulator, 0.5 - 8
    float ringShape = 0.0f;     // RingShapeId
    float ringDepth = 100.0f;   // %

    // Vocoder
    float bandCount = 24.0f;    // filter bands
    float bandSpread = 5.0f;    // octaves spanned
    float attackMs = 12.0f;
    float releaseMs = 180.0f;
    float formantShift = 0.0f;  // semitones
    float tilt = 0.0f;          // dB/oct

    // Routing
    float carrierSource = 0.0f; // 0 input, 1 internal oscillator
    float sync = 0.0f;          // 0 free, 1 ring mod drives the vocoder
    float stereoWidth = 50.0f;  // %
    float drift = 4.0f;         // %

    // Drift CC routing
    float ccMix = 1.0f;
    float ccRingFreq = 2.0f;
    float ccRingRatio = 3.0f;
    float ccBandCount = 4.0f;
    float ccCarrier = 5.0f;
    float ccChannel = 1.0f;
};

// One analysis/resynthesis band pair.
//
// The carrier side and the modulator side carry separate coefficients because
// Formant Shift displaces them: the carrier is analysed at the bank's own band
// edges while the modulator is resynthesised at those edges transposed by the
// shift, which is what moves the formants without moving the band grid.
struct VocoderBand {
    // Carrier side.
    float cZ1 = 0.0f;
    float cZ2 = 0.0f;
    float cb0 = 1.0f;
    float cb1 = 0.0f;
    float cb2 = 0.0f;
    float ca1 = 0.0f;
    float ca2 = 0.0f;

    // Modulator side.
    float mZ1 = 0.0f;
    float mZ2 = 0.0f;
    float mb0 = 1.0f;
    float mb1 = 0.0f;
    float mb2 = 0.0f;
    float ma1 = 0.0f;
    float ma2 = 0.0f;

    // Envelope follower state.
    float env = 0.0f;
    // Cached so processBlock does not recompute the edge maths per sample.
    float lowHz = 0.0f;
    float highHz = 0.0f;
    float tiltGain = 1.0f;
    bool haveCoeffs = false;
};

// Band-limited oscillator phase and wave state for the ring modulator.
struct RingState {
    double phase = 0.0;       // 0-1 within the carrier cycle
    double freq = 220.0;      // Hz, after ratio quantisation
    double driftPhase = 0.0;  // slow LFO that wobbles freq
    float lastSample = 0.0f;  // polyBLEP needs the previous value
    bool haveFreq = false;
};

struct EngineState {
    Parameters params {};      // last applied parameters, for the wrapper to read back
    std::array<VocoderBand, kMaxBands> bands {};
    int activeBands = 0;
    float bankFormantShift = -999.0f;  // forces a rebuild on the first block
    RingState ring {};
    double sampleRate = 0.0;
    double ringRatioHz = 0.0; // modulator fundamental the ratio is measured against

    // Measured outputs the wrapper publishes as status parameters.
    float sibilance = 0.0f;    // top-band share of the carrier, 0-1
    float reductionDb = 0.0f;  // vocoder band-gate gain reduction
    float carrierHz = 0.0f;    // effective carrier frequency

    // Envelope followers used for the reduction meter.
    float carrierEnv = 0.0f;
    float modulatorEnv = 0.0f;

    std::uint32_t randomiseSerial = 0;
    std::uint32_t bankSerial = 0;  // bumped when the filter bank is rebuilt
};

// ── Analysis ───────────────────────────────────────────────────────────────

// Carrier fundamental in Hz, estimated from the top analysis band. Used for the
// status readout and to derive the internal oscillator's frequency.
[[nodiscard]] float estimateCarrierHz(const Parameters& p) noexcept;

// Sibilance: the share of the carrier's energy above 4 kHz. Drives the status
// output and lets the wrapper warn when the carrier is too bright to analyse.
[[nodiscard]] float sibilanceOf(const EngineState& state) noexcept;

// ── Filter bank ────────────────────────────────────────────────────────────

// The band centre and edge frequencies for `count` bands over `spread` octaves.
[[nodiscard]] std::array<float, kMaxBands> bandEdges(const Parameters& p) noexcept;

// Quantise a ratio control to the 16 musical steps, returning the multiplier.
[[nodiscard]] float quantiseRatio(float ratio) noexcept;

// ── DSP ────────────────────────────────────────────────────────────────────

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

void activate(EngineState& state) noexcept;

// Randomise a plausible sound: carrier frequency, ratio, band count and shape.
// Mix and Bypass are left alone so a sweep does not jump the balance around.
[[nodiscard]] Parameters randomiseParameters(const Parameters& p) noexcept;

void processBlock(EngineState& state,
                  const Parameters& params,
                  std::uint32_t frames,
                  double sampleRate,
                  const float* const* inputs,
                  float* const* outputs) noexcept;

[[nodiscard]] std::string serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::voxmod