#include "bassgen_engine.hpp"
#include "bassgen_pattern.hpp"
#include "bassgen_serialization.hpp"
#include "bassgen_state.hpp"
#include "bassgen_transport.hpp"
#include "bassgen_variation.hpp"

#include <cassert>
#include <cmath>

using namespace downspout::bassgen;

namespace {

bool hasEventStartingAt(const PatternState& pattern, const int step) {
    for (int index = 0; index < pattern.eventCount; ++index) {
        if (pattern.events[index].startStep == step) {
            return true;
        }
    }
    return false;
}

const NoteEvent* eventStartingAt(const PatternState& pattern, const int step) {
    for (int index = 0; index < pattern.eventCount; ++index) {
        if (pattern.events[index].startStep == step) {
            return &pattern.events[index];
        }
    }
    return nullptr;
}

int relativePitchClass(const int note, const int rootNote) {
    return (note - rootNote + 1200) % 12;
}

bool pitchClassInSet(const int pitchClass, const int* values, const int count) {
    for (int index = 0; index < count; ++index) {
        if (values[index] == pitchClass) {
            return true;
        }
    }
    return false;
}

bool patternsDiffer(const PatternState& left, const PatternState& right) {
    if (left.eventCount != right.eventCount ||
        left.patternSteps != right.patternSteps ||
        left.stepsPerBar != right.stepsPerBar) {
        return true;
    }

    for (int index = 0; index < left.eventCount; ++index) {
        if (left.events[index].startStep != right.events[index].startStep ||
            left.events[index].durationSteps != right.events[index].durationSteps ||
            left.events[index].note != right.events[index].note ||
            left.events[index].velocity != right.events[index].velocity) {
            return true;
        }
    }

    return false;
}

bool hasNoteOffFor(const BlockResult& result, const int note) {
    for (int index = 0; index < result.eventCount; ++index) {
        if (result.events[index].type == MidiEventType::noteOff && result.events[index].data1 == note) {
            return true;
        }
    }
    return false;
}

InputMidiEvent noteOn(std::uint32_t frame, int channel, int note, int velocity = 100) {
    InputMidiEvent event;
    event.frame = frame;
    event.size = 3;
    event.data[0] = static_cast<std::uint8_t>(0x90 | ((channel - 1) & 0x0f));
    event.data[1] = static_cast<std::uint8_t>(note);
    event.data[2] = static_cast<std::uint8_t>(velocity);
    return event;
}

TransportSnapshot playingTransport(double barBeat) {
    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = barBeat;
    transport.meter = ::downspout::Meter {};
    return transport;
}

void testDeterministicGeneration() {
    Controls controls;
    controls.seed = 12345u;
    controls.genre = GenreId::acid;
    controls.scale = ScaleId::phrygian;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;

    PatternState first;
    PatternState second;
    regeneratePattern(first, controls, ::downspout::Meter {}, true, true);
    regeneratePattern(second, controls, ::downspout::Meter {}, true, true);

    assert(first.patternSteps == second.patternSteps);
    assert(first.stepsPerBeat == second.stepsPerBeat);
    assert(first.stepsPerBar == second.stepsPerBar);
    assert(first.eventCount == second.eventCount);
    assert(first.generationSerial == second.generationSerial);

    for (int index = 0; index < first.eventCount; ++index) {
        assert(first.events[index].startStep == second.events[index].startStep);
        assert(first.events[index].durationSteps == second.events[index].durationSteps);
        assert(first.events[index].note == second.events[index].note);
        assert(first.events[index].velocity == second.events[index].velocity);
    }
}

void testSubdivisionStepCounts() {
    assert(stepsPerBeatForSubdivision(SubdivisionId::quarter) == 1);
    assert(stepsPerBeatForSubdivision(SubdivisionId::eighth) == 2);
    assert(stepsPerBeatForSubdivision(SubdivisionId::sixteenth) == 4);
    assert(stepsPerBeatForSubdivision(SubdivisionId::sixteenthTriplet) == 6);
}

void testRhythmRegenerationKeepsPreviousNotes() {
    Controls controls;
    controls.seed = 77u;
    controls.genre = GenreId::dub;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);
    const PatternState original = pattern;

    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, false);

    assert(pattern.eventCount > 0);
    for (int index = 0; index < pattern.eventCount; ++index) {
        const NoteEvent& expected = original.events[index % original.eventCount];
        assert(pattern.events[index].note == expected.note);
        assert(pattern.events[index].velocity == expected.velocity);
    }
}

