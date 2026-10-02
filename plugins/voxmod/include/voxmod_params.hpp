#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::voxmod {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// Voxmod is a carrier generator plus two parallel colouration engines that
// share one 4-in / 2-out bus:
//
//   in 1,2  the carrier to be analysed  (typically a voice)
//   in 3,4  the modulator             (typically an instrument or drum bus)
//   out 1,2 the processed result
//
// The vocoder path measures the carrier's spectral envelope and re-imposes it
// on the modulator's partials, so the modulator sounds like the carrier is
// "talking" it. The ring-mod path multiplies the modulator by a sine carrier
// at a chosen ratio, producing the inharmonic sidebands of a ring modulator.
//
// Both engines run in parallel into the Mix parameter, so the plugin can be a
// plain ring mod, a plain vocoder, or anywhere between. The ring mod also
// feeds the vocoder's carrier when Sync selects the internal oscillator,
// which is what makes a single input (a drum loop, say) usable.

enum class ParamId : std::uint32_t {
    // Mix
    mix = 0,          // vocoder / ring blend, %
    bypass,
    seed,             // Randomise seed

    // Ring modulator
    ringFreq,         // Hz, carrier frequency
    ringRatio,        // ratio to the modulator: 0.5 - 8, 16 steps
    ringShape,        // 0 sine, 1 triangle, 2 saw, 3 square
    ringDepth,        // %, how much of the modulator is multiplied

    // Vocoder
    bandCount,        // filter bands, 4 - 64
    bandSpread,       // octaves spanned, 1 - 8
    attackMs,         // envelope follower attack
    releaseMs,        // envelope follower release
    formantShift,     // semitones, envelope bias
    tilt,             // dB/oct spectral tilt

    // Routing
    carrierSource,    // 0 = input 1/2, 1 = internal oscillator
    sync,             // 0 = free, 1 = ring mod drives the vocoder
    stereoWidth,      // % modulation of the analyser/carrier split
    drift,            // % carrier pitch drift

    // Drift CC routing
    ccMix,
    ccRingFreq,
    ccRingRatio,
    ccBandCount,
    ccCarrier,
    ccChannel,

    // Action
    randomise,

    // Read-only status
    outCarrierHz,
    outSibilance,
    outReduction,
};

inline constexpr std::size_t kParameterCount = 27;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
    bool trigger = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f, false, false, false},
    {"bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, true, false, false},
    {"seed", "Seed", "", 1.0f, 9999.0f, 1.0f, true, false, false},

    {"ring_freq", "Ring Freq", "Hz", 5.0f, 5000.0f, 220.0f, false, false, false},
    {"ring_ratio", "Ring Ratio", "", 0.5f, 8.0f, 2.0f, true, false, false},
    {"ring_shape", "Ring Shape", "", 0.0f, 3.0f, 0.0f, true, false, false},
    {"ring_depth", "Ring Depth", "%", 0.0f, 100.0f, 100.0f, false, false, false},

    {"band_count", "Bands", "", 4.0f, 64.0f, 24.0f, true, false, false},
    {"band_spread", "Spread", "oct", 1.0f, 8.0f, 5.0f, false, false, false},
    {"attack_ms", "Attack", "ms", 1.0f, 200.0f, 12.0f, false, false, false},
    {"release_ms", "Release", "ms", 20.0f, 1000.0f, 180.0f, false, false, false},
    {"formant_shift", "Formant", "st", -12.0f, 12.0f, 0.0f, false, false, false},
    {"tilt", "Tilt", "dB/oct", -12.0f, 12.0f, 0.0f, false, false, false},

    {"carrier_source", "Carrier", "", 0.0f, 1.0f, 0.0f, true, false, false},
    {"sync", "Sync", "", 0.0f, 1.0f, 0.0f, true, false, false},
    {"stereo_width", "Width", "%", 0.0f, 100.0f, 50.0f, false, false, false},
    {"drift", "Drift", "%", 0.0f, 50.0f, 4.0f, false, false, false},

    {"cc_mix", "CC Mix", "", 0.0f, 127.0f, 1.0f, true, false, false},
    {"cc_ring_freq", "CC Ring Freq", "", 0.0f, 127.0f, 2.0f, true, false, false},
    {"cc_ring_ratio", "CC Ring Ratio", "", 0.0f, 127.0f, 3.0f, true, false, false},
    {"cc_band_count", "CC Bands", "", 0.0f, 127.0f, 4.0f, true, false, false},
    {"cc_carrier", "CC Carrier", "", 0.0f, 127.0f, 5.0f, true, false, false},
    {"cc_channel", "CC Channel", "", 1.0f, 16.0f, 1.0f, true, false, false},

    {"randomise", "Randomise", "", 0.0f, 1.0f, 0.0f, true, false, true},

    {"out_carrier_hz", "Carrier", "Hz", 0.0f, 20000.0f, 0.0f, false, true, false},
    {"out_sibilance", "Sibilance", "", 0.0f, 1.0f, 0.0f, false, true, false},
    {"out_reduction", "Reduction", "dB", -60.0f, 0.0f, 0.0f, false, true, false},
}};

