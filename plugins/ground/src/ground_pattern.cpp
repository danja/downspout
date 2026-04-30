#include "ground_pattern.hpp"

#include "ground_rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace downspout::ground {
namespace {

struct ScaleDef {
    const int* intervals;
    int count;
};

struct PhraseEventList {
    std::array<NoteEvent, 128> events {};
    int count = 0;
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

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue)
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] std::int32_t nextGenerationSerial(const std::int32_t currentSerial)
{
    return currentSerial >= std::numeric_limits<std::int32_t>::max() ? 1 : currentSerial + 1;
}

[[nodiscard]] std::uint32_t seedMix(const Controls& controls,
                                    const std::int32_t generationSerial,
                                    const int phraseIndex,
                                    const std::uint32_t salt)
{
    return controls.seed ^
           (static_cast<std::uint32_t>(generationSerial) * 2654435761u) ^
           (static_cast<std::uint32_t>(phraseIndex + 1) * 2246822519u) ^
           salt;
}

[[nodiscard]] int nearestFromSet(const int raw, const std::initializer_list<int> values)
{
    int best = *values.begin();
    int bestDistance = std::abs(raw - best);
    for (const int value : values) {
        const int distance = std::abs(raw - value);
        if (distance < bestDistance) {
            best = value;
            bestDistance = distance;
        }
    }
    return best;
}

[[nodiscard]] int registerOctaveOffset(const int reg)
{
    return clampi(reg, 0, 3) - 1;
}

[[nodiscard]] int roleBaseDegree(const PhraseRoleId role, Rng& rng)
{
    switch (role) {
    case PhraseRoleId::statement:
        return rng.nextFloat() < 0.75f ? 0 : 2;
    case PhraseRoleId::answer:
        return rng.nextFloat() < 0.55f ? 3 : 4;
    case PhraseRoleId::climb:
        return rng.nextFloat() < 0.55f ? 4 : 5;
    case PhraseRoleId::pedal:
        return rng.nextFloat() < 0.70f ? 0 : 4;
    case PhraseRoleId::breakdown:
        return 0;
    case PhraseRoleId::cadence:
        return rng.nextFloat() < 0.65f ? 4 : 5;
    case PhraseRoleId::release:
        return rng.nextFloat() < 0.60f ? 0 : 2;
    }
    return 0;
}

[[nodiscard]] float roleIntensity(const PhraseRoleId role)
{
    switch (role) {
    case PhraseRoleId::statement: return 0.72f;
    case PhraseRoleId::answer: return 0.62f;
    case PhraseRoleId::climb: return 0.94f;
    case PhraseRoleId::pedal: return 0.48f;
    case PhraseRoleId::breakdown: return 0.26f;
    case PhraseRoleId::cadence: return 0.86f;
    case PhraseRoleId::release: return 0.44f;
    }
    return 0.60f;
}

[[nodiscard]] float roleMotionBias(const PhraseRoleId role, const float motion)
{
    switch (role) {
    case PhraseRoleId::statement: return motion * 0.75f;
    case PhraseRoleId::answer: return motion * 0.60f;
    case PhraseRoleId::climb: return clampf(motion * 1.25f + 0.12f, 0.0f, 1.0f);
    case PhraseRoleId::pedal: return motion * 0.18f;
    case PhraseRoleId::breakdown: return motion * 0.15f;
    case PhraseRoleId::cadence: return motion * 0.55f;
    case PhraseRoleId::release: return motion * 0.35f;
    }
    return motion;
}

[[nodiscard]] int noteFromDegree(const Controls& controls, int degreeIndex, const int phraseRegisterOffset)
{
    const ScaleDef& scale = kScales[static_cast<int>(controls.scale)];
    int octave = degreeIndex / scale.count;
    int degree = degreeIndex % scale.count;
    if (degree < 0) {
        degree += scale.count;
        octave -= 1;
    }

    const int interval = scale.intervals[degree] + 12 * (octave + registerOctaveOffset(controls.reg) + phraseRegisterOffset);
    return clampi(controls.rootNote + interval, 0, 127);
}

[[nodiscard]] PhraseRoleId chooseRole(const Controls& controls,
                                      Rng& rng,
                                      const int phraseIndex,
                                      const int phraseCount,
                                      const int peakPhrase)
{
    if (phraseIndex <= 0) {
        return PhraseRoleId::statement;
    }
    if (phraseIndex == phraseCount - 1) {
        return controls.cadence >= 0.28f ? PhraseRoleId::cadence : PhraseRoleId::release;
    }
    if (phraseIndex == peakPhrase || (phraseIndex + 1) == peakPhrase) {
        return PhraseRoleId::climb;
    }

    const float progress = static_cast<float>(phraseIndex) / static_cast<float>(std::max(1, phraseCount - 1));
    const float breakProb = controls.tension * 0.24f + (progress > 0.55f ? 0.10f : 0.0f);
    if (rng.nextFloat() < breakProb) {
        return PhraseRoleId::breakdown;
    }

    if (controls.sequence > 0.45f && (phraseIndex % 2) == 1) {
        return PhraseRoleId::answer;
    }

    if (rng.nextFloat() < 0.12f + controls.cadence * 0.12f) {
        return PhraseRoleId::pedal;
    }

    return (phraseIndex % 2) == 0 ? PhraseRoleId::statement : PhraseRoleId::answer;
}

