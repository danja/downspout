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

void testSerializationRoundTrip() {
    Controls controls;
    controls.rootNote = 41;
    controls.scale = ScaleId::mixolydian;
    controls.genre = GenreId::funk;
    controls.styleMode = StyleModeId::reel;
    controls.vary = 0.65f;
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

}  // namespace

int main() {
    testDeterministicGeneration();
    testRhythmRegenerationKeepsPreviousNotes();
    testTransportHelpers();
    testVariationMutatesAfterLoopThreshold();
    testExplicitStyleModesChangePatternShape();
    testStateSanitization();
    testEngineRewindResyncAndStopNoteOff();
    testEngineBoundaryEndThenStartScheduling();
    testSerializationRoundTrip();
    testCompoundMeterGenerationTracksPulseGrid();
    testTripleMeterGenerationTracksSecondaryBeat();
    testEngineRegeneratesOnMeterChange();
    return 0;
}
