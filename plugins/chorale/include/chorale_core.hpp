#pragma once

#include <array>
#include <cstdint>

namespace downspout::chorale {

// A stereo chorus with two voicings. Input is Stereo (each side feeds its own delay line, so the image survives) or Mono (L + R summed, as in the source).
//
// Modes I, I+II and II follow the junologue-chorus emulation of a 1980s polysynth ensemble chorus by Peter Allwin (MIT,
// https://github.com/peterall/junologue-chorus, via the JuKnow Chorus JSFX port, MIT): the input is summed to mono, soft-limited, low-passed
// and written to a delay line; two fixed triangle LFOs (0.513 Hz and 0.863 Hz) sweep a short delay (about 1.5 to 5.4 ms) read in opposite
// directions for the left and right outputs, and the wet signal is low-passed again. The ranges, rates, filters and gains are that design's.
//
// Mode Custom is Downspout's own: 1 to 4 voices at a chosen Rate, Depth and Delay, sine-swept, each read in opposite directions
// for left and right, spread in phase so every voice is a different part of the cycle. Tone, Width and Mix apply to all modes.
//
// Modes Flanger and Phaser reuse Rate, Depth and Delay as the sweep of a short fed-back delay (comb notches) and a cascade of
// modulated first-order allpasses (moving phase notches). Feedback sets the flanger's loop gain; Voices sets the number of
// allpass sections in the phaser. Neither adds a second state format: all runtime memory lives in EngineState and is cleared
// by resetState()/activate(). Both are Downspout's own designs, not ports from the source chorus.

inline constexpr int kRing = 8192;                 // delay line, a power of two
inline constexpr int kRingMask = kRing - 1;

// Juno modes (seconds, Hz). Source values.
inline constexpr double kT0Min = 0.00154, kT0Max = 0.00515;   // left tap
inline constexpr double kT1Min = 0.00151, kT1Max = 0.00540;   // right tap
inline constexpr double kRate1 = 0.513, kRate2 = 0.863;
inline constexpr double kPreMinHz = 2000.0, kPreMaxHz = 23500.0;
inline constexpr double kPostMinHz = 6000.0, kPostMaxHz = 23500.0;
inline constexpr double kSmoothSeconds = 0.02;

inline constexpr int kModeCount = 6;               // I, I+II, II, Custom, Flanger, Phaser
inline constexpr int kMaxVoices = 4;
inline constexpr int kMaxStages = 12;              // phaser allpass sections per side

inline constexpr float kRateMin = 0.05f, kRateMax = 8.0f;     // Hz
inline constexpr float kDepthMin = 0.0f, kDepthMax = 10.0f;   // ms
inline constexpr float kDelayMin = 1.0f, kDelayMax = 30.0f;   // ms
inline constexpr float kFeedbackMax = 0.92f;                  // flanger loop gain, strictly below unity
inline constexpr float kStageMin = 2.0f;                      // phaser sections

// Controllers on the CC channel. Each takes its control over until the panel or the host moves that control again.
enum CcTarget : int { kCcRate = 0, kCcDepth, kCcWidth, kCcMix, kCcTargetCount };
inline constexpr int kCcNumber[kCcTargetCount] = {1, 2, 3, 4};

struct Parameters {
    float mode = 1.0f;       // 0 I, 1 I+II, 2 II, 3 Custom, 4 Flanger, 5 Phaser
    float tone = 0.5f;       // 0..1: both low-pass filters, squared law as in the source
    float mix = 0.5f;        // 0..1: equal power
    float rate = 0.8f;       // Hz, Custom/Flanger/Phaser. In sync mode it scales with the host BPM.
    float depth = 3.0f;      // ms either side of Delay, Custom/Flanger/Phaser
    float delay = 8.0f;      // ms, Custom; flange centre (capped at 12 ms) or phaser sweep centre
    float voices = 2.0f;     // 1..4, Custom; also the phaser stage count
    float width = 1.0f;      // 0 mono wet .. 1 full stereo wet
    float input = 1.0f;      // 0 Mono (L + R summed into one delay line, as in the source), 1 Stereo (a delay line per side)
    float sync = 0.0f;       // 0 free, 1 host tempo sync (BBT)
    float ccChannel = 1.0f;  // 0 = ignore CC
    float feedback = 0.45f;  // 0..kFeedbackMax, Flanger loop gain
    float stages = 4.0f;     // kStageMin..kMaxStages, phaser allpass sections
};

struct Transport {
    bool valid = false;
    bool playing = false;
    double bpm = 120.0;
};

struct MidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

// Read by the panel: where each voice's left tap is in its sweep (0 short .. 1 long), and the values actually in force
// (a controller can differ from the parameter).
struct Meters {
    std::array<float, kMaxVoices> tap {};
    float rate = 0.0f, depth = 0.0f, width = 0.0f, mix = 0.0f;
};

struct OnePole {
    double ff0 = 0.5, fb1 = 0.0;
    double z = 0.0;
};

struct EngineState {
    std::array<float, kRing> ring {}, ringR {};   // ringR is used in Stereo input only
    int widx = 0;
    double lfo1 = -1.0, lfo2 = -1.0;      // Juno triangles, -1..1
    double phase = 0.0;                   // Custom, turns
    double ga = 0.0, gb = 0.0, gc = 0.0;  // voicing gains
    double gf = 0.0, gp = 0.0;            // flanger and phaser gains, crossfaded with the voicings
    double dry = 1.0, wet = 0.0;
    double fr = 0.5;                      // smoothed tone
    double delayMs = 8.0, depthMs = 3.0, widthNow = 1.0, rateNow = 0.8, feedbackNow = 0.45;
    int coefCounter = 0;
    OnePole pre, preR, postL, postR;
    OnePole fbDampL, fbDampR;             // damping inside the flanger feedback loop
    double flLastL = 0.0, flLastR = 0.0;  // last flanger tap, fed back into the shared ring
    std::array<float, kMaxStages> apL {}, apR {};  // phaser allpass section states
    std::array<bool, kCcTargetCount> ccActive {};
    std::array<float, kCcTargetCount> ccValue {};   // 0..1
    bool started = false;
    Meters meters {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// What a controller value (0..1) means for a target, as a value of the control.
[[nodiscard]] float ccToValue(CcTarget target, float normalised) noexcept;

// Hand a control back to the panel or host.
void releaseCc(EngineState& state, CcTarget target) noexcept;

// Clears the running state (delay line, filters, LFOs). There is no patch in the state.
void resetState(EngineState& state) noexcept;

void processBlock(EngineState& state, const Parameters& params, float sampleRate, std::uint32_t frames, const MidiEvent* events,
                  std::uint32_t eventCount, const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                  const Transport* transport = nullptr) noexcept;

}  // namespace downspout::chorale
