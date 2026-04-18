#include "bassgen_transport.hpp"

#include <cmath>

namespace downspout::bassgen {
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

[[nodiscard]] bool eventActiveAt(const NoteEvent& event, double localStep) {
    const double start = static_cast<double>(event.startStep);
    const double end = static_cast<double>(event.startStep + event.durationSteps);
    return localStep >= start && localStep < end;
}

}  // namespace

bool transportRestartDetected(bool wasPlaying,
                              std::int64_t lastTransportStep,
                              std::int64_t startStepFloor) {
    return !wasPlaying || (lastTransportStep >= 0 && startStepFloor < lastTransportStep);
}

double localStepFromAbsolute(const PatternState& pattern, double absSteps) {
    const double localStep = std::fmod(absSteps, static_cast<double>(pattern.patternSteps));
    return localStep < 0.0 ? localStep + pattern.patternSteps : localStep;
}

const NoteEvent* findActiveEvent(const PatternState& pattern, double localStep) {
    for (int index = 0; index < pattern.eventCount; ++index) {
        if (eventActiveAt(pattern.events[index], localStep)) {
            return &pattern.events[index];
        }
    }
    return nullptr;
}

const NoteEvent* findEventStartingAt(const PatternState& pattern, int localStep) {
    for (int index = 0; index < pattern.eventCount; ++index) {
        if (pattern.events[index].startStep == localStep) {
            return &pattern.events[index];
        }
    }
    return nullptr;
}

bool anyEventEndsAt(const PatternState& pattern, int localStep) {
    for (int index = 0; index < pattern.eventCount; ++index) {
        const int endStep = (pattern.events[index].startStep + pattern.events[index].durationSteps) % pattern.patternSteps;
        if (pattern.events[index].durationSteps < pattern.patternSteps && endStep == localStep) {
            return true;
        }
    }
    return false;
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

int localStepForBoundary(const PatternState& pattern, std::int64_t boundary) {
    const double localStep = std::fmod(static_cast<double>(boundary), static_cast<double>(pattern.patternSteps));
    return static_cast<int>(localStep < 0.0 ? localStep + pattern.patternSteps : localStep);
}

}  // namespace downspout::bassgen