void testTransportHelpers() {
    PatternState pattern;
    pattern.patternSteps = 16;
    pattern.eventCount = 2;
    pattern.events[0] = NoteEvent{0, 4, 36, 100};
    pattern.events[1] = NoteEvent{8, 2, 40, 90};

    assert(transportRestartDetected(false, -1, 0));
    assert(transportRestartDetected(true, 12, 4));
    assert(!transportRestartDetected(true, 4, 8));

    assert(std::fabs(localStepFromAbsolute(pattern, 18.5) - 2.5) < 1e-9);
    assert(findActiveEvent(pattern, 1.0)->note == 36);
    assert(findActiveEvent(pattern, 9.0)->note == 40);
    assert(findEventStartingAt(pattern, 8)->velocity == 90);
    assert(anyEventEndsAt(pattern, 4));
    assert(!anyEventEndsAt(pattern, 5));
    assert(localStepForBoundary(pattern, 17) == 1);
    assert(frameForBoundary(12.0, 16.0, 64, 14) == 31);
}

void testVariationMutatesAfterLoopThreshold() {
    Controls controls;
    controls.seed = 19u;
    controls.vary = 1.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);
    const int originalSerial = pattern.generationSerial;

    VariationState variation;
    const bool changed = applyLoopVariation(pattern, variation, controls, ::downspout::Meter {}, 4.0);

    assert(changed);
    assert(variation.completedLoops == 1);
    assert(variation.lastMutationLoop == 1);
    assert(pattern.generationSerial > originalSerial);
}

void testExplicitStyleModesChangePatternShape() {
    Controls controls;
    controls.seed = 314u;
    controls.genre = GenreId::funk;
    controls.lengthBeats = 12;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.68f;
    controls.hold = 0.40f;

    PatternState jig;
    controls.styleMode = StyleModeId::jig;
    regeneratePattern(jig, controls, ::downspout::makeMeter(6, 8), true, true);

    PatternState straight;
    controls.styleMode = StyleModeId::straight;
    regeneratePattern(straight, controls, ::downspout::makeMeter(6, 8), true, true);

    assert(hasEventStartingAt(jig, 12));
    assert(patternsDiffer(jig, straight));
}

void testJazzGenreOutlinesTwoFiveOne() {
    Controls controls;
    controls.seed = 909u;
    controls.genre = GenreId::jazz;
    controls.scale = ScaleId::major;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.82f;
    controls.hold = 0.25f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    assert(pattern.stepsPerBar == 16);
    assert(pattern.patternSteps == 64);

    const NoteEvent* two = eventStartingAt(pattern, 0);
    const NoteEvent* five = eventStartingAt(pattern, 16);
    const NoteEvent* one = eventStartingAt(pattern, 32);
    const NoteEvent* turnaround = eventStartingAt(pattern, 48);

    assert(two != nullptr);
    assert(five != nullptr);
    assert(one != nullptr);
    assert(turnaround != nullptr);
    assert(relativePitchClass(two->note, controls.rootNote) == 2);
    assert(relativePitchClass(five->note, controls.rootNote) == 7);
    assert(relativePitchClass(one->note, controls.rootNote) == 0);
    assert(relativePitchClass(turnaround->note, controls.rootNote) == 9);
    assert(hasEventStartingAt(pattern, 4));
    assert(hasEventStartingAt(pattern, 8));
}

void testJazzRoleColorsUseModalChordTones() {
    Controls controls;
    controls.seed = 909u;
    controls.genre = GenreId::jazz;
    controls.scale = ScaleId::major;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.82f;
    controls.hold = 0.25f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    const NoteEvent* twoBeatTwo = eventStartingAt(pattern, 4);
    const NoteEvent* fiveBeatTwo = eventStartingAt(pattern, 20);
    const NoteEvent* oneBeatThree = eventStartingAt(pattern, 40);

    assert(twoBeatTwo != nullptr);
    assert(fiveBeatTwo != nullptr);
    assert(oneBeatThree != nullptr);
    assert(relativePitchClass(twoBeatTwo->note, controls.rootNote) == 5);
    assert(relativePitchClass(fiveBeatTwo->note, controls.rootNote) == 11);
    assert(relativePitchClass(oneBeatThree->note, controls.rootNote) == 7);
}

void testJazzDominantBarCanUseAlteredColor() {
    Controls controls;
    controls.seed = 5u;
    controls.genre = GenreId::jazz;
    controls.scale = ScaleId::major;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.82f;
    controls.hold = 0.25f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    const NoteEvent* dominantColorTone = eventStartingAt(pattern, 24);

    assert(dominantColorTone != nullptr);
    assert(relativePitchClass(dominantColorTone->note, controls.rootNote) == 8);
}

