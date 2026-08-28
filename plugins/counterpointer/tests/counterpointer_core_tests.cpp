#include "counterpointer_engine.hpp"
#include "counterpointer_serialization.hpp"
#include "counterpointer_transport.hpp"

#include <array>
#include <cassert>

using namespace downspout::counterpointer;

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

TransportSnapshot runningTransport(const double bar, const double barBeat = 0.0)
{
    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = bar;
    transport.barBeat = barBeat;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    return transport;
}

bool hasNoteOn(const BlockResult& result)
{
    for (int i = 0; i < result.eventCount; ++i)
    {
        const ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(i)];
        if (event.size >= 3 && (event.data[0] & 0xf0) == 0x90 && event.data[2] > 0)
            return true;
    }
    return false;
}

int countNoteOns(const BlockResult& result)
{
    int count = 0;
    for (int i = 0; i < result.eventCount; ++i)
    {
        const ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(i)];
        if (event.size >= 3 && (event.data[0] & 0xf0) == 0x90 && event.data[2] > 0)
            ++count;
    }
    return count;
}

int firstNoteOnPitch(const BlockResult& result)
{
    for (int i = 0; i < result.eventCount; ++i)
    {
        const ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(i)];
        if (event.size >= 3 && (event.data[0] & 0xf0) == 0x90 && event.data[2] > 0)
            return event.data[1];
    }
    return -1;
}

int firstActiveStepNote(const PhraseState& phrase, const int index)
{
    const PhraseStep& step = phrase.steps[static_cast<std::size_t>(index)];
    if (!step.active || step.hitCount <= 0)
        return -1;
    return step.hits[0].note;
}

int wrap12(const int value)
{
    int out = value % 12;
    if (out < 0)
        out += 12;
    return out;
}

bool pitchClassInScale(const int note, const int key, const int* intervals, const int intervalCount)
{
    const int rel = wrap12(note - key);
    for (int i = 0; i < intervalCount; ++i)
    {
        if (intervals[i] == rel)
            return true;
    }
    return false;
}

void testTransportHelpers()
{
    Controls controls = defaultControls();
    controls.cycle_bars = 2;
    controls.granularity = GRANULARITY_BEAT;
    assert(counterpointer_segment_count_for_controls(controls, 4.0) == 8);
    assert(counterpointer_segment_index_for_time(controls, 4.0, 8, 0.0) == 0);
    assert(counterpointer_segment_index_for_time(controls, 4.0, 8, 3.25) == 3);
    assert(counterpointer_segment_index_for_time(controls, 4.0, 8, 7.99) == 7);
}

void testScaleIdsStayAppendOnly()
{
    // Ordinals are serialised in host state — any insertion breaks saved projects.
    // When adding scales, append before SCALE_COUNT and update these pins.
    assert(SCALE_CHROMATIC         == 0);
    assert(SCALE_MAJOR             == 1);
    assert(SCALE_PHRYGIAN          == 7);
    assert(SCALE_LOCRIAN           == 10);
    assert(SCALE_PHRYGIAN_DOMINANT == 11);
    assert(SCALE_BLUES             == 16);
    assert(SCALE_WHOLE_TONE        == 17);
    assert(SCALE_ALTERED           == 18);
    assert(SCALE_BEBOP_DOMINANT    == 21);
    assert(SCALE_BEBOP_MAJOR       == 22);
    assert(SCALE_BEBOP_MINOR       == 23);
    assert(SCALE_COUNT             == 24);

    Controls controls = defaultControls();
    controls.scale = 999;
    assert(clampControls(controls).scale == SCALE_BEBOP_MINOR);  // clamps to COUNT-1
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
    assert(result.events[0].data[0] == 0x90);
    assert(result.events[0].data[1] == 60);
}

void testRunningTransportPassThroughBeforePhraseReady()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.pass_input = true;
    const InputMidiEvent event = makeEvent(0, 0x90, 64, 100);

    const BlockResult result = processBlock(state, controls, runningTransport(0.0), 256, 48000.0, &event, 1);
    assert(!result.ready);
    assert(result.eventCount == 1);
    assert(result.events[0].data[0] == 0x90);
    assert(result.events[0].data[1] == 64);
}