[[nodiscard]] int phraseRegisterOffset(const Controls& controls,
                                       const PhraseRoleId role,
                                       const int phraseIndex,
                                       const int phraseCount)
{
    const float progress = static_cast<float>(phraseIndex) / static_cast<float>(std::max(1, phraseCount - 1));
    int offset = static_cast<int>(std::floor(progress * controls.registerArc * 3.0f));

    switch (role) {
    case PhraseRoleId::pedal:
    case PhraseRoleId::breakdown:
        offset -= 1;
        break;
    case PhraseRoleId::climb:
    case PhraseRoleId::cadence:
        offset += 1;
        break;
    default:
        break;
    }

    return clampi(offset, -1, 2);
}

[[nodiscard]] int chooseDegreeForStep(const PhrasePlan& plan,
                                      const Controls& controls,
                                      Rng& rng,
                                      const int ordinal,
                                      const int eventCountEstimate,
                                      const int localStep,
                                      const int phraseSteps,
                                      const int previousDegree)
{
    const bool strongBeat = (localStep % 4) == 0;
    const int base = plan.rootDegree;

    switch (plan.role) {
    case PhraseRoleId::statement:
        if (strongBeat) {
            return rng.nextFloat() < 0.72f ? base : base + 2;
        }
        return clampi(previousDegree + rng.nextInt(-1, 1), base - 1, base + 3);
    case PhraseRoleId::answer:
        if (strongBeat && rng.nextFloat() < 0.60f) {
            return base;
        }
        return clampi(previousDegree + rng.nextInt(-2, 0), base - 2, base + 2);
    case PhraseRoleId::climb: {
        const float progress = static_cast<float>(ordinal) / static_cast<float>(std::max(1, eventCountEstimate - 1));
        const int rise = static_cast<int>(std::floor(progress * (2.0f + controls.motion * 4.0f)));
        return clampi(base + rise + rng.nextInt(0, 1), base, base + 6);
    }
    case PhraseRoleId::pedal:
        return rng.nextFloat() < 0.82f ? base : base + 1;
    case PhraseRoleId::breakdown:
        return rng.nextFloat() < 0.70f ? base : base - 1;
    case PhraseRoleId::cadence: {
        if ((localStep + 4) >= phraseSteps) {
            return 0;
        }
        if (ordinal >= std::max(1, eventCountEstimate - 2)) {
            return rng.nextFloat() < 0.60f ? 4 : 6;
        }
        return clampi(base + rng.nextInt(0, 2), base, base + 3);
    }
    case PhraseRoleId::release:
        return clampi(base - (ordinal / 2) + rng.nextInt(-1, 0), 0, std::max(0, base + 1));
    }

    return base;
}

void appendOnset(std::array<bool, 128>& onset, const int localStep)
{
    if (localStep >= 0 && localStep < static_cast<int>(onset.size())) {
        onset[static_cast<std::size_t>(localStep)] = true;
    }
}

void maybeAppendOnset(std::array<bool, 128>& onset,
                      Rng& rng,
                      const float probability,
                      const int localStep)
{
    if (probability > 0.0f && rng.nextFloat() < probability) {
        appendOnset(onset, localStep);
    }
}