void testColorControlsJazzDominantIntensity() {
    Controls controls;
    controls.seed = 5u;
    controls.genre = GenreId::jazz;
    controls.scale = ScaleId::major;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.82f;
    controls.hold = 0.25f;

    PatternState lowColor;
    controls.color = 0.0f;
    regeneratePattern(lowColor, controls, ::downspout::Meter {}, true, true);

    PatternState highColor;
    controls.color = 1.0f;
    regeneratePattern(highColor, controls, ::downspout::Meter {}, true, true);

    const NoteEvent* lowDominantTone = eventStartingAt(lowColor, 24);
    const NoteEvent* highDominantTone = eventStartingAt(highColor, 24);

    assert(lowDominantTone != nullptr);
    assert(highDominantTone != nullptr);
    assert(relativePitchClass(lowDominantTone->note, controls.rootNote) == 2);
    assert(relativePitchClass(highDominantTone->note, controls.rootNote) == 8);
}

void testColorInfluencesNonJazzGenres() {
    Controls controls;
    controls.seed = 77u;
    controls.genre = GenreId::funk;
    controls.scale = ScaleId::blues;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.80f;
    controls.hold = 0.25f;

    PatternState lowColor;
    controls.color = 0.0f;
    regeneratePattern(lowColor, controls, ::downspout::Meter {}, true, true);

    PatternState highColor;
    controls.color = 1.0f;
    regeneratePattern(highColor, controls, ::downspout::Meter {}, true, true);

    assert(patternsDiffer(lowColor, highColor));
}

void testJazzStrongBeatsTargetChordTones() {
    Controls controls;
    controls.seed = 909u;
    controls.genre = GenreId::jazz;
    controls.scale = ScaleId::major;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.82f;
    controls.hold = 0.25f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    static constexpr int kTwoChord[] = {2, 5, 9, 0};
    static constexpr int kFiveChord[] = {7, 11, 2, 5};
    static constexpr int kOneChord[] = {0, 4, 7, 11};
    static constexpr int kTurnaroundChord[] = {9, 0, 4, 7};

    const NoteEvent* twoRoot = eventStartingAt(pattern, 0);
    const NoteEvent* twoThird = eventStartingAt(pattern, 4);
    const NoteEvent* twoFifth = eventStartingAt(pattern, 8);
    const NoteEvent* fiveRoot = eventStartingAt(pattern, 16);
    const NoteEvent* fiveThird = eventStartingAt(pattern, 20);
    const NoteEvent* fiveFifth = eventStartingAt(pattern, 24);
    const NoteEvent* fiveSeventh = eventStartingAt(pattern, 28);
    const NoteEvent* oneRoot = eventStartingAt(pattern, 32);
    const NoteEvent* oneThird = eventStartingAt(pattern, 36);
    const NoteEvent* oneFifth = eventStartingAt(pattern, 40);
    const NoteEvent* oneSeventh = eventStartingAt(pattern, 44);
    const NoteEvent* turnaroundRoot = eventStartingAt(pattern, 48);
    const NoteEvent* turnaroundThird = eventStartingAt(pattern, 52);
    const NoteEvent* turnaroundFifth = eventStartingAt(pattern, 56);
    const NoteEvent* turnaroundSeventh = eventStartingAt(pattern, 60);

    assert(twoRoot != nullptr);
    assert(twoThird != nullptr);
    assert(twoFifth != nullptr);
    assert(fiveRoot != nullptr);
    assert(fiveThird != nullptr);
    assert(fiveFifth != nullptr);
    assert(fiveSeventh != nullptr);
    assert(oneRoot != nullptr);
    assert(oneThird != nullptr);
    assert(oneFifth != nullptr);
    assert(oneSeventh != nullptr);
    assert(turnaroundRoot != nullptr);
    assert(turnaroundThird != nullptr);
    assert(turnaroundFifth != nullptr);
    assert(turnaroundSeventh != nullptr);

    assert(pitchClassInSet(relativePitchClass(twoRoot->note, controls.rootNote), kTwoChord, 4));
    assert(pitchClassInSet(relativePitchClass(twoThird->note, controls.rootNote), kTwoChord, 4));
    assert(pitchClassInSet(relativePitchClass(twoFifth->note, controls.rootNote), kTwoChord, 4));
    assert(pitchClassInSet(relativePitchClass(fiveRoot->note, controls.rootNote), kFiveChord, 4));
    assert(pitchClassInSet(relativePitchClass(fiveThird->note, controls.rootNote), kFiveChord, 4));
    assert(pitchClassInSet(relativePitchClass(fiveFifth->note, controls.rootNote), kFiveChord, 4));
    assert(pitchClassInSet(relativePitchClass(fiveSeventh->note, controls.rootNote), kFiveChord, 4));
    assert(pitchClassInSet(relativePitchClass(oneRoot->note, controls.rootNote), kOneChord, 4));
    assert(pitchClassInSet(relativePitchClass(oneThird->note, controls.rootNote), kOneChord, 4));
    assert(pitchClassInSet(relativePitchClass(oneFifth->note, controls.rootNote), kOneChord, 4));
    assert(pitchClassInSet(relativePitchClass(oneSeventh->note, controls.rootNote), kOneChord, 4));
    assert(pitchClassInSet(relativePitchClass(turnaroundRoot->note, controls.rootNote), kTurnaroundChord, 4));
    assert(pitchClassInSet(relativePitchClass(turnaroundThird->note, controls.rootNote), kTurnaroundChord, 4));
    assert(pitchClassInSet(relativePitchClass(turnaroundFifth->note, controls.rootNote), kTurnaroundChord, 4));
    assert(pitchClassInSet(relativePitchClass(turnaroundSeventh->note, controls.rootNote), kTurnaroundChord, 4));
}