void testSerializationRoundTrip()
{
    Controls controls = defaultControls();
    controls.key = 9;
    controls.scale = SCALE_DORIAN;
    controls.cycle_bars = 3;
    controls.granularity = GRANULARITY_HALF_BAR;
    controls.follow = 0.24f;
    controls.counter = 0.81f;
    controls.short_random = 0.33f;
    controls.long_random = 0.44f;
    controls.density = 0.72f;
    controls.rhythm_follow = 0.61f;
    controls.syncopation = 0.35f;
    controls.consonance = 0.93f;
    controls.color = 0.67f;
    controls.embellish = 0.84f;
    controls.regularity = 0.91f;
    controls.reg = REGISTER_HIGH;
    controls.span = 0.42f;
    controls.gate = 0.58f;
    controls.velocity_follow = 0.25f;
    controls.pass_input = false;
    controls.output_channel = 5;
    controls.action_learn = 7;
    controls.freeze = true;
    controls.response_mode = RESPONSE_BASS_DESCEND;

    const auto controlsRoundTrip = deserializeControls(serializeControls(controls));
    assert(controlsRoundTrip.has_value());
    assert(controlsRoundTrip->key == 9);
    assert(controlsRoundTrip->scale == SCALE_DORIAN);
    assert(controlsRoundTrip->cycle_bars == 3);
    assert(controlsRoundTrip->reg == REGISTER_HIGH);
    assert(controlsRoundTrip->embellish > 0.83f && controlsRoundTrip->embellish < 0.85f);
    assert(controlsRoundTrip->color > 0.66f && controlsRoundTrip->color < 0.68f);
    assert(controlsRoundTrip->regularity > 0.90f && controlsRoundTrip->regularity < 0.92f);
    assert(!controlsRoundTrip->pass_input);
    assert(controlsRoundTrip->output_channel == 5);
    assert(controlsRoundTrip->freeze);
    assert(controlsRoundTrip->response_mode == RESPONSE_BASS_DESCEND);

    PhraseState phrase;
    phrase.segmentCount = 2;
    phrase.ready = true;
    phrase.steps[0].active = true;
    phrase.steps[0].note = 72;
    phrase.steps[0].velocity = 100;
    phrase.steps[0].onset = 0.25;
    phrase.steps[0].gate = 0.50;
    phrase.steps[0].hitCount = 2;
    phrase.steps[0].hits[0].active = true;
    phrase.steps[0].hits[0].note = 72;
    phrase.steps[0].hits[0].velocity = 100;
    phrase.steps[0].hits[0].onset = 0.25;
    phrase.steps[0].hits[0].gate = 0.50;
    phrase.steps[0].hits[1].active = true;
    phrase.steps[0].hits[1].note = 76;
    phrase.steps[0].hits[1].velocity = 88;
    phrase.steps[0].hits[1].onset = 0.72;
    phrase.steps[0].hits[1].gate = 0.30;
    phrase.steps[1].active = false;
    phrase.steps[1].note = 74;

    const auto phraseRoundTrip = deserializePhraseState(serializePhraseState(phrase));
    assert(phraseRoundTrip.has_value());
    assert(phraseRoundTrip->ready);
    assert(phraseRoundTrip->segmentCount == 2);
    assert(phraseRoundTrip->steps[0].active);
    assert(phraseRoundTrip->steps[0].note == 72);
    assert(phraseRoundTrip->steps[0].hitCount == 2);
    assert(phraseRoundTrip->steps[0].hits[1].note == 76);
    assert(phraseRoundTrip->steps[1].note == 74);

    VariationState variation = defaultVariationState();
    variation.completed_cycles = 10;
    variation.last_mutation_cycle = 8;
    variation.mutation_serial = 4;
    const auto variationRoundTrip = deserializeVariationState(serializeVariationState(variation));
    assert(variationRoundTrip.has_value());
    assert(variationRoundTrip->completed_cycles == 10);
    assert(variationRoundTrip->mutation_serial == 4);
}

