#include "drumgen_state.hpp"

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

PatternState sanitizePatternState(const PatternState& raw, bool* valid) {
    PatternState pattern = raw;
    pattern.version = kPatternStateVersion;
    pattern.bars = clampi(pattern.bars, kMinBars, kMaxBars);
    pattern.stepsPerBeat = clampi(pattern.stepsPerBeat, 1, 4);
    pattern.stepsPerBar = clampi(pattern.stepsPerBar, 4, 16);
    pattern.totalSteps = clampi(pattern.totalSteps, 1, kMaxPatternSteps);

    for (int lane = 0; lane < kLaneCount; ++lane) {
        pattern.lanes[lane].midiNote = clampi(pattern.lanes[lane].midiNote, 0, 127);
        for (int step = 0; step < kMaxPatternSteps; ++step) {
            pattern.lanes[lane].steps[step].velocity = static_cast<std::uint8_t>(
                clampi(pattern.lanes[lane].steps[step].velocity, 0, 127));
        }
    }

    if (valid) {
        *valid = raw.totalSteps > 0 && raw.bars > 0;
    }

    return pattern;
}

VariationState sanitizeVariationState(const VariationState& raw) {
    VariationState variation = raw;
    variation.version = kVariationStateVersion;
    return variation;
}

}  // namespace downspout::drumgen
