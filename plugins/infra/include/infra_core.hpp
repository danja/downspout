#pragma once

#include <array>
#include <cstdint>

namespace downspout::infra {

// An LFO driving a stereo VCA. The LFO runs at a whole-number multiple of the host's beat rate (cycles per beat),
// kept between 1 and 20 Hz, and is locked to the transport position so it repeats identically on every pass. Each
// channel reads the same waveform at its own phase offset (0, 90, 180 or 270 degrees).

inline constexpr float kMinRateHz = 1.0f;
inline constexpr float kMaxRateHz = 20.0f;
inline constexpr float kAutoTargetHz = 10.0f;   // Multiple = Auto picks the whole multiple of the beat rate nearest to this
inline constexpr int kMaxMultiple = 40;         // 40 cycles per beat reaches 20 Hz at 30 BPM
inline constexpr double kFallbackBpm = 120.0;   // the host gives no tempo

enum class Waveform : int { Sine = 0, Triangle, RampUp, RampDown, Square, SoftSquare, SampleHold, SmoothRandom };
inline constexpr int kWaveformCount = 8;

struct Parameters {
    float waveform = 0.0f;   // Waveform
    float multiple = 0.0f;   // 0 = Auto (nearest to 10 Hz), 1..40 cycles per beat
    float phaseL   = 1.0f;   // 0..3: 0, 90, 180, 270 degrees
    float phaseR   = 1.0f;
    float rangeDb  = 3.0f;   // 0..40 dB: how far the VCA falls from the LFO peak (0 dB) to its trough
};

// What the host says about time. `beatPosition` is in beats (the host's BBT beat) from the start of the song.
struct Transport {
    bool   playing = false;
    bool   valid = false;       // bpm and beatPosition are real
    double bpm = 0.0;
    double beatPosition = 0.0;
};

// Read by the panel.
struct Meters {
    float rateHz = 0.0f;
    float multiple = 0.0f;       // the multiple in use (Auto resolved, clamped to 1..20 Hz)
    std::array<float, 2> lfo {}; // 0..1 value of each channel's LFO at the end of the block
};

struct EngineState {
    double freePhase = 0.0;      // cycles, advances while the transport is stopped
    std::array<float, 2> smoothedDb {};
    bool primed = false;
    Meters meters {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Cycles per beat actually used: Auto resolves to the whole multiple whose rate is nearest to 10 Hz, and any request is
// moved into the range that keeps the rate between 1 and 20 Hz. Falls back to 120 BPM when bpm is not positive.
[[nodiscard]] int effectiveMultiple(float requested, double bpm) noexcept;
[[nodiscard]] double rateHz(int multiple, double bpm) noexcept;

// LFO value 0..1 (1 = VCA at 0 dB) for an unwrapped phase in cycles (the integer part is the cycle count, which seeds
// the random waveforms, so a given cycle always sounds the same).
[[nodiscard]] float lfoValue(Waveform wave, double cycles) noexcept;

// VCA gain in dB for an LFO value: 0 dB at 1, -rangeDb at 0.
[[nodiscard]] float gainDbFor(float lfo, float rangeDb) noexcept;

// Clears the gain smoother and free-running phase only; never touches the patch.
void resetState(EngineState& state) noexcept;

void processBlock(EngineState& state, const Parameters& params, float sampleRate, uint32_t frames, const Transport& transport,
                  const float* const* inputs, float* const* outputs) noexcept;

}  // namespace downspout::infra