void testLearnsIncomingPatternAndEmitsCounterMelody()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.output_channel = 2;
    controls.density = 1.0f;
    controls.rhythm_follow = 0.8f;

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 8> firstCycle = {{
        makeEvent(0, 0x90, 48, 100),
        makeEvent(24000, 0x80, 48, 0),
        makeEvent(24000, 0x90, 50, 96),
        makeEvent(48000, 0x80, 50, 0),
        makeEvent(48000, 0x90, 52, 96),
        makeEvent(72000, 0x80, 52, 0),
        makeEvent(72000, 0x90, 55, 104),
        makeEvent(95999, 0x80, 55, 0),
    }};

    const BlockResult learned = processBlock(state,
                                             controls,
                                             runningTransport(0.0),
                                             frameCount,
                                             48000.0,
                                             firstCycle.data(),
                                             static_cast<std::uint32_t>(firstCycle.size()));
    assert(learned.ready);
    assert(state.playbackPhrase.ready);

    const BlockResult playback = processBlock(state,
                                              controls,
                                              runningTransport(1.0),
                                              24000,
                                              48000.0,
                                              nullptr,
                                              0);
    assert(playback.ready);
    assert(hasNoteOn(playback));
    assert(firstNoteOnPitch(playback) >= 0);
    assert((playback.events[0].data[0] & 0x0f) == 1 || playback.eventCount > 1);
}

void testLearnsIncomingPatternAcrossHostBlocks()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.density = 1.0f;
    controls.rhythm_follow = 0.8f;

    constexpr std::uint32_t beatFrames = 24000;
    const std::array<InputMidiEvent, 2> beat0 = {{
        makeEvent(0, 0x90, 48, 100),
        makeEvent(12000, 0x80, 48, 0),
    }};
    const std::array<InputMidiEvent, 2> beat1 = {{
        makeEvent(0, 0x90, 50, 96),
        makeEvent(12000, 0x80, 50, 0),
    }};
    const std::array<InputMidiEvent, 2> beat2 = {{
        makeEvent(0, 0x90, 52, 96),
        makeEvent(12000, 0x80, 52, 0),
    }};
    const std::array<InputMidiEvent, 2> beat3 = {{
        makeEvent(0, 0x90, 55, 104),
        makeEvent(12000, 0x80, 55, 0),
    }};

    BlockResult result = processBlock(state, controls, runningTransport(0.0, 0.0), beatFrames, 48000.0, beat0.data(), beat0.size());
    assert(!result.ready);
    result = processBlock(state, controls, runningTransport(0.0, 1.0), beatFrames, 48000.0, beat1.data(), beat1.size());
    assert(!result.ready);
    result = processBlock(state, controls, runningTransport(0.0, 2.0), beatFrames, 48000.0, beat2.data(), beat2.size());
    assert(!result.ready);
    result = processBlock(state, controls, runningTransport(0.0, 3.0), beatFrames, 48000.0, beat3.data(), beat3.size());
    assert(result.ready);
    assert(state.playbackPhrase.ready);

    const BlockResult playback =
        processBlock(state, controls, runningTransport(1.0), beatFrames, 48000.0, nullptr, 0);
    assert(playback.ready);
    assert(hasNoteOn(playback));
}

void testDeterministicForSameInput()
{
    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.density = 1.0f;
    controls.short_random = 0.35f;

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 4> firstCycle = {{
        makeEvent(0, 0x90, 60, 100),
        makeEvent(24000, 0x80, 60, 0),
        makeEvent(48000, 0x90, 67, 100),
        makeEvent(72000, 0x80, 67, 0),
    }};

    EngineState a;
    EngineState b;
    activate(a);
    activate(b);

    const BlockResult learnedA =
        processBlock(a, controls, runningTransport(0.0), frameCount, 48000.0, firstCycle.data(), firstCycle.size());
    const BlockResult learnedB =
        processBlock(b, controls, runningTransport(0.0), frameCount, 48000.0, firstCycle.data(), firstCycle.size());
    assert(learnedA.ready);
    assert(learnedB.ready);
    const BlockResult outA = processBlock(a, controls, runningTransport(1.0), 48000, 48000.0, nullptr, 0);
    const BlockResult outB = processBlock(b, controls, runningTransport(1.0), 48000, 48000.0, nullptr, 0);

    assert(outA.eventCount == outB.eventCount);
    for (int i = 0; i < outA.eventCount; ++i)
    {
        assert(outA.events[i].frame == outB.events[i].frame);
        assert(outA.events[i].size == outB.events[i].size);
        assert(outA.events[i].data == outB.events[i].data);
    }
}

