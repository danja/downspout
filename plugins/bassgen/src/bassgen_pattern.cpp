#include "bassgen_pattern.hpp"

#include "bassgen_rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::bassgen {
namespace {

struct ScaleDef {
    const int* intervals;
    int count;
};

enum class StepRole : std::uint8_t {
    weak = 0,
    pickup,
    secondary,
    primary
};

constexpr int kScaleMinor[] = {0, 2, 3, 5, 7, 8, 10};
constexpr int kScaleMajor[] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kScaleDorian[] = {0, 2, 3, 5, 7, 9, 10};
constexpr int kScalePhrygian[] = {0, 1, 3, 5, 7, 8, 10};
constexpr int kScalePentMinor[] = {0, 3, 5, 7, 10};
constexpr int kScaleBlues[] = {0, 3, 5, 6, 7, 10};
constexpr int kScaleMixolydian[] = {0, 2, 4, 5, 7, 9, 10};
constexpr int kScaleHarmonicMinor[] = {0, 2, 3, 5, 7, 8, 11};
constexpr int kScalePentMajor[] = {0, 2, 4, 7, 9};
constexpr int kScaleLocrian[] = {0, 1, 3, 5, 6, 8, 10};
constexpr int kScalePhrygianDominant[] = {0, 1, 4, 5, 7, 8, 10};

constexpr ScaleDef kScales[] = {
    {kScaleMinor, 7},
    {kScaleMajor, 7},
    {kScaleDorian, 7},
    {kScalePhrygian, 7},
    {kScalePentMinor, 5},
    {kScaleBlues, 6},
    {kScaleMixolydian, 7},
    {kScaleHarmonicMinor, 7},
    {kScalePentMajor, 5},
    {kScaleLocrian, 7},
    {kScalePhrygianDominant, 7},
};

[[nodiscard]] float clampf(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] int clampi(int value, int minValue, int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] std::int32_t nextGenerationSerial(std::int32_t currentSerial) {
    return currentSerial >= std::numeric_limits<std::int32_t>::max() ? 1 : currentSerial + 1;
}

[[nodiscard]] std::uint32_t seedMixForSerial(const Controls& controls, std::int32_t generationSerial) {
    return controls.seed ^
           (static_cast<std::uint32_t>(static_cast<int>(controls.styleMode) + 1) * 2246822519u) ^
           (static_cast<std::uint32_t>(generationSerial) * 2654435761u);
}

[[nodiscard]] int chooseDegree(Rng& rng, GenreId genre, bool strongBeat, int prevDegree) {
    const float roll = rng.nextFloat();

    if (strongBeat) {
        if (genre == GenreId::funk) {
            if (roll < 0.35f) return 0;
            if (roll < 0.58f) return 4;
            if (roll < 0.76f) return 7;
            return 2;
        }
        if (genre == GenreId::sabbath) {
            if (roll < 0.52f) return 0;
            if (roll < 0.76f) return 4;
            if (roll < 0.90f) return 6;
            return 1;
        }
        if (roll < 0.45f) return 0;
        if (roll < 0.70f) return 4;
        if (roll < 0.82f) return 2;
    }

    switch (genre) {
    case GenreId::funk:
        if (roll < 0.24f) return prevDegree;
        if (roll < 0.45f) return (prevDegree < 7) ? prevDegree + 1 : 4;
        if (roll < 0.61f) return (prevDegree > 0) ? prevDegree - 1 : 0;
        if (roll < 0.78f) return 7;
        return (rng.nextFloat() < 0.5f) ? 0 : 4;
    case GenreId::sabbath:
        if (roll < 0.36f) return 0;
        if (roll < 0.58f) return 4;
        if (roll < 0.74f) return 6;
        if (roll < 0.86f) return 1;
        return (roll < 0.93f) ? prevDegree : 3;
    case GenreId::acid:
        if (roll < 0.25f) return prevDegree;
        if (roll < 0.55f) return clampi(prevDegree + rng.nextInt(-1, 1), 0, 6);
        return rng.nextInt(0, 6);
    case GenreId::dub:
        if (roll < 0.55f) return 0;
        if (roll < 0.75f) return 4;
        return clampi(prevDegree + rng.nextInt(-1, 1), 0, 6);
    case GenreId::ambient:
        if (roll < 0.40f) return prevDegree;
        return clampi(prevDegree + rng.nextInt(-1, 1), 0, 6);
    default:
        if (roll < 0.30f) return 0;
        if (roll < 0.50f) return 4;
        return clampi(prevDegree + rng.nextInt(-1, 1), 0, 6);
    }
}

[[nodiscard]] int noteFromDegree(const Controls& controls, int degreeIndex) {
    const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
    const int octave = degreeIndex / scale.count;
    const int degree = degreeIndex % scale.count;
    const int interval = scale.intervals[degree] + 12 * octave;
    const int base = controls.rootNote + registerOffset(controls.reg);
    return clampi(base + interval, 0, 127);
}

[[nodiscard]] ::downspout::Meter resolveMeter(const ::downspout::Meter& meter) {
    return ::downspout::sanitizeMeter(meter);
}

[[nodiscard]] int stepInBarFor(const PatternState& pattern, const int step) {
    if (pattern.stepsPerBar <= 0) {
        return 0;
    }

    int local = step % pattern.stepsPerBar;
    if (local < 0) {
        local += pattern.stepsPerBar;
    }
    return local;
}

[[nodiscard]] int beatInBarFor(const PatternState& pattern, const int step) {
    if (pattern.stepsPerBeat <= 0) {
        return 0;
    }
    return stepInBarFor(pattern, step) / pattern.stepsPerBeat;
}

[[nodiscard]] bool isBeatStartStep(const PatternState& pattern, const int step) {
    return pattern.stepsPerBeat > 0 && (step % pattern.stepsPerBeat) == 0;
}

[[nodiscard]] bool isPulseStartStep(const PatternState& pattern, const int step) {
    if (!isBeatStartStep(pattern, step)) {
        return false;
    }
    return ::downspout::meterIsPulseStart(pattern.meter, beatInBarFor(pattern, step));
}

[[nodiscard]] bool isPulsePickupStep(const PatternState& pattern, const int step) {
    if (!::downspout::meterHasCompoundFeel(pattern.meter) || pattern.stepsPerBeat <= 1 || pattern.stepsPerBar <= 0) {
        return false;
    }

    const int stepInBar = stepInBarFor(pattern, step);
    const int beatInBar = stepInBar / pattern.stepsPerBeat;
    const int stepInBeat = stepInBar % pattern.stepsPerBeat;
    if (stepInBeat != (pattern.stepsPerBeat - 1)) {
        return false;
    }

    const int nextBeat = (beatInBar + 1) % pattern.meter.numerator;
    return ::downspout::meterIsPulseStart(pattern.meter, nextBeat);
}

[[nodiscard]] bool isTripleMeter(const PatternState& pattern) {
    return pattern.meter.numerator == 3 && !::downspout::meterHasCompoundFeel(pattern.meter);
}

[[nodiscard]] bool isTriplePickupStep(const PatternState& pattern, const int step) {
    if (!isTripleMeter(pattern) || pattern.stepsPerBeat <= 1 || pattern.stepsPerBar <= 0) {
        return false;
    }

    const int stepInBar = stepInBarFor(pattern, step);
    const int beatInBar = stepInBar / pattern.stepsPerBeat;
    const int stepInBeat = stepInBar % pattern.stepsPerBeat;
    return beatInBar < 2 && stepInBeat == (pattern.stepsPerBeat - 1);
}

[[nodiscard]] bool isPrimaryAccentStep(const PatternState& pattern, const int step) {
    if (::downspout::meterHasCompoundFeel(pattern.meter)) {
        return isPulseStartStep(pattern, step);
    }
    if (isTripleMeter(pattern)) {
        return isBeatStartStep(pattern, step) && beatInBarFor(pattern, step) == 0;
    }
    return isBeatStartStep(pattern, step);
}

[[nodiscard]] bool isSecondaryAccentStep(const PatternState& pattern, const int step) {
    if (::downspout::meterHasCompoundFeel(pattern.meter)) {
        return isBeatStartStep(pattern, step) && !isPulseStartStep(pattern, step);
    }
    if (isTripleMeter(pattern)) {
        return isBeatStartStep(pattern, step) && beatInBarFor(pattern, step) == 1;
    }
    return false;
}

[[nodiscard]] int beatForFraction(const PatternState& pattern, const int numerator, const int denominator) {
    if (pattern.meter.numerator <= 1 || denominator <= 0) {
        return 0;
    }

    return clampi((pattern.meter.numerator * numerator) / denominator, 0, pattern.meter.numerator - 1);
}

[[nodiscard]] bool isPickupToBeat(const PatternState& pattern, const int step, const int targetBeat) {
    if (pattern.stepsPerBeat <= 1 || pattern.meter.numerator <= 0) {
        return false;
    }

    const int stepInBar = stepInBarFor(pattern, step);
    const int beatInBar = stepInBar / pattern.stepsPerBeat;
    const int stepInBeat = stepInBar % pattern.stepsPerBeat;
    if (stepInBeat != (pattern.stepsPerBeat - 1)) {
        return false;
    }

    const int nextBeat = (beatInBar + 1) % pattern.meter.numerator;
    return nextBeat == clampi(targetBeat, 0, pattern.meter.numerator - 1);
}

[[nodiscard]] StepRole explicitStyleRole(const PatternState& pattern, const int step, const StyleModeId styleMode) {
    const int beat = beatInBarFor(pattern, step);
    const bool beatStart = isBeatStartStep(pattern, step);

    if (!beatStart && pattern.stepsPerBeat <= 1) {
        return StepRole::weak;
    }

    switch (styleMode) {
    case StyleModeId::straight:
        if (beatStart) {
            return StepRole::primary;
        }
        return isPickupToBeat(pattern, step, (beat + 1) % std::max(1, pattern.meter.numerator))
            ? StepRole::pickup
            : StepRole::weak;

    case StyleModeId::reel: {
        const int halfBeat = beatForFraction(pattern, 1, 2);
        if (beatStart && (beat == 0 || beat == halfBeat)) {
            return StepRole::primary;
        }
        if (beatStart) {
            return StepRole::secondary;
        }
        if (isPickupToBeat(pattern, step, halfBeat) || isPickupToBeat(pattern, step, 0)) {
            return StepRole::pickup;
        }
        return StepRole::weak;
    }

    case StyleModeId::waltz:
        if (beatStart && beat == 0) {
            return StepRole::primary;
        }
        if (beatStart && (beat == beatForFraction(pattern, 1, 3) || beat == beatForFraction(pattern, 2, 3))) {
            return StepRole::secondary;
        }
        if (isPickupToBeat(pattern, step, beatForFraction(pattern, 1, 3)) ||
            isPickupToBeat(pattern, step, beatForFraction(pattern, 2, 3)) ||
            isPickupToBeat(pattern, step, 0)) {
            return StepRole::pickup;
        }
        return StepRole::weak;

    case StyleModeId::jig: {
        const int pulseBeat = beatForFraction(pattern, 1, 2);
        if (beatStart && (beat == 0 || beat == pulseBeat)) {
            return StepRole::primary;
        }
        if (beatStart) {
            return StepRole::secondary;
        }
        if (isPickupToBeat(pattern, step, pulseBeat) || isPickupToBeat(pattern, step, 0)) {
            return StepRole::pickup;
        }
        return StepRole::weak;
    }

    case StyleModeId::slipJig: {
        const int secondPulse = beatForFraction(pattern, 1, 3);
        const int thirdPulse = beatForFraction(pattern, 2, 3);
        if (beatStart && (beat == 0 || beat == secondPulse || beat == thirdPulse)) {
            return StepRole::primary;
        }
        if (beatStart) {
            return StepRole::secondary;
        }
        if (isPickupToBeat(pattern, step, secondPulse) ||
            isPickupToBeat(pattern, step, thirdPulse) ||
            isPickupToBeat(pattern, step, 0)) {
            return StepRole::pickup;
        }
        return StepRole::weak;
    }

    case StyleModeId::autoMode:
    case StyleModeId::count:
    default:
        return StepRole::weak;
    }
}

[[nodiscard]] StepRole stepRoleFor(const PatternState& pattern, const Controls& controls, const int step) {
    if (controls.styleMode != StyleModeId::autoMode) {
        return explicitStyleRole(pattern, step, controls.styleMode);
    }

    if (isPrimaryAccentStep(pattern, step)) {
        return StepRole::primary;
    }
    if (isSecondaryAccentStep(pattern, step)) {
        return StepRole::secondary;
    }
    if (isPulsePickupStep(pattern, step) || isTriplePickupStep(pattern, step)) {
        return StepRole::pickup;
    }
    return StepRole::weak;
}

[[nodiscard]] float meterDensityBias(const PatternState& pattern, const int step) {
    if (::downspout::meterHasCompoundFeel(pattern.meter)) {
        if (isPulseStartStep(pattern, step)) {
            return 1.92f;
        }
        if (isPulsePickupStep(pattern, step)) {
            return 1.28f;
        }
        if (isBeatStartStep(pattern, step)) {
            return 0.26f;
        }
        return 0.08f;
    }
    if (isTripleMeter(pattern)) {
        const int beat = beatInBarFor(pattern, step);
        if (isBeatStartStep(pattern, step)) {
            if (beat == 0) return 1.68f;
            if (beat == 1) return 1.08f;
            return 0.78f;
        }
        if (isTriplePickupStep(pattern, step)) {
            return beat == 0 ? 0.58f : 0.92f;
        }
        return 0.18f;
    }
    if (isBeatStartStep(pattern, step)) {
        return 1.0f;
    }
    return 0.94f;
}

[[nodiscard]] float styleDensityBias(const PatternState& pattern,
                                     const Controls& controls,
                                     const int step) {
    const StepRole role = stepRoleFor(pattern, controls, step);

    if (controls.styleMode == StyleModeId::autoMode) {
        return meterDensityBias(pattern, step);
    }

    switch (controls.styleMode) {
    case StyleModeId::straight:
        switch (role) {
        case StepRole::primary: return 1.06f;
        case StepRole::pickup: return 0.56f;
        case StepRole::secondary:
        case StepRole::weak:
        default: return 0.90f;
        }
    case StyleModeId::reel:
        switch (role) {
        case StepRole::primary: return 1.94f;
        case StepRole::secondary: return 0.86f;
        case StepRole::pickup: return 1.04f;
        case StepRole::weak:
        default: return 0.14f;
        }
    case StyleModeId::waltz:
        switch (role) {
        case StepRole::primary: return 1.82f;
        case StepRole::secondary: return 0.96f;
        case StepRole::pickup: return 0.84f;
        case StepRole::weak:
        default: return 0.18f;
        }
    case StyleModeId::jig:
        switch (role) {
        case StepRole::primary: return 1.96f;
        case StepRole::secondary: return 0.72f;
        case StepRole::pickup: return 1.12f;
        case StepRole::weak:
        default: return 0.10f;
        }
    case StyleModeId::slipJig:
        switch (role) {
        case StepRole::primary: return 1.74f;
        case StepRole::secondary: return 0.70f;
        case StepRole::pickup: return 0.98f;
        case StepRole::weak:
        default: return 0.10f;
        }
    case StyleModeId::autoMode:
    case StyleModeId::count:
    default:
        return meterDensityBias(pattern, step);
    }
}

[[nodiscard]] float genreDensityBias(GenreId genre, bool strong) {
    switch (genre) {
    case GenreId::techno: return strong ? 1.15f : 0.78f;
    case GenreId::acid: return strong ? 0.95f : 1.10f;
    case GenreId::house: return strong ? 0.80f : 1.20f;
    case GenreId::electro: return strong ? 1.00f : 1.05f;
    case GenreId::dub: return strong ? 1.20f : 0.58f;
    case GenreId::ambient: return strong ? 0.90f : 0.45f;
    case GenreId::funk: return strong ? 0.84f : 1.28f;
    case GenreId::sabbath: return strong ? 1.22f : 0.52f;
    default: return 1.0f;
    }
}

void reinforceMeterPulses(std::array<bool, kMaxPatternSteps>& onset,
                          const PatternState& pattern,
                          const Controls& controls,
                          Rng& rng) {
    if (pattern.stepsPerBar <= 0 || pattern.stepsPerBeat <= 0) {
        return;
    }

    const int barCount = std::max(1, (pattern.patternSteps + pattern.stepsPerBar - 1) / pattern.stepsPerBar);
    if (::downspout::meterHasCompoundFeel(pattern.meter)) {
        const int pulseCount = ::downspout::meterPulseCount(pattern.meter);
        for (int bar = 0; bar < barCount; ++bar) {
            for (int pulseIndex = 1; pulseIndex < pulseCount; ++pulseIndex) {
                const int pulseBeat = ::downspout::meterGroupStartBeat(pattern.meter, pulseIndex);
                const int step = bar * pattern.stepsPerBar + pulseBeat * pattern.stepsPerBeat;
                if (step <= 0 || step >= pattern.patternSteps) {
                    continue;
                }

                const bool alreadyCovered =
                    onset[step] ||
                    (step > 0 && onset[step - 1]) ||
                    ((step + 1) < pattern.patternSteps && onset[step + 1]);
                if (alreadyCovered) {
                    continue;
                }

                float probability = 0.52f + controls.density * 0.34f;
                if (controls.genre == GenreId::ambient) {
                    probability -= 0.10f;
                } else if (controls.genre == GenreId::dub || controls.genre == GenreId::sabbath) {
                    probability += 0.10f;
                }

                if (rng.nextFloat() < clampf(probability, 0.32f, 0.94f)) {
                    onset[step] = true;
                }

                if (step > 0 && !onset[step - 1] && rng.nextFloat() < clampf(controls.density * 0.22f, 0.0f, 0.28f)) {
                    onset[step - 1] = true;
                }
            }
        }
        return;
    }

    if (isTripleMeter(pattern)) {
        for (int bar = 0; bar < barCount; ++bar) {
            const int barStart = bar * pattern.stepsPerBar;
            const int beatTwoStep = barStart + pattern.stepsPerBeat;
            const int beatThreeStep = barStart + pattern.stepsPerBeat * 2;

            if (beatTwoStep < pattern.patternSteps && !onset[beatTwoStep] &&
                rng.nextFloat() < clampf(0.46f + controls.density * 0.34f, 0.28f, 0.88f)) {
                onset[beatTwoStep] = true;
            }

            if (beatThreeStep < pattern.patternSteps && !onset[beatThreeStep] &&
                rng.nextFloat() < clampf(0.28f + controls.density * 0.28f, 0.16f, 0.74f)) {
                onset[beatThreeStep] = true;
            }
        }
    }
}

void reinforceStyleAnchors(std::array<bool, kMaxPatternSteps>& onset,
                           const PatternState& pattern,
                           const Controls& controls,
                           Rng& rng) {
    if (controls.styleMode == StyleModeId::autoMode ||
        pattern.stepsPerBar <= 0 ||
        pattern.patternSteps <= 0) {
        return;
    }

    const int barCount = std::max(1, (pattern.patternSteps + pattern.stepsPerBar - 1) / pattern.stepsPerBar);
    for (int bar = 0; bar < barCount; ++bar) {
        const int barStart = bar * pattern.stepsPerBar;
        const int barEnd = std::min(pattern.patternSteps, barStart + pattern.stepsPerBar);
        for (int step = barStart; step < barEnd; ++step) {
            const StepRole role = explicitStyleRole(pattern, step, controls.styleMode);
            if (role == StepRole::weak) {
                continue;
            }

            const bool alreadyCovered =
                onset[step] ||
                (step > 0 && onset[step - 1]) ||
                ((step + 1) < pattern.patternSteps && onset[step + 1]);
            if (alreadyCovered) {
                continue;
            }

            if (role == StepRole::primary) {
                onset[step] = true;
                continue;
            }

            float probability = 0.0f;
            switch (role) {
            case StepRole::secondary:
                probability = 0.42f + controls.density * 0.18f;
                break;
            case StepRole::pickup:
                probability = 0.16f + controls.density * 0.34f;
                break;
            case StepRole::primary:
            case StepRole::weak:
            default:
                break;
            }

            if (rng.nextFloat() < clampf(probability, 0.0f, 0.96f)) {
                onset[step] = true;
            }
        }
    }
}

[[nodiscard]] int nextOnsetStep(const std::array<bool, kMaxPatternSteps>& onset, int patternSteps, int step) {
    for (int index = step + 1; index < patternSteps; ++index) {
        if (onset[index]) {
            return index;
        }
    }
    return patternSteps;
}

[[nodiscard]] int chooseDurationSteps(const Controls& controls,
                                      const PatternState& pattern,
                                      Rng& rng,
                                      const int step,
                                      int availableSteps) {
    if (availableSteps <= 1) {
        return 1;
    }

    float holdBias = controls.hold;
    switch (controls.genre) {
    case GenreId::funk:
        holdBias *= 0.72f;
        break;
    case GenreId::sabbath:
        holdBias = clampf(holdBias * 1.35f + 0.12f, 0.0f, 1.0f);
        break;
    default:
        break;
    }

    const int maxHold = clampi(static_cast<int>(std::floor(1.0f + holdBias * static_cast<float>(availableSteps - 1))),
                               1,
                               availableSteps);

    int minHold = 1;
    const StepRole role = stepRoleFor(pattern, controls, step);
    if (controls.styleMode != StyleModeId::autoMode) {
        switch (controls.styleMode) {
        case StyleModeId::reel:
            if (role == StepRole::primary) {
                minHold = std::min(availableSteps, pattern.stepsPerBeat * 2);
            } else if (role == StepRole::secondary) {
                minHold = std::min(availableSteps, pattern.stepsPerBeat);
            } else if (role == StepRole::pickup) {
                minHold = std::min(availableSteps, std::max(1, pattern.stepsPerBeat / 2));
            }
            break;
        case StyleModeId::waltz:
        case StyleModeId::jig:
        case StyleModeId::slipJig:
            if (role == StepRole::primary) {
                minHold = std::min(availableSteps, pattern.stepsPerBeat * 2);
            } else if (role == StepRole::secondary) {
                minHold = std::min(availableSteps, pattern.stepsPerBeat);
            } else if (role == StepRole::pickup) {
                minHold = std::min(availableSteps, std::max(1, pattern.stepsPerBeat / 2));
            }
            break;
        case StyleModeId::straight:
            if (role == StepRole::primary) {
                minHold = std::min(availableSteps, pattern.stepsPerBeat);
            } else if (role == StepRole::pickup) {
                minHold = std::min(availableSteps, std::max(1, pattern.stepsPerBeat / 2));
            }
            break;
        case StyleModeId::autoMode:
        case StyleModeId::count:
        default:
            break;
        }
    } else if (::downspout::meterHasCompoundFeel(pattern.meter)) {
        if (isPulseStartStep(pattern, step)) {
            minHold = std::min(availableSteps, pattern.stepsPerBeat * 2);
        } else if (isSecondaryAccentStep(pattern, step)) {
            minHold = std::min(availableSteps, pattern.stepsPerBeat);
        } else if (isPulsePickupStep(pattern, step)) {
            minHold = std::min(availableSteps, std::max(1, pattern.stepsPerBeat / 2));
        }
    } else if (isTripleMeter(pattern)) {
        if (isPrimaryAccentStep(pattern, step)) {
            minHold = std::min(availableSteps, pattern.stepsPerBeat * 2);
        } else if (isSecondaryAccentStep(pattern, step)) {
            minHold = std::min(availableSteps, pattern.stepsPerBeat);
        } else if (isTriplePickupStep(pattern, step)) {
            minHold = std::min(availableSteps, std::max(1, pattern.stepsPerBeat / 2));
        }
    }

    if (maxHold <= minHold) {
        return minHold;
    }
    return clampi(minHold + rng.nextInt(0, maxHold - minHold), minHold, availableSteps);
}

[[nodiscard]] int sabbathCellLength(const PatternState& pattern) {
    if (pattern.eventCount >= 8) return 4;
    if (pattern.eventCount >= 4) return 3;
    return 2;
}

void buildSabbathDegreeCell(std::array<int, 4>& cell, int cellLen, Rng& rng) {
    if (cellLen <= 0) {
        return;
    }

    cell[0] = 0;
    if (cellLen > 1) {
        const float roll = rng.nextFloat();
        cell[1] = (roll < 0.45f) ? 4 : ((roll < 0.78f) ? 6 : 1);
    }
    if (cellLen > 2) {
        const float roll = rng.nextFloat();
        cell[2] = (roll < 0.40f) ? 0 : ((roll < 0.68f) ? 6 : ((roll < 0.88f) ? 1 : 3));
    }
    if (cellLen > 3) {
        const float roll = rng.nextFloat();
        cell[3] = (roll < 0.50f) ? 4 : ((roll < 0.78f) ? 0 : 6);
    }
}

[[nodiscard]] int sabbathCellDegree(const PatternState& pattern,
                                    Rng& rng,
                                    const std::array<int, 4>& cell,
                                    int cellLen,
                                    int eventIndex) {
    const int degree = cell[eventIndex % cellLen];
    const bool phraseRestart = (eventIndex % cellLen) == 0;
    const bool latePhrase = eventIndex >= cellLen && pattern.eventCount > cellLen;
    if (!latePhrase || phraseRestart || rng.nextFloat() < 0.70f) {
        return degree;
    }

    const float roll = rng.nextFloat();
    if (roll < 0.45f) return degree;
    if (roll < 0.72f) return 0;
    if (roll < 0.86f) return 4;
    return (degree == 6) ? 1 : 6;
}

void ensureFirstEvent(PatternState& pattern, const Controls& controls) {
    if (pattern.eventCount > 0) {
        return;
    }

    pattern.eventCount = 1;
    pattern.events[0].startStep = 0;
    pattern.events[0].durationSteps = pattern.stepsPerBeat;
    pattern.events[0].note = clampi(controls.rootNote + registerOffset(controls.reg), 0, 127);
    pattern.events[0].velocity = 96;
}

void generateRhythm(PatternState& pattern,
                    const Controls& controls,
                    const ::downspout::Meter& rawMeter,
                    Rng& rng) {
    pattern.eventCount = 0;
    pattern.meter = resolveMeter(rawMeter);
    pattern.stepsPerBeat = stepsPerBeatForSubdivision(controls.subdivision);
    pattern.stepsPerBar = ::downspout::meterStepsPerBar(pattern.meter, pattern.stepsPerBeat);
    pattern.patternSteps = clampi(controls.lengthBeats * pattern.stepsPerBeat, 1, kMaxPatternSteps);

    std::array<bool, kMaxPatternSteps> onset {};
    onset[0] = true;
    int cooldown = 0;

    for (int step = 1; step < pattern.patternSteps; ++step) {
        const bool strong = stepRoleFor(pattern, controls, step) == StepRole::primary;
        float probability = controls.density *
                            genreDensityBias(controls.genre, strong) *
                            styleDensityBias(pattern, controls, step);

        if (cooldown > 0) {
            probability *= 0.30f;
            cooldown -= 1;
        }

        if (rng.nextFloat() < clampf(probability, 0.02f, 0.95f)) {
            onset[step] = true;
            cooldown = 1;
        }
    }

    if (controls.styleMode == StyleModeId::autoMode) {
        reinforceMeterPulses(onset, pattern, controls, rng);
    } else {
        reinforceStyleAnchors(onset, pattern, controls, rng);
    }

    for (int step = 0; step < pattern.patternSteps && pattern.eventCount < kMaxEvents; ++step) {
        if (!onset[step]) {
            continue;
        }

        const int nextStep = nextOnsetStep(onset, pattern.patternSteps, step);
        const int available = clampi(nextStep - step, 1, pattern.patternSteps);
        const int duration = chooseDurationSteps(controls, pattern, rng, step, available);

        NoteEvent& event = pattern.events[pattern.eventCount++];
        event.startStep = step;
        event.durationSteps = duration;
        event.note = controls.rootNote;
        event.velocity = 96;
    }

    ensureFirstEvent(pattern, controls);
}

void generateNotes(PatternState& pattern, const Controls& controls, Rng& rng) {
    int prevDegree = 0;
    std::array<int, 4> sabbathCell {0, 4, 6, 0};
    int sabbathCellLen = 0;
    if (controls.genre == GenreId::sabbath) {
        sabbathCellLen = sabbathCellLength(pattern);
        buildSabbathDegreeCell(sabbathCell, sabbathCellLen, rng);
    }

    for (int index = 0; index < pattern.eventCount; ++index) {
        NoteEvent& event = pattern.events[index];
        const StepRole role = stepRoleFor(pattern, controls, event.startStep);
        const bool strong = role == StepRole::primary;
        const bool secondaryAccent = role == StepRole::secondary;
        const int degree = (controls.genre == GenreId::sabbath)
            ? sabbathCellDegree(pattern, rng, sabbathCell, sabbathCellLen, index)
            : chooseDegree(rng, controls.genre, strong, prevDegree);
        prevDegree = degree;
        event.note = noteFromDegree(controls, degree);

        const int baseVelocity = 86;
        const int accentBoost = strong
            ? static_cast<int>(std::lround(controls.accent * 28.0f))
            : (secondaryAccent
                ? static_cast<int>(std::lround(controls.accent * 12.0f))
                : (role == StepRole::pickup ? static_cast<int>(std::lround(controls.accent * 8.0f)) : 0));
        const int randomBoost = rng.nextInt(0, 10);
        event.velocity = clampi(baseVelocity + accentBoost + randomBoost, 1, 127);
    }
}

void sortEvents(PatternState& pattern) {
    std::sort(pattern.events.begin(),
              pattern.events.begin() + pattern.eventCount,
              [](const NoteEvent& left, const NoteEvent& right) {
                  return left.startStep < right.startStep;
              });
}

void copyNoteContentFromPattern(PatternState& target, const PatternState& source) {
    if (source.eventCount <= 0) {
        return;
    }

    for (int index = 0; index < target.eventCount; ++index) {
        const NoteEvent& sourceEvent = source.events[index % source.eventCount];
        target.events[index].note = sourceEvent.note;
        target.events[index].velocity = sourceEvent.velocity;
    }
}

}  // namespace

