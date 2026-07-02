#include "melgen_engine.hpp"
#include "melgen_pattern.hpp"
#include "melgen_serialization.hpp"
#include "melgen_transport.hpp"
#include "melgen_variation.hpp"

#include <cassert>
#include <cmath>

using namespace downspout::melgen;

namespace {

bool patternsEqual(const PatternState& a, const PatternState& b)
{
    if (a.eventCount != b.eventCount ||
        a.phraseCount != b.phraseCount ||
        a.patternSteps != b.patternSteps ||
        a.stepsPerBar != b.stepsPerBar) {
        return false;
    }
    for (int i = 0; i < a.eventCount; ++i) {
        if (a.events[i].startStep != b.events[i].startStep ||
            a.events[i].durationSteps != b.events[i].durationSteps ||
            a.events[i].note != b.events[i].note ||
            a.events[i].velocity != b.events[i].velocity) {
            return false;
        }
    }
    return true;
}

int phraseEndNote(const PatternState& pattern, int phraseIndex)
{
    const PhraseInfo& phrase = pattern.phrases[phraseIndex];
    const int end = phrase.startStep + phrase.lengthSteps;
    int note = -1;
    int latest = -1;
    for (int i = 0; i < pattern.eventCount; ++i) {
        const NoteEvent& event = pattern.events[i];
        if (event.startStep >= phrase.startStep && event.startStep < end && event.startStep >= latest) {
            latest = event.startStep;
            note = event.note;
        }
    }
    return note;
}

int phraseStartNote(const PatternState& pattern, int phraseIndex)
{
    const PhraseInfo& phrase = pattern.phrases[phraseIndex];
    const int end = phrase.startStep + phrase.lengthSteps;
    int note = -1;
    int earliest = pattern.patternSteps + 1;
    for (int i = 0; i < pattern.eventCount; ++i) {
        const NoteEvent& event = pattern.events[i];
        if (event.startStep >= phrase.startStep && event.startStep < end && event.startStep < earliest) {
            earliest = event.startStep;
            note = event.note;
        }
    }
    return note;
}

InputMidiEvent noteOn(std::uint32_t frame, std::uint8_t note, std::uint8_t velocity = 100)
{
    InputMidiEvent event {};
    event.frame = frame;
    event.size = 3;
    event.data[0] = 0x90;
    event.data[1] = note;
    event.data[2] = velocity;
    return event;
}

void testDeterministicGeneration()
{
    Controls controls;
    controls.seed = 1234u;
    controls.period = PeriodId::callAnswer;
    controls.structure = 0.85f;

    PatternState first;
    PatternState second;
    regeneratePattern(first, controls, ::downspout::Meter {}, true, true);
    regeneratePattern(second, controls, ::downspout::Meter {}, true, true);

    assert(patternsEqual(first, second));
    assert(first.phraseCount > 1);
}

void testSubdivisionStepCounts()
{
    assert(stepsPerBeatForSubdivision(SubdivisionId::quarter) == 1);
    assert(stepsPerBeatForSubdivision(SubdivisionId::eighth) == 2);
    assert(stepsPerBeatForSubdivision(SubdivisionId::sixteenth) == 4);
    assert(stepsPerBeatForSubdivision(SubdivisionId::sixteenthTriplet) == 6);
}

void testCallAnswerPhraseMetadata()
{
    Controls controls;
    controls.lengthBeats = 16;
    controls.phraseLengthBars = 2;
    controls.period = PeriodId::callAnswer;
    controls.structure = 0.95f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    assert(pattern.phraseCount == 2);
    assert(pattern.phrases[0].role == PhraseRole::call);
    assert(pattern.phrases[1].role == PhraseRole::answer);
    assert(pattern.phrases[1].sourcePhrase == 0);
    assert(pattern.events[0].startStep < pattern.phrases[1].startStep);
}

void testStructureChangesPhraseRelationship()
{
    Controls structured;
    structured.seed = 77u;
    structured.period = PeriodId::aa;
    structured.structure = 1.0f;
    structured.range = 0.2f;
    structured.leap = 0.0f;

    PatternState high;
    regeneratePattern(high, structured, ::downspout::Meter {}, true, true);

    Controls loose = structured;
    loose.structure = 0.0f;
    loose.period = PeriodId::free;
    PatternState low;
    regeneratePattern(low, loose, ::downspout::Meter {}, true, true);

    assert(high.phraseCount >= 2);
    assert(low.phraseCount >= 2);
    assert(!patternsEqual(high, low));
}

void testCadenceTargetsRoot()
{
    Controls controls;
    controls.rootNote = 60;
    controls.scale = ScaleId::major;
    controls.period = PeriodId::callAnswer;
    controls.cadence = 1.0f;
    controls.structure = 1.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    const int answerEnd = phraseEndNote(pattern, 1);
    assert(answerEnd >= 0);
    assert((answerEnd - controls.rootNote) % 12 == 0);
}

void testColorInfluencesGeneratedLine()
{
    Controls low;
    low.seed = 4242u;
    low.scale = ScaleId::bebopDominant;
    low.period = PeriodId::callAnswer;
    low.density = 0.8f;
    low.rest = 0.05f;
    low.color = 0.0f;

    Controls high = low;
    high.color = 1.0f;

    PatternState restrained;
    PatternState colored;
    regeneratePattern(restrained, low, ::downspout::Meter {}, true, true);
    regeneratePattern(colored, high, ::downspout::Meter {}, true, true);

    assert(restrained.eventCount > 0);
    assert(colored.eventCount > 0);
    assert(!patternsEqual(restrained, colored));
}

void testStrictFugueRegionAnswersAtDominant()
{
    Controls controls;
    controls.rootNote = 60;
    controls.scale = ScaleId::major;
    controls.lengthBeats = 8;
    controls.phraseLengthBars = 1;
    controls.period = PeriodId::callAnswer;
    controls.answer = AnswerId::related;
    controls.structure = 1.0f;
    controls.leap = 0.0f;
    controls.rest = 0.0f;
    controls.density = 1.0f;
    controls.cadence = 0.0f;
    controls.color = 0.0f;
    controls.seed = 2026u;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, true, true);

