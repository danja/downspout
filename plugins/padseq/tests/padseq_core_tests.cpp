#include "downspout/test_assert.h"

#include "padseq_core.hpp"
#include "padseq_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace downspout::padseq;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(const bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkEq(const int got, const int want, const std::string& what)
{
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%s (got %d, want %d)", what.c_str(), got, want);
    check(got == want, buf);
}

void checkNear(const double got, const double want, const double tolerance, const std::string& what)
{
    char buf[224];
    std::snprintf(buf, sizeof(buf), "%s (got %.6f, want %.6f +-%.6f)",
                  what.c_str(), got, want, tolerance);
    check(std::fabs(got - want) <= tolerance, buf);
}

std::uint32_t p(const ParamId id)
{
    return static_cast<std::uint32_t>(id);
}

template <typename F>
bool anyEvent(const ProcessResult& result, F predicate)
{
    return std::any_of(result.events.begin(), result.events.begin() + result.eventCount, predicate);
}

bool hasNote(const ProcessResult& result,
             const std::uint32_t frame,
             const std::uint8_t status,
             const std::uint8_t note,
             const std::uint8_t velocity)
{
    return anyEvent(result, [&](const MidiMessage& e) {
        return e.frame == frame && e.size == 3 && e.data[0] == status &&
               e.data[1] == note && e.data[2] == velocity;
    });
}

int countNoteOffsOnChannel(const ProcessResult& result, const std::uint8_t channel)
{
    int count = 0;
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        const MidiMessage& e = result.events[i];
        if (e.size == 3 && e.data[0] == static_cast<std::uint8_t>(0x80u | channel) && e.data[2] == 0u)
            ++count;
    }
    return count;
}

bool isLedSysex(const MidiMessage& e)
{
    return e.size >= 8u && e.data[0] == 0xf0u && e.data[1] == 0x00u &&
           e.data[2] == 0x20u && e.data[3] == 0x29u && e.data[4] == 0x02u &&
           e.data[5] == 0x0du && e.data[6] == 0x03u && e.data[e.size - 1u] == 0xf7u;
}

bool isProgrammerModeSysex(const MidiMessage& e)
{
    static constexpr std::uint8_t kMode[] = {0xf0u, 0x00u, 0x20u, 0x29u,
                                              0x02u, 0x0du, 0x0eu, 0x01u, 0xf7u};
    return e.size == sizeof(kMode) && std::memcmp(e.data.data(), kMode, sizeof(kMode)) == 0;
}

bool sysexLedHasColor(const ProcessResult& result,
                      const std::uint8_t id,
                      const std::uint8_t color)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        const MidiMessage& e = result.events[i];
        if (!isLedSysex(e))
            continue;
        for (std::size_t j = 7u; j + 2u < static_cast<std::size_t>(e.size) - 1u; j += 3u)
        {
            if (e.data[j] == 0x00u && e.data[j + 1u] == id && e.data[j + 2u] == color)
                return true;
        }
    }
    return false;
}

int countLedNoteOrCc(const ProcessResult& result)
{
    int count = 0;
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        const MidiMessage& e = result.events[i];
        if (e.size != 3u)
            continue;
        const std::uint8_t status = e.data[0];
        if ((status & 0xf0u) == 0x90u && (status & 0x0fu) <= 2u)
            ++count;
        if ((status & 0xf0u) == 0xb0u)
            ++count;
    }
    return count;
}

MidiMessage midi(const std::uint32_t frame, const std::uint8_t status,
                 const std::uint8_t data1, const std::uint8_t data2)
{
    MidiMessage message {};
    message.frame = frame;
    message.size = 3;
    message.data[0] = status;
    message.data[1] = data1;
    message.data[2] = data2;
    return message;
}

// Status cells are displayed as note rows / step columns, unlike the saved
// step-major cell parameters.
std::uint32_t statusIndex(const std::size_t note, const std::size_t stepColumn)
{
    return static_cast<std::uint32_t>(note * kGridWidth + stepColumn);
}

std::uint64_t patternHash(const Processor& processor)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (int step = 0; step < kMaxSteps; ++step)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            hash ^= processor.cellActive(static_cast<std::size_t>(step), col) ? 1u : 0u;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

