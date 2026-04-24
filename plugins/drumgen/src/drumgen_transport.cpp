#include "drumgen_transport.hpp"

#include <cmath>

namespace downspout::drumgen {
namespace {

[[nodiscard]] int clampi(int value, int minValue, int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

}  // namespace

bool transportRestartDetected(bool wasPlaying,
                              std::int64_t lastTransportStep,
                              std::int64_t startStepFloor) {
    return !wasPlaying || (lastTransportStep >= 0 && startStepFloor < lastTransportStep);
}

double localStepFromAbsolute(const PatternState& pattern, double absSteps) {
    const double localStep = std::fmod(absSteps, static_cast<double>(pattern.totalSteps));
    return localStep < 0.0 ? localStep + pattern.totalSteps : localStep;
}

int localStepForBoundary(const PatternState& pattern, std::int64_t boundary) {
    const double localStep = std::fmod(static_cast<double>(boundary), static_cast<double>(pattern.totalSteps));
    return static_cast<int>(localStep < 0.0 ? localStep + pattern.totalSteps : localStep);
}

std::uint32_t frameForBoundary(double absStepsStart,
                               double absStepsEnd,
                               std::uint32_t nframes,
                               std::int64_t boundary) {
    const double relSteps = static_cast<double>(boundary) - absStepsStart;
    const double t = relSteps / (absStepsEnd - absStepsStart + 1e-12);
    return static_cast<std::uint32_t>(clampi(static_cast<int>(std::floor(t * static_cast<double>(nframes))),
                                             0,
                                             static_cast<int>(nframes) - 1));
}

}  // namespace downspout::drumgen