void addRoleSyncopation(std::array<bool, 128>& onset,
                        const Controls& controls,
                        const PhrasePlan& plan,
                        Rng& rng,
                        const int barIndex,
                        const int phraseBars,
                        const int localBarStart,
                        const float density,
                        const float motion)
{
    switch (plan.role) {
    case PhraseRoleId::statement:
        if (barIndex > 0 && density > 0.34f) {
            appendOnset(onset, localBarStart + (((barIndex % 2) == 0 && motion > 0.38f) ? 15 : 7));
        }
        break;
    case PhraseRoleId::answer:
        if (density > 0.28f) {
            appendOnset(onset, localBarStart + (((barIndex % 2) == 0) ? 10 : 15));
        }
        break;
    case PhraseRoleId::climb:
        if (motion > 0.32f) {
            appendOnset(onset, localBarStart + (((barIndex % 2) == 0) ? 6 : 14));
        }
        maybeAppendOnset(onset, rng, density * (0.20f + motion * 0.30f), localBarStart + 15);
        break;
    case PhraseRoleId::pedal:
        if (density > 0.64f && barIndex < phraseBars - 1) {
            appendOnset(onset, localBarStart + 15);
        }
        break;
    case PhraseRoleId::breakdown:
        maybeAppendOnset(onset, rng, density * 0.18f, localBarStart + 11);
        break;
    case PhraseRoleId::cadence:
        if (barIndex == phraseBars - 1) {
            appendOnset(onset, localBarStart + 10);
            appendOnset(onset, localBarStart + 14);
        } else if (density > 0.30f) {
            appendOnset(onset, localBarStart + 7);
        }
        break;
    case PhraseRoleId::release:
        if (barIndex < phraseBars - 1 && density > 0.24f) {
            appendOnset(onset, localBarStart + (((barIndex % 2) == 0) ? 7 : 15));
        }
        break;
    }

    if (controls.style == StyleId::grounded && motion > 0.52f) {
        appendOnset(onset, localBarStart + (((barIndex % 2) == 0) ? 10 : 15));
    } else if (controls.style == StyleId::pulse && density > 0.38f) {
        appendOnset(onset, localBarStart + (((barIndex % 2) == 0) ? 6 : 14));
    } else if (controls.style == StyleId::march && motion > 0.46f) {
        maybeAppendOnset(onset, rng, 0.40f + density * 0.20f, localBarStart + 10);
    }
}

void buildBarOnsets(std::array<bool, 128>& onset,
                    const Controls& controls,
                    const PhrasePlan& plan,
                    Rng& rng,
                    const int barIndex,
                    const int phraseBars,
                    const int localBarStart)
{
    const float density = clampf(controls.density * plan.intensity, 0.05f, 1.0f);
    const float motion = clampf(plan.motionBias, 0.0f, 1.0f);

    if (plan.role == PhraseRoleId::pedal || controls.style == StyleId::drone) {
        appendOnset(onset, localBarStart);
        if (density > 0.72f && (barIndex % 2) == 1) {
            appendOnset(onset, localBarStart + 8);
        }
        return;
    }

    if (plan.role == PhraseRoleId::breakdown) {
        if (barIndex == 0 || rng.nextFloat() < density * 0.40f) {
            appendOnset(onset, localBarStart);
        }
        if (barIndex == phraseBars - 1 && rng.nextFloat() < 0.35f) {
            appendOnset(onset, localBarStart + 12);
        }
        return;
    }

    switch (controls.style) {
    case StyleId::grounded:
        appendOnset(onset, localBarStart);
        if (rng.nextFloat() < density * 0.72f) appendOnset(onset, localBarStart + 8);
        if (rng.nextFloat() < density * 0.38f) appendOnset(onset, localBarStart + 12);
        break;
    case StyleId::ostinato:
        appendOnset(onset, localBarStart);
        appendOnset(onset, localBarStart + 3);
        appendOnset(onset, localBarStart + 8);
        appendOnset(onset, localBarStart + 11);
        if (rng.nextFloat() < density * 0.45f) appendOnset(onset, localBarStart + 14);
        break;
    case StyleId::march:
        appendOnset(onset, localBarStart);
        appendOnset(onset, localBarStart + 4);
        appendOnset(onset, localBarStart + 8);
        appendOnset(onset, localBarStart + 12);
        if (rng.nextFloat() < density * 0.25f) appendOnset(onset, localBarStart + 14);
        break;
    case StyleId::pulse:
        appendOnset(onset, localBarStart);
        appendOnset(onset, localBarStart + 8);
        if (rng.nextFloat() < density * 0.40f) appendOnset(onset, localBarStart + 12);
        break;
    case StyleId::drone:
        appendOnset(onset, localBarStart);
        break;
    case StyleId::climb:
        appendOnset(onset, localBarStart);
        appendOnset(onset, localBarStart + 4);
        appendOnset(onset, localBarStart + 8);
        appendOnset(onset, localBarStart + 12);
        if (rng.nextFloat() < density * 0.65f) appendOnset(onset, localBarStart + 2);
        if (rng.nextFloat() < density * (0.35f + motion * 0.55f)) appendOnset(onset, localBarStart + 10);
        if (rng.nextFloat() < density * motion * 0.45f) appendOnset(onset, localBarStart + 14);
        break;
    default:
        break;
    }

    if (plan.role == PhraseRoleId::cadence && barIndex == phraseBars - 1) {
        appendOnset(onset, localBarStart);
        appendOnset(onset, localBarStart + 12);
    } else if (plan.role == PhraseRoleId::climb && rng.nextFloat() < density * 0.45f) {
        appendOnset(onset, localBarStart + 6);
    } else if (plan.role == PhraseRoleId::release) {
        if (rng.nextFloat() < density * 0.22f) appendOnset(onset, localBarStart + 8);
    }

    addRoleSyncopation(onset, controls, plan, rng, barIndex, phraseBars, localBarStart, density, motion);
}

