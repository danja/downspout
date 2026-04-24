#include "drumgen_engine.hpp"
#include "drumgen_pattern.hpp"
#include "drumgen_serialization.hpp"
#include "drumgen_state.hpp"
#include "drumgen_transport.hpp"
#include "drumgen_variation.hpp"

#include <cassert>
#include <cmath>

using namespace downspout::drumgen;

namespace {

bool equalStep(const DrumStepCell& a, const DrumStepCell& b) {
    return a.velocity == b.velocity && a.flags == b.flags;
}

PatternState makeSingleHitPattern(int step, int note, int velocity) {
    PatternState pattern;
    pattern.version = kPatternStateVersion;
    pattern.bars = 1;
    pattern.stepsPerBeat = 4;
    pattern.stepsPerBar = 16;
    pattern.totalSteps = 16;
    pattern.generationSerial = 1;
    pattern.lanes[static_cast<int>(LaneId::kick)].midiNote = note;
    pattern.lanes[static_cast<int>(LaneId::kick)].steps[step].velocity = static_cast<std::uint8_t>(velocity);
    return pattern;
}

TransportSnapshot makePlayingTransport(double barBeat) {
    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = barBeat;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;
    return transport;
}

int activePendingCount(const EngineState& state) {
    int count = 0;
    for (const PendingNoteOff& pending : state.pendingNoteOffs) {
        if (pending.active) {
            count += 1;
        }
    }
    return count;
}

void testDeterministicGeneration() {
    Controls controls;
    controls.seed = 12345u;
    controls.genre = GenreId::electro;
    controls.kitMap = KitMapId::gm;
    controls.bars = 4;
    controls.resolution = ResolutionId::sixteenthTriplet;

    PatternState first;
    PatternState second;
    regeneratePattern(first, controls, false);
    regeneratePattern(second, controls, false);

    assert(first.bars == second.bars);
    assert(first.stepsPerBeat == second.stepsPerBeat);
    assert(first.stepsPerBar == second.stepsPerBar);
    assert(first.totalSteps == second.totalSteps);
    assert(first.generationSerial == second.generationSerial);

    for (int lane = 0; lane < kLaneCount; ++lane) {
        assert(first.lanes[lane].midiNote == second.lanes[lane].midiNote);
        for (int step = 0; step < first.totalSteps; ++step) {
            assert(equalStep(first.lanes[lane].steps[step], second.lanes[lane].steps[step]));
        }
    }
}

void testFillRefreshKeepsEarlierBars() {
    Controls controls;
    controls.seed = 77u;
    controls.genre = GenreId::rock;
    controls.bars = 4;

    PatternState pattern;
    regeneratePattern(pattern, controls, false);
    const PatternState original = pattern;

    regeneratePattern(pattern, controls, true);

    assert(pattern.generationSerial > original.generationSerial);
    assert(pattern.stepsPerBar == original.stepsPerBar);
    assert(pattern.totalSteps == original.totalSteps);

    const int prefixSteps = pattern.totalSteps - pattern.stepsPerBar;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < prefixSteps; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
    }
}

void testRefreshBarKeepsOtherBars() {
    Controls controls;
    controls.seed = 313u;
    controls.genre = GenreId::bossa;
    controls.bars = 3;

    PatternState pattern;
    regeneratePattern(pattern, controls, false);
    const PatternState original = pattern;

    refreshBar(pattern, controls, 1);

    const int barStart = pattern.stepsPerBar;
    const int barEnd = barStart + pattern.stepsPerBar;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < barStart; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barEnd; step < pattern.totalSteps; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
    }
}

void testRefreshFillBarTargetsChosenBar() {
    Controls controls;
    controls.seed = 123u;
    controls.genre = GenreId::electro;
    controls.bars = 4;
    controls.fill = 0.10f;

    PatternState pattern;
    regeneratePattern(pattern, controls, false);
    const PatternState original = pattern;

    refreshFillBar(pattern, controls, 1);

    const int barStart = pattern.stepsPerBar;
    const int barEnd = barStart + pattern.stepsPerBar;
    bool sawFillFlag = false;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < barStart; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barEnd; step < pattern.totalSteps; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barStart; step < barEnd; ++step) {
            if ((pattern.lanes[lane].steps[step].flags & kStepFlagFill) != 0) {
                sawFillFlag = true;
            }
        }
    }

    assert(pattern.generationSerial > original.generationSerial);
    assert(sawFillFlag);
}