void testDefaultsAndOrdinals()
{
    checkEq(static_cast<int>(ScaleId::chromatic), 0, "chromatic ordinal");
    checkEq(static_cast<int>(ScaleId::major), 1, "major ordinal");
    checkEq(static_cast<int>(ScaleId::minor), 3, "minor ordinal");
    checkEq(static_cast<int>(ScaleId::pentatonic), 16, "canonical pentatonic ordinal");
    checkEq(static_cast<int>(ScaleId::bebopMinor), 24, "bebopMinor ordinal");
    checkEq(static_cast<int>(ScaleId::hirajoshi), 25, "hirajoshi ordinal");
    checkEq(static_cast<int>(ScaleId::romanian), 32, "romanian ordinal");
    checkEq(static_cast<int>(ScaleId::count), 33, "scale count");
    checkEq(kScaleNames.size(), 33, "scale name table size");
    checkEq(static_cast<int>(kParameterCount), 348, "parameter count");

    Processor processor;
    processor.init(48000.0);
    checkNear(processor.getParameter(p(ParamId::steps)), 16.0, 1e-6, "default steps");
    checkNear(processor.getParameter(p(ParamId::scale)), 1.0, 1e-6, "default scale");
    checkNear(processor.getParameter(p(ParamId::root)), 60.0, 1e-6, "default root");
    checkNear(processor.getParameter(p(ParamId::page)), 0.0, 1e-6, "default page");
    checkNear(processor.getParameter(p(ParamId::channel)), 4.0, 1e-6, "default channel");
    checkNear(processor.getParameter(p(ParamId::tempo)), 120.0, 1e-6, "default tempo");
    checkNear(processor.getParameter(p(ParamId::velocity)), 100.0, 1e-6, "default velocity");
    checkNear(processor.getParameter(p(ParamId::seed)), 12345.0, 1e-6, "default seed");
    checkNear(processor.getParameter(p(ParamId::ledFeedback)), 0.0, 1e-6,
              "LED feedback defaults off so the controller sees no unnecessary traffic");
    checkNear(processor.getParameter(p(ParamId::passInput)), 0.0, 1e-6, "default pass-through");
    checkNear(processor.getParameter(p(ParamId::statusStep)), -1.0, 1e-6, "default step status");
}

void testScalePitchSets()
{
    // Independent reference from docs/scales.md; a typo in either table is caught
    // by a direct comparison rather than by the plugin agreeing with itself.
    const std::vector<std::vector<int>> expected = {
        {0,1,2,3,4,5,6,7,8,9,10,11},
        {0,2,4,5,7,9,11},
        {0,2,4,5,7,9,11},
        {0,2,3,5,7,8,10},
        {0,2,3,5,7,8,11},
        {0,2,3,5,7,9,11},
        {0,2,3,5,7,9,10},
        {0,1,3,5,7,8,10},
        {0,2,4,6,7,9,11},
        {0,2,4,5,7,9,10},
        {0,1,3,5,6,8,10},
        {0,1,4,5,7,8,10},
        {0,1,3,5,7,9,11},
        {0,1,3,5,7,8,11},
        {0,2,4,7,9},
        {0,3,5,7,10},
        {0,2,4,7,9},
        {0,3,5,6,7,10},
        {0,2,4,6,8,10},
        {0,1,3,4,6,8,10},
        {0,1,3,4,6,7,9,10},
        {0,2,3,5,6,8,9,11},
        {0,2,4,5,7,9,10,11},
        {0,2,4,5,7,8,9,11},
        {0,2,3,4,5,7,9,10},
        {0,2,3,7,8},
        {0,1,5,7,10},
        {0,1,5,6,10},
        {0,2,5,7,9},
        {0,4,7},
        {0,3,7},
        {0,7},
        {0,2,3,6,7,9,10},
    };
    checkEq(static_cast<int>(expected.size()), static_cast<int>(ScaleId::count), "reference scale table size");

    for (std::size_t scale = 0; scale < expected.size(); ++scale)
    {
        std::vector<int> actual;
        const int degrees = scaleDegreeCount(scale);
        for (int degree = 0; degree < degrees; ++degree)
            actual.push_back(scaleStepAt(scale, degree));
        check(actual == expected[scale], "scale " + std::to_string(scale) + " pitch set");
    }

    checkEq(scaleStepAt(static_cast<std::size_t>(ScaleId::major), 7), 12, "major wraps by octave");
    checkEq(scaleStepAt(static_cast<std::size_t>(ScaleId::major), -1), -1, "negative degree below root");
    checkEq(foldMidi(130), 118, "fold high note by whole octave");
    checkEq(foldMidi(-2), 10, "fold low note by whole octave");
}