void testStrictCounterpointAnswersSubjectAtDominant()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.key = 0;
    controls.scale = SCALE_MAJOR;
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.density = 1.0f;
    controls.counter = 0.92f;
    controls.follow = 0.12f;
    controls.regularity = 0.96f;
    controls.short_random = 0.0f;
    controls.long_random = 0.0f;
    controls.embellish = 0.0f;
    controls.reg = REGISTER_MID;

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 8> subject = {{
        makeEvent(0, 0x90, 60, 100),
        makeEvent(12000, 0x80, 60, 0),
        makeEvent(24000, 0x90, 62, 100),
        makeEvent(36000, 0x80, 62, 0),
        makeEvent(48000, 0x90, 64, 100),
        makeEvent(60000, 0x80, 64, 0),
        makeEvent(72000, 0x90, 65, 100),
        makeEvent(84000, 0x80, 65, 0),
    }};

    const BlockResult learned =
        processBlock(state, controls, runningTransport(0.0), frameCount, 48000.0, subject.data(), subject.size());
    assert(learned.ready);
    assert(state.playbackPhrase.ready);
    assert(state.playbackPhrase.segmentCount == 4);
    assert(firstActiveStepNote(state.playbackPhrase, 0) == 67);
    assert(firstActiveStepNote(state.playbackPhrase, 1) == 69);
    assert(firstActiveStepNote(state.playbackPhrase, 2) == 71);
    assert(firstActiveStepNote(state.playbackPhrase, 3) == 72);
}

void testBassDescendModeAnswersAscendingInputBelowIt()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.key = 0;
    controls.scale = SCALE_NAT_MINOR;
    controls.response_mode = RESPONSE_BASS_DESCEND;
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.density = 1.0f;
    controls.counter = 1.0f;
    controls.follow = 0.0f;
    controls.short_random = 0.0f;
    controls.long_random = 0.0f;
    controls.embellish = 0.0f;
    controls.reg = REGISTER_MID;
    controls.span = 0.80f;

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 8> ascending = {{
        makeEvent(0, 0x90, 60, 100),
        makeEvent(12000, 0x80, 60, 0),
        makeEvent(24000, 0x90, 62, 100),
        makeEvent(36000, 0x80, 62, 0),
        makeEvent(48000, 0x90, 64, 100),
        makeEvent(60000, 0x80, 64, 0),
        makeEvent(72000, 0x90, 65, 100),
        makeEvent(84000, 0x80, 65, 0),
    }};

    const BlockResult learned =
        processBlock(state, controls, runningTransport(0.0), frameCount, 48000.0, ascending.data(), ascending.size());
    assert(learned.ready);
    assert(state.playbackPhrase.ready);
    assert(state.playbackPhrase.segmentCount == 4);

    const int first = firstActiveStepNote(state.playbackPhrase, 0);
    const int second = firstActiveStepNote(state.playbackPhrase, 1);
    const int third = firstActiveStepNote(state.playbackPhrase, 2);
    const int fourth = firstActiveStepNote(state.playbackPhrase, 3);
    assert(first >= 0 && second >= 0 && third >= 0 && fourth >= 0);
    assert(first < 60);
    assert(second <= first);
    assert(third <= second);
    assert(fourth <= third);
}

