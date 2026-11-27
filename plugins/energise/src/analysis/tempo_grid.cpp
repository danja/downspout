#include "energise_analysis.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::energise {
namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double wrappedDistance(const double time, const double phase, const double period)
{
    double remainder = std::fmod(time - phase, period);
    if (remainder < 0.0)
        remainder += period;
    return std::min(remainder, period - remainder);
}

[[nodiscard]] double signedResidual(const double time, const double phase, const double period)
{
    double remainder = std::fmod(time - phase, period);
    if (remainder < 0.0)
        remainder += period;
    return remainder > period * 0.5 ? remainder - period : remainder;
}

[[nodiscard]] double gaussian(const double distance, const double width)
{
    const double ratio = distance / std::max(1e-6, width);
    return std::exp(-0.5 * ratio * ratio);
}

[[nodiscard]] double circularPhase(const std::vector<DetectedOnset>& onsets, const double step)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (const DetectedOnset& onset : onsets) {
        const double angle = kPi * 2.0 * onset.timeSec / step;
        const double weight = std::max(0.05, static_cast<double>(onset.strength));
        real += weight * std::cos(angle);
        imaginary += weight * std::sin(angle);
    }
    if (real == 0.0 && imaginary == 0.0)
        return 0.0;
    double phase = std::atan2(imaginary, real) / (kPi * 2.0) * step;
    while (phase < 0.0)
        phase += step;
    return phase;
}

}  // namespace

GrooveMap fitTempoGrid(const CaptureBuffer&, const std::vector<DetectedOnset>& onsets,
                       const int gridDivisions, const float driftAllowance)
{
    GrooveMap groove {};
    groove.gridDivisionsPerBeat = std::max(1, gridDivisions);
    groove.bpm = 120.0;
    if (onsets.size() < 2)
        return groove;

    double bestScore = -1.0;
    double bestBpm = 120.0;
    for (int candidate = 240; candidate <= 800; ++candidate) {
        const double bpm = static_cast<double>(candidate) * 0.25;  // 60..200 BPM in 0.25 steps
        const double beat = 60.0 / bpm;
        const double step = beat / groove.gridDivisionsPerBeat;
        const double phase = circularPhase(onsets, step);
        double score = 0.0;
        for (const DetectedOnset& onset : onsets) {
            const double weight = std::max(0.05, static_cast<double>(onset.strength));
            const double gridDistance = wrappedDistance(onset.timeSec, phase, step);
            const double beatDistance = wrappedDistance(onset.timeSec, phase, beat);
            score += weight * gaussian(gridDistance, step * 0.18);
            score += 0.7 * weight * weight * gaussian(beatDistance, beat * 0.12);
        }
        if (score > bestScore) {
            bestScore = score;
            bestBpm = bpm;
        }
    }

    const double beat = 60.0 / bestBpm;
    const double step = beat / groove.gridDivisionsPerBeat;
    const double phase = circularPhase(onsets, step);
    groove.bpm = bestBpm;
    groove.phaseSec = phase;

    double aligned = 0.0;
    double total = 0.0;
    for (const DetectedOnset& onset : onsets) {
        const double weight = std::max(0.05, static_cast<double>(onset.strength));
        total += weight;
        aligned += weight * gaussian(wrappedDistance(onset.timeSec, phase, step), step * 0.18);
    }
    groove.confidence = static_cast<float>(std::clamp(total > 0.0 ? aligned / total : 0.0, 0.0, 1.0));

    if (driftAllowance > 0.01f && onsets.size() >= 9) {
        double sumT = 0.0;
        double sumR = 0.0;
        double sumTT = 0.0;
        double sumTR = 0.0;
        for (const DetectedOnset& onset : onsets) {
            const double t = onset.timeSec;
            const double r = signedResidual(t, phase, step);
            sumT += t;
            sumR += r;
            sumTT += t * t;
            sumTR += t * r;
        }
        const double denominator = static_cast<double>(onsets.size()) * sumTT - sumT * sumT;
        if (std::fabs(denominator) > 1e-9) {
            double slope = (static_cast<double>(onsets.size()) * sumTR - sumT * sumR) / denominator;
            const double limit = 0.05 * std::max(0.0, static_cast<double>(driftAllowance));
            slope = std::clamp(slope, -limit, limit);
            if (std::fabs(slope) > 0.0005) {
                groove.drift.push_back({0.0, bestBpm});
                groove.drift.push_back({onsets.back().timeSec, bestBpm / (1.0 + slope)});
            }
        }
    }
    return groove;
}

}  // namespace downspout::energise