void testJazzApproachAndEnclosureNotesTargetChordTones() {
    Controls controls;
    controls.seed = 1u;
    controls.genre = GenreId::jazz;
    controls.scale = ScaleId::major;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.95f;
    controls.hold = 0.20f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    const NoteEvent* upperEnclosure = eventStartingAt(pattern, 2);
    const NoteEvent* lowerEnclosure = eventStartingAt(pattern, 3);
    const NoteEvent* target = eventStartingAt(pattern, 4);

    assert(upperEnclosure != nullptr);
    assert(lowerEnclosure != nullptr);
    assert(target != nullptr);
    assert(relativePitchClass(upperEnclosure->note, controls.rootNote) == 6);
    assert(relativePitchClass(lowerEnclosure->note, controls.rootNote) == 4);
    assert(relativePitchClass(target->note, controls.rootNote) == 5);

    controls.seed = 5u;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    const NoteEvent* lowerApproach = eventStartingAt(pattern, 31);
    const NoteEvent* nextBarRoot = eventStartingAt(pattern, 32);

    assert(lowerApproach != nullptr);
    assert(nextBarRoot != nullptr);
    assert(relativePitchClass(lowerApproach->note, controls.rootNote) == 11);
    assert(relativePitchClass(nextBarRoot->note, controls.rootNote) == 0);
}

void testJazzScaleIdsAreAppended() {
    assert(static_cast<int>(ScaleId::minor) == 0);
    assert(static_cast<int>(ScaleId::wholeTone) == 13);
    assert(static_cast<int>(ScaleId::altered) == 14);
    assert(static_cast<int>(ScaleId::halfWholeDiminished) == 15);
    assert(static_cast<int>(ScaleId::wholeHalfDiminished) == 16);
    assert(static_cast<int>(ScaleId::bebopDominant) == 17);
    assert(static_cast<int>(ScaleId::bebopMajor) == 18);
    assert(static_cast<int>(ScaleId::bebopMinor) == 19);
    assert(static_cast<int>(ScaleId::count) == 20);
}

void testFugueGenreIsAppendedAndAnswersAtDominant() {
    assert(static_cast<int>(GenreId::jazz) == 8);
    assert(static_cast<int>(GenreId::fugue) == 9);
    assert(static_cast<int>(GenreId::rock) == 10);
    assert(static_cast<int>(GenreId::moroder) == 11);
    assert(static_cast<int>(GenreId::count) == 12);

    Controls controls;
    controls.seed = 1701u;
    controls.genre = GenreId::fugue;
    controls.scale = ScaleId::harmonicMinor;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.55f;
    controls.color = 0.20f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    assert(pattern.stepsPerBar == 16);
    assert(pattern.patternSteps == 64);

    const NoteEvent* subject = eventStartingAt(pattern, 0);
    const NoteEvent* answer = eventStartingAt(pattern, 16);
    const NoteEvent* cadencePedal = eventStartingAt(pattern, 56);

    assert(subject != nullptr);
    assert(answer != nullptr);
    assert(cadencePedal != nullptr);
    assert(relativePitchClass(subject->note, controls.rootNote) == 0);
    assert(relativePitchClass(answer->note, controls.rootNote) == 7);
    assert(relativePitchClass(cadencePedal->note, controls.rootNote) == 0);
}

void testRockGenrePinsBeatAnchors() {
    Controls controls;
    controls.seed = 6161u;
    controls.genre = GenreId::rock;
    controls.scale = ScaleId::minor;
    controls.lengthBeats = 8;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.25f;
    controls.hold = 0.52f;
    controls.accent = 0.78f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    assert(pattern.stepsPerBeat == 4);
    assert(pattern.patternSteps == 32);
    for (int step = 0; step < pattern.patternSteps; step += pattern.stepsPerBeat) {
        const NoteEvent* event = eventStartingAt(pattern, step);
        assert(event != nullptr);
        assert(event->velocity >= 100);
    }
}