void testLayoutHelpers()
{
    Processor layout;
    layout.init(48000.0);

    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            const std::uint8_t note = gridToNote(row, col);
            std::size_t decodedRow = 99;
            std::size_t decodedCol = 99;
            check(noteToGrid(note, decodedRow, decodedCol), "grid note round trip");
            checkEq(static_cast<int>(decodedRow), static_cast<int>(row), "grid row round trip");
            checkEq(static_cast<int>(decodedCol), static_cast<int>(col), "grid column round trip");
        }
    }
    std::size_t row = 0;
    std::size_t col = 0;
    check(!noteToGrid(10, row, col), "note 10 is not a grid pad");
    check(!noteToGrid(89, row, col), "note 89 is not a grid pad");
    check(!noteToGrid(19, row, col), "note 19 is not a grid pad");
    checkEq(static_cast<int>(cellIndex(3, 5)), 29, "step-major cell index");
    checkEq(layout.patternStepFor(0, 0), 0, "page 0 column 0 scans step 0");
    checkEq(layout.patternStepFor(0, 7), 7, "page 0 column 7 scans step 7");
    checkEq(layout.patternStepFor(1, 0), 8, "page 1 column 0 scans step 8");
    checkEq(static_cast<int>(statusIndex(5, 2)), 42,
            "status cells are note rows with step columns");
    checkEq(pageCount(1), 1, "one step is one page");
    checkEq(pageCount(8), 1, "eight steps is one page");
    checkEq(pageCount(9), 2, "nine steps is two pages");
    checkEq(pageCount(32), 4, "32 steps is four pages");
}

void testNoteMapping()
{
    Processor processor;
    processor.init(48000.0);
    checkEq(processor.noteForRow(0), 60, "major root note");
    checkEq(processor.noteForRow(1), 62, "major second degree");
    checkEq(processor.noteForRow(6), 71, "major seventh degree");
    checkEq(processor.noteForRow(7), 72, "major octave degree");

    processor.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::minor));
    checkEq(processor.noteForRow(2), 63, "minor third degree");
    processor.setParameter(p(ParamId::root), 48.0f);
    checkEq(processor.noteForRow(0), 48, "root parameter changes notes");
    processor.setParameter(p(ParamId::noteView), 1.0f);
    checkEq(processor.noteForRow(0), 50, "view up one scale step");
    processor.setParameter(p(ParamId::noteView), -1.0f);
    checkEq(processor.noteForRow(0), 46, "view down one scale step");

    processor.setParameter(p(ParamId::noteMap), static_cast<float>(NoteMap::gmDrums));
    for (std::size_t note = 0; note < kGridHeight; ++note)
        checkEq(processor.noteForRow(note), kDrumNotes[note], "GM drum row note");
}

void testCellsAndGridInput()
{
    Processor processor;
    processor.init(48000.0);
    const std::size_t index = cellIndex(2, 3);
    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(index), 1.0f);
    check(processor.cellActive(2, 3), "cell parameter activates grid cell");
    checkNear(processor.getParameter(p(ParamId::statusCellStart) + statusIndex(3, 2)),
              1.0, 1e-6, "status mirrors cell with note rows and step columns");

    TransportSnapshot transport {};
    // Row 5 selects the note and column 6 selects the step on the visible page.
    const MidiMessage press = midi(0, 0x90u, gridToNote(5, 6), 127u);
    const MidiMessage release = midi(0, 0x80u, gridToNote(5, 6), 0u);
    const MidiMessage events[] = {press, release};
    const ProcessResult consumed = processor.processBlock(100, transport, events, 2);
    check(processor.cellActive(6, 5), "Launchpad row is the note and column is the step");
    check(processor.cellActive(2, 3), "existing cells survive grid input");
    checkEq(countLedNoteOrCc(consumed), 0,
            "LED feedback is off by default, so no controller note/CC artifacts are emitted");

    processor.setParameter(p(ParamId::ledFeedback), 1.0f);
    const ProcessResult lit = processor.processBlock(100, transport, nullptr, 0);
    check(sysexLedHasColor(lit, gridToNote(5, 6), kLedGreen),
          "LED feedback maps note rows to hardware rows and steps to columns");
    checkEq(countLedNoteOrCc(lit), 0,
            "LED feedback uses SysEx only, not channel-1 notes or CCs");
    processor.setParameter(p(ParamId::ledFeedback), 0.0f);

    processor.processBlock(100, transport, &press, 1);
    check(!processor.cellActive(6, 5), "second press toggles the cell off");

    processor.setParameter(p(ParamId::page), 1.0f);
    checkNear(processor.getParameter(p(ParamId::statusCellStart) + statusIndex(3, 2)),
              0.0, 1e-6, "page switch hides cells outside the visible steps");
    check(processor.cellActive(2, 3), "page switch keeps the whole pattern");

    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(6, 0)), 1.0f);
    processor.setParameter(p(ParamId::steps), 4.0f);
    check(processor.cellActive(2, 3), "cells within shortened pattern remain");
    check(!processor.cellActive(6, 0), "cells past shortened pattern are cleared");
    checkNear(processor.getParameter(p(ParamId::page)), 0.0, 1e-6, "page clamps after shortening");
}