void testTransportHelpers() {
    PatternState pattern;
    pattern.totalSteps = 16;

    assert(transportRestartDetected(false, -1, 0));
    assert(transportRestartDetected(true, 12, 4));
    assert(!transportRestartDetected(true, 4, 8));

    assert(std::fabs(localStepFromAbsolute(pattern, 18.5) - 2.5) < 1e-9);
    assert(localStepForBoundary(pattern, 17) == 1);
    assert(frameForBoundary(12.0, 16.0, 64, 14) == 32 || frameForBoundary(12.0, 16.0, 64, 14) == 31);
}

void testVariationBehavior() {
    Controls controls;
    controls.seed = 19u;
    controls.vary = 0.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, false);

    VariationState variation;
    assert(!applyLoopVariation(pattern, variation, controls));
    assert(variation.completedLoops == 1);
    assert(variation.lastMutationLoop == 0);

    controls.vary = 1.0f;
    const int originalSerial = pattern.generationSerial;
    const bool changed = applyLoopVariation(pattern, variation, controls);
    assert(changed);
    assert(pattern.generationSerial > originalSerial);
    assert(variation.lastMutationLoop == variation.completedLoops);
}

void testStateSanitization() {
    PatternState raw;
    raw.bars = 999;
    raw.stepsPerBeat = 99;
    raw.stepsPerBar = 99;
    raw.totalSteps = 999;
    raw.lanes[0].midiNote = 999;
    raw.lanes[0].steps[0].velocity = 255;

    bool valid = false;
    const PatternState sanitized = sanitizePatternState(raw, &valid);
    assert(valid);
    assert(sanitized.version == kPatternStateVersion);
    assert(sanitized.bars == kMaxBars);
    assert(sanitized.stepsPerBeat == 4);
    assert(sanitized.stepsPerBar == 16);
    assert(sanitized.totalSteps == kMaxPatternSteps);
    assert(sanitized.lanes[0].midiNote == 127);
    assert(sanitized.lanes[0].steps[0].velocity == 127);

    VariationState variation;
    variation.version = 99;
    const VariationState sanitizedVariation = sanitizeVariationState(variation);
    assert(sanitizedVariation.version == kVariationStateVersion);
}

void testSerializationRoundTrip() {
    Controls controls;
    controls.genre = GenreId::afro;
    controls.channel = 8;
    controls.kitMap = KitMapId::gm;
    controls.bars = 4;
    controls.resolution = ResolutionId::sixteenthTriplet;
    controls.vary = 0.65f;
    controls.seed = 999u;
    controls.actionMutate = 3;

    PatternState pattern;
    regeneratePattern(pattern, controls, false);

    VariationState variation;
    variation.completedLoops = 7;
    variation.lastMutationLoop = 5;

    const auto controlsRoundTrip = deserializeControls(serializeControls(controls));
    const auto patternRoundTrip = deserializePatternState(serializePatternState(pattern));
    const auto variationRoundTrip = deserializeVariationState(serializeVariationState(variation));

    assert(controlsRoundTrip.has_value());
    assert(patternRoundTrip.has_value());
    assert(variationRoundTrip.has_value());
    assert(controlsRoundTrip->genre == controls.genre);
    assert(controlsRoundTrip->channel == controls.channel);
    assert(controlsRoundTrip->kitMap == controls.kitMap);
    assert(controlsRoundTrip->actionMutate == controls.actionMutate);
    assert(patternRoundTrip->bars == pattern.bars);
    assert(patternRoundTrip->stepsPerBeat == pattern.stepsPerBeat);
    assert(patternRoundTrip->generationSerial == pattern.generationSerial);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        assert(patternRoundTrip->lanes[lane].midiNote == pattern.lanes[lane].midiNote);
        for (int step = 0; step < kMaxPatternSteps; ++step) {
            assert(equalStep(patternRoundTrip->lanes[lane].steps[step], pattern.lanes[lane].steps[step]));
        }
    }
    assert(variationRoundTrip->completedLoops == 7);
    assert(variationRoundTrip->lastMutationLoop == 5);
}

void testEngineCarriesPendingNoteOffsAcrossBlocks() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(0, 36, 100);
    state.patternValid = true;

    const double sampleRate = 48000.0;
    const auto first = processBlock(state, controls, makePlayingTransport(0.0), 2048, sampleRate);
    assert(first.eventCount == 1);
    assert(first.events[0].type == MidiEventType::noteOn);
    assert(first.events[0].frame == 0);
    assert(first.events[0].channel == 9);
    assert(first.events[0].data1 == 36);
    assert(first.events[0].data2 == 100);
    assert(activePendingCount(state) == 1);
    assert(state.pendingNoteOffs[0].remainingSamples == 52);

    const double nextBarBeat = (2048.0 * 120.0) / (60.0 * sampleRate);
    const auto second = processBlock(state, controls, makePlayingTransport(nextBarBeat), 2048, sampleRate);
    assert(second.eventCount == 1);
    assert(second.events[0].type == MidiEventType::noteOff);
    assert(second.events[0].frame == 52);
    assert(second.events[0].channel == 9);
    assert(second.events[0].data1 == 36);
    assert(activePendingCount(state) == 0);
}