[[nodiscard]] int nextOnset(const std::array<bool, 128>& onset, const int totalSteps, const int step)
{
    for (int index = step + 1; index < totalSteps; ++index) {
        if (onset[static_cast<std::size_t>(index)]) {
            return index;
        }
    }
    return totalSteps;
}

[[nodiscard]] int chooseDuration(const Controls& controls,
                                 const PhrasePlan& plan,
                                 Rng& rng,
                                 const int available,
                                 const int localStep,
                                 const int phraseSteps)
{
    if (available <= 1) {
        return 1;
    }

    int desired = 4;
    switch (controls.style) {
    case StyleId::grounded: desired = 8; break;
    case StyleId::ostinato: desired = 3; break;
    case StyleId::march: desired = 4; break;
    case StyleId::pulse: desired = 8; break;
    case StyleId::drone: desired = 16; break;
    case StyleId::climb: desired = 4; break;
    default: break;
    }

    switch (plan.role) {
    case PhraseRoleId::pedal:
        desired = std::max(desired, 12);
        break;
    case PhraseRoleId::breakdown:
        desired = 4;
        break;
    case PhraseRoleId::cadence:
        if ((localStep + 4) >= phraseSteps) {
            return available;
        }
        desired = std::max(desired, 6);
        break;
    case PhraseRoleId::release:
        desired = std::max(desired, 6);
        break;
    default:
        break;
    }

    desired += rng.nextInt(0, std::max(0, desired / 2));
    return clampi(desired, 1, available);
}

[[nodiscard]] float legatoAmountForPhrase(const Controls& controls, const PhrasePlan& plan)
{
    const float density = clampf(controls.density * plan.intensity, 0.0f, 1.0f);
    float legato = 0.48f + (1.0f - density) * 0.18f + controls.sequence * 0.08f;

    switch (controls.style) {
    case StyleId::grounded: legato += 0.14f; break;
    case StyleId::pulse: legato += 0.10f; break;
    case StyleId::drone: legato += 0.30f; break;
    case StyleId::ostinato: legato -= 0.18f; break;
    case StyleId::march: legato -= 0.10f; break;
    case StyleId::climb: legato += 0.02f; break;
    default: break;
    }

    switch (plan.role) {
    case PhraseRoleId::statement: legato += 0.06f; break;
    case PhraseRoleId::answer: legato += 0.04f; break;
    case PhraseRoleId::climb: legato -= 0.06f; break;
    case PhraseRoleId::pedal: legato += 0.24f; break;
    case PhraseRoleId::breakdown: legato -= 0.24f; break;
    case PhraseRoleId::cadence: legato += 0.12f; break;
    case PhraseRoleId::release: legato += 0.18f; break;
    }

    return clampf(legato, 0.0f, 1.0f);
}

void applyLegatoShaping(PhraseEventList& list, const Controls& controls, const PhrasePlan& plan, Rng& rng)
{
    if (list.count <= 0) {
        return;
    }

    const float legato = legatoAmountForPhrase(controls, plan);
    const int phraseEnd = plan.startStep + plan.stepCount;

    for (int index = 0; index < list.count; ++index) {
        NoteEvent& event = list.events[static_cast<std::size_t>(index)];
        const int nextStart = index + 1 < list.count
            ? list.events[static_cast<std::size_t>(index + 1)].startStep
            : phraseEnd;
        const int maxDuration = clampi(nextStart - event.startStep, 1, phraseEnd - event.startStep);

        int tailGap = 1;
        if (index + 1 >= list.count) {
            tailGap = legato >= 0.66f ? 0 : 1;
        } else if (legato >= 0.82f) {
            tailGap = 0;
        } else if (legato >= 0.60f) {
            tailGap = rng.nextFloat() < 0.65f ? 0 : 1;
        } else if (legato >= 0.42f) {
            tailGap = 1;
        } else {
            tailGap = 1 + (rng.nextFloat() < 0.35f ? 1 : 0);
        }

        const int stretched = clampi(maxDuration - tailGap, 1, maxDuration);
        event.durationSteps = std::max(event.durationSteps, stretched);
    }
}

void mergeRepeatedNotes(PhraseEventList& list, const PhrasePlan& plan)
{
    if (list.count <= 1) {
        return;
    }

    PhraseEventList merged {};
    const int phraseEnd = plan.startStep + plan.stepCount;

    for (int index = 0; index < list.count; ++index) {
        const NoteEvent& current = list.events[static_cast<std::size_t>(index)];

        if (merged.count > 0) {
            NoteEvent& previous = merged.events[static_cast<std::size_t>(merged.count - 1)];
            const int previousEnd = previous.startStep + previous.durationSteps;
            const int currentEnd = current.startStep + current.durationSteps;

            if (previous.note == current.note && current.startStep <= previousEnd) {
                previous.durationSteps = clampi(std::max(previousEnd, currentEnd) - previous.startStep,
                                                1,
                                                phraseEnd - previous.startStep);
                previous.velocity = std::max(previous.velocity, current.velocity);
                continue;
            }
        }

        merged.events[static_cast<std::size_t>(merged.count++)] = current;
    }

    list = merged;
}