void testFreeClock()
{
    Processor processor;
    processor.init(48000.0);
    processor.activate();
    processor.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(0, 0)), 1.0f);
    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(1, 1)), 1.0f);

    TransportSnapshot transport {};
    ProcessResult first = processor.processBlock(6000, transport, nullptr, 0);
    check(hasNote(first, 0, 0x93u, 60, 100), "free clock plays step 0 note-on at frame 0");
    checkNear(processor.getParameter(p(ParamId::statusRunning)), 1.0, 1e-6, "free clock reports running");
    checkNear(processor.getParameter(p(ParamId::statusStep)), 0.0, 1e-6, "step status starts at zero");

    ProcessResult second = processor.processBlock(6000, transport, nullptr, 0);
    check(hasNote(second, 0, 0x83u, 60, 0), "step 0 note-off lands on the next step boundary");
    check(hasNote(second, 0, 0x93u, 62, 100), "free clock plays step 1 on the next boundary");
    checkNear(processor.getParameter(p(ParamId::statusStep)), 1.0, 1e-6, "step status follows free clock");

    processor.setParameter(p(ParamId::tempo), 240.0f);
    const ProcessResult faster = processor.processBlock(3000, transport, nullptr, 0);
    static_cast<void>(faster);
    checkNear(processor.getParameter(p(ParamId::statusStep)), 2.0, 1e-6,
              "tempo change advances the free clock without a burst of stale steps");
}

void testHostClockRewindAndStop()
{
    Processor processor;
    processor.init(48000.0);
    processor.activate();
    processor.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::host));
    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(0, 0)), 1.0f);
    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(1, 1)), 1.0f);

    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bpm = 120.0;
    transport.beatsPerBar = 4.0;
    transport.bar = 0.0;
    transport.barBeat = 0.0;

    const ProcessResult first = processor.processBlock(12000, transport, nullptr, 0);
    check(hasNote(first, 0, 0x93u, 60, 100), "host clock plays step 0 at transport start");
    check(hasNote(first, 6000, 0x93u, 62, 100), "host clock plays step 1 at its boundary");
    check(hasNote(first, 6000, 0x83u, 60, 0), "host note-off is timed to the next step");
    checkNear(processor.getParameter(p(ParamId::statusRunning)), 1.0, 1e-6, "host transport reports running");

    transport.barBeat = 2.0;  // absolute step 8: sequence restarts from its first step
    const ProcessResult jumped = processor.processBlock(2000, transport, nullptr, 0);
    check(hasNote(jumped, 0, 0x93u, 60, 100), "forward transport jump restarts the pattern");
    checkNear(processor.getParameter(p(ParamId::statusStep)), 0.0, 1e-6, "status step resets on jump");

    transport.barBeat = 0.0;  // rewind to the beginning
    const ProcessResult rewound = processor.processBlock(2000, transport, nullptr, 0);
    check(hasNote(rewound, 0, 0x93u, 60, 100), "rewind replays step 0");

    // Start one manual note so the stop has something pending to silence.
    const MidiMessage side = midi(0, 0xb0u, kSideButtonCCs[0], 127u);
    processor.processBlock(100, transport, &side, 1);
    transport.playing = false;
    const ProcessResult stopped = processor.processBlock(2000, transport, nullptr, 0);
    check(countNoteOffsOnChannel(stopped, 3) > 0, "stopping flushes queued note-offs");
    checkNear(processor.getParameter(p(ParamId::statusRunning)), 0.0, 1e-6, "stopped transport is not running");
    checkNear(processor.getParameter(p(ParamId::statusStep)), -1.0, 1e-6, "stopped transport clears step status");
}