Controls clampControls(const Controls& raw) {
    Controls controls = raw;
    controls.rootNote = clampi(controls.rootNote, 0, 127);
    controls.scale = static_cast<ScaleId>(clampi(static_cast<int>(controls.scale), 0, static_cast<int>(ScaleId::count) - 1));
    controls.genre = static_cast<GenreId>(clampi(static_cast<int>(controls.genre), 0, static_cast<int>(GenreId::count) - 1));
    controls.styleMode = static_cast<StyleModeId>(clampi(static_cast<int>(controls.styleMode), 0, static_cast<int>(StyleModeId::count) - 1));
    controls.channel = clampi(controls.channel, 1, 16);
    controls.lengthBeats = clampi(controls.lengthBeats, kMinLengthBeats, kMaxLengthBeats);
    controls.subdivision = static_cast<SubdivisionId>(clampi(static_cast<int>(controls.subdivision), 0, static_cast<int>(SubdivisionId::count) - 1));
    controls.density = clampf(controls.density, 0.0f, 1.0f);
    controls.reg = clampi(controls.reg, 0, 3);
    controls.hold = clampf(controls.hold, 0.0f, 1.0f);
    controls.accent = clampf(controls.accent, 0.0f, 1.0f);
    controls.vary = clampf(controls.vary, 0.0f, 1.0f);
    controls.actionNew = clampi(controls.actionNew, 0, 1048576);
    controls.actionNotes = clampi(controls.actionNotes, 0, 1048576);
    controls.actionRhythm = clampi(controls.actionRhythm, 0, 1048576);
    return controls;
}

