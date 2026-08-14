#pragma once

#include <array>
#include <cstdint>

namespace downspout::bubbles {

inline constexpr int kMaxBubbleVoices  = 36;
inline constexpr int kNumDripVoices    = 16;
inline constexpr int kNumBodyModes     = 4;
inline constexpr int kDelayBufferLen   = 8192;  // power of 2
inline constexpr int kControlUpdatePeriod = 64;

// Water environment modes
// mode parameter is stored as float; cast to int to index the table.
enum class WaterMode : int {
    Stream  = 0,
    River   = 1,
    Ocean   = 2,
    Bubbles = 3,
    Drips   = 4,
    Rain    = 5,
    Custom  = 6,
    Count
};

struct Parameters {
    float mode        = 6.0f;   // WaterMode::Custom
    float flow        = 0.50f;  // overall flow intensity / wave amplitude
    float turbulence  = 0.40f;  // roughness / whitecap energy
    float size        = 0.45f;  // resonator / body size (0=large, 1=small)
    float density     = 0.50f;  // bubble / drip spawn density
    float heat        = 0.30f;  // boiling energy, faster bubbles
    float depth       = 0.20f;  // underwater LP character
    float brightness  = 0.55f;  // high-frequency content
    float resonance   = 0.55f;  // filter Q scaling
    float randomness  = 0.40f;  // stochastic frequency jitter
    float space       = 0.35f;  // stereo spread (delay wet/feedback)
    float drive       = 0.35f;  // saturation amount
    float output      = 0.80f;  // post-saturation output level
};

struct TransportSnapshot {
    bool valid   = false;
    bool playing = false;
};

// One-pole low-pass filter
struct OnePole {
    float a0 = 1.0f;
    float b1 = 0.0f;
    float z1 = 0.0f;
};

// Biquad bandpass (b1=0, b2=-b0, DF2 transposed)
struct BiquadBP {
    float b0 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;
};

struct BubbleVoice {
    bool  active   = false;
    float envAmp   = 0.0f;
    float envDecay = 0.999f;
    float impAmp   = 0.0f;
    float impDecay = 0.990f;
    BiquadBP resonator;
};

struct DripVoice {
    bool  active   = false;
    float envAmp   = 0.0f;
    float envDecay = 0.999f;
    float impAmp   = 0.0f;
    float impDecay = 0.990f;
    BiquadBP resonator1;
    BiquadBP resonator2;
};

struct EngineState {
    // Flow subtractive synthesis
    OnePole flowLP1;
    OnePole flowLP2;
    // Turbulence bands
    BiquadBP turbBand1;
    BiquadBP turbBand2;
    // Physical body resonances
    std::array<BiquadBP, kNumBodyModes> bodyModes;
    // Depth (underwater) low-pass per channel
    OnePole depthLPL;
    OnePole depthLPR;
    // Bubble and drip voice pools
    std::array<BubbleVoice, kMaxBubbleVoices> bubbles;
    std::array<DripVoice,   kNumDripVoices>   drips;
    // Wave oscillator (ocean/river rhythm)
    float wavePhase  = 0.0f;
    float wavePhaseR = 0.30f;   // stereo offset
    // Stereo delay for spatial spread
    std::array<float, kDelayBufferLen> delayL {};
    std::array<float, kDelayBufferLen> delayR {};
    int delayWrL = 0;
    int delayWrR = 0;
    // DC blockers (per channel)
    float dcXm1L = 0.0f, dcYm1L = 0.0f;
    float dcXm1R = 0.0f, dcYm1R = 0.0f;
    // LCG RNG state
    uint32_t rngState = 0x12345678u;
    // Stochastic spawn accumulators
    float bubbleSpawnAccum = 0.0f;
    float dripSpawnAccum   = 0.0f;
    // Gate / MIDI
    bool  gateOn      = true;   // starts open: generates ambient sound without MIDI
    float gateAmp     = 1.0f;
    int   midiNote    = 60;
    float midiVelocity = 0.80f;
    // Coefficient update scheduling
    int controlCounter = 0;
    // Cached computed values (updated every kControlUpdatePeriod samples)
    float cachedWaveRate        = 0.001f;
    float cachedDelayFeedback   = 0.30f;
    float cachedDelayWet        = 0.30f;
    int   cachedDelayTapL       = 2048;
    int   cachedDelayTapR       = 3072;
    float cachedWaveModL        = 0.5f;
    float cachedWaveModR        = 0.5f;
    float cachedBubbleSpawnRate = 0.0f;
    float cachedDripSpawnRate   = 0.0f;
    float cachedTurbGain        = 0.5f;
    float cachedSrInv           = 1.0f / 48000.0f;
    double sampleRate           = 48000.0;
};

// Minimal MIDI event passed from the DPF wrapper to processBlock
struct InputMidiEvent {
    uint32_t frame = 0;
    uint8_t  data[3] = {};
};

}  // namespace downspout::bubbles