void sortEvents(PhraseEventList& list)
{
    std::sort(list.events.begin(),
              list.events.begin() + list.count,
              [](const NoteEvent& a, const NoteEvent& b) {
                  return a.startStep < b.startStep;
              });
}

void generatePhraseEvents(PhraseEventList& out,
                          const PhrasePlan& plan,
                          const Controls& controls,
                          const PhrasePlan* previousPlan,
                          const PhraseEventList* previousEvents,
                          const bool useSequence,
                          const float mutationStrength,
                          Rng& rng)
{
    out = {};

    if (useSequence && previousPlan != nullptr && previousEvents != nullptr && previousEvents->count > 0) {
        const int semitoneShift =
            noteFromDegree(controls, plan.rootDegree, plan.registerOffset) -
            noteFromDegree(controls, previousPlan->rootDegree, previousPlan->registerOffset);

        for (int index = 0; index < previousEvents->count && out.count < static_cast<int>(out.events.size()); ++index) {
            NoteEvent event = previousEvents->events[static_cast<std::size_t>(index)];
            const int localStart = event.startStep - previousPlan->startStep;
            int jitter = 0;
            if (mutationStrength > 0.25f) {
                jitter = rng.nextInt(-2, 2);
            } else if (rng.nextFloat() < (0.10f + controls.motion * 0.12f + controls.tension * 0.08f)) {
                jitter = rng.nextFloat() < 0.5f ? -1 : 1;
            }
            event.startStep = clampi(plan.startStep + localStart + jitter,
                                     plan.startStep,
                                     plan.startStep + plan.stepCount - 1);
            event.durationSteps = clampi(event.durationSteps + (mutationStrength > 0.30f ? rng.nextInt(-1, 1) : 0),
                                         1,
                                         (plan.startStep + plan.stepCount) - event.startStep);
            event.note = clampi(event.note + semitoneShift, 0, 127);
            event.velocity = clampi(event.velocity + rng.nextInt(-4, 4), 52, 124);
            out.events[static_cast<std::size_t>(out.count++)] = event;
        }

        sortEvents(out);
        applyLegatoShaping(out, controls, plan, rng);
        mergeRepeatedNotes(out, plan);
        return;
    }

    std::array<bool, 128> onset {};
    const int phraseSteps = clampi(plan.stepCount, 1, static_cast<int>(onset.size()));
    const int phraseBars = std::max(1, plan.bars);

    for (int bar = 0; bar < phraseBars; ++bar) {
        buildBarOnsets(onset, controls, plan, rng, bar, phraseBars, bar * 16);
    }

    onset[0] = true;
    if (plan.role == PhraseRoleId::cadence) {
        onset[phraseSteps - 4] = true;
    }

    int previousDegree = plan.rootDegree;
    int ordinal = 0;
    for (int step = 0; step < phraseSteps && out.count < static_cast<int>(out.events.size()); ++step) {
        if (!onset[static_cast<std::size_t>(step)]) {
            continue;
        }

        const int next = nextOnset(onset, phraseSteps, step);
        const int available = clampi(next - step, 1, phraseSteps - step);
        const int degree = chooseDegreeForStep(plan,
                                               controls,
                                               rng,
                                               ordinal,
                                               12,
                                               step,
                                               phraseSteps,
                                               previousDegree);
        previousDegree = degree;

        NoteEvent event;
        event.startStep = plan.startStep + step;
        event.durationSteps = chooseDuration(controls, plan, rng, available, step, phraseSteps);
        event.note = noteFromDegree(controls, degree, plan.registerOffset);
        event.velocity = clampi(static_cast<int>(78.0f + plan.intensity * 28.0f) +
                                    ((step % 16) == 0 ? 10 : 0) +
                                    rng.nextInt(-6, 6),
                                48,
                                124);
        out.events[static_cast<std::size_t>(out.count++)] = event;
        ++ordinal;
    }

    if (out.count <= 0) {
        out.events[0].startStep = plan.startStep;
        out.events[0].durationSteps = std::max(1, plan.stepCount);
        out.events[0].note = noteFromDegree(controls, plan.rootDegree, plan.registerOffset);
        out.events[0].velocity = 84;
        out.count = 1;
    }

    sortEvents(out);
    applyLegatoShaping(out, controls, plan, rng);
    mergeRepeatedNotes(out, plan);
}