bool structuralControlsChanged(const Controls& a, const Controls& b) {
    return a.rootNote != b.rootNote ||
           a.scale != b.scale ||
           a.genre != b.genre ||
           a.styleMode != b.styleMode ||
           a.lengthBeats != b.lengthBeats ||
           a.subdivision != b.subdivision ||
           std::fabs(a.density - b.density) > 0.0001f ||
           a.reg != b.reg ||
           std::fabs(a.hold - b.hold) > 0.0001f ||
           std::fabs(a.accent - b.accent) > 0.0001f ||
           a.seed != b.seed;
}

int stepsPerBeatForSubdivision(SubdivisionId subdivision) {
    switch (subdivision) {
    case SubdivisionId::eighth: return 2;
    case SubdivisionId::sixteenth: return 4;
    case SubdivisionId::sixteenthTriplet: return 6;
    default: return 4;
    }
}

int registerOffset(int reg) {
    switch (reg) {
    case 0: return -12;
    case 1: return 0;
    case 2: return 12;
    case 3: return 24;
    default: return 0;
    }
}

void regeneratePattern(PatternState& pattern,
                       const Controls& controls,
                       const ::downspout::Meter& meter,
                       bool regenRhythm,
                       bool regenNotes) {
    if (!regenRhythm && pattern.eventCount <= 0) {
        regenRhythm = true;
        regenNotes = true;
    }

    const std::int32_t nextSerial = nextGenerationSerial(pattern.generationSerial);
    const std::uint32_t seedMix = seedMixForSerial(controls, nextSerial);
    const PatternState previousPattern = pattern;
    const bool hadPreviousPattern = previousPattern.eventCount > 0;

    Rng rng;
    rng.seed(seedMix);

    if (regenRhythm || pattern.patternSteps <= 0 || pattern.stepsPerBeat <= 0) {
        generateRhythm(pattern, controls, meter, rng);
    } else {
        pattern.meter = resolveMeter(meter);
        pattern.stepsPerBar = ::downspout::meterStepsPerBar(pattern.meter, pattern.stepsPerBeat);
    }

    if (regenRhythm && !regenNotes && hadPreviousPattern) {
        copyNoteContentFromPattern(pattern, previousPattern);
    }

    if (regenNotes || pattern.eventCount <= 0) {
        if (regenRhythm) {
            rng.seed(seedMix ^ 0xA5A5A5A5u);
        }
        generateNotes(pattern, controls, rng);
    }

    sortEvents(pattern);
    pattern.version = kPatternStateVersion;
    pattern.generationSerial = nextSerial;
}