// ── Ring modulator ──────────────────────────────────────────────────────────
//
// The modulator is multiplied by a periodic wave at f = ringFreq * ringRatio.
// The ratio is quantised to 16 musical steps so the "tuning" is repeatable:
// each step is a semitone above the one below, from -12 to +35 semitones, so
// the control behaves like a pitch knob rather than a free frequency.

inline constexpr int kRatioSteps = 16;
inline constexpr float kRatioMinSemitones = -12.0f;
inline constexpr float kRatioMaxSemitones = 35.0f;

// The oscillator is band-limited with a polyBLEP correction so the square and
// saw shapes do not alias into the modulator's spectrum, which would defeat the
// point of a ring modulator: the sidebands should be the only new content.
inline constexpr float kBlepMinFreq = 8000.0f;

enum class RingShapeId : std::uint32_t {
    sine = 0,
    triangle,
    saw,
    square,
    count,
};

// ── Vocoder ────────────────────────────────────────────────────────────────
//
// A filter-bank vocoder: the carrier is split into `bandCount` bands over
// `bandSpread` octaves, each with its own envelope follower. Those envelopes
// drive a parallel bank on the modulator, so the modulator inherits the
// carrier's spectral shape.
//
// The band edges are laid out logarithmically between a fixed low edge and a
// high edge `bandSpread` octaves above it. That is deliberately musical rather
// than linear: a linear bank would spend most of its bands on treble nobody
// hears and starve the formants.

inline constexpr int kMaxBands = 64;
inline constexpr float kVocoderLowHz = 60.0f;
inline constexpr float kMaxTiltDbPerOct = 12.0f;

// A band's gain is held at unity when its envelope is this small, so silence in
// the carrier does not turn the modulator's noise floor up.
inline constexpr float kBandGate = 1.0e-4f;

// ── Envelope follower ──────────────────────────────────────────────────────
//
// Asymmetric one-pole: fast attack, slow release, matching how a formant
// envelope behaves. Both coefficients are time-based so the response does not
// change with the host's block size.

[[nodiscard]] float attackCoeff(float milliseconds, double sampleRate) noexcept;
[[nodiscard]] float releaseCoeff(float milliseconds, double sampleRate) noexcept;

// ── Fixed MIDI controller map ──────────────────────────────────────────────
//
// Downspout convention (see magneto/include/magneto_params.hpp): CC 1-4 are the
// four most audible controls, which is what Drift's four lanes emit by default.
// CC 5 continues the block because Voxmod has five performance controls.

inline constexpr std::uint8_t kCcMix = 1;
inline constexpr std::uint8_t kCcRingFreq = 2;
inline constexpr std::uint8_t kCcRingRatio = 3;
inline constexpr std::uint8_t kCcBandCount = 4;
inline constexpr std::uint8_t kCcCarrier = 5;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 5> kControllerMap = {{
    {kCcMix, ParamId::mix, "Mix"},
    {kCcRingFreq, ParamId::ringFreq, "Ring freq"},
    {kCcRingRatio, ParamId::ringRatio, "Ring ratio"},
    {kCcBandCount, ParamId::bandCount, "Bands"},
    {kCcCarrier, ParamId::carrierSource, "Carrier"},
}};

// ── Bus layout ─────────────────────────────────────────────────────────────
//
// Four inputs: 0,1 the carrier, 2,3 the modulator. Two outputs.
inline constexpr std::size_t kCarrierChannels = 2;
inline constexpr std::size_t kModulatorChannels = 2;
inline constexpr std::size_t kAudioInputs = 4;
inline constexpr std::size_t kAudioOutputs = 2;

[[nodiscard]] constexpr std::uint32_t index(const ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

}  // namespace downspout::voxmod