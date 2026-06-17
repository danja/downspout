#include "cadence_ai_state.hpp"
#include "cadence_engine.hpp"
#include "cadence_harmony.hpp"
#include "cadence_serialization.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <string>

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
    controls.color = 0.83f;
    controls.chord_size = CHORD_SIZE_SEVENTHS;
    controls.note_length = 0.66f;
    controls.reg = REGISTER_HIGH;
    controls.spread = 0.74f;
    controls.arpeggio = 0.52f;
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
    assert(std::fabs(controlsRoundTrip->color - 0.83f) < 1e-6f);
    assert(std::fabs(controlsRoundTrip->spread - 0.74f) < 1e-6f);
    assert(std::fabs(controlsRoundTrip->arpeggio - 0.52f) < 1e-6f);
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

void testHighColorFavorsJazzCadenceRoles()
{
    std::array<SegmentCapture, kMaxSegments> capture {};
    for (int s = 0; s < 4; ++s) {
        for (int pc = 0; pc < 12; ++pc) {
            capture[static_cast<std::size_t>(s)].duration[static_cast<std::size_t>(pc)] = 0.02;
        }
    }

    Controls controls = defaultControls();
    controls.key = 0;
    controls.scale = SCALE_BEBOP_MAJOR;
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.chord_size = CHORD_SIZE_SEVENTHS;
    controls.complexity = 0.82f;
    controls.movement = 0.78f;
    controls.color = 1.0f;

    std::array<ChordSlot, kMaxSegments> slots {};
    const CadenceBuildOptions options {};
    const bool built = cadence_build_progression_from_capture(capture.data(), 4, controls, nullptr, 0, options, slots.data());

    assert(built);
    assert(slots[1].valid);
    assert(slots[2].valid);
    assert(slots[3].valid);
    assert(slots[1].root_pc == 2);
    assert(slots[1].quality == QUALITY_MIN7 || slots[1].quality == QUALITY_MINOR);
    assert(slots[2].root_pc == 7);
    assert(slots[2].quality == QUALITY_DOM7);
    assert(slots[3].root_pc == 0);
    assert(slots[3].quality == QUALITY_MAJ7 || slots[3].quality == QUALITY_MAJOR);
}

void testHighColorFavorsCircleOfFifthsAndSuspendedDominant()
{
    std::array<SegmentCapture, kMaxSegments> capture {};
    for (int s = 0; s < 4; ++s) {
        for (int pc = 0; pc < 12; ++pc) {
            capture[static_cast<std::size_t>(s)].duration[static_cast<std::size_t>(pc)] = 0.02;
        }
    }

    Controls controls = defaultControls();
    controls.key = 0;
    controls.scale = SCALE_MAJOR;
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.chord_size = CHORD_SIZE_TRIADS;
    controls.complexity = 0.90f;
    controls.movement = 0.88f;
    controls.color = 1.0f;

    std::array<ChordSlot, kMaxSegments> slots {};
    const CadenceBuildOptions options {};
    const bool built = cadence_build_progression_from_capture(capture.data(), 4, controls, nullptr, 0, options, slots.data());

    assert(built);
    assert(slots[0].valid);
    assert(slots[1].valid);
    assert(slots[2].valid);
    assert(slots[3].valid);
    assert(slots[0].root_pc == 9);
    assert(slots[0].quality == QUALITY_MINOR);
    assert(slots[1].root_pc == 2);
    assert(slots[1].quality == QUALITY_MINOR);
    assert(slots[2].root_pc == 7);
    assert(slots[2].quality == QUALITY_SUS4);
    assert(slots[3].root_pc == 0);
    assert(slots[3].quality == QUALITY_MAJOR);
}