void testManualNoteTrigger()
{
    Processor processor;
    processor.init(48000.0);
    processor.activate();
    processor.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    TransportSnapshot transport {};
    const MidiMessage side = midi(10, 0xb0u, kSideButtonCCs[0], 127u);
    const ProcessResult pressed = processor.processBlock(100, transport, &side, 1);
    check(hasNote(pressed, 10, 0x93u, 60, 100), "side CC 19 plays note row 0");
    check(!anyEvent(pressed, [&](const MidiMessage& e) {
              return e.size == 3 && e.data[0] == 0xb0u && e.data[1] == kSideButtonCCs[0] &&
                     e.data[2] == 127u;
          }), "Launchpad input is not echoed");

    // 5900 samples remain in the first 6000-sample step, so the note-off is due
    // at that boundary rather than at the start of the block.
    const ProcessResult later = processor.processBlock(6000, transport, nullptr, 0);
    check(hasNote(later, 5900, 0x83u, 60, 0), "manual note-off is timed to the grid step");

    // The UI side buttons use trigger parameters; every column must play its own
    // note and use the same step-timed note-off as the hardware side CCs.
    Processor uiProcessor;
    uiProcessor.init(48000.0);
    uiProcessor.activate();
    uiProcessor.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        const std::uint32_t id = p(ParamId::triggerStart) + static_cast<std::uint32_t>(col);
        uiProcessor.setParameter(id, 1.0f);
        uiProcessor.setParameter(id, 0.0f);
    }
    const ProcessResult uiTriggers = uiProcessor.processBlock(100, transport, nullptr, 0);
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        check(hasNote(uiTriggers, 0, 0x93u, uiProcessor.noteForRow(col), 100),
              "UI trigger parameter plays note row " + std::to_string(col + 1));
    }
    const ProcessResult uiOffs = uiProcessor.processBlock(6000, transport, nullptr, 0);
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        check(hasNote(uiOffs, 5900, 0x83u, uiProcessor.noteForRow(col), 0),
              "UI trigger note-off is timed to note row " + std::to_string(col + 1));
    }
}

void testLateBlockSideNoteOff()
{
    // A side button pressed near the end of a block must release at the boundary
    // inside that same block, not one block late.
    Processor freeClock;
    freeClock.init(48000.0);
    freeClock.activate();
    freeClock.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    TransportSnapshot transport {};
    freeClock.processBlock(100, transport, nullptr, 0);
    const MidiMessage late = midi(5800, 0xb0u, kSideButtonCCs[0], 127u);
    const ProcessResult freeBlock = freeClock.processBlock(6000, transport, &late, 1);
    check(hasNote(freeBlock, 5800, 0x93u, 60, 100), "late free-block side CC sounds at its frame");
    check(hasNote(freeBlock, 5900, 0x83u, 60, 0),
          "late free-block note-off lands on the boundary in the same block");

    Processor hostClock;
    hostClock.init(48000.0);
    hostClock.activate();
    hostClock.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::host));
    transport.valid = true;
    transport.playing = true;
    transport.bpm = 120.0;
    transport.beatsPerBar = 4.0;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    const ProcessResult hostBlock = hostClock.processBlock(12000, transport, &late, 1);
    check(hasNote(hostBlock, 5800, 0x93u, 60, 100), "late host-block side CC sounds at its frame");
    check(hasNote(hostBlock, 6000, 0x83u, 60, 0),
          "late host-block note-off lands on the boundary in the same block");

    Processor stopped;
    stopped.init(48000.0);
    stopped.activate();
    stopped.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::host));
    transport.valid = true;
    transport.playing = false;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    const MidiMessage early = midi(100, 0xb0u, kSideButtonCCs[0], 127u);
    const ProcessResult pressed = stopped.processBlock(100, transport, &early, 1);
    check(hasNote(pressed, 100, 0x93u, 60, 100), "side CC sounds while the host transport is stopped");
    const ProcessResult released = stopped.processBlock(6000, transport, nullptr, 0);
    check(hasNote(released, 5900, 0x83u, 60, 0),
          "stopped-host manual note releases on a free-tempo step instead of sustaining");
}