void testMoroderGenreUsesRepetitiveBrightPulse() {
    Controls controls;
    controls.seed = 1977u;
    controls.genre = GenreId::moroder;
    controls.scale = ScaleId::major;
    controls.rootNote = 36;
    controls.lengthBeats = 8;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.82f;
    controls.hold = 0.12f;
    controls.accent = 0.70f;
    controls.color = 0.15f;

    PatternState lowColor;
    regeneratePattern(lowColor, controls, ::downspout::Meter {}, true, true);

    assert(lowColor.stepsPerBeat == 4);
    assert(lowColor.patternSteps == 32);
    for (int step = 0; step < 12; ++step) {
        assert(hasEventStartingAt(lowColor, step));
    }

    const NoteEvent* first = eventStartingAt(lowColor, 0);
    const NoteEvent* second = eventStartingAt(lowColor, 1);
    assert(first != nullptr);
    assert(second != nullptr);
    assert(relativePitchClass(first->note, controls.rootNote) == 0);
    assert(relativePitchClass(second->note, controls.rootNote) == 7);
    assert(first->velocity > second->velocity);

    controls.color = 0.95f;
    PatternState highColor;
    regeneratePattern(highColor, controls, ::downspout::Meter {}, true, true);

    bool hasBrightTurn = false;
    bool pitchClasses[12] {};
    int pitchClassCount = 0;
    for (int index = 0; index < highColor.eventCount; ++index) {
        const int pc = relativePitchClass(highColor.events[index].note, controls.rootNote);
        if (!pitchClasses[pc]) {
            pitchClasses[pc] = true;
            ++pitchClassCount;
        }
        if (pc == 4 || pc == 9 || pc == 11 || pc == 2) {
            hasBrightTurn = true;
        }
    }

    assert(hasBrightTurn);
    assert(pitchClassCount >= 4);
    assert(patternsDiffer(lowColor, highColor));
}

void testBebopDominantScaleConstrainsGeneratedNotes() {
    Controls controls;
    controls.seed = 1001u;
    controls.genre = GenreId::jazz;
    controls.scale = ScaleId::bebopDominant;
    controls.rootNote = 36;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.82f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    static constexpr int kAllowed[] = {0, 2, 4, 5, 7, 9, 10, 11};
    assert(pattern.eventCount > 0);
    for (int index = 0; index < pattern.eventCount; ++index) {
        const int pc = relativePitchClass(pattern.events[index].note, controls.rootNote);
        assert(pitchClassInSet(pc, kAllowed, 8));
    }
}

void testStateSanitization() {
    PatternState raw;
    raw.patternSteps = 999;
    raw.stepsPerBeat = 99;
    raw.meter = ::downspout::makeMeter(6, 8);
    raw.eventCount = 2;
    raw.events[0] = NoteEvent{-5, 0, -1, 200};
    raw.events[1] = NoteEvent{999, 999, 300, -2};

    bool valid = false;
    const PatternState sanitized = sanitizePatternState(raw, &valid);
    assert(valid);
    assert(sanitized.version == kPatternStateVersion);
    assert(sanitized.patternSteps == kMaxPatternSteps);
    assert(sanitized.stepsPerBeat == 6);
    assert(sanitized.stepsPerBar == 36);
    assert(sanitized.meter.numerator == 6);
    assert(sanitized.meter.denominator == 8);
    assert(sanitized.events[0].startStep == 0);
    assert(sanitized.events[0].durationSteps == 1);
    assert(sanitized.events[0].note == 0);
    assert(sanitized.events[0].velocity == 127);
    assert(sanitized.events[1].startStep == kMaxPatternSteps - 1);
    assert(sanitized.events[1].durationSteps == kMaxPatternSteps);
    assert(sanitized.events[1].note == 127);
    assert(sanitized.events[1].velocity == 1);

    VariationState variation;
    variation.version = 99;
    const VariationState sanitizedVariation = sanitizeVariationState(variation);
    assert(sanitizedVariation.version == kVariationStateVersion);

    Controls controls;
    controls.color = 2.0f;
    assert(std::fabs(clampControls(controls).color - 1.0f) < 0.0001f);
    controls.color = -1.0f;
    assert(std::fabs(clampControls(controls).color) < 0.0001f);
}

void testEngineRewindResyncAndStopNoteOff() {
    Controls controls;
    controls.lengthBeats = 8;
    controls.subdivision = SubdivisionId::sixteenth;

    EngineState engine;
    activate(engine, controls);

    engine.patternValid = true;
    engine.pattern.patternSteps = 32;
    engine.pattern.stepsPerBeat = 4;
    engine.pattern.stepsPerBar = 16;
    engine.pattern.meter = ::downspout::Meter {};
    engine.pattern.eventCount = 1;
    engine.pattern.events[0] = NoteEvent{0, 8, 48, 101};

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = 1.0;
    transport.meter = ::downspout::Meter {};

    BlockResult result = processBlock(engine, controls, transport, 64, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 48);

    engine.wasPlaying = true;
    engine.lastTransportStep = 10;
    engine.activeNote = 70;
    transport.barBeat = 0.0;
    result = processBlock(engine, controls, transport, 64, 48000.0);
    assert(result.eventCount >= 2);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].data1 == 70);
    assert(result.events[1].type == MidiEventType::noteOn);
    assert(result.events[1].data1 == 48);

    transport.playing = false;
    result = processBlock(engine, controls, transport, 64, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(engine.activeNote == -1);
}