void testExtendedChordSizeBuildsExtensionVoicings()
{
    std::array<SegmentCapture, kMaxSegments> capture {};
    capture[0].duration[0] = 1.0;
    capture[0].duration[4] = 0.8;
    capture[0].duration[7] = 0.7;
    capture[0].duration[11] = 0.5;
    capture[0].duration[2] = 0.45;
    capture[0].onset[0] = 1.0;
    capture[0].onset[4] = 0.6;
    capture[0].onset[7] = 0.6;
    capture[0].onset[11] = 0.4;
    capture[0].onset[2] = 0.4;

    Controls controls = defaultControls();
    controls.key = 0;
    controls.scale = SCALE_BEBOP_MAJOR;
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BAR;
    controls.chord_size = CHORD_SIZE_EXTENDED;
    controls.complexity = 1.0f;
    controls.color = 1.0f;

    std::array<ChordSlot, kMaxSegments> slots {};
    const CadenceBuildOptions options {};
    const bool built = cadence_build_progression_from_capture(capture.data(), 1, controls, nullptr, 0, options, slots.data());

    assert(built);
    assert(slots[0].valid);
    assert(slots[0].note_count > 4);

    ProgressionState progression;
    progression.segmentCount = 1;
    progression.ready = true;
    progression.slots[0] = slots[0];

    const auto roundTrip = deserializeProgressionState(serializeProgressionState(progression));
    assert(roundTrip.has_value());
    assert(roundTrip->slots[0].quality == slots[0].quality);
    assert(roundTrip->slots[0].note_count == slots[0].note_count);
    assert(roundTrip->slots[0].notes[4] == slots[0].notes[4]);
}

void testAiStateSummaryIncludesProgressionContext()
{
    Controls controls = defaultControls();
    controls.key = 0;
    controls.scale = SCALE_BEBOP_MAJOR;
    controls.chord_size = CHORD_SIZE_EXTENDED;
    controls.color = 0.85f;
    controls.spread = 0.45f;
    controls.arpeggio = 0.25f;

    ProgressionState progression;
    progression.ready = true;
    progression.segmentCount = 2;
    progression.slots[0].valid = true;
    progression.slots[0].root_pc = 2;
    progression.slots[0].quality = QUALITY_MIN9;
    progression.slots[0].note_count = 5;
    progression.slots[0].notes = {50, 53, 57, 60, 64, 0};
    progression.slots[1].valid = true;
    progression.slots[1].root_pc = 7;
    progression.slots[1].quality = QUALITY_DOM13;
    progression.slots[1].note_count = 6;
    progression.slots[1].notes = {55, 59, 62, 65, 69, 76};

    const std::string summary = summarizeCadenceAiState(controls, progression);
    assert(summary.find("\"plugin\":\"cadence\"") != std::string::npos);
    assert(summary.find("\"scale\":\"bebop_major\"") != std::string::npos);
    assert(summary.find("\"chord_size\":\"extended\"") != std::string::npos);
    assert(summary.find("\"progression_ready\":true") != std::string::npos);
    assert(summary.find("\"quality\":\"minor_9\"") != std::string::npos);
    assert(summary.find("\"quality\":\"dominant_13\"") != std::string::npos);
    assert(summary.find("\"notes\":[55,59,62,65,69,76]") != std::string::npos);
}

void testSpreadControlWidensVoicings()
{
    std::array<SegmentCapture, kMaxSegments> capture {};
    capture[0].duration[0] = 1.0;
    capture[0].duration[4] = 0.9;
    capture[0].duration[7] = 0.8;
    capture[0].onset[0] = 1.0;
    capture[0].onset[4] = 0.8;
    capture[0].onset[7] = 0.8;

    Controls closeControls = defaultControls();
    closeControls.key = 0;
    closeControls.scale = SCALE_MAJOR;
    closeControls.cycle_bars = 1;
    closeControls.granularity = GRANULARITY_BAR;
    closeControls.spread = 0.0f;

    Controls wideControls = closeControls;
    wideControls.spread = 1.0f;

    std::array<ChordSlot, kMaxSegments> closeSlots {};
    std::array<ChordSlot, kMaxSegments> wideSlots {};
    const CadenceBuildOptions options {};

    assert(cadence_build_progression_from_capture(capture.data(), 1, closeControls, nullptr, 0, options, closeSlots.data()));
    assert(cadence_build_progression_from_capture(capture.data(), 1, wideControls, nullptr, 0, options, wideSlots.data()));
    assert(closeSlots[0].valid);
    assert(wideSlots[0].valid);

    const int closeRange = closeSlots[0].notes[closeSlots[0].note_count - 1] - closeSlots[0].notes[0];
    const int wideRange = wideSlots[0].notes[wideSlots[0].note_count - 1] - wideSlots[0].notes[0];
    assert(wideRange > closeRange);
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

void testHarmonyChangeKeepsLearnedProgressionReady()
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
    assert(state.haveLearnedCapture);

    controls.color = 1.0f;
    transport.bar = 1.0;
    transport.barBeat = 0.0;

    const BlockResult changed = processBlock(state,
                                             controls,
                                             transport,
                                             24000,
                                             48000.0,
                                             nullptr,
                                             0);
    assert(changed.ready);
    assert(state.ready);
    assert(state.playbackSegmentCount == 4);
}

