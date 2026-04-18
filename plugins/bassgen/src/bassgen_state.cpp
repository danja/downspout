#include "bassgen_state.hpp"

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

}  // namespace

PatternState sanitizePatternState(const PatternState& raw, bool* valid) {
    PatternState pattern = raw;
    pattern.version = kPatternStateVersion;
    pattern.eventCount = clampi(pattern.eventCount, 0, kMaxEvents);
    pattern.patternSteps = clampi(pattern.patternSteps, 1, kMaxPatternSteps);
    pattern.stepsPerBeat = clampi(pattern.stepsPerBeat, 1, 6);

    for (int index = 0; index < pattern.eventCount; ++index) {
        pattern.events[index].startStep = clampi(pattern.events[index].startStep, 0, pattern.patternSteps - 1);
        pattern.events[index].durationSteps = clampi(pattern.events[index].durationSteps, 1, pattern.patternSteps);
        pattern.events[index].note = clampi(pattern.events[index].note, 0, 127);
        pattern.events[index].velocity = clampi(pattern.events[index].velocity, 1, 127);
    }

    if (valid) {
        *valid = raw.eventCount > 0 && raw.patternSteps > 0;
    }

    return pattern;
}

VariationState sanitizeVariationState(const VariationState& raw) {
    VariationState variation = raw;
    variation.version = kVariationStateVersion;
    return variation;
}

}  // namespace downspout::bassgen