void testEngineStopClearsPendingNoteOffs() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(0, 36, 100);
    state.patternValid = true;
    state.wasPlaying = true;
    state.lastTransportStep = 3;
    state.pendingNoteOffs[0].active = true;
    state.pendingNoteOffs[0].note = 60;
    state.pendingNoteOffs[0].channel = 10;
    state.pendingNoteOffs[0].remainingSamples = 4000;

    TransportSnapshot stopped;
    stopped.valid = true;
    stopped.playing = false;
    stopped.beatsPerBar = 4.0;
    stopped.bpm = 120.0;

    const auto result = processBlock(state, controls, stopped, 2048, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].frame == 0);
    assert(result.events[0].channel == 9);
    assert(result.events[0].data1 == 60);
    assert(activePendingCount(state) == 0);
    assert(!state.wasPlaying);
    assert(state.lastTransportStep == -1);
}

void testEngineRestartReplaysCurrentStep() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(0, 36, 100);
    state.patternValid = true;
    state.wasPlaying = true;
    state.lastTransportStep = 8;
    state.pendingNoteOffs[0].active = true;
    state.pendingNoteOffs[0].note = 72;
    state.pendingNoteOffs[0].channel = 10;
    state.pendingNoteOffs[0].remainingSamples = 5000;

    const auto result = processBlock(state, controls, makePlayingTransport(0.0), 256, 48000.0);
    assert(result.eventCount == 2);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].frame == 0);
    assert(result.events[0].data1 == 72);
    assert(result.events[1].type == MidiEventType::noteOn);
    assert(result.events[1].frame == 0);
    assert(result.events[1].data1 == 36);
    assert(result.events[1].data2 == 100);
    assert(activePendingCount(state) == 1);
    assert(state.pendingNoteOffs[0].active);
    assert(state.pendingNoteOffs[0].note == 36);
    assert(state.pendingNoteOffs[0].remainingSamples == 1844);
}

void testEngineSchedulesBoundaryHitsWithinBlock() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(1, 36, 100);
    state.patternValid = true;
    state.wasPlaying = true;
    state.lastTransportStep = 0;

    const auto result = processBlock(state, controls, makePlayingTransport(0.125), 4800, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].channel == 9);
    assert(result.events[0].data1 == 36);
    assert(result.events[0].data2 == 100);
    assert(result.events[0].frame == 2999 || result.events[0].frame == 3000);
    assert(activePendingCount(state) == 1);
    assert(state.pendingNoteOffs[0].remainingSamples == 299 ||
           state.pendingNoteOffs[0].remainingSamples == 300);
}

void testEngineFillTriggerTargetsCurrentBar() {
    Controls controls;
    controls.seed = 987u;
    controls.bars = 4;
    controls.fill = 0.10f;

    EngineState state;
    activate(state, controls);
    const PatternState original = state.pattern;

    controls.actionFill = 1;
    const auto result = processBlock(state, controls, makePlayingTransport(0.0), 256, 48000.0);
    (void)result;

    const int barStart = 0;
    const int barEnd = state.pattern.stepsPerBar;
    bool sawFillFlag = false;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = barEnd; step < state.pattern.totalSteps; ++step) {
            assert(equalStep(state.pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barStart; step < barEnd; ++step) {
            if ((state.pattern.lanes[lane].steps[step].flags & kStepFlagFill) != 0) {
                sawFillFlag = true;
            }
        }
    }

    assert(state.pattern.generationSerial > original.generationSerial);
    assert(sawFillFlag);
}

}  // namespace

int main() {
    testDeterministicGeneration();
    testFillRefreshKeepsEarlierBars();
    testRefreshBarKeepsOtherBars();
    testRefreshFillBarTargetsChosenBar();
    testTransportHelpers();
    testVariationBehavior();
    testStateSanitization();
    testSerializationRoundTrip();
    testEngineCarriesPendingNoteOffsAcrossBlocks();
    testEngineStopClearsPendingNoteOffs();
    testEngineRestartReplaysCurrentStep();
    testEngineSchedulesBoundaryHitsWithinBlock();
    testEngineFillTriggerTargetsCurrentBar();
    return 0;
}