void testEngineBoundaryEndThenStartScheduling() {
    Controls controls;
    controls.channel = 2;

    EngineState engine;
    activate(engine, controls);

    engine.patternValid = true;
    engine.pattern.patternSteps = 16;
    engine.pattern.stepsPerBeat = 4;
    engine.pattern.stepsPerBar = 16;
    engine.pattern.meter = ::downspout::Meter {};
    engine.pattern.eventCount = 2;
    engine.pattern.events[0] = NoteEvent{0, 1, 40, 90};
    engine.pattern.events[1] = NoteEvent{1, 2, 43, 95};
    engine.activeNote = 40;
    engine.wasPlaying = true;
    engine.lastTransportStep = 0;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = 0.249;
    transport.meter = ::downspout::Meter {};

    const BlockResult result = processBlock(engine, controls, transport, 1024, 48000.0);
    assert(result.eventCount >= 2);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].data1 == 40);
    assert(result.events[1].type == MidiEventType::noteOn);
    assert(result.events[1].data1 == 43);
    assert(result.events[1].frame >= result.events[0].frame);
    assert(result.events[1].channel == 1);
}

void testIncomingMidiDodgeSuppressesMatchedStep() {
    Controls controls;
    controls.followDodge = -1.0f;
    controls.listenChannel = 10;
    controls.listenNote = 36;

    EngineState engine;
    activate(engine, controls);

    engine.patternValid = true;
    engine.pattern.patternSteps = 16;
    engine.pattern.stepsPerBeat = 4;
    engine.pattern.stepsPerBar = 16;
    engine.pattern.meter = ::downspout::Meter {};
    engine.pattern.eventCount = 1;
    engine.pattern.events[0] = NoteEvent{1, 2, 43, 95};
    engine.wasPlaying = true;
    engine.lastTransportStep = 0;

    const InputMidiEvent input = noteOn(0, 10, 36, 110);
    const BlockResult result = processBlock(engine,
                                            controls,
                                            playingTransport(0.249),
                                            1024,
                                            48000.0,
                                            &input,
                                            1);

    assert(result.eventCount == 0);
    assert(engine.activeNote == -1);
}

void testIncomingMidiFollowInjectsMatchedStep() {
    Controls controls;
    controls.followDodge = 1.0f;
    controls.listenChannel = 10;
    controls.listenNote = 36;

    EngineState engine;
    activate(engine, controls);

    engine.patternValid = true;
    engine.pattern.patternSteps = 16;
    engine.pattern.stepsPerBeat = 4;
    engine.pattern.stepsPerBar = 16;
    engine.pattern.meter = ::downspout::Meter {};
    engine.pattern.eventCount = 1;
    engine.pattern.events[0] = NoteEvent{4, 2, 48, 92};
    engine.wasPlaying = true;
    engine.lastTransportStep = 0;

    const InputMidiEvent input = noteOn(0, 10, 36, 120);
    BlockResult result = processBlock(engine,
                                      controls,
                                      playingTransport(0.249),
                                      1024,
                                      48000.0,
                                      &input,
                                      1);

    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 48);
    assert(engine.activeNote == 48);

    result = processBlock(engine,
                          controls,
                          playingTransport(0.499),
                          1024,
                          48000.0,
                          nullptr,
                          0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].data1 == 48);
    assert(engine.activeNote == -1);
}

void testIncomingMidiChannelGuideInjectsCompanionNote() {
    Controls controls;
    controls.followDodge = 1.0f;
    controls.inputSensitivity = 1.0f;
    controls.inputMatchMode = InputMatchModeId::channel;
    controls.listenChannel = 4;
    controls.listenNote = 36;

    EngineState engine;
    activate(engine, controls);

    engine.patternValid = true;
    engine.pattern.patternSteps = 16;
    engine.pattern.stepsPerBeat = 4;
    engine.pattern.stepsPerBar = 16;
    engine.pattern.meter = ::downspout::Meter {};
    engine.pattern.eventCount = 1;
    engine.pattern.events[0] = NoteEvent{4, 2, 48, 92};
    engine.wasPlaying = true;
    engine.lastTransportStep = 0;

    const InputMidiEvent input = noteOn(0, 4, 50, 120);
    const BlockResult result = processBlock(engine,
                                            controls,
                                            playingTransport(0.249),
                                            1024,
                                            48000.0,
                                            &input,
                                            1);

    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 57);
    assert(engine.activeNote == 57);
}

