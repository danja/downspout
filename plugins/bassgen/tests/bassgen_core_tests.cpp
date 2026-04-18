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

void testDeterministicGeneration() {
    Controls controls;
    controls.seed = 12345u;
    controls.genre = GenreId::acid;
    controls.scale = ScaleId::phrygian;
    controls.lengthBeats = 16;
    controls.subdivision = SubdivisionId::sixteenth;

    PatternState first;
    PatternState second;
    regeneratePattern(first, controls, true, true);
    regeneratePattern(second, controls, true, true);

    assert(first.patternSteps == second.patternSteps);
    assert(first.stepsPerBeat == second.stepsPerBeat);
    assert(first.eventCount == second.eventCount);
    assert(first.generationSerial == second.generationSerial);

    for (int index = 0; index < first.eventCount; ++index) {
        assert(first.events[index].startStep == second.events[index].startStep);
        assert(first.events[index].durationSteps == second.events[index].durationSteps);
        assert(first.events[index].note == second.events[index].note);
        assert(first.events[index].velocity == second.events[index].velocity);
    }
}

void testRhythmRegenerationKeepsPreviousNotes() {
    Controls controls;
    controls.seed = 77u;
    controls.genre = GenreId::dub;

    PatternState pattern;
    regeneratePattern(pattern, controls, true, true);
    const PatternState original = pattern;

    regeneratePattern(pattern, controls, true, false);

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
    regeneratePattern(pattern, controls, true, true);
    const int originalSerial = pattern.generationSerial;

    VariationState variation;
    const bool changed = applyLoopVariation(pattern, variation, controls, 4.0);

    assert(changed);
    assert(variation.completedLoops == 1);
    assert(variation.lastMutationLoop == 1);
    assert(pattern.generationSerial > originalSerial);
}

void testStateSanitization() {
    PatternState raw;
    raw.patternSteps = 999;
    raw.stepsPerBeat = 99;
    raw.eventCount = 2;
    raw.events[0] = NoteEvent{-5, 0, -1, 200};
    raw.events[1] = NoteEvent{999, 999, 300, -2};

    bool valid = false;
    const PatternState sanitized = sanitizePatternState(raw, &valid);
    assert(valid);
    assert(sanitized.version == kPatternStateVersion);
    assert(sanitized.patternSteps == kMaxPatternSteps);
    assert(sanitized.stepsPerBeat == 6);
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
    engine.pattern.eventCount = 1;
    engine.pattern.events[0] = NoteEvent{0, 8, 48, 101};

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = 1.0;

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
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = 0.249;

    const BlockResult result = processBlock(engine, controls, transport, 1024, 48000.0);
    assert(result.eventCount >= 2);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].data1 == 40);
    assert(result.events[1].type == MidiEventType::noteOn);
    assert(result.events[1].data1 == 43);
    assert(result.events[1].frame >= result.events[0].frame);
    assert(result.events[1].channel == 1);
}

void testSerializationRoundTrip() {
    Controls controls;
    controls.rootNote = 41;
    controls.scale = ScaleId::mixolydian;
    controls.genre = GenreId::funk;
    controls.vary = 0.65f;
    controls.seed = 999u;
    controls.actionNotes = 3;

    PatternState pattern;
    regeneratePattern(pattern, controls, true, true);

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
    assert(controlsRoundTrip->actionNotes == controls.actionNotes);
    assert(patternRoundTrip->eventCount == pattern.eventCount);
    assert(patternRoundTrip->patternSteps == pattern.patternSteps);
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

}  // namespace

int main() {
    testDeterministicGeneration();
    testRhythmRegenerationKeepsPreviousNotes();
    testTransportHelpers();
    testVariationMutatesAfterLoopThreshold();
    testStateSanitization();
    testEngineRewindResyncAndStopNoteOff();
    testEngineBoundaryEndThenStartScheduling();
    testSerializationRoundTrip();
    return 0;
}