void testNewModalScalesConstrainGeneratedNotes()
{
    struct Case {
        int scale;
        std::array<int, 7> intervals;
    };

    constexpr std::array<Case, 3> cases = {{
        {SCALE_PHRYGIAN, {0, 1, 3, 5, 7, 8, 10}},
        {SCALE_LOCRIAN, {0, 1, 3, 5, 6, 8, 10}},
        {SCALE_PHRYGIAN_DOMINANT, {0, 1, 4, 5, 7, 8, 10}},
    }};

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 8> subject = {{
        makeEvent(0, 0x90, 60, 100),
        makeEvent(12000, 0x80, 60, 0),
        makeEvent(24000, 0x90, 62, 100),
        makeEvent(36000, 0x80, 62, 0),
        makeEvent(48000, 0x90, 64, 100),
        makeEvent(60000, 0x80, 64, 0),
        makeEvent(72000, 0x90, 65, 100),
        makeEvent(84000, 0x80, 65, 0),
    }};

    for (const Case& item : cases)
    {
        EngineState state;
        activate(state);

        Controls controls = defaultControls();
        controls.key = 0;
        controls.scale = item.scale;
        controls.cycle_bars = 1;
        controls.granularity = GRANULARITY_BEAT;
        controls.pass_input = false;
        controls.density = 1.0f;
        controls.counter = 0.92f;
        controls.follow = 0.12f;
        controls.regularity = 0.96f;
        controls.short_random = 0.0f;
        controls.long_random = 0.0f;
        controls.embellish = 0.0f;
        controls.reg = REGISTER_MID;

        const BlockResult learned =
            processBlock(state, controls, runningTransport(0.0), frameCount, 48000.0, subject.data(), subject.size());
        assert(learned.ready);
        assert(state.playbackPhrase.ready);

        for (int segment = 0; segment < state.playbackPhrase.segmentCount; ++segment)
        {
            const int note = firstActiveStepNote(state.playbackPhrase, segment);
            assert(note >= 0);
            assert(pitchClassInScale(note, controls.key, item.intervals.data(), static_cast<int>(item.intervals.size())));
        }
    }
}

void testEmbellishCanOutnumberInputNotes()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BAR;
    controls.pass_input = false;
    controls.density = 1.0f;
    controls.embellish = 1.0f;
    controls.regularity = 1.0f;

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 2> firstCycle = {{
        makeEvent(0, 0x90, 48, 100),
        makeEvent(95999, 0x80, 48, 0),
    }};

    const BlockResult learned =
        processBlock(state, controls, runningTransport(0.0), frameCount, 48000.0, firstCycle.data(), firstCycle.size());
    assert(learned.ready);

    const BlockResult playback = processBlock(state,
                                              controls,
                                              runningTransport(1.0),
                                              frameCount,
                                              48000.0,
                                              nullptr,
                                              0);
    assert(playback.ready);
    assert(countNoteOns(playback) > 1);
}

void testFreezeKeepsLearnedPhrase()
{
    EngineState state;
    activate(state);

    Controls controls = defaultControls();
    controls.cycle_bars = 1;
    controls.granularity = GRANULARITY_BEAT;
    controls.pass_input = false;
    controls.density = 1.0f;

    constexpr std::uint32_t frameCount = 96000;
    const std::array<InputMidiEvent, 2> firstCycle = {{
        makeEvent(0, 0x90, 60, 100),
        makeEvent(95999, 0x80, 60, 0),
    }};
    const BlockResult learned =
        processBlock(state, controls, runningTransport(0.0), frameCount, 48000.0, firstCycle.data(), firstCycle.size());
    assert(learned.ready);
    const int learnedPitch = static_cast<int>(state.playbackPhrase.steps[0].note);

    controls.freeze = true;
    const std::array<InputMidiEvent, 2> differentCycle = {{
        makeEvent(0, 0x90, 36, 100),
        makeEvent(95999, 0x80, 36, 0),
    }};
    const BlockResult frozen =
        processBlock(state, controls, runningTransport(1.0), frameCount, 48000.0, differentCycle.data(), differentCycle.size());
    assert(frozen.ready);
    assert(static_cast<int>(state.playbackPhrase.steps[0].note) == learnedPitch);
}

}  // namespace

int main()
{
    testTransportHelpers();
    testStoppedTransportPassThrough();
    testRunningTransportPassThroughBeforePhraseReady();
    testSerializationRoundTrip();
    testLearnsIncomingPatternAndEmitsCounterMelody();
    testLearnsIncomingPatternAcrossHostBlocks();
    testDeterministicForSameInput();
    testStrictCounterpointAnswersSubjectAtDominant();
    testBassDescendModeAnswersAscendingInputBelowIt();
    testScaleIdsStayAppendOnly();
    testNewModalScalesConstrainGeneratedNotes();
    testEmbellishCanOutnumberInputNotes();
    testFreezeKeepsLearnedPhrase();
    return 0;
}
