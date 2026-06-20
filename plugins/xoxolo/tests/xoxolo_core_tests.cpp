#include "xoxolo_engine.hpp"
#include "xoxolo_serialization.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

namespace {

using namespace downspout::xoxolo;

TransportSnapshot playingTransport(double barBeat = 0.0)
{
    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = barBeat;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.meter = downspout::meterFromTimeSignature(4.0, 4.0);
    return transport;
}

void defaultPatternMatchesDrumkit()
{
    PatternState pattern = makeDefaultPattern();
    assert(pattern.totalSteps == kDefaultSteps);
    assert(pattern.notePreset == NotePresetId::downspout);
    assert(activeLaneCountForPreset(pattern.notePreset) == kDownspoutLaneCount);
    for (int lane = 0; lane < activeLaneCountForPreset(pattern.notePreset); ++lane)
        assert(pattern.lanes[static_cast<std::size_t>(lane)].midiNote == kDefaultLanes[static_cast<std::size_t>(lane)].note);
}

void presetAppliesAvlDrumkitsMap()
{
    PatternState pattern = makeDefaultPattern();
    applyNotePreset(pattern, NotePresetId::avlDrumkits);
    assert(pattern.notePreset == NotePresetId::avlDrumkits);
    assert(pattern.lanes[0].midiNote == 36);
    assert(activeLaneCountForPreset(pattern.notePreset) == kAvlDrumkitsLaneCount);
    assert(pattern.lanes[1].midiNote == 37);
    assert(pattern.lanes[2].midiNote == 38);
    assert(pattern.lanes[3].midiNote == 39);
    assert(pattern.lanes[20].midiNote == 56);
    assert(pattern.lanes[28].midiNote == 64);
    assert(std::string(laneSpecsForPreset(pattern.notePreset)[20].name) == "Cowbell");
}

void togglesCells()
{
    PatternState pattern = makeDefaultPattern();
    assert(!cellActive(pattern, 0, 0));
    setCell(pattern, 0, 0, true);
    assert(cellActive(pattern, 0, 0));
    setCell(pattern, 0, 0, false);
    assert(!cellActive(pattern, 0, 0));
}

void resizePreservesOnlyVisibleCells()
{
    PatternState pattern = makeDefaultPattern();
    resizePattern(pattern, 32, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 32);
    setCell(pattern, 0, 0, true);
    setCell(pattern, 0, 20, true);

    resizePattern(pattern, 13, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 13);
    assert(cellActive(pattern, 0, 0));

    resizePattern(pattern, 32, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 32);
    assert(cellActive(pattern, 0, 0));
    assert(!cellActive(pattern, 0, 20));
}

void controlsClampStepRange()
{
    Controls controls {};
    controls.steps = 99;
    controls = clampControls(controls);
    assert(controls.steps == kMaxSteps);

    PatternState pattern = makeDefaultPattern();
    resizePattern(pattern, 4, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == kMinSteps);
}

void clampsNotesAndChannel()
{
    PatternState pattern = makeDefaultPattern();
    pattern.lanes[0].midiNote = -10;
    pattern.lanes[1].midiNote = 200;
    sanitizePattern(pattern);
    assert(pattern.lanes[0].midiNote == 0);
    assert(pattern.lanes[1].midiNote == 127);

    Controls controls {};
    controls.channel = 99;
    assert(clampControls(controls).channel == 16);
}

void stoppedTransportEmitsNoSequence()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    setCell(state.pattern, 0, 0, true);
    Controls controls {};
    activate(state, controls);

    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = false;
    const BlockResult result = processBlock(state, controls, transport, 512, 48000.0);
    assert(result.eventCount == 0);
    assert(result.currentStep == -1);
}

void playStartEmitsCurrentStep()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    setCell(state.pattern, 0, 0, true);
    Controls controls {};
    activate(state, controls);

    const BlockResult result = processBlock(state, controls, playingTransport(), 512, 48000.0);
    assert(result.eventCount >= 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 36);
    assert(result.currentStep == 0);
}

void boundaryEmitsLaterStep()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    setCell(state.pattern, 0, 1, true);
    Controls controls {};
    activate(state, controls);

    const BlockResult result = processBlock(state, controls, playingTransport(0.24), 2400, 48000.0);
    bool found = false;
    for (int i = 0; i < result.eventCount; ++i)
        found = found || (result.events[static_cast<std::size_t>(i)].type == MidiEventType::noteOn &&
                          result.events[static_cast<std::size_t>(i)].data1 == 36);
    assert(found);
}

void previewEmitsOnePair()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    state.pattern.lanes[2].midiNote = 72;
    Controls controls {};
    controls.channel = 3;
    controls.previewLane = 2;
    controls.previewSerial = 1;
    activate(state, Controls {});

    const BlockResult result = processBlock(state, controls, TransportSnapshot {}, 4096, 48000.0);
    assert(result.eventCount == 2);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].channel == 9);
    assert(result.events[0].data1 == 72);
    assert(result.events[1].type == MidiEventType::noteOff);
    assert(result.events[1].channel == 9);
    assert(result.events[1].data1 == 72);
}

void serializationRoundTrips()
{
    PatternState pattern = makeDefaultPattern();
    pattern.totalSteps = 27;
    pattern.resolution = ResolutionId::sixteenth;
    pattern.channel = 9;
    applyNotePreset(pattern, NotePresetId::avlDrumkits);
    resizePattern(pattern, pattern.totalSteps, pattern.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
    pattern.lanes[3].midiNote = 72;
    setCell(pattern, 3, 7, true);
    setCell(pattern, 10, 26, true);

    const std::string text = serializePatternState(pattern);
    assert(text.find("length=27\n") != std::string::npos);
    assert(text.find("steps=3,") != std::string::npos);
    const auto parsed = deserializePatternState(text);
    assert(parsed.has_value());
    assert(parsed->resolution == ResolutionId::sixteenth);
    assert(parsed->channel == 9);
    assert(parsed->notePreset == NotePresetId::avlDrumkits);
    assert(parsed->totalSteps == 27);
    assert(parsed->lanes[3].midiNote == 72);
    assert(cellActive(*parsed, 3, 7));
    assert(cellActive(*parsed, 10, 26));
}

}  // namespace

int main()
{
    defaultPatternMatchesDrumkit();
    presetAppliesAvlDrumkitsMap();
    togglesCells();
    resizePreservesOnlyVisibleCells();
    controlsClampStepRange();
    clampsNotesAndChannel();
    stoppedTransportEmitsNoSequence();
    playStartEmitsCurrentStep();
    boundaryEmitsLaterStep();
    previewEmitsOnePair();
    serializationRoundTrips();
    std::cout << "xoxolo core tests passed\n";
    return 0;
}
