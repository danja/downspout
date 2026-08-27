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

enum class JazzChordRole : std::uint8_t {
    two = 0,
    five,
    one,
    turnaround
};

enum class JazzDominantColor : std::uint8_t {
    mixolydian = 0,
    altered,
    halfWholeDiminished,
    wholeTone,
    bebopDominant
};

enum class FugueBarRole : std::uint8_t {
    subject = 0,
    answer,
    episode,
    pedalCadence
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
constexpr int kScaleLydian[] = {0, 2, 4, 6, 7, 9, 11};
constexpr int kScaleMelodicMinor[] = {0, 2, 3, 5, 7, 9, 11};
constexpr int kScaleWholeTone[] = {0, 2, 4, 6, 8, 10};
constexpr int kScaleAltered[] = {0, 1, 3, 4, 6, 8, 10};
constexpr int kScaleHalfWholeDiminished[] = {0, 1, 3, 4, 6, 7, 9, 10};
constexpr int kScaleWholeHalfDiminished[] = {0, 2, 3, 5, 6, 8, 9, 11};
constexpr int kScaleBebopDominant[] = {0, 2, 4, 5, 7, 9, 10, 11};
constexpr int kScaleBebopMajor[] = {0, 2, 4, 5, 7, 8, 9, 11};
constexpr int kScaleBebopMinor[] = {0, 2, 3, 4, 5, 7, 9, 10};
constexpr int kScaleIonian[] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kScaleNeapolitanMajor[] = {0, 1, 4, 5, 7, 9, 11};
constexpr int kScaleNeapolitanMinor[] = {0, 1, 3, 5, 7, 8, 10};

constexpr ScaleDef kScales[] = {
    {kScaleMajor, 7},
    {kScaleIonian, 7},
    {kScaleMinor, 7},
    {kScaleHarmonicMinor, 7},
    {kScaleMelodicMinor, 7},
    {kScaleDorian, 7},
    {kScalePhrygian, 7},
    {kScaleLydian, 7},
    {kScaleMixolydian, 7},
    {kScaleLocrian, 7},
    {kScalePhrygianDominant, 7},
    {kScaleNeapolitanMajor, 7},
    {kScaleNeapolitanMinor, 7},
    {kScalePentMajor, 5},
    {kScalePentMinor, 5},
    {kScaleBlues, 6},
    {kScaleWholeTone, 6},
    {kScaleAltered, 7},
    {kScaleHalfWholeDiminished, 8},
    {kScaleWholeHalfDiminished, 8},
    {kScaleBebopDominant, 8},
    {kScaleBebopMajor, 8},
    {kScaleBebopMinor, 8},
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

[[nodiscard]] std::uint32_t mixU32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 2246822519u;
    value ^= value >> 13;
    value *= 3266489917u;
    value ^= value >> 16;
    return value;
}

[[nodiscard]] int chooseDegree(Rng& rng, const Controls& controls, bool strongBeat, int prevDegree) {
    const float roll = rng.nextFloat();
    const GenreId genre = controls.genre;
    const float color = controls.color;

    if (strongBeat) {
        if (genre == GenreId::funk) {
            if (roll < 0.35f - color * 0.08f) return 0;
            if (roll < 0.58f) return 4;
            if (roll < 0.76f + color * 0.08f) return 7;
            if (color > 0.55f && roll < 0.90f) return 6;
            return 2;
        }
        if (genre == GenreId::sabbath) {
            if (roll < 0.52f - color * 0.10f) return 0;
            if (roll < 0.76f) return 4;
            if (roll < 0.90f + color * 0.08f) return 6;
            return 1;
        }
        if (genre == GenreId::jazz) {
            if (roll < 0.48f) return 0;
            if (roll < 0.70f) return 4;
            if (roll < 0.88f) return 2;
            return 6;
        }
        if (genre == GenreId::fugue) {
            if (roll < 0.52f) return 0;
            if (roll < 0.74f) return 4;
            if (roll < 0.90f) return 2;
            return 6;
        }
        if (genre == GenreId::rock) {
            if (roll < 0.62f) return 0;
            if (roll < 0.86f) return 4;
            if (roll < 0.96f) return 7;
            return 2;
        }
        if (genre == GenreId::moroder) {
            if (roll < 0.58f - color * 0.10f) return 0;
            if (roll < 0.78f) return 7;
            if (roll < 0.90f + color * 0.06f) return 4;
            return color > 0.62f ? 5 : 2;
        }
        if (roll < 0.45f) return 0;
        if (roll < 0.70f) return 4;
        if (roll < 0.82f) return 2;
    }

    switch (genre) {
    case GenreId::funk:
        if (roll < 0.24f - color * 0.08f) return prevDegree;
        if (roll < 0.45f) return (prevDegree < 7) ? prevDegree + 1 : 4;
        if (roll < 0.61f) return (prevDegree > 0) ? prevDegree - 1 : 0;
        if (roll < 0.78f + color * 0.08f) return 7;
        if (color > 0.55f && roll < 0.90f) return 6;
        return (rng.nextFloat() < 0.5f) ? 0 : 4;
    case GenreId::sabbath:
        if (roll < 0.36f - color * 0.08f) return 0;
        if (roll < 0.58f) return 4;
        if (roll < 0.74f + color * 0.10f) return 6;
        if (roll < 0.86f + color * 0.08f) return 1;
        return (roll < 0.93f) ? prevDegree : 3;
    case GenreId::jazz:
        if (roll < 0.34f) return clampi(prevDegree + 1, 0, 9);
        if (roll < 0.58f) return clampi(prevDegree - 1, 0, 9);
        if (roll < 0.78f) return 4;
        if (roll < 0.90f) return 2;
        return 6;
    case GenreId::fugue:
        if (roll < 0.52f) return clampi(prevDegree + 1, 0, 9);
        if (roll < 0.72f) return clampi(prevDegree - 1, 0, 9);
        if (roll < 0.84f) return 4;
        if (color > 0.55f && roll < 0.92f) return 6;
        return 2;
    case GenreId::rock:
        if (roll < 0.42f - color * 0.08f) return 0;
        if (roll < 0.66f) return 4;
        if (roll < 0.82f + color * 0.08f) return 7;
        if (roll < 0.92f) return prevDegree == 0 ? 4 : 0;
        return 2;
    case GenreId::moroder:
        if (roll < 0.42f - color * 0.10f) return 0;
        if (roll < 0.60f) return prevDegree == 0 ? 7 : 0;
        if (roll < 0.76f) return 4;
        if (roll < 0.88f + color * 0.08f) return 5;
        return color > 0.55f ? 6 : 2;
    case GenreId::acid:
        if (roll < 0.25f - color * 0.10f) return prevDegree;
        if (roll < 0.55f - color * 0.08f) return clampi(prevDegree + rng.nextInt(-1, 1), 0, 6);
        return rng.nextInt(0, color > 0.65f ? 8 : 6);
    case GenreId::dub:
        if (roll < 0.55f - color * 0.12f) return 0;
        if (roll < 0.75f) return 4;
        if (color > 0.55f && roll < 0.88f) return 6;
        return clampi(prevDegree + rng.nextInt(-1, 1), 0, 6);
    case GenreId::ambient:
        if (roll < 0.40f - color * 0.10f) return prevDegree;
        return clampi(prevDegree + rng.nextInt(-1, color > 0.6f ? 2 : 1), 0, color > 0.7f ? 8 : 6);
    default:
        if (roll < 0.30f - color * 0.08f) return 0;
        if (roll < 0.50f) return 4;
        if (color > 0.65f && roll < 0.68f) return 6;
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

[[nodiscard]] int noteFromSemitoneOffset(const Controls& controls,
                                         const int semitoneOffset,
                                         const int previousNote) {
    const int base = controls.rootNote + registerOffset(controls.reg);
    int best = clampi(base + semitoneOffset, 0, 127);
    int bestDistance = std::abs(best - previousNote);
    for (int octave = -2; octave <= 4; ++octave) {
        const int candidate = clampi(base + semitoneOffset + octave * 12, 0, 127);
        const int distance = std::abs(candidate - previousNote);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

[[nodiscard]] int scaleDegreeCount(const Controls& controls) {
    return kScales[static_cast<int>(controls.scale)].count;
}

[[nodiscard]] int nearestEquivalentDegree(const int baseDegree, const int previousDegree, const int degreeCount) {
    int best = baseDegree;
    int bestDistance = std::abs(baseDegree - previousDegree);
    for (int octave = -1; octave <= 3; ++octave) {
        const int candidate = baseDegree + octave * degreeCount;
        if (candidate < 0) {
            continue;
        }
        const int distance = std::abs(candidate - previousDegree);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

[[nodiscard]] JazzChordRole jazzChordRoleForBar(const int barIndex) {
    switch (((barIndex % 4) + 4) % 4) {
    case 0: return JazzChordRole::two;
    case 1: return JazzChordRole::five;
    case 2: return JazzChordRole::one;
    case 3: return JazzChordRole::turnaround;
    default: return JazzChordRole::one;
    }
}

[[nodiscard]] int jazzProgressionRootForRole(const JazzChordRole role, const int degreeCount) {
    const int two = std::min(1, degreeCount - 1);
    const int five = std::min(4, degreeCount - 1);
    const int six = std::min(5, degreeCount - 1);

    switch (role) {
    case JazzChordRole::two: return two;
    case JazzChordRole::five: return five;
    case JazzChordRole::one: return 0;
    case JazzChordRole::turnaround: return six;
    default: return 0;
    }
}

[[nodiscard]] int jazzProgressionRootForBar(const int barIndex, const int degreeCount) {
    return jazzProgressionRootForRole(jazzChordRoleForBar(barIndex), degreeCount);
}

[[nodiscard]] bool jazzUsesSelectedScaleVocabulary(const ScaleId scale) {
    return scale == ScaleId::altered ||
           scale == ScaleId::halfWholeDiminished ||
           scale == ScaleId::wholeHalfDiminished ||
           scale == ScaleId::bebopDominant ||
           scale == ScaleId::bebopMajor ||
           scale == ScaleId::bebopMinor;
}

[[nodiscard]] int jazzChordRootSemitoneForRole(const JazzChordRole role) {
    switch (role) {
    case JazzChordRole::two: return 2;
    case JazzChordRole::five: return 7;
    case JazzChordRole::one: return 0;
    case JazzChordRole::turnaround: return 9;
    default: return 0;
    }
}

[[nodiscard]] JazzDominantColor jazzDominantColorForBar(const Controls& controls,
                                                        const PatternState& pattern,
                                                        const int barIndex) {
    const std::uint32_t seed =
        mixU32(controls.seed ^
               (static_cast<std::uint32_t>(barIndex + 1) * 374761393u) ^
               (static_cast<std::uint32_t>(pattern.generationSerial + 1) * 668265263u));
    const std::uint32_t choice = seed % 100u;
    const float color = controls.color;
    const std::uint32_t bebopLimit = static_cast<std::uint32_t>(20.0f + color * 20.0f);
    const std::uint32_t alteredLimit = static_cast<std::uint32_t>(bebopLimit + 8.0f + color * 32.0f);
    const std::uint32_t diminishedLimit = static_cast<std::uint32_t>(alteredLimit + 10.0f + color * 24.0f);
    const std::uint32_t wholeToneLimit = static_cast<std::uint32_t>(diminishedLimit + color * 18.0f);

    if (choice < bebopLimit) return JazzDominantColor::bebopDominant;
    if (choice < alteredLimit) return JazzDominantColor::altered;
    if (choice < diminishedLimit) return JazzDominantColor::halfWholeDiminished;
    if (choice < wholeToneLimit) return JazzDominantColor::wholeTone;
    return JazzDominantColor::mixolydian;
}

[[nodiscard]] ScaleDef jazzColorScaleForRole(const JazzChordRole role,
                                             const JazzDominantColor dominantColor) {
    switch (role) {
    case JazzChordRole::two:
    case JazzChordRole::turnaround:
        return {kScaleDorian, 7};
    case JazzChordRole::five:
        switch (dominantColor) {
        case JazzDominantColor::altered: return {kScaleAltered, 7};
        case JazzDominantColor::halfWholeDiminished: return {kScaleHalfWholeDiminished, 8};
        case JazzDominantColor::wholeTone: return {kScaleWholeTone, 6};
        case JazzDominantColor::bebopDominant: return {kScaleBebopDominant, 8};
        case JazzDominantColor::mixolydian:
        default:
            break;
        }
        return {kScaleMixolydian, 7};
    case JazzChordRole::one:
        return {kScaleLydian, 7};
    default:
        return {kScaleMajor, 7};
    }
}

[[nodiscard]] int jazzTargetChordIntervalForBeat(const JazzChordRole role,
                                                 const int beatIndex,
                                                 const JazzDominantColor dominantColor) {
    if (beatIndex == 0) {
        return 0;
    }

    switch (role) {
    case JazzChordRole::two:
    case JazzChordRole::turnaround:
        if (beatIndex == 1) return 3;
        if (beatIndex == 2) return 7;
        return 10;

    case JazzChordRole::five:
        if (beatIndex == 1) return 4;
        if (beatIndex >= 3) return 10;
        switch (dominantColor) {
        case JazzDominantColor::altered: return 1;
        case JazzDominantColor::halfWholeDiminished: return 6;
        case JazzDominantColor::wholeTone: return 8;
        case JazzDominantColor::bebopDominant: return 11;
        case JazzDominantColor::mixolydian:
        default:
            return 7;
        }

    case JazzChordRole::one:
        if (beatIndex == 1) return 4;
        if (beatIndex == 2) return 7;
        return 11;

    default:
        return 0;
    }
}

[[nodiscard]] int jazzRoleChordInterval(const JazzChordRole role,
                                        const int beatIndex,
                                        const JazzDominantColor dominantColor,
                                        Rng& rng) {
    (void)rng;
    return jazzTargetChordIntervalForBeat(role, beatIndex, dominantColor);
}

[[nodiscard]] int jazzChordToneDegreeForBeat(const JazzChordRole role,
                                             const int root,
                                             const int beatIndex,
                                             Rng& rng) {
    if (beatIndex == 0) {
        return root;
    }

    switch (role) {
    case JazzChordRole::two:
        if (beatIndex == 1) return rng.nextFloat() < 0.72f ? root + 2 : root + 6;
        if (beatIndex == 2) return root + 4;
        return rng.nextFloat() < 0.64f ? root + 6 : root + 2;

    case JazzChordRole::five:
        if (beatIndex == 1) return rng.nextFloat() < 0.62f ? root + 2 : root + 6;
        if (beatIndex == 2) return root + 4;
        return rng.nextFloat() < 0.64f ? root + 6 : root + 2;

    case JazzChordRole::one:
        if (beatIndex == 1) return rng.nextFloat() < 0.64f ? root + 2 : root + 6;
        if (beatIndex == 2) return root + 4;
        return rng.nextFloat() < 0.64f ? root + 6 : root + 2;

    case JazzChordRole::turnaround:
        if (beatIndex == 1) return rng.nextFloat() < 0.58f ? root + 2 : root + 6;
        if (beatIndex == 2) return root + 4;
        return rng.nextFloat() < 0.60f ? root + 6 : root + 2;

    default:
        return root;
    }
}

[[nodiscard]] int jazzApproachDegree(Rng& rng,
                                     const int targetBaseDegree,
                                     const int previousDegree,
                                     const int degreeCount) {
    int target = nearestEquivalentDegree(targetBaseDegree, previousDegree, degreeCount);
    if (target <= previousDegree && targetBaseDegree == 0) {
        target += degreeCount;
    }

    const int below = std::max(0, target - 1);
    const int above = target + 1;
    if (rng.nextFloat() < 0.72f) {
        return below;
    }
    return above;
}

[[nodiscard]] int jazzNearestPaletteNote(const Controls& controls,
                                         const JazzChordRole role,
                                         const JazzDominantColor dominantColor,
                                         const int previousNote,
                                         Rng& rng) {
    const ScaleDef scale = jazzColorScaleForRole(role, dominantColor);
    const int chordRoot = jazzChordRootSemitoneForRole(role);
    int bestAbove = -1;
    int bestBelow = -1;
    int bestAny = noteFromSemitoneOffset(controls, chordRoot, previousNote);
    int bestAnyDistance = std::abs(bestAny - previousNote);

    for (int octave = -2; octave <= 4; ++octave) {
        for (int i = 0; i < scale.count; ++i) {
            const int offset = chordRoot + scale.intervals[i] + octave * 12;
            const int candidate = clampi(controls.rootNote + registerOffset(controls.reg) + offset, 0, 127);
            if (candidate == previousNote) {
                continue;
            }
            const int distance = std::abs(candidate - previousNote);
            if (distance < bestAnyDistance) {
                bestAny = candidate;
                bestAnyDistance = distance;
            }
            if (candidate > previousNote && (bestAbove < 0 || candidate < bestAbove)) {
                bestAbove = candidate;
            }
            if (candidate < previousNote && (bestBelow < 0 || candidate > bestBelow)) {
                bestBelow = candidate;
            }
        }
    }

    const bool moveUp = rng.nextFloat() < 0.58f;
    if (moveUp && bestAbove >= 0) {
        return bestAbove;
    }
    if (!moveUp && bestBelow >= 0) {
        return bestBelow;
    }
    return bestAny;
}

[[nodiscard]] int jazzTargetNoteForBeat(const PatternState& pattern,
                                        const Controls& controls,
                                        const int barIndex,
                                        const int beatIndex,
                                        const JazzDominantColor dominantColor,
                                        const int previousNote) {
    const bool nextBar = beatIndex >= pattern.meter.numerator;
    const JazzChordRole role = jazzChordRoleForBar(nextBar ? barIndex + 1 : barIndex);
    const int chordRoot = jazzChordRootSemitoneForRole(role);
    const int interval = nextBar ? 0 : jazzTargetChordIntervalForBeat(role, beatIndex, dominantColor);
    return noteFromSemitoneOffset(controls, chordRoot + interval, previousNote);
}

[[nodiscard]] int jazzApproachOrEnclosureNote(const int targetNote, const int stepsToTarget) {
    if (stepsToTarget == 2) {
        return clampi(targetNote + 1, 0, 127);
    }
    return clampi(targetNote - 1, 0, 127);
}

[[nodiscard]] bool jazzApproachWindowForEvent(const PatternState& pattern,
                                              const int stepInBar,
                                              const int stepInBeat,
                                              int& targetBeat,
                                              int& stepsToTarget) {
    if (pattern.stepsPerBeat <= 1 || stepInBeat <= 0) {
        return false;
    }

    stepsToTarget = pattern.stepsPerBeat - stepInBeat;
    if (stepsToTarget > 2) {
        return false;
    }

    targetBeat = (stepInBar / pattern.stepsPerBeat) + 1;
    return true;
}

[[nodiscard]] int jazzRoleColoredNoteForEvent(const PatternState& pattern,
                                              const Controls& controls,
                                              Rng& rng,
                                              const NoteEvent& event,
                                              const int previousNote) {
    const int barIndex = pattern.stepsPerBar > 0 ? event.startStep / pattern.stepsPerBar : 0;
    const int stepInBar = pattern.stepsPerBar > 0 ? event.startStep % pattern.stepsPerBar : 0;
    const int beatIndex = pattern.stepsPerBeat > 0 ? stepInBar / pattern.stepsPerBeat : 0;
    const int stepInBeat = pattern.stepsPerBeat > 0 ? stepInBar % pattern.stepsPerBeat : 0;
    const bool beatStart = stepInBeat == 0;

    const JazzChordRole role = jazzChordRoleForBar(barIndex);
    const JazzDominantColor dominantColor = role == JazzChordRole::five
        ? jazzDominantColorForBar(controls, pattern, barIndex)
        : JazzDominantColor::mixolydian;
    int targetBeat = 0;
    int stepsToTarget = 0;
    if (jazzApproachWindowForEvent(pattern, stepInBar, stepInBeat, targetBeat, stepsToTarget)) {
        const int targetNote = jazzTargetNoteForBeat(pattern, controls, barIndex, targetBeat, dominantColor, previousNote);
        return jazzApproachOrEnclosureNote(targetNote, stepsToTarget);
    }

    if (beatStart) {
        const int interval = jazzChordRootSemitoneForRole(role) + jazzRoleChordInterval(role, beatIndex, dominantColor, rng);
        return noteFromSemitoneOffset(controls, interval, previousNote);
    }

    return jazzNearestPaletteNote(controls, role, dominantColor, previousNote, rng);
}

[[nodiscard]] int jazzDegreeForEvent(const PatternState& pattern,
                                     const Controls& controls,
                                     Rng& rng,
                                     const NoteEvent& event,
                                     const int previousDegree) {
    const int degreeCount = scaleDegreeCount(controls);
    const int span = std::max(1, degreeCount);
    const int barIndex = pattern.stepsPerBar > 0 ? event.startStep / pattern.stepsPerBar : 0;
    const int stepInBar = pattern.stepsPerBar > 0 ? event.startStep % pattern.stepsPerBar : 0;
    const int beatIndex = pattern.stepsPerBeat > 0 ? stepInBar / pattern.stepsPerBeat : 0;
    const int stepInBeat = pattern.stepsPerBeat > 0 ? stepInBar % pattern.stepsPerBeat : 0;
    const bool beatStart = stepInBeat == 0;

    const int rootBase = jazzProgressionRootForBar(barIndex, span);
    const int root = nearestEquivalentDegree(rootBase, previousDegree, span);

    const JazzChordRole role = jazzChordRoleForBar(barIndex);
    int targetBeat = 0;
    int stepsToTarget = 0;
    if (jazzApproachWindowForEvent(pattern, stepInBar, stepInBeat, targetBeat, stepsToTarget)) {
        const int targetBase = targetBeat >= pattern.meter.numerator
            ? jazzProgressionRootForBar(barIndex + 1, span)
            : rootBase + targetBeat;
        return clampi(jazzApproachDegree(rng, targetBase, previousDegree, span), 0, span * 3 - 1);
    }

    if (beatStart) {
        return clampi(jazzChordToneDegreeForBeat(role, root, beatIndex, rng), 0, span * 3 - 1);
    }

    const int direction = rng.nextFloat() < 0.65f ? 1 : -1;
    return clampi(previousDegree + direction, 0, span * 3 - 1);
}

[[nodiscard]] FugueBarRole fugueRoleForBar(const int barIndex) {
    switch (((barIndex % 4) + 4) % 4) {
    case 0: return FugueBarRole::subject;
    case 1: return FugueBarRole::answer;
    case 2: return FugueBarRole::episode;
    case 3: return FugueBarRole::pedalCadence;
    default: return FugueBarRole::subject;
    }
}

[[nodiscard]] bool fugueUsesMinorSubject(const ScaleId scale) {
    return scale == ScaleId::minor ||
           scale == ScaleId::harmonicMinor ||
           scale == ScaleId::dorian ||
           scale == ScaleId::phrygian ||
           scale == ScaleId::melodicMinor;
}

[[nodiscard]] int fugueSubjectInterval(const ScaleId scale, const int subjectIndex) {
    static constexpr int kMajorSubject[] = {0, 2, 4, 5, 7, 5, 4, 2};
    static constexpr int kMinorSubject[] = {0, 2, 3, 5, 7, 5, 3, 2};
    const int index = clampi(subjectIndex, 0, 7);
    return fugueUsesMinorSubject(scale) ? kMinorSubject[index] : kMajorSubject[index];
}

[[nodiscard]] int fugueSubjectIndexForEvent(const PatternState& pattern, const NoteEvent& event) {
    if (pattern.stepsPerBar <= 0) {
        return 0;
    }

    int stepInBar = event.startStep % pattern.stepsPerBar;
    if (stepInBar < 0) {
        stepInBar += pattern.stepsPerBar;
    }
    return clampi((stepInBar * 8) / pattern.stepsPerBar, 0, 7);
}

[[nodiscard]] int fugueEpisodeRoot(const int barIndex) {
    static constexpr int kSequenceRoots[] = {9, 2, 7, 0};
    return kSequenceRoots[((barIndex % 4) + 4) % 4];
}

[[nodiscard]] int fugueNoteForEvent(const PatternState& pattern,
                                    const Controls& controls,
                                    const NoteEvent& event,
                                    const int previousNote) {
    const int barIndex = pattern.stepsPerBar > 0 ? event.startStep / pattern.stepsPerBar : 0;
    int stepInBar = pattern.stepsPerBar > 0 ? event.startStep % pattern.stepsPerBar : 0;
    if (stepInBar < 0) {
        stepInBar += pattern.stepsPerBar;
    }
    const int beatIndex = pattern.stepsPerBeat > 0 ? stepInBar / pattern.stepsPerBeat : 0;
    const int stepInBeat = pattern.stepsPerBeat > 0 ? stepInBar % pattern.stepsPerBeat : 0;
    const int subjectIndex = fugueSubjectIndexForEvent(pattern, event);
    const FugueBarRole role = fugueRoleForBar(barIndex);

    int root = 0;
    int interval = fugueSubjectInterval(controls.scale, subjectIndex);

    switch (role) {
    case FugueBarRole::subject:
        root = 0;
        break;
    case FugueBarRole::answer:
        root = 7;
        break;
    case FugueBarRole::episode:
        root = fugueEpisodeRoot(barIndex);
        interval = fugueSubjectInterval(controls.scale, subjectIndex / 2);
        break;
    case FugueBarRole::pedalCadence:
        if (beatIndex >= std::max(1, pattern.meter.numerator / 2)) {
            root = 0;
            interval = 0;
        } else {
            root = 7;
            interval = fugueUsesMinorSubject(controls.scale) ? 11 : 10;
        }
        break;
    default:
        break;
    }

    if (controls.color > 0.45f && stepInBeat == std::max(0, pattern.stepsPerBeat - 1)) {
        const int target = role == FugueBarRole::answer ? 7 : 0;
        return noteFromSemitoneOffset(controls, target - 1, previousNote);
    }

    return noteFromSemitoneOffset(controls, root + interval, previousNote);
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
    case GenreId::jazz: return strong ? 1.35f : 0.26f;
    case GenreId::fugue: return strong ? 1.48f : 0.42f;
    case GenreId::rock: return strong ? 1.55f : 0.18f;
    case GenreId::moroder: return strong ? 1.10f : 1.34f;
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
                } else if (controls.genre == GenreId::dub ||
                           controls.genre == GenreId::sabbath ||
                           controls.genre == GenreId::rock) {
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

void reinforceJazzWalkingBeats(std::array<bool, kMaxPatternSteps>& onset,
                               const PatternState& pattern,
                               const Controls& controls,
                               Rng& rng) {
    if (controls.genre != GenreId::jazz ||
        pattern.stepsPerBeat <= 0 ||
        pattern.patternSteps <= 0) {
        return;
    }

    const float beatProbability = clampf(0.58f + controls.density * 0.38f, 0.58f, 0.98f);
    for (int step = 0; step < pattern.patternSteps; step += pattern.stepsPerBeat) {
        if (step == 0 || rng.nextFloat() < beatProbability) {
            onset[step] = true;
        }
    }

    // Reinforce approach notes: the step immediately before each bar boundary
    // enables jazzApproachOrEnclosureNote to place a lower/upper chromatic
    // approach into the bar's first beat. Place unconditionally when the bar
    // boundary beat is active and density is above the walking-bass threshold.
    if (pattern.stepsPerBeat <= 1 || pattern.stepsPerBar <= 0) {
        return;
    }
    for (int barBoundary = pattern.stepsPerBar; barBoundary < pattern.patternSteps; barBoundary += pattern.stepsPerBar) {
        const int approachStep = barBoundary - 1;
        if (approachStep > 0 &&
            approachStep < pattern.patternSteps &&
            !onset[approachStep] &&
            onset[barBoundary] &&
            controls.density >= 0.70f) {
            onset[approachStep] = true;
        }
    }
}

void reinforceFugueSubjectSteps(std::array<bool, kMaxPatternSteps>& onset,
                                const PatternState& pattern,
                                const Controls& controls,
                                Rng& rng) {
    if (controls.genre != GenreId::fugue ||
        pattern.stepsPerBeat <= 0 ||
        pattern.stepsPerBar <= 0 ||
        pattern.patternSteps <= 0) {
        return;
    }

    const int halfBeat = std::max(1, pattern.stepsPerBeat / 2);
    const int barCount = std::max(1, (pattern.patternSteps + pattern.stepsPerBar - 1) / pattern.stepsPerBar);
    const float flowProbability = clampf(0.54f + controls.density * 0.34f, 0.50f, 0.92f);
    for (int bar = 0; bar < barCount; ++bar) {
        const int barStart = bar * pattern.stepsPerBar;
        if (barStart < pattern.patternSteps) {
            onset[barStart] = true;
        }

        for (int local = halfBeat; local < pattern.stepsPerBar; local += halfBeat) {
            const int step = barStart + local;
            if (step >= pattern.patternSteps) {
                break;
            }
            const bool beatStart = (local % pattern.stepsPerBeat) == 0;
            const float probability = beatStart ? 0.88f : flowProbability;
            if (rng.nextFloat() < probability) {
                onset[step] = true;
            }
        }
    }
}

void reinforceRockBeatAnchors(std::array<bool, kMaxPatternSteps>& onset,
                              const PatternState& pattern,
                              const Controls& controls) {
    if (controls.genre != GenreId::rock ||
        pattern.stepsPerBeat <= 0 ||
        pattern.patternSteps <= 0) {
        return;
    }

    for (int step = 0; step < pattern.patternSteps; step += pattern.stepsPerBeat) {
        onset[step] = true;
    }
}

void reinforceMoroderPulse(std::array<bool, kMaxPatternSteps>& onset,
                           const PatternState& pattern,
                           const Controls& controls,
                           Rng& rng) {
    if (controls.genre != GenreId::moroder ||
        pattern.stepsPerBeat <= 0 ||
        pattern.patternSteps <= 0) {
        return;
    }

    const int pulseStep = std::max(1, pattern.stepsPerBeat / 4);
    const int phraseSteps = pattern.stepsPerBar > 0 ? pattern.stepsPerBar : pattern.stepsPerBeat * 4;
    for (int step = 0; step < pattern.patternSteps; step += pulseStep) {
        const int local = phraseSteps > 0 ? step % phraseSteps : step;
        const bool beatStart = isBeatStartStep(pattern, step);
        const bool offbeatPulse = !beatStart && (step % pulseStep) == 0;
        const bool phraseTurn = local >= std::max(0, phraseSteps - pattern.stepsPerBeat);

        float probability = beatStart ? 0.88f : 0.72f;
        probability += controls.density * 0.20f;
        probability += offbeatPulse ? controls.color * 0.08f : 0.0f;
        probability -= phraseTurn ? (0.14f - controls.hold * 0.08f) : 0.0f;

        if (step == 0 || (controls.density >= 0.78f && !phraseTurn) ||
            rng.nextFloat() < clampf(probability, 0.52f, 0.99f)) {
            onset[step] = true;
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
    case GenreId::rock:
        holdBias = clampf(holdBias * 1.15f + 0.10f, 0.0f, 1.0f);
        break;
    case GenreId::moroder:
        holdBias = clampf(holdBias * 0.35f + 0.08f, 0.0f, 1.0f);
        break;
    case GenreId::jazz:
        holdBias *= 0.58f;
        break;
    case GenreId::fugue:
        holdBias = clampf(holdBias * 0.82f + 0.12f, 0.0f, 1.0f);
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

[[nodiscard]] int moroderDegreeForEvent(const PatternState& pattern,
                                        const Controls& controls,
                                        Rng& rng,
                                        const NoteEvent& event,
                                        const int previousDegree) {
    const int degreeCount = std::max(1, scaleDegreeCount(controls));
    const int stepInBar = stepInBarFor(pattern, event.startStep);
    const int pulseStep = std::max(1, pattern.stepsPerBeat / 4);
    const int pulseIndex = pulseStep > 0 ? stepInBar / pulseStep : 0;
    const int barIndex = pattern.stepsPerBar > 0 ? event.startStep / pattern.stepsPerBar : 0;
    const int beatIndex = pattern.stepsPerBeat > 0 ? stepInBar / pattern.stepsPerBeat : 0;
    const int stepInBeat = pattern.stepsPerBeat > 0 ? stepInBar % pattern.stepsPerBeat : 0;
    const bool beatStart = pattern.stepsPerBeat > 0 && (stepInBar % pattern.stepsPerBeat) == 0;
    const bool phraseEnd = pattern.stepsPerBar > 0 && stepInBar >= pattern.stepsPerBar - pattern.stepsPerBeat;
    const int fifth = std::min(4, degreeCount - 1);
    const int sixth = std::min(5, degreeCount - 1);
    const int seventh = std::min(6, degreeCount - 1);
    const int third = std::min(2, degreeCount - 1);
    const int second = std::min(1, degreeCount - 1);
    const int fourth = std::min(3, degreeCount - 1);

    int phraseRoot = 0;
    switch (((barIndex % 4) + 4) % 4) {
    case 1:
        phraseRoot = controls.color > 0.42f ? third : 0;
        break;
    case 2:
        phraseRoot = controls.color > 0.62f ? fourth : 0;
        break;
    case 3:
        phraseRoot = controls.color > 0.72f ? second : 0;
        break;
    default:
        break;
    }

    int degree = phraseRoot;
    switch (((pulseIndex % 16) + 16) % 16) {
    case 1:
    case 5:
    case 9:
    case 13:
        degree = phraseRoot + fifth + degreeCount;
        break;
    case 3:
        degree = phraseRoot + (controls.color > 0.46f ? sixth : fifth);
        break;
    case 6:
        degree = phraseRoot + (controls.color > 0.34f ? third : 0);
        break;
    case 7:
        degree = phraseRoot + (controls.color > 0.66f ? second : fifth);
        break;
    case 10:
        degree = phraseRoot + (controls.color > 0.54f ? fourth : 0);
        break;
    case 11:
        degree = phraseRoot + (controls.color > 0.58f ? sixth : fifth);
        break;
    case 14:
        degree = phraseRoot + (controls.color > 0.50f ? third : 0);
        break;
    case 15:
        degree = phraseRoot + (controls.color > 0.68f ? seventh : fifth);
        break;
    default:
        break;
    }

    if (phraseEnd && controls.color > 0.68f && rng.nextFloat() < 0.42f + controls.density * 0.24f) {
        degree = rng.nextFloat() < 0.58f ? seventh : second;
    } else if (!beatStart && controls.density > 0.62f && rng.nextFloat() < controls.color * 0.30f) {
        const bool risingTurn = previousDegree <= (phraseRoot + third);
        degree = phraseRoot + (risingTurn ? third : second);
    } else if (stepInBeat == std::max(0, pattern.stepsPerBeat - 1) &&
               controls.color > 0.78f &&
               rng.nextFloat() < 0.26f + controls.density * 0.18f) {
        degree = phraseRoot + (rng.nextFloat() < 0.5f ? second : seventh);
    }

    return clampi(degree, 0, degreeCount * 3 - 1);
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
    reinforceJazzWalkingBeats(onset, pattern, controls, rng);
    reinforceFugueSubjectSteps(onset, pattern, controls, rng);
    reinforceRockBeatAnchors(onset, pattern, controls);
    reinforceMoroderPulse(onset, pattern, controls, rng);

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
    int prevJazzNote = controls.rootNote + registerOffset(controls.reg);
    int prevFugueNote = controls.rootNote + registerOffset(controls.reg);
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
        if (controls.genre == GenreId::fugue) {
            event.note = fugueNoteForEvent(pattern, controls, event, prevFugueNote);
            prevFugueNote = event.note;
        } else if (controls.genre == GenreId::jazz && !jazzUsesSelectedScaleVocabulary(controls.scale)) {
            event.note = jazzRoleColoredNoteForEvent(pattern, controls, rng, event, prevJazzNote);
            prevJazzNote = event.note;
        } else {
            const int degree = controls.genre == GenreId::jazz
                ? jazzDegreeForEvent(pattern, controls, rng, event, prevDegree)
                : (controls.genre == GenreId::sabbath)
                    ? sabbathCellDegree(pattern, rng, sabbathCell, sabbathCellLen, index)
                    : (controls.genre == GenreId::moroder)
                        ? moroderDegreeForEvent(pattern, controls, rng, event, prevDegree)
                        : chooseDegree(rng, controls, strong, prevDegree);
            prevDegree = degree;
            event.note = noteFromDegree(controls, degree);
            prevJazzNote = event.note;
        }

        const int baseVelocity = controls.genre == GenreId::rock
            ? 92
            : (controls.genre == GenreId::moroder ? 88 : 86);
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
    controls.color = clampf(controls.color, 0.0f, 1.0f);
    controls.vary = clampf(controls.vary, 0.0f, 1.0f);
    controls.followDodge = clampf(controls.followDodge, -1.0f, 1.0f);
    controls.listenChannel = clampi(controls.listenChannel, 1, 16);
    controls.listenNote = clampi(controls.listenNote, 0, 127);
    controls.inputMatchMode = static_cast<InputMatchModeId>(clampi(static_cast<int>(controls.inputMatchMode), 0, static_cast<int>(InputMatchModeId::count) - 1));
    controls.inputSensitivity = clampf(controls.inputSensitivity, 0.0f, 1.0f);
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
           std::fabs(a.color - b.color) > 0.0001f ||
           a.seed != b.seed;
}

int stepsPerBeatForSubdivision(SubdivisionId subdivision) {
    switch (subdivision) {
    case SubdivisionId::eighth: return 2;
    case SubdivisionId::sixteenth: return 4;
    case SubdivisionId::sixteenthTriplet: return 6;
    case SubdivisionId::quarter: return 1;
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
