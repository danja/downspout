#include "xoxolo_engine.hpp"
#include "xoxolo_serialization.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

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
    assert(pattern.totalSteps == 16);
    for (int lane = 0; lane < kLaneCount; ++lane)
        assert(pattern.lanes[static_cast<std::size_t>(lane)].midiNote == kDefaultLanes[static_cast<std::size_t>(lane)].note);
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
    resizePattern(pattern, 2, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 32);
    setCell(pattern, 0, 0, true);
    setCell(pattern, 0, 20, true);

    resizePattern(pattern, 1, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 16);
    assert(cellActive(pattern, 0, 0));

    resizePattern(pattern, 2, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 32);
    assert(cellActive(pattern, 0, 0));
    assert(!cellActive(pattern, 0, 20));
}

void unsupportedResolutionIsReduced()
{
    Controls controls {};
    controls.bars = 4;
    controls.resolution = ResolutionId::sixteenth;
    controls = clampControls(controls);
    assert(controls.resolution == ResolutionId::eighth);

    PatternState pattern = makeDefaultPattern();
    resizePattern(pattern, controls.bars, controls.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 32);
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
    Controls controls {};
    controls.previewLane = 2;
    controls.previewSerial = 1;
    activate(state, Controls {});

    const BlockResult result = processBlock(state, controls, TransportSnapshot {}, 4096, 48000.0);
    assert(result.eventCount == 2);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 40);
    assert(result.events[1].type == MidiEventType::noteOff);
    assert(result.events[1].data1 == 40);
}

void serializationRoundTrips()
{
    PatternState pattern = makeDefaultPattern();
    pattern.bars = 2;
    pattern.resolution = ResolutionId::sixteenth;
    pattern.channel = 9;
    resizePattern(pattern, pattern.bars, pattern.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
    pattern.lanes[3].midiNote = 72;
    setCell(pattern, 3, 7, true);
    setCell(pattern, 10, 31, true);

    const std::string text = serializePatternState(pattern);
    const auto parsed = deserializePatternState(text);
    assert(parsed.has_value());
    assert(parsed->bars == 2);
    assert(parsed->resolution == ResolutionId::sixteenth);
    assert(parsed->channel == 9);
    assert(parsed->totalSteps == 32);
    assert(parsed->lanes[3].midiNote == 72);
    assert(cellActive(*parsed, 3, 7));
    assert(cellActive(*parsed, 10, 31));
}

}  // namespace

int main()
{
    defaultPatternMatchesDrumkit();
    togglesCells();
    resizePreservesOnlyVisibleCells();
    unsupportedResolutionIsReduced();
    clampsNotesAndChannel();
    stoppedTransportEmitsNoSequence();
    playStartEmitsCurrentStep();
    boundaryEmitsLaterStep();
    previewEmitsOnePair();
    serializationRoundTrips();
    std::cout << "xoxolo core tests passed\n";
    return 0;
}