void buildPhrasePlan(FormState& form, const Controls& controls)
{
    form.formBars = normalizeFormBars(controls.formBars);
    form.phraseBars = normalizePhraseBars(controls.phraseBars, form.formBars);
    form.stepsPerBeat = 4;
    form.patternSteps = clampi(form.formBars * 16, 1, kMaxPatternSteps);
    form.phraseCount = clampi(form.formBars / std::max(1, form.phraseBars), 1, kMaxPhraseCount);

    Rng plannerRng;
    plannerRng.seed(seedMix(controls, form.generationSerial, -1, 0x2c9277b5u));
    const int peakPhrase = clampi(static_cast<int>(std::lround(static_cast<double>(form.phraseCount - 1) *
                                                               (0.35 + controls.tension * 0.40))),
                                  1,
                                  std::max(1, form.phraseCount - 1));

    for (int index = 0; index < form.phraseCount; ++index) {
        PhrasePlan& phrase = form.phrases[static_cast<std::size_t>(index)];
        phrase = {};
        phrase.startBar = index * form.phraseBars;
        phrase.bars = form.phraseBars;
        phrase.startStep = phrase.startBar * 16;
        phrase.stepCount = form.phraseBars * 16;
        phrase.role = chooseRole(controls, plannerRng, index, form.phraseCount, peakPhrase);
        phrase.rootDegree = roleBaseDegree(phrase.role, plannerRng);
        phrase.registerOffset = phraseRegisterOffset(controls, phrase.role, index, form.phraseCount);
        phrase.intensity = roleIntensity(phrase.role);
        phrase.motionBias = roleMotionBias(phrase.role, controls.motion);
    }
}

void rebuildFromPhraseEvents(FormState& form,
                             const std::array<PhraseEventList, kMaxPhraseCount>& phraseEvents)
{
    form.eventCount = 0;
    for (int phraseIndex = 0; phraseIndex < form.phraseCount; ++phraseIndex) {
        PhrasePlan& phrase = form.phrases[static_cast<std::size_t>(phraseIndex)];
        phrase.eventStartIndex = form.eventCount;
        phrase.eventCount = phraseEvents[static_cast<std::size_t>(phraseIndex)].count;

        for (int eventIndex = 0;
             eventIndex < phraseEvents[static_cast<std::size_t>(phraseIndex)].count && form.eventCount < kMaxEvents;
             ++eventIndex) {
            form.events[static_cast<std::size_t>(form.eventCount++)] =
                phraseEvents[static_cast<std::size_t>(phraseIndex)].events[static_cast<std::size_t>(eventIndex)];
        }
    }
}

void copyExistingPhraseEvents(std::array<PhraseEventList, kMaxPhraseCount>& phraseEvents, const FormState& form)
{
    for (int phraseIndex = 0; phraseIndex < form.phraseCount; ++phraseIndex) {
        const PhrasePlan& phrase = form.phrases[static_cast<std::size_t>(phraseIndex)];
        PhraseEventList& list = phraseEvents[static_cast<std::size_t>(phraseIndex)];
        list = {};
        for (int eventIndex = 0; eventIndex < phrase.eventCount && eventIndex < static_cast<int>(list.events.size()); ++eventIndex) {
            list.events[static_cast<std::size_t>(eventIndex)] =
                form.events[static_cast<std::size_t>(phrase.eventStartIndex + eventIndex)];
        }
        list.count = clampi(phrase.eventCount, 0, static_cast<int>(list.events.size()));
    }
}

void regenerateSinglePhrasePlan(PhrasePlan& phrase,
                                const Controls& controls,
                                const int phraseIndex,
                                const int phraseCount,
                                const int generationSerial)
{
    Rng rng;
    rng.seed(seedMix(controls, generationSerial, phraseIndex, 0x7f4a7c15u));
    const int peakPhrase = clampi(static_cast<int>(std::lround(static_cast<double>(phraseCount - 1) *
                                                               (0.35 + controls.tension * 0.40))),
                                  1,
                                  std::max(1, phraseCount - 1));
    phrase.role = chooseRole(controls, rng, phraseIndex, phraseCount, peakPhrase);
    phrase.rootDegree = roleBaseDegree(phrase.role, rng);
    phrase.registerOffset = phraseRegisterOffset(controls, phrase.role, phraseIndex, phraseCount);
    phrase.intensity = roleIntensity(phrase.role);
    phrase.motionBias = roleMotionBias(phrase.role, controls.motion);
}

}  // namespace

int normalizeFormBars(const int rawBars)
{
    return nearestFromSet(rawBars, {8, 16, 32, 64});
}

int normalizePhraseBars(const int rawPhraseBars, const int formBars)
{
    int best = 2;
    int bestDistance = std::numeric_limits<int>::max();
    for (const int value : {2, 4, 8}) {
        if (value > formBars || (formBars % value) != 0) {
            continue;
        }
        const int distance = std::abs(rawPhraseBars - value);
        if (distance < bestDistance) {
            best = value;
            bestDistance = distance;
        }
    }
    return best;
}

