#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace downspout {

inline constexpr int kMaxMeterGroups = 8;

struct Meter {
    std::int32_t numerator = 4;
    std::int32_t denominator = 4;
    std::int32_t groupCount = 4;
    std::array<std::int32_t, kMaxMeterGroups> groups {1, 1, 1, 1, 0, 0, 0, 0};
};

[[nodiscard]] inline int clampMeterValue(const int value, const int minValue, const int maxValue)
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] inline int sanitizeMeterDenominator(const int rawDenominator)
{
    int best = 4;
    int bestDistance = std::abs(rawDenominator - best);

    for (const int candidate : {1, 2, 4, 8, 16, 32}) {
        const int distance = std::abs(rawDenominator - candidate);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }

    return best;
}

[[nodiscard]] inline Meter makeMeter(const int rawNumerator, const int rawDenominator)
{
    Meter meter;
    meter.numerator = clampMeterValue(rawNumerator, 1, 32);
    meter.denominator = sanitizeMeterDenominator(rawDenominator);
    meter.groupCount = 0;
    meter.groups.fill(0);

    if (meter.denominator == 8 && meter.numerator > 3 && (meter.numerator % 3) == 0) {
        meter.groupCount = clampMeterValue(meter.numerator / 3, 1, kMaxMeterGroups);
        for (int index = 0; index < meter.groupCount; ++index) {
            meter.groups[static_cast<std::size_t>(index)] = 3;
        }
        return meter;
    }

    if (meter.numerator == 5) {
        meter.groupCount = 2;
        meter.groups[0] = 3;
        meter.groups[1] = 2;
        return meter;
    }

    if (meter.numerator == 7) {
        meter.groupCount = 3;
        meter.groups[0] = 2;
        meter.groups[1] = 2;
        meter.groups[2] = 3;
        return meter;
    }

    meter.groupCount = clampMeterValue(meter.numerator, 1, kMaxMeterGroups);
    for (int index = 0; index < meter.groupCount; ++index) {
        meter.groups[static_cast<std::size_t>(index)] = 1;
    }

    if (meter.numerator > meter.groupCount) {
        meter.groups[static_cast<std::size_t>(meter.groupCount - 1)] += meter.numerator - meter.groupCount;
    }

    return meter;
}

[[nodiscard]] inline Meter sanitizeMeter(const Meter& rawMeter)
{
    return makeMeter(rawMeter.numerator, rawMeter.denominator);
}

[[nodiscard]] inline Meter meterFromTimeSignature(const double beatsPerBar, const double beatType)
{
    const int numerator = clampMeterValue(static_cast<int>(std::lround(beatsPerBar)), 1, 32);
    const int denominator = clampMeterValue(static_cast<int>(std::lround(beatType)), 1, 32);
    return makeMeter(numerator, denominator);
}

[[nodiscard]] inline bool metersEqual(const Meter& a, const Meter& b)
{
    const Meter left = sanitizeMeter(a);
    const Meter right = sanitizeMeter(b);

    if (left.numerator != right.numerator ||
        left.denominator != right.denominator ||
        left.groupCount != right.groupCount) {
        return false;
    }

    for (int index = 0; index < left.groupCount; ++index) {
        if (left.groups[static_cast<std::size_t>(index)] != right.groups[static_cast<std::size_t>(index)]) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline int meterStepsPerBar(const Meter& meter, const int stepsPerBeat)
{
    return sanitizeMeter(meter).numerator * (stepsPerBeat > 0 ? stepsPerBeat : 1);
}

[[nodiscard]] inline int meterPulseCount(const Meter& meter)
{
    return sanitizeMeter(meter).groupCount;
}

[[nodiscard]] inline int meterGroupSize(const Meter& meter, const int groupIndex)
{
    const Meter normalized = sanitizeMeter(meter);
    const int index = clampMeterValue(groupIndex, 0, normalized.groupCount - 1);
    return normalized.groups[static_cast<std::size_t>(index)];
}

[[nodiscard]] inline int meterGroupStartBeat(const Meter& meter, const int groupIndex)
{
    const Meter normalized = sanitizeMeter(meter);
    int clampedIndex = clampMeterValue(groupIndex, 0, normalized.groupCount - 1);
    int beat = 0;

    for (int index = 0; index < clampedIndex; ++index) {
        beat += normalized.groups[static_cast<std::size_t>(index)];
    }

    return beat;
}

[[nodiscard]] inline int meterPulseIndexForBeat(const Meter& meter, const int beatIndex)
{
    const Meter normalized = sanitizeMeter(meter);
    const int clampedBeat = clampMeterValue(beatIndex, 0, normalized.numerator - 1);
    int beatCursor = 0;

    for (int index = 0; index < normalized.groupCount; ++index) {
        beatCursor += normalized.groups[static_cast<std::size_t>(index)];
        if (clampedBeat < beatCursor) {
            return index;
        }
    }

    return normalized.groupCount - 1;
}

[[nodiscard]] inline bool meterIsPulseStart(const Meter& meter, const int beatIndex)
{
    const Meter normalized = sanitizeMeter(meter);
    for (int index = 0; index < normalized.groupCount; ++index) {
        if (meterGroupStartBeat(normalized, index) == beatIndex) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool meterHasCompoundFeel(const Meter& meter)
{
    const Meter normalized = sanitizeMeter(meter);
    if (normalized.denominator != 8 || normalized.groupCount <= 1) {
        return false;
    }

    for (int index = 0; index < normalized.groupCount; ++index) {
        if (normalized.groups[static_cast<std::size_t>(index)] != 3) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline int meterScaledStepFromLegacy16th(const int stepsPerBar, const int legacyStep)
{
    if (stepsPerBar <= 0) {
        return 0;
    }

    const double scaled = (static_cast<double>(legacyStep) / 16.0) * static_cast<double>(stepsPerBar);
    return clampMeterValue(static_cast<int>(std::lround(scaled)), 0, stepsPerBar - 1);
}

}  // namespace downspout