void partialNoteMutation(PatternState& pattern,
                         const Controls& controls,
                         float strength) {
    if (pattern.eventCount <= 0) {
        regeneratePattern(pattern, controls, pattern.meter, true, true);
        return;
    }

    const std::int32_t nextSerial = nextGenerationSerial(pattern.generationSerial);
    const std::uint32_t seedMix = seedMixForSerial(controls, nextSerial);
    PatternState mutated = pattern;

    Rng noteRng;
    noteRng.seed(seedMix ^ 0xA5A5A5A5u);
    generateNotes(mutated, controls, noteRng);

    Rng selectRng;
    selectRng.seed(seedMix ^ 0x5A5A5A5Au);

    strength = clampf(strength, 0.05f, 1.0f);
    const int eventCount = clampi(pattern.eventCount, 1, kMaxEvents);
    const int mutationCount = clampi(static_cast<int>(std::lround(1.0f + strength * static_cast<float>(eventCount - 1))),
                                     1,
                                     eventCount);

    std::array<int, kMaxEvents> indices {};
    for (int index = 0; index < eventCount; ++index) {
        indices[index] = index;
    }

    for (int index = 0; index < mutationCount; ++index) {
        const int swapIndex = index + selectRng.nextInt(0, eventCount - index - 1);
        std::swap(indices[index], indices[swapIndex]);

        const int eventIndex = indices[index];
        pattern.events[eventIndex].note = mutated.events[eventIndex].note;
        pattern.events[eventIndex].velocity = mutated.events[eventIndex].velocity;
    }

    pattern.version = kPatternStateVersion;
    pattern.generationSerial = nextSerial;
}

}  // namespace downspout::bassgen