void testTopButtonsAndStatus()
{
    Processor processor;
    processor.init(48000.0);
    processor.activate();
    processor.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    TransportSnapshot transport {};
    const auto press = [&](const std::size_t index) {
        const MidiMessage event = midi(0, 0xb0u, kTopButtonCCs[index], 127u);
        return processor.processBlock(100, transport, &event, 1);
    };

    press(static_cast<std::size_t>(TopButton::viewDown));
    checkNear(processor.getParameter(p(ParamId::noteView)), -1.0, 1e-6, "view down button");
    checkNear(processor.getParameter(p(ParamId::statusNoteView)), -1.0, 1e-6, "view status mirrors control");
    press(static_cast<std::size_t>(TopButton::viewUp));
    checkNear(processor.getParameter(p(ParamId::noteView)), 0.0, 1e-6, "view up button");

    processor.setParameter(p(ParamId::steps), 32.0f);
    processor.setParameter(p(ParamId::page), 1.0f);
    press(static_cast<std::size_t>(TopButton::pageBack));
    checkNear(processor.getParameter(p(ParamId::page)), 0.0, 1e-6, "page back button");
    press(static_cast<std::size_t>(TopButton::pageForward));
    checkNear(processor.getParameter(p(ParamId::page)), 1.0, 1e-6, "page forward button");
    checkNear(processor.getParameter(p(ParamId::statusPage)), 1.0, 1e-6, "page status mirrors control");

    press(static_cast<std::size_t>(TopButton::scaleBack));
    checkNear(processor.getParameter(p(ParamId::scale)), 0.0, 1e-6, "scale back button");
    press(static_cast<std::size_t>(TopButton::scaleForward));
    checkNear(processor.getParameter(p(ParamId::scale)), 1.0, 1e-6, "scale forward button");
    checkNear(processor.getParameter(p(ParamId::statusScale)), 1.0, 1e-6, "scale status mirrors control");

    press(static_cast<std::size_t>(TopButton::logo));
    checkNear(processor.getParameter(p(ParamId::scale)), 1.0, 1e-6, "logo button is unused");

    press(static_cast<std::size_t>(TopButton::randomise));
    check(patternHash(processor) != 0u, "randomise button creates a pattern");

    // Paint the LEDs, then clear: the hard reset must not be undone by the next
    // periodic feedback block.
    processor.setParameter(p(ParamId::ledFeedback), 1.0f);
    processor.processBlock(100, transport, nullptr, 0);
    const ProcessResult cleared = press(static_cast<std::size_t>(TopButton::clear));
    check(patternHash(processor) == patternHash(Processor{}), "clear button empties the pattern");
    check(countNoteOffsOnChannel(cleared, 3) == 128, "clear button emits all-notes-off panic");
    check(anyEvent(cleared, [](const MidiMessage& e) {
              return e.size > 3 && e.data[0] == 0xf0u && e.data[e.size - 1u] == 0xf7u;
          }), "clear button emits Launchpad SysEx");
    check(sysexLedHasColor(cleared, gridToNote(0, 0), kLedOff),
          "clear button emits a hard all-LED-off SysEx");
    checkEq(countLedNoteOrCc(cleared), 0,
            "clear does not use note or CC messages for LED reset");

    const ProcessResult held = processor.processBlock(100, transport, nullptr, 0);
    checkEq(held.eventCount, 0u, "clear holds every LED dark until the next intentional action");

    // Page 1 is still selected from the page-button checks above.
    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(8, 0)), 1.0f);
    const ProcessResult edited = processor.processBlock(100, transport, nullptr, 0);
    check(sysexLedHasColor(edited, gridToNote(0, 0), kLedGreen),
          "an intentional edit restores LED feedback after clear");
}

void testRandomiseAndClear()
{
    Processor processor;
    processor.init(48000.0);
    processor.activate();

    processor.setParameter(p(ParamId::randomise), 1.0f);
    int active = 0;
    for (int step = 0; step < kMaxSteps; ++step)
        for (std::size_t col = 0; col < kGridWidth; ++col)
            active += processor.cellActive(static_cast<std::size_t>(step), col) ? 1 : 0;
    check(active > 0, "randomise fills an empty pattern");
    const std::uint64_t first = patternHash(processor);

    processor.setParameter(p(ParamId::randomise), 0.0f);
    processor.setParameter(p(ParamId::randomise), 1.0f);
    check(patternHash(processor) != first, "a second randomise changes the pattern");

    for (int step = 0; step < kMaxSteps; ++step)
        for (std::size_t col = 0; col < kGridWidth; ++col)
            processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(
                static_cast<std::size_t>(step), col)), 1.0f);
    processor.setParameter(p(ParamId::randomise), 0.0f);
    processor.setParameter(p(ParamId::randomise), 1.0f);
    check(patternHash(processor) != 0u, "randomise keeps some hits from a full pattern");
    int after = 0;
    for (int step = 0; step < kMaxSteps; ++step)
        for (std::size_t col = 0; col < kGridWidth; ++col)
            after += processor.cellActive(static_cast<std::size_t>(step), col) ? 1 : 0;
    check(after < static_cast<int>(32 * kGridWidth), "randomise thins a full pattern");

    Processor seeded;
    seeded.init(48000.0);
    seeded.setParameter(p(ParamId::seed), 7.0f);
    seeded.setParameter(p(ParamId::randomise), 1.0f);
    const std::uint64_t seedA = patternHash(seeded);
    seeded.setParameter(p(ParamId::seed), 9001.0f);
    seeded.setParameter(p(ParamId::randomise), 0.0f);
    seeded.setParameter(p(ParamId::randomise), 1.0f);
    check(patternHash(seeded) != seedA, "seed parameter changes randomisation");

    TransportSnapshot transport {};
    seeded.setParameter(p(ParamId::ledFeedback), 1.0f);
    seeded.processBlock(100, transport, nullptr, 0);
    seeded.setParameter(p(ParamId::clear), 1.0f);
    const ProcessResult panicResult = seeded.processBlock(100, transport, nullptr, 0);
    check(countNoteOffsOnChannel(panicResult, 3) == 128, "clear parameter emits all-notes-off panic");
    checkEq(countLedNoteOrCc(panicResult), 0,
            "clear parameter never encodes LED colours as notes or CCs");
    check(sysexLedHasColor(panicResult, gridToNote(7, 7), kLedOff),
          "clear parameter switches off the whole 8x8 grid");

    const ProcessResult held = seeded.processBlock(100, transport, nullptr, 0);
    checkEq(held.eventCount, 0u, "clear parameter suppresses feedback until a new edit or run");
}