    assert(pattern.phraseCount == 2);
    assert(pattern.phrases[0].role == PhraseRole::call);
    assert(pattern.phrases[1].role == PhraseRole::answer);
    assert(phraseStartNote(pattern, 0) == 60);
    assert(phraseStartNote(pattern, 1) == 67);
}

void testEngineSchedulesMidi()
{
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
    engine.pattern.events[0] = NoteEvent{0, 4, 64, 101};

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = 1.0;
    transport.meter = ::downspout::Meter {};

    const BlockResult result = processBlock(engine, controls, transport, 64, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 64);
}

void testFollowInfluencesGeneratedNoteWithoutCopyingInput()
{
    Controls controls;
    controls.lengthBeats = 8;
    controls.subdivision = SubdivisionId::sixteenth;
    controls.rootNote = 60;
    controls.scale = ScaleId::major;
    controls.follow = 1.0f;

    EngineState engine;
    activate(engine, controls);
    engine.patternValid = true;
    engine.pattern.patternSteps = 32;
    engine.pattern.stepsPerBeat = 4;
    engine.pattern.stepsPerBar = 16;
    engine.pattern.meter = ::downspout::Meter {};
    engine.pattern.eventCount = 1;
    engine.pattern.events[0] = NoteEvent{0, 4, 72, 101};

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.meter = ::downspout::Meter {};

    const InputMidiEvent input = noteOn(0, 62);
    const BlockResult result = processBlock(engine, controls, transport, 64, 48000.0, &input, 1);

    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 != 72);
    assert(result.events[0].data1 != 62);
    assert(result.events[0].data1 >= 60);
    assert(result.events[0].data1 <= 72);
}

void testSerializationRoundTrip()
{
    Controls controls;
    controls.rootNote = 62;
    controls.period = PeriodId::aba;
    controls.contour = ContourId::rising;
    controls.answer = AnswerId::invert;
    controls.structure = 0.73f;
    controls.follow = 0.37f;
    controls.color = 0.81f;
    controls.actionNotes = 4;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::makeMeter(6, 8), true, true);

    VariationState variation;
    variation.completedLoops = 3;
    variation.lastMutationLoop = 2;

    const auto controlsRoundTrip = deserializeControls(serializeControls(controls));
    const auto patternRoundTrip = deserializePatternState(serializePatternState(pattern));
    const auto variationRoundTrip = deserializeVariationState(serializeVariationState(variation));

    assert(controlsRoundTrip.has_value());
    assert(patternRoundTrip.has_value());
    assert(variationRoundTrip.has_value());
    assert(controlsRoundTrip->rootNote == controls.rootNote);
    assert(controlsRoundTrip->period == controls.period);
    assert(controlsRoundTrip->contour == controls.contour);
    assert(controlsRoundTrip->answer == controls.answer);
    assert(std::fabs(controlsRoundTrip->follow - controls.follow) < 0.0001f);
    assert(std::fabs(controlsRoundTrip->color - controls.color) < 0.0001f);
    assert(patternRoundTrip->eventCount == pattern.eventCount);
    assert(patternRoundTrip->phraseCount == pattern.phraseCount);
    assert(patternRoundTrip->meter.numerator == 6);
    assert(patternRoundTrip->meter.denominator == 8);
    assert(variationRoundTrip->completedLoops == 3);
}

}  // namespace

int main()
{
    testDeterministicGeneration();
    testSubdivisionStepCounts();
    testCallAnswerPhraseMetadata();
    testStructureChangesPhraseRelationship();
    testCadenceTargetsRoot();
    testColorInfluencesGeneratedLine();
    testStrictFugueRegionAnswersAtDominant();
    testEngineSchedulesMidi();
    testFollowInfluencesGeneratedNoteWithoutCopyingInput();
    testSerializationRoundTrip();
    return 0;
}
