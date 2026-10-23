#pragma once

#include <array>
#include <cstdint>

namespace downspout::glue {

// Stereo-linked feed-forward bus compressor. Inspired by the Glue Compressor in
// oxide-studio's oxide-fx, but a redesign rather than a port: the source has a hard knee,
// no sidechain filter, a discontinuous soft clipper and a crude auto release (see
// docs/design.md).

struct Parameters {
    float thresholdDb = -18.0f;  // -60..0 dB, on the detector level
    float ratio       = 4.0f;    // 1.1..20 : 1
    float kneeDb      = 6.0f;    // 0..24 dB, soft knee centred on the threshold
    float attackMs    = 10.0f;   // 0.1..100 ms
    float releaseMs   = 200.0f;  // 20..2000 ms (ignored while Auto Release is on)
    float autoRelease = 0.0f;    // 0 off, 1 on: release follows how long the compression has lasted
    float scHpfHz     = 60.0f;   // 20..400 Hz sidechain high-pass; 20 = off
    float rms         = 0.0f;    // 0 peak detector, 1 RMS detector
    float rangeDb     = 40.0f;   // 1..40 dB cap on gain reduction
    float makeupDb    = 0.0f;    // 0..24 dB
    float softClip    = 0.0f;    // 0 off, 1 on
    float mix         = 100.0f;  // 0..100 % wet
    float sidechain   = 0.0f;    // 0 the detector listens to the input, 1 to the sidechain input (inputs 3-4)
    float lookaheadMs = 0.0f;    // 0..10 ms: the audio is delayed this long (reported as latency) so the gain is already down when a peak arrives; 0 = off
    float ccChannel   = 1.0f;    // 0 = ignore MIDI, 1-16: the channel whose CC 1-4 drive the controls (Drift's default lanes)
};

inline constexpr float kHpfOffHz = 20.0f;

// What the panel shows. Both are measured from the audio, not echoed from the controls.
struct Meters {
    float reductionDb = 0.0f;  // gain reduction at the end of the last block, 0 or positive
    float levelDb     = -120.0f;  // detector level at the end of the last block
};

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

inline constexpr int kMaxLookahead = 2048;   // samples: 10 ms at 192 kHz, rounded up to a power of two

struct EngineState {
    std::array<std::array<float, kMaxLookahead>, 2> delay {};   // the audio path, when Look-ahead is on
    int delayPos = 0;
    std::array<Biquad, 2> hpf {};
    std::array<float, 2> meanSquare {};  // RMS detector memory
    float gainDb = 0.0f;                 // smoothed gain, 0 or negative
    float autoDepth = 0.0f;              // slow average of the reduction, positive dB
    float makeupLinear = 1.0f;
    float wet = 1.0f;
    bool primed = false;
    Meters meters {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Samples of delay (latency) the Look-ahead control adds at this sample rate; 0 when it is off.
[[nodiscard]] int lookaheadSamples(const Parameters& p, float sampleRate) noexcept;

// Static gain computer: gain in dB (0 or negative) for a detector level in dB, including the
// soft knee and the range cap. The panel plots this and the tests hold the audio to it.
[[nodiscard]] float staticGainDb(const Parameters& p, float levelDb) noexcept;

// Linear up to 0.5, then a tanh shoulder that is continuous in value and slope and never
// exceeds 1.
[[nodiscard]] float softClipSample(float x) noexcept;

// Release time actually used by Auto Release for a given average reduction depth.
[[nodiscard]] float autoReleaseMs(float depthDb) noexcept;

// MIDI control, following the Drift convention (CC 1-4 on a channel): a controller overrides one control until the panel
// moves it or the host automates it. The mapping and override logic are here so they are tested; the wrapper only parses MIDI.
inline constexpr int kCcSlots = 4;   // CC 1 Threshold, CC 2 Ratio, CC 3 Makeup, CC 4 Mix
struct LiveControl {
    std::array<float, kCcSlots> value {};   // in parameter units (Threshold can be negative, so `active` says which are set)
    std::array<bool, kCcSlots> active {false, false, false, false};
};
[[nodiscard]] float ccToValue(int slot, int cc) noexcept;
[[nodiscard]] bool ccChannelMatches(unsigned char status, float ccChannel) noexcept;
void handleController(LiveControl& live, int controller, int value) noexcept;
[[nodiscard]] Parameters withLive(const Parameters& p, const LiveControl& live) noexcept;

// Clears the detector and gain memory only; never touches the patch.
void resetState(EngineState& state) noexcept;

// `sidechain` is the optional stereo sidechain input; it is only listened to when Parameters::sidechain is 1 and both
// pointers are set, otherwise the detector uses the input (so a host that gives no sidechain does not silence the detector).
void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  uint32_t          frames,
                  const float* const* inputs,
                  float* const*       outputs,
                  const float* const* sidechain = nullptr) noexcept;

}  // namespace downspout::glue
