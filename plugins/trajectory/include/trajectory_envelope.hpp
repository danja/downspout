#pragma once

#include <algorithm>
#include <cstdint>

namespace downspout::trajectory {

// Per-sample step sizes for an ADSR, computed once per render segment. Times are
// for a full-scale ramp, as in the Flues original's attack and release: a step is
// 1 / (seconds * sampleRate). Decay falls from 1 to sustain at the same rate a
// full 1 -> 0 decay would, so a low sustain finishes sooner.
struct AdsrSteps {
    float attack = 1.0f;
    float decay = 1.0f;
    float release = 1.0f;
    float sustain = 1.0f;
};

[[nodiscard]] inline float envelopeStep(const float milliseconds, const double sampleRate) noexcept
{
    const double seconds = std::max(0.0001, static_cast<double>(milliseconds) * 0.001);
    return static_cast<float>(1.0 / std::max(1.0, seconds * sampleRate));
}

class Adsr {
public:
    enum class Stage : std::uint8_t { idle, attack, decay, sustain, release };

    void reset() noexcept
    {
        stage_ = Stage::idle;
        level_ = 0.0f;
    }

    // Retriggering climbs from wherever the level is, so stealing a sounding voice
    // does not click. A fresh voice starts from zero.
    void gateOn() noexcept { stage_ = Stage::attack; }

    void gateOff() noexcept
    {
        if (stage_ != Stage::idle)
            stage_ = Stage::release;
    }

    [[nodiscard]] float process(const AdsrSteps& steps) noexcept
    {
        switch (stage_)
        {
        case Stage::attack:
            level_ += steps.attack;
            if (level_ >= 1.0f)
            {
                level_ = 1.0f;
                stage_ = Stage::decay;
            }
            break;

        case Stage::decay:
            level_ -= steps.decay;
            if (level_ <= steps.sustain)
            {
                level_ = steps.sustain;
                stage_ = Stage::sustain;
            }
            break;

        case Stage::sustain:
            level_ = steps.sustain;
            break;

        case Stage::release:
            level_ -= steps.release;
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
        return level_;
    }

    [[nodiscard]] float level() const noexcept { return level_; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] bool active() const noexcept { return stage_ != Stage::idle; }
    [[nodiscard]] bool releasing() const noexcept { return stage_ == Stage::release; }

private:
    Stage stage_ = Stage::idle;
    float level_ = 0.0f;
};

}  // namespace downspout::trajectory