void testPassInputAndLedFeedback()
{
    Processor processor;
    processor.init(48000.0);
    processor.activate();
    processor.setParameter(p(ParamId::passInput), 1.0f);
    TransportSnapshot transport {};

    const MidiMessage unrelated = midi(0, 0xb0u, 7u, 64u);
    const ProcessResult passed = processor.processBlock(100, transport, &unrelated, 1);
    check(hasNote(passed, 0, 0xb0u, 7u, 64u), "unrelated CC passes through when enabled");

    const MidiMessage grid = midi(0, 0x90u, gridToNote(0, 0), 127u);
    const ProcessResult consumed = processor.processBlock(100, transport, &grid, 1);
    check(!hasNote(consumed, 0, 0x90u, gridToNote(0, 0), 127u), "grid input is consumed, not echoed");

    processor.setParameter(p(ParamId::passInput), 0.0f);
    const ProcessResult blocked = processor.processBlock(100, transport, &unrelated, 1);
    check(!hasNote(blocked, 0, 0xb0u, 7u, 64u), "pass-through is off by default");

    Processor dark;
    dark.init(48000.0);
    dark.setParameter(p(ParamId::ledFeedback), 0.0f);
    const ProcessResult noLeds = dark.processBlock(100, transport, nullptr, 0);
    check(anyEvent(noLeds, isProgrammerModeSysex),
          "a connected Launchpad enters programmer mode even with LED feedback off");
    checkEq(countLedNoteOrCc(noLeds), 0, "no note or CC LED traffic when feedback is off");
    check(!anyEvent(noLeds, isLedSysex), "no bulk lighting message when feedback is off");

    Processor lit;
    lit.init(48000.0);
    lit.activate();
    lit.setParameter(p(ParamId::ledFeedback), 1.0f);
    lit.setParameter(p(ParamId::cellStart), 1.0f);
    const ProcessResult withLeds = lit.processBlock(100, transport, nullptr, 0);
    check(sysexLedHasColor(withLeds, gridToNote(0, 0), kLedGreen),
          "active cells light green on the Launchpad");
    check(sysexLedHasColor(withLeds, gridToNote(0, 7), kLedOff),
          "step 8 on the bottom note row stays dark on page 0");
    check(sysexLedHasColor(withLeds, kTopButtonCCs[0], kLedBlue),
          "top buttons mirror the control scheme");
    checkEq(countLedNoteOrCc(withLeds), 0,
            "LED feedback is SysEx only, so an instrument cannot hear the scan");
    check(anyEvent(withLeds, [](const MidiMessage& e) {
              return isLedSysex(e) && static_cast<std::size_t>(e.size) == 7u + 3u * 81u + 1u;
          }), "one bulk SysEx paints all 64 grid, 8 side and 9 top LEDs");
}

void testProgrammerModeHandshake()
{
    TransportSnapshot transport {};
    Processor processor;
    processor.init(48000.0);

    const ProcessResult first = processor.processBlock(100, transport, nullptr, 0);
    check(anyEvent(first, isProgrammerModeSysex),
          "first block puts the Launchpad into programmer mode");
    check(!anyEvent(first, isLedSysex), "the handshake does not imply LED feedback");

    const ProcessResult second = processor.processBlock(100, transport, nullptr, 0);
    check(!anyEvent(second, isProgrammerModeSysex),
          "programmer mode is sent once, not on every refresh");

    processor.activate();
    const ProcessResult resumed = processor.processBlock(100, transport, nullptr, 0);
    check(anyEvent(resumed, isProgrammerModeSysex),
          "activation re-enters programmer mode after a host resume");

    processor.setParameter(p(ParamId::ledFeedback), 1.0f);
    const ProcessResult lit = processor.processBlock(100, transport, nullptr, 0);
    check(anyEvent(lit, isProgrammerModeSysex) && anyEvent(lit, isLedSysex),
          "enabling LED feedback re-handshakes and paints the grid");
}

