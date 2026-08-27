#include "melgen_pattern.hpp"

#include "melgen_rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::melgen {
namespace {

struct ScaleDef {
    const int* intervals;
    int count;
};

struct MotifEvent {
    int startStep = 0;
    int durationSteps = 1;
    int degree = 0;
    int velocity = 92;
};

struct Motif {
    std::array<MotifEvent, 64> events {};
    int eventCount = 0;
    int lengthSteps = 0;
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

static_assert((sizeof(kScales) / sizeof(kScales[0])) == static_cast<int>(ScaleId::count),
              "MelGen scale table must match the scale enum");

[[nodiscard]] float clampf(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] std::int32_t nextGenerationSerial(std::int32_t currentSerial)
{
    return currentSerial >= std::numeric_limits<std::int32_t>::max() ? 1 : currentSerial + 1;
}

[[nodiscard]] std::uint32_t seedMixForSerial(const Controls& controls, const std::int32_t serial)
{
    return controls.seed ^
           (static_cast<std::uint32_t>(static_cast<int>(controls.period) + 1) * 2246822519u) ^
           (static_cast<std::uint32_t>(static_cast<int>(controls.contour) + 3) * 3266489917u) ^
           (static_cast<std::uint32_t>(serial) * 2654435761u);
}

[[nodiscard]] ::downspout::Meter resolveMeter(const ::downspout::Meter& meter)
{
    return ::downspout::sanitizeMeter(meter);
}

[[nodiscard]] int contourTargetDegree(const Controls& controls, const float progress, const int maxDegree)
{
    const int mid = maxDegree / 2;
    switch (controls.contour) {
    case ContourId::flat:
        return mid;
    case ContourId::rising:
        return static_cast<int>(std::lround(progress * static_cast<float>(maxDegree)));
    case ContourId::falling:
        return static_cast<int>(std::lround((1.0f - progress) * static_cast<float>(maxDegree)));
    case ContourId::arch:
        return static_cast<int>(std::lround((1.0f - std::fabs(progress * 2.0f - 1.0f)) * static_cast<float>(maxDegree)));
    case ContourId::invertedArch:
        return static_cast<int>(std::lround(std::fabs(progress * 2.0f - 1.0f) * static_cast<float>(maxDegree)));
    case ContourId::wandering:
    case ContourId::count:
        break;
    }
    return mid;
}

[[nodiscard]] int noteFromDegree(const Controls& controls, int degreeIndex)
{
    const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
    degreeIndex = clampi(degreeIndex, 0, scale.count * 5 - 1);
    const int octave = degreeIndex / scale.count;
    const int degree = degreeIndex % scale.count;
    const int interval = scale.intervals[degree] + 12 * octave;
    return clampi(controls.rootNote + registerOffset(controls.reg) + interval, 0, 127);
}

[[nodiscard]] bool strictFugueMode(const Controls& controls)
{
    return controls.period == PeriodId::callAnswer &&
           controls.structure >= 0.88f &&
           controls.leap <= 0.22f &&
           controls.rest <= 0.25f;
}

[[nodiscard]] bool isJazzColorScale(const ScaleId scale)
{
    switch (scale) {
    case ScaleId::dorian:
    case ScaleId::mixolydian:
    case ScaleId::lydian:
    case ScaleId::melodicMinor:
    case ScaleId::altered:
    case ScaleId::halfWholeDiminished:
    case ScaleId::wholeHalfDiminished:
    case ScaleId::bebopDominant:
    case ScaleId::bebopMajor:
    case ScaleId::bebopMinor:
        return true;
    case ScaleId::minor:
    case ScaleId::major:
    case ScaleId::phrygian:
    case ScaleId::pentMinor:
    case ScaleId::blues:
    case ScaleId::harmonicMinor:
    case ScaleId::pentMajor:
    case ScaleId::locrian:
    case ScaleId::phrygianDominant:
    case ScaleId::wholeTone:
    case ScaleId::count:
        break;
    }
    return false;
}

[[nodiscard]] int applyColorToNote(Rng& rng,
                                   const Controls& controls,
                                   const PatternState& pattern,
                                   const int startStep,
                                   const int baseNote,
                                   const int nextNote,
                                   const bool protectCadence)
{
    const float color = clampf(controls.color, 0.0f, 1.0f);
    if (color <= 0.001f || protectCadence) {
        return baseNote;
    }

    const int beatSteps = std::max(1, pattern.stepsPerBeat);
    const bool weakStep = (startStep % beatSteps) != 0;
    float chance = color * (weakStep ? 0.34f : 0.07f);
    if (nextNote >= 0) {
        chance += color * 0.16f;
    }
    if (isJazzColorScale(controls.scale)) {
        chance += color * 0.08f;
    }

    if (rng.nextFloat() >= clampf(chance, 0.0f, 0.82f)) {
        return baseNote;
    }

    int note = baseNote;
    if (nextNote >= 0 && color > 0.40f) {
        if (baseNote < nextNote) {
            note = nextNote - 1;
        } else if (baseNote > nextNote) {
            note = nextNote + 1;
        } else {
            note = nextNote + (rng.nextFloat() < 0.5f ? -1 : 1);
        }

        if (color > 0.78f && weakStep && rng.nextFloat() < 0.35f) {
            note = nextNote + (note < nextNote ? 1 : -1);
        }
    } else {
        note += rng.nextFloat() < 0.5f ? -1 : 1;
    }

    return clampi(note, 0, 127);
}

[[nodiscard]] int phraseLengthStepsFor(const Controls& controls, const PatternState& pattern)
{
    const int bars = clampi(controls.phraseLengthBars, 1, 8);
    return clampi(bars * pattern.stepsPerBar, pattern.stepsPerBeat, pattern.patternSteps);
}

[[nodiscard]] PhraseRole roleForPhrase(const Controls& controls, const int phraseIndex)
{
    switch (controls.period) {
    case PeriodId::aa:
        return phraseIndex == 0 ? PhraseRole::call : PhraseRole::repeat;
    case PeriodId::ab:
        return (phraseIndex % 2) == 0 ? PhraseRole::call : PhraseRole::contrast;
    case PeriodId::aaPrime:
        return phraseIndex == 0 ? PhraseRole::call : PhraseRole::variation;
    case PeriodId::callAnswer:
        return (phraseIndex % 2) == 0 ? PhraseRole::call : PhraseRole::answer;
    case PeriodId::aba:
        if ((phraseIndex % 3) == 0) return PhraseRole::call;
        if ((phraseIndex % 3) == 1) return PhraseRole::contrast;
        return PhraseRole::variation;
    case PeriodId::free:
    case PeriodId::count:
        break;
    }
    return PhraseRole::free;
}

[[nodiscard]] int sourceForPhrase(const Controls& controls, const int phraseIndex)
{
    if (phraseIndex <= 0 || controls.period == PeriodId::free) {
        return -1;
    }
    switch (roleForPhrase(controls, phraseIndex)) {
    case PhraseRole::repeat:
    case PhraseRole::variation:
    case PhraseRole::answer:
        return phraseIndex - 1;
    case PhraseRole::cadence:
    case PhraseRole::contrast:
    case PhraseRole::call:
    case PhraseRole::free:
        break;
    }
    return -1;
}

void appendEvent(PatternState& pattern, const int start, const int duration, const int note, const int velocity)
{
    if (pattern.eventCount >= kMaxEvents || start < 0 || start >= pattern.patternSteps) {
        return;
    }
    NoteEvent& event = pattern.events[pattern.eventCount++];
    event.startStep = start;
    event.durationSteps = clampi(duration, 1, pattern.patternSteps - start);
    event.note = clampi(note, 0, 127);
    event.velocity = clampi(velocity, 1, 127);
}

void appendMotifEvent(Motif& motif, const MotifEvent& event)
{
    if (motif.eventCount >= static_cast<int>(motif.events.size())) {
        return;
    }
    motif.events[motif.eventCount++] = event;
}

[[nodiscard]] int chooseDuration(Rng& rng, const Controls& controls, const PatternState& pattern)
{
    const int shortDur = std::max(1, pattern.stepsPerBeat / 2);
    const int beatDur = std::max(1, pattern.stepsPerBeat);
    if (rng.nextFloat() < controls.hold) {
        return rng.nextFloat() < 0.35f ? beatDur * 2 : beatDur;
    }
    return shortDur;
}

[[nodiscard]] int chooseNextDegree(Rng& rng,
                                   const Controls& controls,
                                   const int previous,
                                   const int target,
                                   const int maxDegree)
{
    const float structure = clampf(controls.structure, 0.0f, 1.0f);
    const float color = clampf(controls.color, 0.0f, 1.0f);
    const float leapChance = clampf(controls.leap * (1.15f - structure * 0.55f) + color * 0.08f, 0.0f, 1.0f);
    int degree = previous;

    if (rng.nextFloat() < leapChance) {
        const int jump = rng.nextInt(2, std::max(2, 2 + static_cast<int>(std::lround(controls.range * 7.0f))));
        degree += rng.nextFloat() < 0.5f ? -jump : jump;
    } else {
        const int direction = target > previous ? 1 : (target < previous ? -1 : 0);
        const int wander = rng.nextInt(-1, 1);
        if (rng.nextFloat() < color * 0.18f) {
            degree += rng.nextFloat() < 0.5f ? -1 : 1;
        } else {
            degree += rng.nextFloat() < structure ? direction : wander;
        }
    }

    if (rng.nextFloat() < structure * 0.35f) {
        degree = static_cast<int>(std::lround(static_cast<float>(degree) * 0.7f + static_cast<float>(target) * 0.3f));
    }

    if (rng.nextFloat() < color * (1.0f - structure) * 0.20f) {
        degree += rng.nextInt(-2, 2);
    }

    return clampi(degree, 0, maxDegree);
}

[[nodiscard]] Motif generateFreshMotif(Rng& rng, const Controls& controls, const PatternState& pattern, const int phraseIndex)
{
    Motif motif;
    motif.lengthSteps = phraseLengthStepsFor(controls, pattern);

    const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
    const int maxDegree = clampi(scale.count - 1 + static_cast<int>(std::lround(controls.range * 17.0f)), 3, scale.count * 4 - 1);

    if (strictFugueMode(controls)) {
        constexpr int kSubjectDegrees[] = {0, 1, 2, 4, 3, 2, 1, 0};
        const int beatSteps = std::max(1, pattern.stepsPerBeat);
        const int spacing = controls.density >= 0.78f ? std::max(1, beatSteps / 2) : beatSteps;
        const int duration = clampi(static_cast<int>(std::lround(static_cast<float>(spacing) * (0.72f + controls.hold * 0.32f))),
                                    1,
                                    std::max(1, beatSteps));
        const int phraseLift = (phraseIndex % 2) == 0 ? 0 : 4;

        for (int step = 0, subjectIndex = 0; step < motif.lengthSteps; step += spacing, ++subjectIndex) {
            if (controls.density < 0.95f &&
                subjectIndex > 0 &&
                (subjectIndex % 5) == 4 &&
                rng.nextFloat() > controls.density) {
                continue;
            }
            const int degree = clampi(kSubjectDegrees[subjectIndex % static_cast<int>(sizeof(kSubjectDegrees) / sizeof(kSubjectDegrees[0]))] + phraseLift,
                                      0,
                                      maxDegree);
            const bool strong = (step % beatSteps) == 0;
            const int accent = strong ? static_cast<int>(std::lround(controls.accent * 22.0f)) : 0;
            appendMotifEvent(motif, {step, duration, degree, 86 + accent});
        }

        if (motif.eventCount == 0) {
            appendMotifEvent(motif, {0, beatSteps, 0, 96});
        }
        return motif;
    }

    int degree = contourTargetDegree(controls, phraseIndex % 2 == 0 ? 0.12f : 0.36f, maxDegree);

    for (int step = 0; step < motif.lengthSteps;) {
        const float progress = motif.lengthSteps > 1 ? static_cast<float>(step) / static_cast<float>(motif.lengthSteps - 1) : 0.0f;
        const bool strong = (step % std::max(1, pattern.stepsPerBeat)) == 0;
        const float restChance = clampf(controls.rest * (strong ? 0.45f : 1.15f), 0.0f, 0.9f);
        const float hitChance = clampf(controls.density * (strong ? 1.35f : 0.85f), 0.02f, 0.98f);
        const int duration = chooseDuration(rng, controls, pattern);

        if (rng.nextFloat() >= restChance && rng.nextFloat() < hitChance) {
            const int target = contourTargetDegree(controls, progress, maxDegree);
            degree = chooseNextDegree(rng, controls, degree, target, maxDegree);
            const int accent = strong ? static_cast<int>(std::lround(controls.accent * 22.0f)) : 0;
            appendMotifEvent(motif, {step, duration, degree, 82 + accent + rng.nextInt(-6, 9)});
        }

        const int move = rng.nextFloat() < 0.70f ? std::max(1, pattern.stepsPerBeat / 2) : std::max(1, pattern.stepsPerBeat);
        step += move;
    }

    if (motif.eventCount == 0) {
        appendMotifEvent(motif, {0, std::max(1, pattern.stepsPerBeat), contourTargetDegree(controls, 0.0f, maxDegree), 96});
    }

    return motif;
}

[[nodiscard]] Motif deriveMotif(Rng& rng,
                                const Controls& controls,
                                const PatternState& pattern,
                                const Motif& source,
                                const PhraseRole role)
{
    Motif motif = source;
    motif.lengthSteps = phraseLengthStepsFor(controls, pattern);

    const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
    const int maxDegree = clampi(scale.count - 1 + static_cast<int>(std::lround(controls.range * 17.0f)), 3, scale.count * 4 - 1);
    const bool strictFugueAnswer = strictFugueMode(controls) && role == PhraseRole::answer;
    const int shift = strictFugueAnswer ? 4 :
        (controls.answer == AnswerId::transpose ? rng.nextInt(1, 3) :
            (role == PhraseRole::answer ? 2 : 0));
    const bool invert = controls.answer == AnswerId::invert ||
                        (strictFugueAnswer
                            ? controls.color >= 0.75f
                            : (role == PhraseRole::answer && rng.nextFloat() < controls.structure * 0.25f));
    const float mutation = strictFugueAnswer
        ? clampf(controls.color * 0.08f, 0.0f, 1.0f)
        : clampf(1.0f - controls.structure + controls.color * 0.25f, 0.0f, 1.0f);
    const int center = maxDegree / 2;

    for (int i = 0; i < motif.eventCount; ++i) {
        MotifEvent& event = motif.events[i];
        if (controls.answer == AnswerId::compress && role == PhraseRole::answer) {
            event.startStep = event.startStep / 2;
            event.durationSteps = std::max(1, event.durationSteps / 2);
        } else if (controls.answer == AnswerId::expand && role == PhraseRole::answer) {
            event.startStep = std::min(motif.lengthSteps - 1, event.startStep * 2);
            event.durationSteps = std::max(1, event.durationSteps * 2);
        }

        if (invert) {
            event.degree = center - (event.degree - center);
        }
        event.degree += shift;

        if (role == PhraseRole::variation && i >= motif.eventCount - 2) {
            event.degree += rng.nextInt(-2, 2);
        } else if (role == PhraseRole::contrast) {
            event.degree = contourTargetDegree(controls, 1.0f - (static_cast<float>(event.startStep) / static_cast<float>(std::max(1, motif.lengthSteps))), maxDegree);
        } else if (rng.nextFloat() < mutation * 0.35f) {
            event.degree += rng.nextInt(-2, 2);
        }

        event.degree = clampi(event.degree, 0, maxDegree);
        event.velocity = clampi(event.velocity + rng.nextInt(-5, 5), 1, 127);
    }

    return motif;
}

void applyCadence(const Controls& controls, Motif& motif, const PhraseRole role)
{
    if (motif.eventCount <= 0 || controls.cadence <= 0.001f) {
        return;
    }

    MotifEvent& last = motif.events[motif.eventCount - 1];
    const bool strongEnding = role == PhraseRole::answer ||
                              role == PhraseRole::variation ||
                              role == PhraseRole::cadence ||
                              controls.period == PeriodId::aa;
    const float targetStrength = strongEnding ? controls.cadence : controls.cadence * 0.45f;
    if (targetStrength < 0.15f) {
        return;
    }

    if (strictFugueMode(controls) && role == PhraseRole::answer) {
        last.degree = 4;
    } else if (targetStrength > 0.74f) {
        last.degree = 0;
    } else if (targetStrength > 0.42f) {
        last.degree = 4;
    } else {
        last.degree = 2;
    }
}

void realizeMotif(PatternState& pattern, const Controls& controls, const Motif& motif, const int phraseStart, Rng& rng)
{
    for (int i = 0; i < motif.eventCount; ++i) {
        const MotifEvent& event = motif.events[i];
        const int start = phraseStart + event.startStep;
        if (start >= pattern.patternSteps) {
            continue;
        }
        const int baseNote = noteFromDegree(controls, event.degree);
        const int nextNote = i + 1 < motif.eventCount ? noteFromDegree(controls, motif.events[i + 1].degree) : -1;
        const bool protectCadence = i == motif.eventCount - 1 && controls.cadence > 0.20f;
        const int note = applyColorToNote(rng, controls, pattern, event.startStep, baseNote, nextNote, protectCadence);
        appendEvent(pattern, start, event.durationSteps, note, event.velocity);
    }
}

void sortEvents(PatternState& pattern)
{
    std::sort(pattern.events.begin(), pattern.events.begin() + pattern.eventCount, [](const NoteEvent& a, const NoteEvent& b) {
        if (a.startStep == b.startStep) {
            return a.durationSteps < b.durationSteps;
        }
        return a.startStep < b.startStep;
    });
}

}  // namespace

Controls clampControls(const Controls& raw)
{
    Controls controls = raw;
    controls.rootNote = clampi(controls.rootNote, 0, 127);
    controls.scale = static_cast<ScaleId>(clampi(static_cast<int>(controls.scale), 0, static_cast<int>(ScaleId::count) - 1));
    controls.channel = clampi(controls.channel, 1, 16);
    controls.lengthBeats = clampi(controls.lengthBeats, kMinLengthBeats, kMaxLengthBeats);
    controls.phraseLengthBars = clampi(controls.phraseLengthBars, 1, 8);
    controls.subdivision = static_cast<SubdivisionId>(clampi(static_cast<int>(controls.subdivision), 0, static_cast<int>(SubdivisionId::count) - 1));
    controls.period = static_cast<PeriodId>(clampi(static_cast<int>(controls.period), 0, static_cast<int>(PeriodId::count) - 1));
    controls.contour = static_cast<ContourId>(clampi(static_cast<int>(controls.contour), 0, static_cast<int>(ContourId::count) - 1));
    controls.answer = static_cast<AnswerId>(clampi(static_cast<int>(controls.answer), 0, static_cast<int>(AnswerId::count) - 1));
    controls.density = clampf(controls.density, 0.0f, 1.0f);
    controls.reg = clampi(controls.reg, 0, 4);
    controls.hold = clampf(controls.hold, 0.0f, 1.0f);
    controls.accent = clampf(controls.accent, 0.0f, 1.0f);
    controls.structure = clampf(controls.structure, 0.0f, 1.0f);
    controls.range = clampf(controls.range, 0.0f, 1.0f);
    controls.leap = clampf(controls.leap, 0.0f, 1.0f);
    controls.rest = clampf(controls.rest, 0.0f, 1.0f);
    controls.cadence = clampf(controls.cadence, 0.0f, 1.0f);
    controls.color = clampf(controls.color, 0.0f, 1.0f);
    controls.vary = clampf(controls.vary, 0.0f, 1.0f);
    controls.follow = clampf(controls.follow, 0.0f, 1.0f);
    controls.seed = std::max<std::uint32_t>(1u, controls.seed);
    return controls;
}

bool structuralControlsChanged(const Controls& a, const Controls& b)
{
    return a.rootNote != b.rootNote ||
           a.scale != b.scale ||
           a.lengthBeats != b.lengthBeats ||
           a.phraseLengthBars != b.phraseLengthBars ||
           a.subdivision != b.subdivision ||
           a.period != b.period ||
           a.contour != b.contour ||
           a.answer != b.answer ||
           a.density != b.density ||
           a.reg != b.reg ||
           a.hold != b.hold ||
           a.accent != b.accent ||
           a.structure != b.structure ||
           a.range != b.range ||
           a.leap != b.leap ||
           a.rest != b.rest ||
           a.cadence != b.cadence ||
           a.color != b.color ||
           a.seed != b.seed;
}

int stepsPerBeatForSubdivision(const SubdivisionId subdivision)
{
    switch (subdivision) {
    case SubdivisionId::quarter: return 1;
    case SubdivisionId::eighth: return 2;
    case SubdivisionId::sixteenthTriplet: return 6;
    case SubdivisionId::sixteenth:
    case SubdivisionId::count:
        break;
    }
    return 4;
}

int registerOffset(const int reg)
{
    return (clampi(reg, 0, 4) - 1) * 12;
}

int nearestScaleNote(const Controls& rawControls, const int targetNote, const int minNote, const int maxNote)
{
    const Controls controls = clampControls(rawControls);
    const int lo = clampi(std::min(minNote, maxNote), 0, 127);
    const int hi = clampi(std::max(minNote, maxNote), 0, 127);
    const int target = clampi(targetNote, lo, hi);
    const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
    const int rootPc = (controls.rootNote % 12 + 12) % 12;

    int best = target;
    int bestDistance = 128;
    for (int note = lo; note <= hi; ++note) {
        const int interval = ((note - rootPc) % 12 + 12) % 12;
        bool inScale = false;
        for (int i = 0; i < scale.count; ++i) {
            if (scale.intervals[i] == interval) {
                inScale = true;
                break;
            }
        }
        if (!inScale) {
            continue;
        }

        const int distance = std::abs(note - target);
        if (distance < bestDistance || (distance == bestDistance && std::abs(note - controls.rootNote) < std::abs(best - controls.rootNote))) {
            best = note;
            bestDistance = distance;
        }
    }

    return best;
}

void regeneratePattern(PatternState& pattern,
                       const Controls& rawControls,
                       const ::downspout::Meter& meter,
                       const bool regenRhythm,
                       const bool regenNotes)
{
    const Controls controls = clampControls(rawControls);
    const PatternState previous = pattern;
    pattern = PatternState {};
    pattern.version = kPatternStateVersion;
    pattern.generationSerial = nextGenerationSerial(previous.generationSerial);
    pattern.stepsPerBeat = stepsPerBeatForSubdivision(controls.subdivision);
    pattern.meter = resolveMeter(meter);
    pattern.stepsPerBar = ::downspout::meterStepsPerBar(pattern.meter, pattern.stepsPerBeat);
    pattern.patternSteps = clampi(controls.lengthBeats * pattern.stepsPerBeat, 1, kMaxPatternSteps);

    Rng rng;
    rng.seed(seedMixForSerial(controls, (regenRhythm && regenNotes) ? pattern.generationSerial : previous.generationSerial));

    const int phraseLength = phraseLengthStepsFor(controls, pattern);
    const int phraseCount = clampi((pattern.patternSteps + phraseLength - 1) / phraseLength, 1, kMaxPhrases);
    pattern.phraseCount = phraseCount;

    std::array<Motif, kMaxPhrases> motifs {};
    for (int phrase = 0; phrase < phraseCount; ++phrase) {
        const int start = phrase * phraseLength;
        const PhraseRole role = roleForPhrase(controls, phrase);
        const int source = sourceForPhrase(controls, phrase);
        pattern.phrases[phrase] = {start, std::min(phraseLength, pattern.patternSteps - start), role, source};

        if (!regenRhythm && previous.eventCount > 0) {
            continue;
        }

        if (source >= 0 && source < phrase && controls.structure > 0.05f) {
            motifs[phrase] = deriveMotif(rng, controls, pattern, motifs[source], role);
        } else {
            motifs[phrase] = generateFreshMotif(rng, controls, pattern, phrase);
        }
        applyCadence(controls, motifs[phrase], role);
    }

    if (!regenRhythm && previous.eventCount > 0) {
        pattern.eventCount = previous.eventCount;
        for (int i = 0; i < previous.eventCount; ++i) {
            NoteEvent event = previous.events[i];
            if (regenNotes) {
                const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
                const int maxDegree = clampi(scale.count - 1 + static_cast<int>(std::lround(controls.range * 17.0f)), 3, scale.count * 4 - 1);
                const int baseNote = noteFromDegree(controls, rng.nextInt(0, maxDegree));
                const int nextNote = i + 1 < previous.eventCount ? previous.events[i + 1].note : -1;
                event.note = applyColorToNote(rng, controls, pattern, event.startStep, baseNote, nextNote, false);
            }
            pattern.events[i] = event;
        }
    } else {
        for (int phrase = 0; phrase < phraseCount; ++phrase) {
            realizeMotif(pattern, controls, motifs[phrase], pattern.phrases[phrase].startStep, rng);
        }
    }

    sortEvents(pattern);
}

void partialNoteMutation(PatternState& pattern, const Controls& rawControls, const float strength)
{
    const Controls controls = clampControls(rawControls);
    if (pattern.eventCount <= 0) {
        regeneratePattern(pattern, controls, pattern.meter, true, true);
        return;
    }

    Rng rng;
    rng.seed(controls.seed ^
             (static_cast<std::uint32_t>(pattern.generationSerial) * 747796405u) ^
             (static_cast<std::uint32_t>(pattern.eventCount) * 2891336453u));

    const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
    const int maxDegree = clampi(scale.count - 1 + static_cast<int>(std::lround(controls.range * 17.0f)), 3, scale.count * 4 - 1);
    const float chance = clampf(strength, 0.0f, 1.0f);
    for (int i = 0; i < pattern.eventCount; ++i) {
        if (rng.nextFloat() < chance) {
            const int baseNote = noteFromDegree(controls, rng.nextInt(0, maxDegree));
            const int nextNote = i + 1 < pattern.eventCount ? pattern.events[i + 1].note : -1;
            pattern.events[i].note = applyColorToNote(rng, controls, pattern, pattern.events[i].startStep, baseNote, nextNote, false);
            pattern.events[i].velocity = clampi(pattern.events[i].velocity + rng.nextInt(-10, 10), 1, 127);
        }
    }

    pattern.generationSerial = nextGenerationSerial(pattern.generationSerial);
}

}  // namespace downspout::melgen
