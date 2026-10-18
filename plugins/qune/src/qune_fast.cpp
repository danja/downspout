#include "qune_shifters.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::qune {

namespace {

constexpr double kGuard = 4.0;  // closest the read head may get to the newest sample
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxSegment = 512;

}  // namespace

void FastShifter::prepare(const double sampleRate)
{
    sampleRate_ = sampleRate;
    nominalDelay_ = std::max(16, static_cast<int>(std::lround(0.004 * sampleRate)));
    minimumSpan_ = static_cast<float>(0.003 * sampleRate);
    lastPeriod_ = static_cast<float>(sampleRate / 200.0);
    reset();
}

void FastShifter::reset()
{
    delayA_ = delayB_ = static_cast<double>(nominalDelay_);
    fading_ = false;
    fadePosition_ = 0;
    fadeLength_ = 1;
}

void FastShifter::startJump(const History& history, const std::int64_t now, const double target,
                            const float period, const int fadeLength, const bool voiced)
{
    double chosen = target;

    if (voiced && period > 24.0f)
    {
        // Slide the landing point so the waveform just before it matches the
        // waveform just before the head's current position.
        const int length = std::min(static_cast<int>(period), kMaxSegment);
        const int range = static_cast<int>(period / 8.0f);
        const std::int64_t oldRead = now - static_cast<std::int64_t>(std::llround(delayA_));
        const std::int64_t newRead = now - static_cast<std::int64_t>(std::llround(target));

        float oldSegment[kMaxSegment];
        for (int j = 0; j < length; ++j)
            oldSegment[j] = history.mono(oldRead - length + j);

        float bestScore = -1.0e30f;
        int bestShift = 0;
        // Moving the landing point later in time means a smaller delay.
        for (int shift = -range; shift <= range; ++shift)
        {
            const double candidateDelay = target - static_cast<double>(shift);
            if (candidateDelay < kGuard + 2.0)
                continue;
            const std::int64_t read = newRead + shift;
            float corr = 0.0f;
            float energy = 1.0e-9f;
            for (int j = 0; j < length; ++j)
            {
                const float v = history.mono(read - length + j);
                corr += oldSegment[j] * v;
                energy += v * v;
            }
            const float score = corr / std::sqrt(energy);
            if (score > bestScore)
            {
                bestScore = score;
                bestShift = shift;
            }
        }
        chosen = target - static_cast<double>(bestShift);
    }

    delayB_ = delayA_;
    delayA_ = std::max(chosen, kGuard + 1.0);
    fading_ = true;
    fadePosition_ = 0;
    fadeLength_ = std::max(1, fadeLength);
}

void FastShifter::process(const History& history, const ShiftControl& control, float* outLeft,
                          float* outRight, const int frames)
{
    const std::int64_t end = history.count();
    const std::int64_t start = end - frames;
    const double ratio = std::clamp(static_cast<double>(control.ratio), 0.25, 4.0);
    const double rate = 1.0 - ratio;  // change in delay per sample

    if (control.voiced && control.period > 8.0f)
        lastPeriod_ = control.period;
    const float period = lastPeriod_;

    // Whole periods at least minimumSpan_ long, so a splice repeats or skips an
    // integer number of cycles.
    const float periods = std::max(1.0f, std::ceil(minimumSpan_ / period));
    const double span = static_cast<double>(periods * period);
    const double fadeBudget = std::min(0.5 * period, span / (2.0 + 2.0 * std::fabs(rate)));
    const int fadeLength = static_cast<int>(std::clamp(fadeBudget, 8.0, 2048.0));

    // Pitching up shortens the delay and the outgoing head keeps shortening
    // through the crossfade, so leave room for that before it reaches the newest
    // sample.
    const double delayMin = kGuard + fadeLength * std::max(0.0, -rate);
    const double delayMax = delayMin + span;
    const double ceiling = static_cast<double>(history.capacity()) - 64.0 - 2.0 * span;

    for (int i = 0; i < frames; ++i)
    {
        const std::int64_t now = start + i;
        delayA_ += rate;
        if (fading_)
            delayB_ += rate;

        if (!fading_)
        {
            if (delayA_ < kGuard + 1.0 || (rate < 0.0 && delayA_ < delayMin))
                startJump(history, now, delayA_ + span, period, fadeLength, control.voiced);
            else if (delayA_ > ceiling || (rate > 0.0 && delayA_ > delayMax))
                startJump(history, now, std::max(delayA_ - span, kGuard + 2.0), period, fadeLength,
                          control.voiced);
        }

        const double positionA = static_cast<double>(now) - delayA_;
        float left = history.cubic(0, positionA);
        float right = history.cubic(1, positionA);
        if (fading_)
        {
            const double positionB = static_cast<double>(now) - std::max(delayB_, kGuard);
            const float t = static_cast<float>(fadePosition_) / static_cast<float>(fadeLength_);
            const float gain = 0.5f - 0.5f * static_cast<float>(std::cos(kPi * t));
            left = gain * left + (1.0f - gain) * history.cubic(0, positionB);
            right = gain * right + (1.0f - gain) * history.cubic(1, positionB);
            if (++fadePosition_ >= fadeLength_)
                fading_ = false;
        }
        outLeft[i] = left;
        outRight[i] = right;
    }
}

}  // namespace downspout::qune
