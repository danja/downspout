#include "m_mix_engine.hpp"
#include "m_mix_serialization.hpp"

#include "downspout/test_assert.h"
#include <cmath>

using namespace downspout::mmix;

namespace {

InputMidiEvent noteOn(const std::uint32_t frame, const std::uint8_t note = 60, const std::uint8_t velocity = 100) {
    InputMidiEvent event {};
    event.frame = frame;
    event.size = 3;
    event.data[0] = 0x90;
    event.data[1] = note;
    event.data[2] = velocity;
    return event;
}

InputMidiEvent noteOff(const std::uint32_t frame, const std::uint8_t note = 60) {
    InputMidiEvent event {};
    event.frame = frame;
    event.size = 3;
    event.data[0] = 0x80;
    event.data[1] = note;
    event.data[2] = 0;
    return event;
}

TransportSnapshot playingTransport() {
    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 60.0;
    return transport;
}

Parameters stableParameters() {
    Parameters parameters {};
    parameters.totalBars = 4.0f;
    parameters.division = 4.0f;
    parameters.steps = 4.0f;
    parameters.offset = 0.0f;
    parameters.fadeBars = 0.0f;
    parameters.granularity = 4.0f;
    parameters.maintain = 100.0f;
    parameters.fade = 0.0f;
    parameters.cut = 0.0f;
    parameters.bias = 100.0f;
    parameters.velocityFades = 1.0f;
    return parameters;
}

void stopped_transport_passes_through() {
    EngineState state {};
    activate(state);

    Parameters parameters = stableParameters();
    TransportSnapshot transport {};
    transport.valid = false;

    const InputMidiEvent events[] = {noteOn(0), noteOff(3)};
    const BlockResult result = processBlock(state, parameters, transport, 8, 1.0, events, 2);

    assert(result.eventCount == 2);
    assert(result.events[0].data[0] == 0x90);
    assert(result.events[0].data[2] == 100);
    assert(result.events[1].data[0] == 0x80);
}

void euclidean_blocks_inactive_blocks() {
    EngineState state {};
    activate(state);

    Parameters parameters = stableParameters();
    parameters.steps = 1.0f;

    const InputMidiEvent events[] = {
        noteOn(1, 60, 100),
        noteOff(2, 60),
        noteOn(5, 62, 100),
        noteOff(6, 62),
    };
    const BlockResult result = processBlock(state, parameters, playingTransport(), 12, 1.0, events, 4);

    assert(result.eventCount == 2);
    assert(result.events[0].frame == 1);
    assert(result.events[0].data[1] == 60);
    assert(result.events[1].frame == 2);
    assert(result.events[1].data[0] == 0x80);
}

void realistic_bar_position_blocks_inactive_block() {
    EngineState state {};
    activate(state);

    Parameters parameters = stableParameters();
    parameters.totalBars = 2.0f;
    parameters.division = 2.0f;
    parameters.steps = 1.0f;

    TransportSnapshot transport = playingTransport();
    const BlockResult first = processBlock(state, parameters, transport, 64, 48000.0, nullptr, 0);
    assert(first.eventCount == 0);

    transport.bar = 1.0;

    const InputMidiEvent events[] = {noteOn(0, 60, 100)};
    const BlockResult result = processBlock(state, parameters, transport, 64, 48000.0, events, 1);

    assert(result.eventCount == 0);
}

void euclidean_fade_scales_note_velocity() {
    EngineState state {};
    activate(state);

    Parameters parameters = stableParameters();
    parameters.totalBars = 1.0f;
    parameters.division = 1.0f;
    parameters.steps = 1.0f;
    parameters.fadeBars = 0.5f;

    const InputMidiEvent events[] = {noteOn(1, 60, 100)};
    const BlockResult result = processBlock(state, parameters, playingTransport(), 4, 1.0, events, 1);

    assert(result.eventCount == 1);
    assert(result.events[0].data[2] == 50);
}

void probabilistic_cut_can_close_gate() {
    EngineState state {};
    activate(state);

    Parameters parameters = stableParameters();
    parameters.maintain = 0.0f;
    parameters.fade = 0.0f;
    parameters.cut = 100.0f;
    parameters.bias = 0.0f;

    const InputMidiEvent events[] = {noteOn(0, 60, 100)};
    const BlockResult result = processBlock(state, parameters, playingTransport(), 4, 1.0, events, 1);

    assert(result.eventCount == 0);
}

void gate_closure_releases_held_notes() {
    EngineState state {};
    activate(state);

    Parameters parameters = stableParameters();
    parameters.totalBars = 2.0f;
    parameters.division = 2.0f;
    parameters.steps = 1.0f;

    const InputMidiEvent events[] = {noteOn(1, 60, 100)};
    const BlockResult result = processBlock(state, parameters, playingTransport(), 9, 1.0, events, 1);

    assert(result.eventCount == 2);
    assert(result.events[0].frame == 1);
    assert(result.events[0].data[0] == 0x90);
    assert(result.events[1].frame == 4);
    assert(result.events[1].data[0] == 0x80);
    assert(result.events[1].data[1] == 60);
}

void serialization_round_trips_parameters() {
    Parameters parameters = stableParameters();
    parameters.fadeBars = 0.25f;
    parameters.fade = 30.0f;
    parameters.cut = 70.0f;
    parameters.velocityFades = 0.0f;

    const auto roundTrip = deserializeParameters(serializeParameters(parameters));
    assert(roundTrip.has_value());
    assert(std::fabs(roundTrip->fadeBars - 0.25f) < 1e-6f);
    assert(std::fabs(roundTrip->fade - 30.0f) < 1e-6f);
    assert(std::fabs(roundTrip->cut - 70.0f) < 1e-6f);
    assert(std::fabs(roundTrip->velocityFades - 0.0f) < 1e-6f);
}

}  // namespace

int main() {
    stopped_transport_passes_through();
    euclidean_blocks_inactive_blocks();
    realistic_bar_position_blocks_inactive_block();
    euclidean_fade_scales_note_velocity();
    probabilistic_cut_can_close_gate();
    gate_closure_releases_held_notes();
    serialization_round_trips_parameters();
    return 0;
}
