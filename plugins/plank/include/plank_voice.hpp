#pragma once

#include "plank_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::plank {

// ── Band-limited wavetables ─────────────────────────────────────────────────
//
// Plinky morphs between seventeen band-limited Miunau tables (six classic
// shapes then nine harmonic "miunau" tables). Morph 0 selects the polyBLEP
// oscillator pair; morph 1 sits on the last table.

inline constexpr std::size_t kWavetableCount = 17;
inline constexpr std::size_t kWavetableSize = 1024;

struct WavetableSet {
    std::int16_t data[kWavetableCount][kWavetableSize];
};

// Plank's seventeen band-limited tables are synthesised by additive series on
// first use rather than copied from Plinky's generated `wavetable.h`, which
// keeps the plugin free of vendored data while preserving the same timbral
// sweep. See plugins/plank/docs/design.md.
[[nodiscard]] const WavetableSet& wavetables();

// ── Voice DSP ───────────────────────────────────────────────────────────────
//
// Ported from Plinky's RunVoice: two 32-bit phase oscillators, a wavetable
// morph path that offsets the pair's phase, and a two-pole resonant filter
// built from Plinky's y1/y2 feedback pair.

struct Oscillator {
    std::uint32_t phase = 0;
    std::uint32_t increment = 0;  // 32-bit fixed, one cycle = 2^32
    std::uint32_t targetIncrement = 0;
    std::int32_t previous = 0;    // previous raw phase, for polyBLEP
};

// Cytomic-style dynamic smoothing, as used by Plinky's knobsmoother. Larger
// parameter jumps smooth faster, which keeps knob turns click-free without
// lagging deliberate moves.
class Smoother {
public:
    void reset(const float value) noexcept
    {
        y1_ = value;
        y2_ = value;
    }

    [[nodiscard]] float update(const float target, const float scale) noexcept
    {
        const float band = std::fabs(y2_ - y1_);
        const float sensitivity = 8.0f / std::max(1.0f, scale);
        const float g = std::min(1.0f, 0.05f + band * sensitivity);
        y1_ += (target - y1_) * g;
        y2_ += (y1_ - y2_) * g;
        return y2_;
    }

private:
    float y1_ = 0.0f;
    float y2_ = 0.0f;
};

// Two-pole resonant lowpass with feedback, mirroring Plinky's y1/y2 recurrence
// including its fixed 0.999 leak and its resonance term on (y2 - y1).
//
// `cutoff` is the filter's own one-pole coefficient and `gain` is the output
// stage. Plinky drives both from the same envelope term; Plank separates them
// so the cutoff control moves the filter rather than the level.
class ResonantFilter {
public:
    void reset() noexcept
    {
        y1_ = 0.0f;
        y2_ = 0.0f;
    }

    [[nodiscard]] float process(const float input,
                                const float cutoff,
                                const float resonance,
                                const float gain) noexcept
    {
        y1_ += (input - (y2_ - y1_) * resonance - y1_) * cutoff;
        y1_ *= kLeak;
        y2_ += (y1_ - y2_) * cutoff;
        y2_ *= kLeak;
        return y2_ * gain;
    }

private:
    static constexpr float kLeak = 0.999f;

    float y1_ = 0.0f;
    float y2_ = 0.0f;
};

// Attack/decay/sustain/release envelope. Plinky runs two of these; Plank uses
// one for amplitude and one for filter/modulation.
class Envelope {
public:
    void reset() noexcept
    {
        level_ = 0.0f;
        stage_ = Stage::idle;
    }

    void gateOn() noexcept;
    void gateOff() noexcept;

    [[nodiscard]] float level() const noexcept { return level_; }
    [[nodiscard]] bool active() const noexcept { return stage_ != Stage::idle; }

    void advance(const float attackStep, const float decayStep, const float releaseStep,
                 const float sustain) noexcept;

private:
    enum class Stage {
        idle,
        attack,
        decay,
        sustain,
        release,
    };

    float level_ = 0.0f;
    Stage stage_ = Stage::idle;
};

struct Lfo {
    double phase = 0.0;
    float value = 0.0f;
    float held = 0.0f;
    std::uint32_t randomState = 0x2545f491u;

    void reset() noexcept
    {
        phase = 0.0;
        value = 0.0f;
        held = 0.0f;
    }

    [[nodiscard]] std::uint32_t random() noexcept;
};

// Per-string voice state. A string owns its voice for the life of a note, so
// column c always sounds the same timbre, matching Plinky's per-finger model.
struct Voice {
    Oscillator oscillator[2];
    ResonantFilter filter;
    Envelope amplitude;
    Envelope modulation;
    Smoother glideSmoother;
    float noise = 0.0f;
    float level = 0.0f;  // smooth amplitude follower, drives the LEDs
    float pitchSemitones = 0.0f;
    float phaseOffset = 0.0f;
    bool held = false;   // pad still down, or latched
    bool sounding = false;
    std::uint8_t degree = 0;  // grid row, i.e. scale degree

    void reset() noexcept;
};

}  // namespace downspout::plank
