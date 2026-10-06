#include "plank_voice.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::plank {

// ── Envelope ────────────────────────────────────────────────────────────────

void Envelope::gateOn() noexcept
{
    // Re-plucking a sounding string restarts the attack, as on Plinky.
    stage_ = Stage::attack;
}

void Envelope::gateOff() noexcept
{
    if (stage_ != Stage::idle)
        stage_ = Stage::release;
}

void Envelope::advance(const float attackStep,
                       const float decayStep,
                       const float releaseStep,
                       const float sustain) noexcept
{
    switch (stage_)
    {
    case Stage::attack:
        level_ += attackStep;
        if (level_ >= 1.0f)
        {
            level_ = 1.0f;
            stage_ = Stage::decay;
        }
        break;

    case Stage::decay:
        level_ -= decayStep;
        if (level_ <= sustain)
        {
            level_ = sustain;
            stage_ = Stage::sustain;
        }
        break;

    case Stage::sustain:
        level_ = sustain;
        break;

    case Stage::release:
        level_ -= releaseStep;
        if (level_ <= 0.0f)
        {
            level_ = 0.0f;
            stage_ = Stage::idle;
        }
        break;

    case Stage::idle:
        level_ = 0.0f;
        break;
    }
}

// ── Lfo ─────────────────────────────────────────────────────────────────────

std::uint32_t Lfo::random() noexcept
{
    randomState ^= randomState << 13u;
    randomState ^= randomState >> 17u;
    randomState ^= randomState << 5u;
    return randomState;
}

// ── Voice ───────────────────────────────────────────────────────────────────

void Voice::reset() noexcept
{
    oscillator[0] = Oscillator {};
    oscillator[1] = Oscillator {};
    filter.reset();
    amplitude.reset();
    modulation.reset();
    glideSmoother.reset(1.0f);
    noise = 0.0f;
    level = 0.0f;
    pitchSemitones = 0.0f;
    phaseOffset = 0.0f;
    held = false;
    sounding = false;
    degree = 0;
}

}  // namespace downspout::plank