void testRestoredProgressionSurvivesVoicingChange()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.output_channel = 1;

    state.ready = true;
    state.baseSegmentCount = 4;
    state.playbackSegmentCount = 4;
    for (int i = 0; i < 4; ++i) {
        ChordSlot& slot = state.baseProgression[static_cast<std::size_t>(i)];
        slot.valid = true;
        slot.root_pc = static_cast<std::uint8_t>((i * 2) % 12);
        slot.quality = QUALITY_MINOR;
        slot.note_count = 3;
        slot.velocity = 96;
        slot.notes = {60, 63, 67, 0, 0, 0};
        state.playback[static_cast<std::size_t>(i)] = slot;
    }
    state.haveLearnedCapture = false;
    state.controlsInitialized = true;
    state.previousControls = controls;

    controls.spread = 1.0f;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    const BlockResult changed = processBlock(state, controls, transport, 24000, 48000.0, nullptr, 0);
    assert(changed.ready);
    assert(state.ready);
    assert(state.baseSegmentCount == 4);
    assert(state.playbackSegmentCount == 4);
}

void testArpeggioCanEmitSingleNotes()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.output_channel = 1;
    controls.comp = 1.0f;
    controls.arpeggio = 1.0f;

    state.ready = true;
    state.playbackSegmentCount = 4;
    state.baseSegmentCount = 4;
    for (int i = 0; i < 4; ++i) {
        ChordSlot& slot = state.playback[static_cast<std::size_t>(i)];
        slot.valid = true;
        slot.root_pc = 0;
        slot.quality = QUALITY_MAJ7;
        slot.note_count = 4;
        slot.velocity = 96;
        slot.notes = {60, 64, 67, 71};
        state.baseProgression[static_cast<std::size_t>(i)] = slot;
    }
    state.controlsInitialized = true;
    state.previousControls = controls;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    const BlockResult result = processBlock(state, controls, transport, 96000, 48000.0, nullptr, 0);
    assert(result.ready);

    for (int i = 0; i < result.eventCount; ++i) {
        const ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(i)];
        if (event.size < 3 || (event.data[0] & 0xF0) != 0x90 || event.data[2] == 0)
            continue;

        int notesAtFrame = 0;
        for (int j = 0; j < result.eventCount; ++j) {
            const ScheduledMidiEvent& candidate = result.events[static_cast<std::size_t>(j)];
            if (candidate.frame == event.frame &&
                candidate.size >= 3 &&
                (candidate.data[0] & 0xF0) == 0x90 &&
                candidate.data[2] > 0) {
                ++notesAtFrame;
            }
        }
        assert(notesAtFrame == 1);
    }
}

}  // namespace

int main()
{
    testHarmonyBuildFromCapture();
    testSerializationRoundTrip();
    testHighColorFavorsJazzCadenceRoles();
    testHighColorFavorsCircleOfFifthsAndSuspendedDominant();
    testExtendedChordSizeBuildsExtensionVoicings();
    testAiStateSummaryIncludesProgressionContext();
    testSpreadControlWidensVoicings();
    testStoppedTransportPassThrough();
    testEngineLearnsAndEmitsOnNextCycle();
    testHarmonyChangeKeepsLearnedProgressionReady();
    testRestoredProgressionSurvivesVoicingChange();
    testArpeggioCanEmitSingleNotes();
    return 0;
}