Controls clampControls(const Controls& raw)
{
    Controls controls = raw;
    controls.rootNote = clampi(controls.rootNote, 0, 127);
    controls.scale = static_cast<ScaleId>(clampi(static_cast<int>(controls.scale), 0, static_cast<int>(ScaleId::count) - 1));
    controls.style = static_cast<StyleId>(clampi(static_cast<int>(controls.style), 0, static_cast<int>(StyleId::count) - 1));
    controls.channel = clampi(controls.channel, 1, 16);
    controls.formBars = normalizeFormBars(controls.formBars);
    controls.phraseBars = normalizePhraseBars(controls.phraseBars, controls.formBars);
    controls.density = clampf(controls.density, 0.0f, 1.0f);
    controls.motion = clampf(controls.motion, 0.0f, 1.0f);
    controls.tension = clampf(controls.tension, 0.0f, 1.0f);
    controls.cadence = clampf(controls.cadence, 0.0f, 1.0f);
    controls.reg = clampi(controls.reg, 0, 3);
    controls.registerArc = clampf(controls.registerArc, 0.0f, 1.0f);
    controls.sequence = clampf(controls.sequence, 0.0f, 1.0f);
    controls.vary = clampf(controls.vary, 0.0f, 1.0f);
    controls.seed = controls.seed == 0 ? 1u : controls.seed;
    controls.actionNewForm = clampi(controls.actionNewForm, 0, 1048576);
    controls.actionNewPhrase = clampi(controls.actionNewPhrase, 0, 1048576);
    controls.actionMutateCell = clampi(controls.actionMutateCell, 0, 1048576);
    return controls;
}

FormState sanitizeFormState(const FormState& raw, bool* valid)
{
    FormState form = raw;
    form.version = kPatternStateVersion;
    form.formBars = normalizeFormBars(form.formBars <= 0 ? 16 : form.formBars);
    form.phraseBars = normalizePhraseBars(form.phraseBars <= 0 ? 4 : form.phraseBars, form.formBars);
    form.phraseCount = clampi(form.phraseCount > 0 ? form.phraseCount : form.formBars / form.phraseBars, 1, kMaxPhraseCount);
    form.stepsPerBeat = 4;
    form.patternSteps = clampi(form.patternSteps > 0 ? form.patternSteps : form.formBars * 16, 1, kMaxPatternSteps);
    form.eventCount = clampi(form.eventCount, 0, kMaxEvents);

    for (int phraseIndex = 0; phraseIndex < form.phraseCount; ++phraseIndex) {
        PhrasePlan& phrase = form.phrases[static_cast<std::size_t>(phraseIndex)];
        phrase.role = static_cast<PhraseRoleId>(clampi(static_cast<int>(phrase.role), 0, static_cast<int>(PhraseRoleId::count) - 1));
        phrase.startBar = clampi(phrase.startBar, 0, std::max(0, form.formBars - 1));
        phrase.bars = clampi(phrase.bars, 1, form.formBars);
        phrase.startStep = clampi(phrase.startStep, 0, form.patternSteps - 1);
        phrase.stepCount = clampi(phrase.stepCount, 1, form.patternSteps - phrase.startStep);
        phrase.eventStartIndex = clampi(phrase.eventStartIndex, 0, form.eventCount);
        phrase.eventCount = clampi(phrase.eventCount, 0, form.eventCount - phrase.eventStartIndex);
        phrase.rootDegree = clampi(phrase.rootDegree, -4, 12);
        phrase.registerOffset = clampi(phrase.registerOffset, -2, 3);
        phrase.intensity = clampf(phrase.intensity, 0.0f, 1.0f);
        phrase.motionBias = clampf(phrase.motionBias, 0.0f, 1.0f);
    }

    for (int index = 0; index < form.eventCount; ++index) {
        NoteEvent& event = form.events[static_cast<std::size_t>(index)];
        event.startStep = clampi(event.startStep, 0, form.patternSteps - 1);
        event.durationSteps = clampi(event.durationSteps, 1, form.patternSteps - event.startStep);
        event.note = clampi(event.note, 0, 127);
        event.velocity = clampi(event.velocity, 1, 127);
    }

    if (valid != nullptr) {
        *valid = raw.eventCount > 0 && raw.patternSteps > 0 && raw.phraseCount > 0;
    }

    return form;
}

VariationState sanitizeVariationState(const VariationState& raw)
{
    VariationState variation = raw;
    variation.version = kVariationStateVersion;
    return variation;
}

bool structureControlsMatch(const Controls& a, const Controls& b)
{
    return a.rootNote == b.rootNote &&
           a.scale == b.scale &&
           a.style == b.style &&
           a.formBars == b.formBars &&
           a.phraseBars == b.phraseBars &&
           std::fabs(a.density - b.density) < 0.0001f &&
           std::fabs(a.motion - b.motion) < 0.0001f &&
           std::fabs(a.tension - b.tension) < 0.0001f &&
           std::fabs(a.cadence - b.cadence) < 0.0001f &&
           a.reg == b.reg &&
           std::fabs(a.registerArc - b.registerArc) < 0.0001f &&
           std::fabs(a.sequence - b.sequence) < 0.0001f &&
           a.seed == b.seed;
}