void testLifecycleRetainsPatch()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(3, 2)), 1.0f);
    processor.setParameter(p(ParamId::scale), 10.0f);
    processor.setParameter(p(ParamId::steps), 24.0f);
    processor.setParameter(p(ParamId::page), 2.0f);

    processor.activate();
    check(processor.cellActive(3, 2), "activate keeps the pattern");
    checkNear(processor.getParameter(p(ParamId::scale)), 10.0, 1e-6, "activate keeps scale");
    checkNear(processor.getParameter(p(ParamId::page)), 2.0, 1e-6, "activate keeps page");
    checkNear(processor.getParameter(p(ParamId::steps)), 24.0, 1e-6, "activate keeps length");

    processor.setSampleRate(44100.0);
    check(processor.cellActive(3, 2), "sample-rate change keeps the pattern");
    checkNear(processor.getParameter(p(ParamId::scale)), 10.0, 1e-6, "sample-rate change keeps scale");
    checkNear(processor.getParameter(p(ParamId::statusStep)), -1.0, 1e-6, "sample-rate change clears runtime step");
}

void testEveryControlHasAnEffect()
{
    TransportSnapshot transport {};
    Processor processor;
    processor.init(48000.0);
    processor.activate();
    processor.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    processor.setParameter(p(ParamId::cellStart), 1.0f);
    const ProcessResult base = processor.processBlock(6000, transport, nullptr, 0);

    Processor velocity;
    velocity.init(48000.0);
    velocity.activate();
    velocity.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    velocity.setParameter(p(ParamId::cellStart), 1.0f);
    velocity.setParameter(p(ParamId::velocity), 127.0f);
    const ProcessResult louder = velocity.processBlock(6000, transport, nullptr, 0);
    check(hasNote(base, 0, 0x93u, 60, 100), "baseline note uses default velocity");
    check(hasNote(louder, 0, 0x93u, 60, 127), "velocity parameter changes emitted note");

    Processor channel;
    channel.init(48000.0);
    channel.activate();
    channel.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    channel.setParameter(p(ParamId::channel), 10.0f);
    channel.setParameter(p(ParamId::cellStart), 1.0f);
    const ProcessResult moved = channel.processBlock(6000, transport, nullptr, 0);
    check(hasNote(moved, 0, 0x99u, 60, 100), "channel parameter changes the MIDI channel");

    Processor steps;
    steps.init(48000.0);
    steps.setParameter(p(ParamId::steps), 32.0f);
    steps.setParameter(p(ParamId::page), 3.0f);
    steps.setParameter(p(ParamId::cellStart) + static_cast<std::uint32_t>(cellIndex(31, 0)), 1.0f);
    check(steps.cellActive(31, 0), "length and page controls expose the last page");
    steps.setParameter(p(ParamId::steps), 8.0f);
    check(!steps.cellActive(31, 0), "shortening length removes later steps");

    checkEq(countLedNoteOrCc(base), 0, "LED feedback is inert by default");

    Processor led;
    led.init(48000.0);
    led.activate();
    led.setParameter(p(ParamId::clockMode), static_cast<float>(ClockMode::free));
    led.setParameter(p(ParamId::ledFeedback), 1.0f);
    led.setParameter(p(ParamId::cellStart), 1.0f);
    const ProcessResult lit = led.processBlock(6000, transport, nullptr, 0);
    check(anyEvent(lit, [](const MidiMessage& e) { return isLedSysex(e); }),
          "LED feedback control has a visible SysEx effect when enabled");
}

} // namespace

int main()
{
    testDefaultsAndOrdinals();
    testScalePitchSets();
    testLayoutHelpers();
    testNoteMapping();
    testCellsAndGridInput();
    testFreeClock();
    testHostClockRewindAndStop();
    testManualNoteTrigger();
    testLateBlockSideNoteOff();
    testTopButtonsAndStatus();
    testRandomiseAndClear();
    testPassInputAndLedFeedback();
    testProgrammerModeHandshake();
    testLifecycleRetainsPatch();
    testEveryControlHasAnEffect();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
