#include "arpgen_core.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace downspout::arpgen;

namespace {

constexpr double kSampleRate = 48000.0;

TransportSnapshot transport(const double quarter, const bool playing = true)
{
    TransportSnapshot value;
    value.valid = true;
    value.playing = playing;
    value.bar = std::floor(quarter / 4.0);
    value.barBeat = quarter - value.bar * 4.0;
    value.beatsPerBar = 4.0;
    value.beatType = 4.0;
    value.bpm = 120.0;
    return value;
}

InputMidiEvent noteEvent(const std::uint32_t frame, const bool on, const int note, const int velocity = 100)
{
    InputMidiEvent event;
    event.frame = frame;
    event.size = 3;
    event.data = {static_cast<std::uint8_t>(on ? 0x90 : 0x80),
                  static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(on ? velocity : 0), 0};
    return event;
}

std::vector<int> noteOns(const BlockResult& result)
{
    std::vector<int> notes;
    for (int i = 0; i < result.eventCount; ++i) {
        const auto& event = result.events[static_cast<std::size_t>(i)];
        if ((event.data[0] & 0xf0) == 0x90 && event.data[2] > 0)
            notes.push_back(event.data[1]);
    }
    return notes;
}

BlockResult runHalfBeat(EngineState& state, const Controls& controls, const double start,
                        const InputMidiEvent* events = nullptr, const std::uint32_t eventCount = 0)
{
    return processBlock(state, controls, transport(start), 12000, kSampleRate, events, eventCount);
}

void testRateValues()
{
    assert(rateInQuarterNotes(RATE_QUARTER) == 1.0);
    assert(rateInQuarterNotes(RATE_EIGHTH) == 0.5);
    assert(std::abs(rateInQuarterNotes(RATE_EIGHTH_TRIPLET) - 1.0 / 3.0) < 1e-12);
    assert(rateInQuarterNotes(RATE_SIXTEENTH) == 0.25);
}

void testChordUpDownAvoidsRepeatedEndpoints()
{
    EngineState state;
    Controls controls;
    controls.rate = RATE_EIGHTH;
    controls.order = ORDER_UP_DOWN;
    controls.octaves = 1;
    const std::array<InputMidiEvent, 3> chord {{
        noteEvent(0, true, 60), noteEvent(0, true, 64), noteEvent(0, true, 67)
    }};
    std::vector<int> sequence;
    for (int block = 0; block < 6; ++block) {
        const auto result = runHalfBeat(state, controls, block * 0.5,
                                        block == 0 ? chord.data() : nullptr,
                                        block == 0 ? static_cast<std::uint32_t>(chord.size()) : 0);
        const auto notes = noteOns(result);
        sequence.insert(sequence.end(), notes.begin(), notes.end());
    }
    const std::vector<int> expected {60, 64, 67, 64, 60, 64};
    assert(sequence == expected);
}

void testDownUpAvoidsRepeatedEndpoints()
{
    EngineState state;
    Controls controls;
    controls.rate = RATE_EIGHTH;
    controls.order = ORDER_DOWN_UP;
    controls.octaves = 1;
    const std::array<InputMidiEvent, 3> chord {{
        noteEvent(0, true, 60), noteEvent(0, true, 64), noteEvent(0, true, 67)
    }};
    std::vector<int> sequence;
    for (int block = 0; block < 5; ++block) {
        const auto result = runHalfBeat(state, controls, block * 0.5,
                                        block == 0 ? chord.data() : nullptr,
                                        block == 0 ? static_cast<std::uint32_t>(chord.size()) : 0);
        const auto notes = noteOns(result);
        sequence.insert(sequence.end(), notes.begin(), notes.end());
    }
    const std::vector<int> expected {67, 64, 60, 64, 67};
    assert(sequence == expected);
}

void testCaptureSliceCommitsAndEmptySliceRetains()
{
    EngineState state;
    Controls controls;
    controls.rate = RATE_EIGHTH;
    controls.captureSlice = CAPTURE_QUARTER_BAR;
    controls.order = ORDER_UP;
    controls.octaves = 1;
    const std::array<InputMidiEvent, 2> first {{noteEvent(0, true, 60), noteEvent(0, true, 64)}};
    runHalfBeat(state, controls, 0.0, first.data(), first.size());
    runHalfBeat(state, controls, 0.5);
    const std::array<InputMidiEvent, 2> second {{noteEvent(0, true, 62), noteEvent(0, true, 65)}};
    runHalfBeat(state, controls, 1.0, second.data(), second.size());
    runHalfBeat(state, controls, 1.5);
    const auto committed = runHalfBeat(state, controls, 2.0);
    assert(noteOns(committed).front() == 62);
    runHalfBeat(state, controls, 2.5);
    runHalfBeat(state, controls, 3.0);
    runHalfBeat(state, controls, 3.5);
    const auto retained = runHalfBeat(state, controls, 4.0);
    assert(noteOns(retained).front() == 62);
}

void testScaleRunSnapsAndStaysInKey()
{
    EngineState state;
    Controls controls;
    controls.mode = MODE_SCALE;
    controls.gate = 1.0f;
    controls.key = 0;
    controls.scale = SCALE_MAJOR;
    controls.scaleShape = SHAPE_RUN;
    controls.octaves = 1;
    controls.order = ORDER_UP;
    controls.rate = RATE_EIGHTH;
    const auto input = noteEvent(0, true, 61);
    std::vector<int> notes;
    for (int block = 0; block < 7; ++block) {
        const auto result = runHalfBeat(state, controls, block * 0.5,
                                        block == 0 ? &input : nullptr, block == 0 ? 1 : 0);
        const auto blockNotes = noteOns(result);
        notes.insert(notes.end(), blockNotes.begin(), blockNotes.end());
    }
    const std::vector<int> expected {60, 62, 64, 65, 67, 69, 71};
    assert(notes == expected);
}

void testScaleTriadUsesScaleDegrees()
{
    EngineState state;
    Controls controls;
    controls.mode = MODE_SCALE;
    controls.key = 2;
    controls.scale = SCALE_NATURAL_MINOR;
    controls.scaleShape = SHAPE_TRIAD;
    controls.octaves = 1;
    controls.order = ORDER_UP;
    controls.rate = RATE_EIGHTH;
    const auto input = noteEvent(0, true, 62);
    std::vector<int> notes;
    for (int block = 0; block < 3; ++block) {
        const auto result = runHalfBeat(state, controls, block * 0.5,
                                        block == 0 ? &input : nullptr, block == 0 ? 1 : 0);
        const auto blockNotes = noteOns(result);
        notes.insert(notes.end(), blockNotes.begin(), blockNotes.end());
    }
    const std::vector<int> expected {62, 65, 69};
    assert(notes == expected);
}

void testStopAndRewindReleaseActiveNote()
{
    EngineState state;
    Controls controls;
    controls.rate = RATE_QUARTER;
    controls.gate = 1.0f;
    const auto input = noteEvent(0, true, 60);
    const auto started = processBlock(state, controls, transport(4.0), 12000, kSampleRate, &input, 1);
    assert(!noteOns(started).empty());
    const auto stopped = processBlock(state, controls, transport(4.5, false), 12000, kSampleRate, nullptr, 0);
    assert(stopped.eventCount == 1);
    assert((stopped.events[0].data[0] & 0xf0) == 0x80);

    const auto restarted = processBlock(state, controls, transport(8.0), 12000, kSampleRate, &input, 1);
    assert(!noteOns(restarted).empty());
    const auto rewound = processBlock(state, controls, transport(2.0), 12000, kSampleRate, nullptr, 0);
    assert(rewound.eventCount >= 1);
    assert((rewound.events[0].data[0] & 0xf0) == 0x80);
}

void testScaleHeldNotesDoNotSurviveStop()
{
    EngineState state;
    Controls controls;
    controls.mode = MODE_SCALE;
    controls.gate = 1.0f;
    const auto input = noteEvent(0, true, 60);
    const auto started = runHalfBeat(state, controls, 0.0, &input, 1);
    assert(!noteOns(started).empty());
    const auto stopped = processBlock(state, controls, transport(0.5, false), 12000,
                                      kSampleRate, nullptr, 0);
    assert(stopped.eventCount == 1);
    assert((stopped.events[0].data[0] & 0xf0) == 0x80);
    const auto restarted = runHalfBeat(state, controls, 1.0);
    assert(noteOns(restarted).empty());
}

void testNonFourFourBarFractions()
{
    EngineState state;
    Controls controls;
    controls.captureSlice = CAPTURE_HALF_BAR;
    TransportSnapshot value;
    value.valid = true;
    value.playing = true;
    value.beatsPerBar = 7.0;
    value.beatType = 8.0;
    value.bpm = 120.0;
    value.bar = 0.0;
    value.barBeat = 0.0;
    const auto input = noteEvent(0, true, 60);
    const auto first = processBlock(state, controls, value, 12000, kSampleRate, &input, 1);
    assert(!noteOns(first).empty());
    assert(state.captureSegment == 0);
    value.barBeat = 3.5;
    const auto second = processBlock(state, controls, value, 12000, kSampleRate, nullptr, 0);
    assert(second.materialCount > 0);
    assert(state.captureSegment == 1);
}

void testTempoChangeKeepsGridAndRescalesFrames()
{
    EngineState state;
    Controls controls;
    controls.rate = RATE_EIGHTH;
    controls.order = ORDER_UP;
    controls.octaves = 1;
    const std::array<InputMidiEvent, 2> chord {{noteEvent(0, true, 60), noteEvent(0, true, 64)}};
    auto at120 = transport(0.0);
    const auto first = processBlock(state, controls, at120, 24000, kSampleRate, chord.data(), chord.size());
    std::vector<std::uint32_t> firstFrames;
    for (int i = 0; i < first.eventCount; ++i) {
        if ((first.events[static_cast<std::size_t>(i)].data[0] & 0xf0) == 0x90)
            firstFrames.push_back(first.events[static_cast<std::size_t>(i)].frame);
    }
    assert((firstFrames == std::vector<std::uint32_t> {0, 12000}));

    auto at60 = transport(1.0);
    at60.bpm = 60.0;
    const auto second = processBlock(state, controls, at60, 48000, kSampleRate, nullptr, 0);
    std::vector<std::uint32_t> secondFrames;
    for (int i = 0; i < second.eventCount; ++i) {
        if ((second.events[static_cast<std::size_t>(i)].data[0] & 0xf0) == 0x90)
            secondFrames.push_back(second.events[static_cast<std::size_t>(i)].frame);
    }
    assert((secondFrames == std::vector<std::uint32_t> {0, 24000}));
}

void testContiguousBarBoundaryDoesNotResetPattern()
{
    EngineState state;
    Controls controls;
    controls.rate = RATE_EIGHTH;
    controls.order = ORDER_UP;
    controls.octaves = 1;
    const std::array<InputMidiEvent, 2> chord {{noteEvent(0, true, 60), noteEvent(0, true, 64)}};
    const auto before = runHalfBeat(state, controls, 3.5, chord.data(), chord.size());
    const auto after = runHalfBeat(state, controls, 4.0);
    assert(noteOns(before).front() == 60);
    assert(noteOns(after).front() == 64);
}

}  // namespace

int main()
{
    testRateValues();
    testChordUpDownAvoidsRepeatedEndpoints();
    testDownUpAvoidsRepeatedEndpoints();
    testCaptureSliceCommitsAndEmptySliceRetains();
    testScaleRunSnapsAndStaysInKey();
    testScaleTriadUsesScaleDegrees();
    testStopAndRewindReleaseActiveNote();
    testScaleHeldNotesDoNotSurviveStop();
    testNonFourFourBarFractions();
    testTempoChangeKeepsGridAndRescalesFrames();
    testContiguousBarBoundaryDoesNotResetPattern();
    std::cout << "arpgen core tests passed\n";
    return 0;
}
