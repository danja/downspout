#include "cadence_engine.hpp"
#include "cadence_harmony.hpp"
#include "cadence_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::cadence;

namespace {

InputMidiEvent makeEvent(const std::uint32_t frame,
                         const std::uint8_t status,
                         const std::uint8_t data1,
                         const std::uint8_t data2)
{
    InputMidiEvent event {};
    event.frame = frame;
    event.size = 3;
    event.data[0] = status;
    event.data[1] = data1;
    event.data[2] = data2;
    return event;
}

void testHarmonyBuildFromCapture()
{
    std::array<SegmentCapture, kMaxSegments> capture {};
    capture[0].duration[0] = 1.0;
    capture[0].onset[0] = 1.4;
    capture[1].duration[2] = 1.0;
    capture[1].onset[2] = 1.3;
    capture[2].duration[4] = 1.0;
    capture[2].onset[4] = 1.2;
    capture[3].duration[7] = 1.0;
    capture[3].onset[7] = 1.5;

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;

    std::array<ChordSlot, kMaxSegments> slots {};
    const CadenceBuildOptions options {};
    const bool built = cadence_build_progression_from_capture(capture.data(), 4, controls, nullptr, 0, options, slots.data());
    assert(built);

    int validCount = 0;
    for (int i = 0; i < 4; ++i) {
        validCount += slots[i].valid ? 1 : 0;
    }
    assert(validCount >= 3);
}

void testSerializationRoundTrip()
{
    Controls controls = defaultControls();
    controls.key = 9;
    controls.scale = SCALE_DORIAN;
    controls.cycle_bars = 3;
    controls.granularity = GRANULARITY_BAR;
    controls.complexity = 0.72f;
    controls.movement = 0.48f;
    controls.chord_size = CHORD_SIZE_SEVENTHS;
    controls.note_length = 0.66f;
    controls.reg = REGISTER_HIGH;
    controls.spread = SPREAD_OPEN;
    controls.pass_input = false;
    controls.output_channel = 5;
    controls.action_learn = 12;
    controls.vary = 0.41f;
    controls.comp = 0.33f;

    const auto controlsRoundTrip = deserializeControls(serializeControls(controls));
    assert(controlsRoundTrip.has_value());
    assert(controlsRoundTrip->key == 9);
    assert(controlsRoundTrip->scale == SCALE_DORIAN);
    assert(controlsRoundTrip->cycle_bars == 3);
    assert(std::fabs(controlsRoundTrip->note_length - 0.66f) < 1e-6f);
    assert(!controlsRoundTrip->pass_input);
    assert(controlsRoundTrip->output_channel == 5);

    ProgressionState progression;
    progression.segmentCount = 2;
    progression.ready = true;
    progression.slots[0].valid = true;
    progression.slots[0].root_pc = 0;
    progression.slots[0].quality = QUALITY_MINOR;
    progression.slots[0].note_count = 3;
    progression.slots[0].velocity = 96;
    progression.slots[0].notes = {48, 51, 55, 0};

    progression.slots[1].valid = true;
    progression.slots[1].root_pc = 7;
    progression.slots[1].quality = QUALITY_MAJOR;
    progression.slots[1].note_count = 3;
    progression.slots[1].velocity = 92;
    progression.slots[1].notes = {55, 59, 62, 0};

    const auto progressionRoundTrip = deserializeProgressionState(serializeProgressionState(progression));
    assert(progressionRoundTrip.has_value());
    assert(progressionRoundTrip->segmentCount == 2);
    assert(progressionRoundTrip->ready);
    assert(progressionRoundTrip->slots[0].valid);
    assert(progressionRoundTrip->slots[1].root_pc == 7);

    VariationState variation = defaultVariationState();
    variation.completed_cycles = 7;
    variation.last_mutation_cycle = 6;
    variation.mutation_serial = 3;
    const auto variationRoundTrip = deserializeVariationState(serializeVariationState(variation));
    assert(variationRoundTrip.has_value());
    assert(variationRoundTrip->completed_cycles == 7);
    assert(variationRoundTrip->mutation_serial == 3);
}

void testStoppedTransportPassThrough()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.pass_input = true;
    const InputMidiEvent event = makeEvent(0, 0x90, 60, 100);

    TransportSnapshot transport;
    transport.valid = false;

    const BlockResult result = processBlock(state, controls, transport, 256, 48000.0, &event, 1);
    assert(!result.ready);
    assert(result.eventCount == 1);
    assert(result.events[0].size == 3);
    assert(result.events[0].data[0] == 0x90);
    assert(result.events[0].data[1] == 60);
    assert(result.events[0].data[2] == 100);
}

void testEngineLearnsAndEmitsOnNextCycle()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.output_channel = 1;

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 8> firstCycle = {{
        makeEvent(0, 0x90, 60, 100),
        makeEvent(24000, 0x80, 60, 0),
        makeEvent(24000, 0x90, 64, 100),
        makeEvent(48000, 0x80, 64, 0),
        makeEvent(48000, 0x90, 67, 100),
        makeEvent(72000, 0x80, 67, 0),
        makeEvent(72000, 0x90, 69, 100),
        makeEvent(95999, 0x80, 69, 0),
    }};

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    const BlockResult learned = processBlock(state,
                                             controls,
                                             transport,
                                             frameCount,
                                             48000.0,
                                             firstCycle.data(),
                                             static_cast<std::uint32_t>(firstCycle.size()));
    assert(learned.ready);
    assert(state.ready);

    transport.bar = 1.0;
    transport.barBeat = 0.0;

    const BlockResult playback = processBlock(state,
                                              controls,
                                              transport,
                                              24000,
                                              48000.0,
                                              nullptr,
                                              0);
    assert(playback.ready);

    bool sawNoteOn = false;
    for (int i = 0; i < playback.eventCount; ++i) {
        if ((playback.events[i].data[0] & 0xF0) == 0x90 && playback.events[i].size >= 3 && playback.events[i].data[2] > 0) {
            sawNoteOn = true;
            break;
        }
    }
    assert(sawNoteOn);
}

}  // namespace

int main()
{
    testHarmonyBuildFromCapture();
    testSerializationRoundTrip();
    testStoppedTransportPassThrough();
    testEngineLearnsAndEmitsOnNextCycle();
    return 0;
}