void regenerateForm(FormState& form, const Controls& rawControls)
{
    const Controls controls = clampControls(rawControls);
    const std::int32_t previousSerial = form.generationSerial;
    form = {};
    form.version = kPatternStateVersion;
    form.generationSerial = nextGenerationSerial(previousSerial);
    buildPhrasePlan(form, controls);

    std::array<PhraseEventList, kMaxPhraseCount> phraseEvents {};
    for (int phraseIndex = 0; phraseIndex < form.phraseCount; ++phraseIndex) {
        Rng rng;
        rng.seed(seedMix(controls, form.generationSerial, phraseIndex, 0xa511e9b3u));
        const PhrasePlan* previousPlan = phraseIndex > 0 ? &form.phrases[static_cast<std::size_t>(phraseIndex - 1)] : nullptr;
        const PhraseEventList* previousEvents = phraseIndex > 0 ? &phraseEvents[static_cast<std::size_t>(phraseIndex - 1)] : nullptr;
        const bool useSequence = controls.sequence > 0.35f &&
                                 phraseIndex > 0 &&
                                 (form.phrases[static_cast<std::size_t>(phraseIndex)].role == PhraseRoleId::answer ||
                                  form.phrases[static_cast<std::size_t>(phraseIndex)].role == PhraseRoleId::release);

        generatePhraseEvents(phraseEvents[static_cast<std::size_t>(phraseIndex)],
                             form.phrases[static_cast<std::size_t>(phraseIndex)],
                             controls,
                             previousPlan,
                             previousEvents,
                             useSequence,
                             0.0f,
                             rng);
    }

    rebuildFromPhraseEvents(form, phraseEvents);
}

void refreshPhrase(FormState& form, const Controls& rawControls, const int phraseIndex)
{
    const Controls controls = clampControls(rawControls);
    bool valid = false;
    form = sanitizeFormState(form, &valid);
    if (!valid) {
        regenerateForm(form, controls);
        return;
    }

    const int index = clampi(phraseIndex, 0, form.phraseCount - 1);
    form.generationSerial = nextGenerationSerial(form.generationSerial);
    regenerateSinglePhrasePlan(form.phrases[static_cast<std::size_t>(index)],
                               controls,
                               index,
                               form.phraseCount,
                               form.generationSerial);

    std::array<PhraseEventList, kMaxPhraseCount> phraseEvents {};
    copyExistingPhraseEvents(phraseEvents, form);

    Rng rng;
    rng.seed(seedMix(controls, form.generationSerial, index, 0xc1426ba3u));
    const PhrasePlan* previousPlan = index > 0 ? &form.phrases[static_cast<std::size_t>(index - 1)] : nullptr;
    const PhraseEventList* previousEvents = index > 0 ? &phraseEvents[static_cast<std::size_t>(index - 1)] : nullptr;
    const bool useSequence = controls.sequence > 0.35f &&
                             index > 0 &&
                             (form.phrases[static_cast<std::size_t>(index)].role == PhraseRoleId::answer ||
                              form.phrases[static_cast<std::size_t>(index)].role == PhraseRoleId::release);

    generatePhraseEvents(phraseEvents[static_cast<std::size_t>(index)],
                         form.phrases[static_cast<std::size_t>(index)],
                         controls,
                         previousPlan,
                         previousEvents,
                         useSequence,
                         0.12f,
                         rng);
    rebuildFromPhraseEvents(form, phraseEvents);
}

void mutatePhraseCell(FormState& form, const Controls& rawControls, const int phraseIndex, const float strength)
{
    const Controls controls = clampControls(rawControls);
    bool valid = false;
    form = sanitizeFormState(form, &valid);
    if (!valid) {
        regenerateForm(form, controls);
        return;
    }

    const int index = clampi(phraseIndex, 0, form.phraseCount - 1);
    form.generationSerial = nextGenerationSerial(form.generationSerial);

    std::array<PhraseEventList, kMaxPhraseCount> phraseEvents {};
    copyExistingPhraseEvents(phraseEvents, form);

    Rng rng;
    rng.seed(seedMix(controls, form.generationSerial, index, 0x91f4b3d1u));
    generatePhraseEvents(phraseEvents[static_cast<std::size_t>(index)],
                         form.phrases[static_cast<std::size_t>(index)],
                         controls,
                         nullptr,
                         nullptr,
                         false,
                         clampf(strength, 0.0f, 1.0f),
                         rng);
    rebuildFromPhraseEvents(form, phraseEvents);
}

}  // namespace downspout::ground