void testIncomingMidiSensitivityZeroIgnoresGuide() {
    Controls controls;
    controls.followDodge = 1.0f;
    controls.inputSensitivity = 0.0f;
    controls.inputMatchMode = InputMatchModeId::channel;
    controls.listenChannel = 4;

    EngineState engine;
    activate(engine, controls);

    engine.patternValid = true;
    engine.pattern.patternSteps = 16;
    engine.pattern.stepsPerBeat = 4;
    engine.pattern.stepsPerBar = 16;
    engine.pattern.meter = ::downspout::Meter {};
    engine.pattern.eventCount = 1;
    engine.pattern.events[0] = NoteEvent{4, 2, 48, 92};
    engine.wasPlaying = true;
    engine.lastTransportStep = 0;

    const InputMidiEvent input = noteOn(0, 4, 50, 120);
    const BlockResult result = processBlock(engine,
                                            controls,
                                            playingTransport(0.249),
                                            1024,
                                            48000.0,
                                            &input,
                                            1);

    assert(result.eventCount == 0);
    assert(engine.activeNote == -1);
}

void testSerializationRoundTrip() {
    Controls controls;
    controls.rootNote = 41;
    controls.scale = ScaleId::mixolydian;
    controls.genre = GenreId::funk;
    controls.styleMode = StyleModeId::reel;
    controls.vary = 0.65f;
    controls.color = 0.72f;
    controls.followDodge = -0.35f;
    controls.listenChannel = 10;
    controls.listenNote = 35;
    controls.inputMatchMode = InputMatchModeId::channel;
    controls.inputSensitivity = 0.42f;
    controls.seed = 999u;
    controls.actionNotes = 3;

    PatternState pattern;
    pattern.meter = ::downspout::makeMeter(6, 8);
    regeneratePattern(pattern, controls, pattern.meter, true, true);

    VariationState variation;
    variation.completedLoops = 7;
    variation.lastMutationLoop = 5;

    const auto controlsRoundTrip = deserializeControls(serializeControls(controls));
    const auto patternRoundTrip = deserializePatternState(serializePatternState(pattern));
    const auto variationRoundTrip = deserializeVariationState(serializeVariationState(variation));

    assert(controlsRoundTrip.has_value());
    assert(patternRoundTrip.has_value());
    assert(variationRoundTrip.has_value());
    assert(controlsRoundTrip->rootNote == controls.rootNote);
    assert(controlsRoundTrip->scale == controls.scale);
    assert(controlsRoundTrip->genre == controls.genre);
    assert(controlsRoundTrip->styleMode == controls.styleMode);
    assert(std::fabs(controlsRoundTrip->color - controls.color) < 0.0001f);
    assert(std::fabs(controlsRoundTrip->followDodge - controls.followDodge) < 0.0001f);
    assert(controlsRoundTrip->listenChannel == controls.listenChannel);
    assert(controlsRoundTrip->listenNote == controls.listenNote);
    assert(controlsRoundTrip->inputMatchMode == controls.inputMatchMode);
    assert(std::fabs(controlsRoundTrip->inputSensitivity - controls.inputSensitivity) < 0.0001f);
    assert(controlsRoundTrip->actionNotes == controls.actionNotes);
    assert(patternRoundTrip->eventCount == pattern.eventCount);
    assert(patternRoundTrip->patternSteps == pattern.patternSteps);
    assert(patternRoundTrip->stepsPerBar == pattern.stepsPerBar);
    assert(patternRoundTrip->meter.numerator == 6);
    assert(patternRoundTrip->meter.denominator == 8);
    assert(patternRoundTrip->generationSerial == pattern.generationSerial);
    for (int index = 0; index < pattern.eventCount; ++index) {
        assert(patternRoundTrip->events[index].startStep == pattern.events[index].startStep);
        assert(patternRoundTrip->events[index].durationSteps == pattern.events[index].durationSteps);
        assert(patternRoundTrip->events[index].note == pattern.events[index].note);
        assert(patternRoundTrip->events[index].velocity == pattern.events[index].velocity);
    }
    assert(variationRoundTrip->completedLoops == 7);
    assert(variationRoundTrip->lastMutationLoop == 5);
}

void testCompoundMeterGenerationTracksPulseGrid() {
    Controls controls;
    controls.seed = 4242u;
    controls.genre = GenreId::dub;
    controls.lengthBeats = 12;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.58f;

    PatternState pattern;
    const ::downspout::Meter jigMeter = ::downspout::makeMeter(6, 8);
    regeneratePattern(pattern, controls, jigMeter, true, true);

    assert(pattern.meter.numerator == 6);
    assert(pattern.meter.denominator == 8);
    assert(pattern.stepsPerBar == 24);
    assert(pattern.patternSteps == 48);

    bool hasSecondaryPulse = false;
    for (int index = 0; index < pattern.eventCount; ++index) {
        if ((pattern.events[index].startStep % pattern.stepsPerBar) == 12) {
            hasSecondaryPulse = true;
            break;
        }
    }

    assert(hasSecondaryPulse);
}

