#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::spliff {

// Simplified Spiff: a 3-band adaptive transient processor. The manual's
// sensitivity-curve sidechain is reduced to two crossover frequencies, the
// per-bin dynamic filter to per-band envelope-driven gain, and the M/S stereo
// section is omitted in v1 (stereo link is fixed at 100% — one shared detector
// would need M/S plumbing; instead both channels share the mono-averaged
// detector, which is the same musical outcome for the common case).
//
// Mode 2 adds a level-independent attack/sustain designer: two envelope time
// constants (fast and slow) are compared as a ratio, so their dB difference
// drives gain with no absolute-level threshold. It is deliberately separate
// from the 3-band Cut/Boost detector so existing projects are unchanged.
//
// The four most-playable controls map 1:1 to Drift's default lane CCs so that
// routing Drift MIDI out -> Spliff MIDI in just works (chipper convention).
inline constexpr float kDefaultCCDepth       = 1.0f;
inline constexpr float kDefaultCCSensitivity = 2.0f;
inline constexpr float kDefaultCCDecay       = 3.0f;
inline constexpr float kDefaultCCMix         = 4.0f;

struct Parameters {
    float mode        = 0.0f;    // 0 = Cut transients, 1 = Boost, 2 = level-independent designer
    float depth       = 0.5f;    // 0-1 amount of cut/boost
    float sensitivity = 0.5f;    // 0-1 detection threshold (higher = more caught)
    float sharpness   = 0.7f;    // 0-1 per-band independence (low = natural)
    float decay       = 0.25f;   // 0-1 recovery time (0 = onset only)
    float decayTilt   = 0.0f;    // -1..1 LF vs HF decay balance (0 = equal)
    float splitLow    = 250.0f;  // Hz, low/mid crossover
    float splitHigh   = 4000.0f; // Hz, mid/high crossover
    float mix         = 100.0f;  // 0-100 dry/wet %
    float trim        = 0.0f;    // -12..+12 dB makeup gain, wet only
    float bypass      = 0.0f;    // 0 = active, 1 = bypassed (DSP still runs)
    float delta       = 0.0f;    // 0 = normal, 1 = monitor wet-dry
    float attackDb    = 0.0f;    // -15..15 dB, designer transient gain (+ boosts)
    float sustainDb   = 0.0f;    // -24..24 dB, designer body/tail gain (+ boosts)
    float ccDepth       = kDefaultCCDepth;       // 0-127 CC# (0 = off)
    float ccSensitivity = kDefaultCCSensitivity;
    float ccDecay       = kDefaultCCDecay;
    float ccMix         = kDefaultCCMix;
    float ccChannel     = 1.0f;  // 1-16 MIDI channel for the four CCs
};

struct ChannelState {
    float lowMem = 0.0f;   // low crossover pole 1
    float lowMem2 = 0.0f;  // low crossover pole 2
    float highMem = 0.0f;  // high crossover pole 1
    float highMem2 = 0.0f; // high crossover pole 2
    std::array<float, 3> gain { 1.0f, 1.0f, 1.0f };  // smoothed per-band gain
};

struct DesignerState {
    float fast = 0.0f;   // 0.2 ms attack / 30 ms release envelope
    float slow = 0.0f;   // 20 ms attack / 250 ms release envelope
    float gain = 1.0f;   // smoothed linear gain from their dB ratio
};

struct EngineState {
    std::array<ChannelState, 2> channels {};
    // Shared mono detector (fixed 100% link): its own crossover memories and
    // per-band fast/slow envelopes, independent of the audio-path splits.
    float detLowMem = 0.0f;
    float detLowMem2 = 0.0f;
    float detHighMem = 0.0f;
    float detHighMem2 = 0.0f;
    std::array<float, 3> detFast {};
    std::array<float, 3> detSlow {};
    DesignerState designer {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Effective values are CC-overridden when active, else the panel value.
void processBlock(EngineState&        state,
                  const Parameters&   params,
                  std::uint32_t       frames,
                  double              sampleRate,
                  const float* const* inputs,
                  float* const*       outputs,
                  float               effectiveDepth,
                  float               effectiveSensitivity,
                  float               effectiveDecay,
                  float               effectiveMix,
                  float* const*       wetMinusDry = nullptr) noexcept;

[[nodiscard]] std::string               serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::spliff