void testTripleMeterGenerationTracksSecondaryBeat() {
    Controls controls;
    controls.seed = 8181u;
    controls.genre = GenreId::funk;
    controls.lengthBeats = 9;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.density = 0.52f;

    PatternState pattern;
    const ::downspout::Meter waltzMeter = ::downspout::makeMeter(3, 4);
    regeneratePattern(pattern, controls, waltzMeter, true, true);

    assert(pattern.meter.numerator == 3);
    assert(pattern.meter.denominator == 4);
    assert(pattern.stepsPerBar == 12);
    assert(pattern.patternSteps == 36);

    bool hasSecondaryBeat = false;
    for (int index = 0; index < pattern.eventCount; ++index) {
        const int localStep = pattern.events[index].startStep % pattern.stepsPerBar;
        if (localStep == pattern.stepsPerBeat || localStep == (pattern.stepsPerBeat * 2)) {
            hasSecondaryBeat = true;
            break;
        }
    }

    assert(hasSecondaryBeat);
}

void testEngineRegeneratesOnMeterChange() {
    Controls controls;
    controls.seed = 202u;
    controls.lengthBeats = 12;

    EngineState engine;
    activate(engine, controls);
    const int originalSerial = engine.pattern.generationSerial;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 6.0;
    transport.beatType = 8.0;
    transport.bpm = 120.0;
    transport.meter = ::downspout::makeMeter(6, 8);

    const BlockResult result = processBlock(engine, controls, transport, 128, 48000.0);
    assert(result.eventCount >= 0);
    assert(engine.pattern.meter.numerator == 6);
    assert(engine.pattern.meter.denominator == 8);
    assert(engine.pattern.stepsPerBar == 24);
    assert(engine.pattern.generationSerial > originalSerial);
}

void testEngineActionCountersRegenerateAndResync() {
    Controls controls;
    controls.seed = 91u;
    controls.lengthBeats = 8;
    controls.subdivision = SubdivisionId::sixteenth;

    EngineState engine;
    activate(engine, controls);
    const int initialSerial = engine.pattern.generationSerial;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 1.0;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.meter = ::downspout::Meter {};

    engine.wasPlaying = true;
    engine.lastTransportStep = 4;
    engine.activeNote = 70;

    controls.actionNew += 1;
    BlockResult result = processBlock(engine, controls, transport, 128, 48000.0);
    assert(engine.pattern.generationSerial > initialSerial);
    assert(hasNoteOffFor(result, 70));
    const int newSerial = engine.pattern.generationSerial;

    controls.actionNotes += 1;
    result = processBlock(engine, controls, transport, 128, 48000.0);
    assert(result.eventCount >= 0);
    assert(engine.pattern.generationSerial > newSerial);
    const int notesSerial = engine.pattern.generationSerial;

    controls.actionRhythm += 1;
    result = processBlock(engine, controls, transport, 128, 48000.0);
    assert(result.eventCount >= 0);
    assert(engine.pattern.generationSerial > notesSerial);
}

}  // namespace

int main() {
    testDeterministicGeneration();
    testSubdivisionStepCounts();
    testRhythmRegenerationKeepsPreviousNotes();
    testTransportHelpers();
    testVariationMutatesAfterLoopThreshold();
    testExplicitStyleModesChangePatternShape();
    testJazzGenreOutlinesTwoFiveOne();
    testJazzRoleColorsUseModalChordTones();
    testJazzDominantBarCanUseAlteredColor();
    testColorControlsJazzDominantIntensity();
    testColorInfluencesNonJazzGenres();
    testJazzStrongBeatsTargetChordTones();
    testJazzApproachAndEnclosureNotesTargetChordTones();
    testJazzScaleIdsAreAppended();
    testFugueGenreIsAppendedAndAnswersAtDominant();
    testRockGenrePinsBeatAnchors();
    testMoroderGenreUsesRepetitiveBrightPulse();
    testBebopDominantScaleConstrainsGeneratedNotes();
    testStateSanitization();
    testEngineRewindResyncAndStopNoteOff();
    testEngineBoundaryEndThenStartScheduling();
    testIncomingMidiDodgeSuppressesMatchedStep();
    testIncomingMidiFollowInjectsMatchedStep();
    testIncomingMidiChannelGuideInjectsCompanionNote();
    testIncomingMidiSensitivityZeroIgnoresGuide();
    testSerializationRoundTrip();
    testCompoundMeterGenerationTracksPulseGrid();
    testTripleMeterGenerationTracksSecondaryBeat();
    testEngineRegeneratesOnMeterChange();
    testEngineActionCountersRegenerateAndResync();
    return 0;
}
