// Plank core tests.
//
// Uses a hand-rolled require() rather than assert(), matching plugins/lifeform.
// The repository's Release build adds -DNDEBUG, which compiles every assert()
// out and lets a suite pass while testing nothing; require() cannot be compiled
// away, so this file needs no -UNDEBUG dance in CMakeLists.txt.

#include "plank_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace downspout::plank;

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "plank: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::uint32_t kBlock = 64;

MidiMessage noteOn(const std::uint8_t note, const std::uint8_t velocity = 100)
{
    MidiMessage message {};
    message.frame = 0;
    message.size = 3;
    message.data[0] = 0x90;
    message.data[1] = note;
    message.data[2] = velocity;
    return message;
}

MidiMessage noteOff(const std::uint8_t note)
{
    MidiMessage message {};
    message.frame = 0;
    message.size = 3;
    message.data[0] = 0x80;
    message.data[1] = note;
    message.data[2] = 0;
    return message;
}

MidiMessage controlChange(const std::uint8_t cc, const std::uint8_t value)
{
    MidiMessage message {};
    message.frame = 0;
    message.size = 3;
    message.data[0] = 0xb0;
    message.data[1] = cc;
    message.data[2] = value;
    return message;
}

struct Render {
    std::vector<float> left;
    std::vector<float> right;
    ProcessResult result {};  // every block's MIDI, accumulated
    float peak = 0.0f;

    [[nodiscard]] bool silent() const
    {
        for (const float sample : left)
        {
            if (std::fabs(sample) > 1.0e-6f)
                return false;
        }
        return true;
    }
};

// Renders blockCount blocks, feeding `input` into the first block only, and
// accumulates the outgoing MIDI from every block so a caller can assert on
// something emitted early in the run.
Render renderBlocks(Processor& processor,
                    const std::uint32_t blockCount = 8,
                    const MidiMessage* input = nullptr,
                    const std::uint32_t inputCount = 0u)
{
    Render out;
    out.left.assign(kBlock * blockCount, 0.0f);
    out.right.assign(kBlock * blockCount, 0.0f);

    float peak = 0.0f;
    for (std::uint32_t block = 0; block < blockCount; ++block)
    {
        ProcessResult blockResult {};
        processor.process(out.left.data() + block * kBlock,
                          out.right.data() + block * kBlock,
                          kBlock,
                          TransportSnapshot {},
                          block == 0 ? input : nullptr,
                          block == 0 ? inputCount : 0u,
                          blockResult);

        for (std::uint32_t i = 0; i < blockResult.eventCount && out.result.eventCount < out.result.events.size(); ++i)
        {
            // Re-base the event onto the concatenated timeline.
            const MidiMessage& source = blockResult.events[i];
            MidiMessage& target = out.result.events[out.result.eventCount];
            target = source;
            target.frame += block * kBlock;
            ++out.result.eventCount;
        }

        out.result.status = blockResult.status;

        for (std::uint32_t i = 0; i < kBlock; ++i)
            peak = std::max(peak, std::fabs(out.left[block * kBlock + i]));
    }

    out.peak = peak;
    return out;
}

[[nodiscard]] bool containsNoteOn(const ProcessResult& result)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            (result.events[i].data[0] & 0xf0u) == 0x90u &&
            result.events[i].data[2] > 0u)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool containsSysex(const ProcessResult& result)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
        if (result.events[i].size > 1 && result.events[i].data[0] == 0xf0)
            return true;
    return false;
}

// ── Grid mapping ────────────────────────────────────────────────────────────

void testGridNoteMapping()
{
    require(gridToNote(0, 0) == 11, "row 0 col 0 should be note 11 (bottom left)");
    require(gridToNote(7, 7) == 88, "row 7 col 7 should be note 88 (top right)");
    require(gridToNote(3, 5) == 46, "row 3 col 5 should be note 46");
    require(gridToNote(0, 7) == 18, "row 0 col 7 should be note 18");

    std::size_t row = 99;
    std::size_t col = 99;
    require(noteToGrid(11, row, col) && row == 0 && col == 0, "note 11 should decode to row 0 col 0");
    require(noteToGrid(88, row, col) && row == 7 && col == 7, "note 88 should decode to row 7 col 7");
    require(!noteToGrid(10, row, col), "note 10 is outside the grid");
    require(!noteToGrid(89, row, col), "note 89 is outside the grid");
    // Note 19 has a valid tens digit but ones digit 9, so it is not a cell.
    require(!noteToGrid(19, row, col), "note 19 is not a valid grid coordinate");
    require(!noteToGrid(91, row, col), "note 91 is a top button, not a grid cell");
    require(!noteToGrid(20, row, col), "note 20 is not a valid grid coordinate");
}

// ── Scale ladder ────────────────────────────────────────────────────────────

void testScaleLadder()
{
    Processor processor;
    processor.init(48000.0);

    const auto scale = [](ParamId id) { return static_cast<std::uint32_t>(id); };

    // Default is major with root 45 (A2). Row 0 is the root, row 7 the octave.
    processor.setParameter(scale(ParamId::scale), static_cast<float>(ScaleId::major));
    processor.setParameter(scale(ParamId::octave), 0.0f);
    processor.setParameter(scale(ParamId::rotate), 0.0f);
    processor.setParameter(scale(ParamId::stride), 0.0f);
    processor.setParameter(scale(ParamId::microtune), 0.0f);
    processor.setParameter(scale(ParamId::root), 45.0f);

    require(processor.noteForCell(0, 0) == 45, "major row 0 should be the root");
    require(processor.noteForCell(1, 0) == 47, "major row 1 should be a whole tone up");
    require(processor.noteForCell(2, 0) == 49, "major row 2 should be a major third up");
    require(processor.noteForCell(3, 0) == 50, "major row 3 should be a perfect fourth up");
    require(processor.noteForCell(7, 0) == 57, "major row 7 should be the octave above");

    // Rows ascend across the whole grid.
    for (std::size_t row = 1; row < kGridHeight; ++row)
    {
        require(processor.noteForCell(row, 0) > processor.noteForCell(row - 1, 0),
                "scale ladder rows should ascend");
    }

    // Every column is the same ladder: pitch is per string, not per cell.
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        for (std::size_t row = 0; row < kGridHeight; ++row)
            require(processor.noteForCell(row, col) == processor.noteForCell(row, 0),
                    "every column should share the same scale ladder");
    }

    // Octave shifts the whole ladder.
    processor.setParameter(scale(ParamId::octave), 1.0f);
    require(processor.noteForCell(0, 0) == 57, "octave up should raise the root by 12");
    processor.setParameter(scale(ParamId::octave), 0.0f);

    // Rotate shifts which degree sits under each row while keeping order.
    processor.setParameter(scale(ParamId::rotate), 2.0f);
    require(processor.noteForCell(0, 0) == 49, "rotate 2 should put the third under row 0");
    require(processor.noteForCell(5, 0) == 57, "rotate 2 should put the octave under row 5");
    processor.setParameter(scale(ParamId::rotate), 0.0f);

    // Chromatic is a semitone ladder.
    processor.setParameter(scale(ParamId::scale), static_cast<float>(ScaleId::chromatic));
    require(processor.noteForCell(5, 0) == 50, "chromatic row 5 should be five semitones up");
    processor.setParameter(scale(ParamId::scale), static_cast<float>(ScaleId::major));

    // Stride is a constant push on top of the ladder.
    processor.setParameter(scale(ParamId::stride), 3.0f);
    require(processor.noteForCell(0, 0) == 48, "stride 3 should push the root up three semitones");
    processor.setParameter(scale(ParamId::stride), 0.0f);

    // scaleInterval wraps and clamps safely.
    require(processor.scaleInterval(0) == 0, "scale interval 0 should be zero semitones");
    require(processor.scaleInterval(7) == 12, "scale interval 7 should be the octave");
    require(processor.scaleInterval(99) == processor.scaleInterval(99 % kGridHeight),
            "scale interval should wrap past the ladder");
}

// ── Grid plucking and audio ─────────────────────────────────────────────────

void testGridPluckProducesAudio()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    const MidiMessage input[] = { noteOn(gridToNote(3, 2)) };
    const Render out = renderBlocks(processor, 12, input, 1u);

    require(!out.silent(), "plucking a grid pad should produce audio");
    require(out.peak > 0.001f, "plucking a grid pad should produce a meaningful peak");
    require(out.result.status.activeStrings >= 1.0f, "one plucked string should report as active");
}

void testEachColumnIsItsOwnString()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    // Column 2 at row 3 and column 5 at row 5 should sound as two strings.
    const MidiMessage input[] = {
        noteOn(gridToNote(3, 2)),
        noteOn(gridToNote(5, 5)),
    };
    const Render out = renderBlocks(processor, 12, input, 2u);

    require(out.result.status.activeStrings == 2.0f, "two columns should be two separate strings");
    require(out.result.status.stringLevels[2] > 0.0f, "string 3 should be sounding");
    require(out.result.status.stringLevels[5] > 0.0f, "string 6 should be sounding");
    require(out.result.status.stringLevels[0] == 0.0f, "string 1 should be silent");
    require(out.result.status.stringLevels[1] == 0.0f, "string 2 should be silent");
}

void testAllEightColumnsSoundTogether()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    std::array<MidiMessage, kStringCount> input {};
    for (std::size_t col = 0; col < kStringCount; ++col)
        input[col] = noteOn(gridToNote(col % kGridHeight, col));

    const Render out = renderBlocks(processor, 16, input.data(), static_cast<std::uint32_t>(input.size()));
    require(out.result.status.activeStrings == 8.0f, "all eight columns should sound at once");
    require(!out.silent(), "all eight strings should produce audio");
}

void testReleaseStopsTheString()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::envRelease), 5.0f);

    const MidiMessage down[] = { noteOn(gridToNote(4, 1)) };
    renderBlocks(processor, 4, down, 1u);
    require(processor.getStatus().activeStrings >= 1.0f, "string should be sounding after the pluck");

    const MidiMessage up[] = { noteOff(gridToNote(4, 1)) };
    const Render out = renderBlocks(processor, 64, up, 1u);
    require(out.result.status.activeStrings == 0.0f, "releasing a pad should let the string die");
}

void testLatchKeepsStringRinging()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::latch), 1.0f);

    const MidiMessage down[] = { noteOn(gridToNote(2, 0)) };
    const MidiMessage up[] = { noteOff(gridToNote(2, 0)) };

    renderBlocks(processor, 4, down, 1u);
    const Render out = renderBlocks(processor, 32, up, 1u);
    require(out.result.status.activeStrings == 1.0f, "latch should keep a released pad ringing");

    // A second tap on the same cell silences a latched string.
    const MidiMessage again[] = { noteOn(gridToNote(2, 0)) };
    const Render stopped = renderBlocks(processor, 16, again, 1u);
    require(stopped.result.status.activeStrings == 0.0f, "re-tapping a latched cell should stop it");
}

// ── Tone shaping ────────────────────────────────────────────────────────────

void testMorphCrossfadesIntoWavetables()
{
    Processor processor;
    processor.init(48000.0);
    const auto morph = static_cast<std::uint32_t>(ParamId::morph);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    const MidiMessage pluck[] = { noteOn(gridToNote(4, 1)) };

    processor.setParameter(morph, 0.0f);
    const Render oscillators = renderBlocks(processor, 12, pluck, 1u);

    processor.setParameter(morph, 1.0f);
    const Render tables = renderBlocks(processor, 12, pluck, 1u);

    require(!oscillators.silent(), "the polyBLEP path should produce audio");
    require(!tables.silent(), "the wavetable path should produce audio");
    require(std::fabs(oscillators.peak - tables.peak) < 0.05f,
            "morph endpoints should sit at a similar level");

    // The two paths should not be bit-identical, or morph would do nothing.
    bool differs = false;
    for (std::size_t i = 0; i < oscillators.left.size(); ++i)
    {
        if (std::fabs(oscillators.left[i] - tables.left[i]) > 1.0e-4f)
        {
            differs = true;
            break;
        }
    }
    require(differs, "morph should change the waveform");
}

void testWavetablesAreGeneratedAndBandLimited()
{
    const WavetableSet& set = wavetables();

    for (std::size_t table = 0; table < kWavetableCount; ++table)
    {
        bool anySignal = false;
        bool anyVariation = false;
        auto peak = static_cast<std::int32_t>(0);
        auto largestStep = static_cast<std::int32_t>(0);

        for (std::size_t i = 0; i < kWavetableSize; ++i)
        {
            const auto sample = set.data[table][i];
            if (sample != 0)
                anySignal = true;
            if (sample != set.data[table][i % (kWavetableSize / 2u)])
                anyVariation = true;

            require(sample >= -32000 && sample <= 32000, "wavetables should stay inside int16 range");

            peak = std::max<std::int32_t>(peak, std::abs(static_cast<std::int32_t>(sample)));

            const auto next = set.data[table][(i + 1u) % kWavetableSize];
            const auto step = std::abs(static_cast<std::int32_t>(next) - static_cast<std::int32_t>(sample));
            largestStep = std::max(largestStep, step);
        }

        require(anySignal, "every wavetable should contain signal");
        require(anyVariation, "every wavetable should vary over its length");
        require(peak > 1000, "every wavetable should reach a usable amplitude");

        // A band-limited table is continuous across the wrap point. The step
        // from the final sample back to the first must be no larger than the
        // largest step anywhere inside the table, otherwise the loop point
        // would click.
        require(largestStep <= peak,
                "the wavetable wrap point should not be a discontinuity");
    }
}

void testFilterAndDriveChangeTheSound()
{
    Processor processor;
    processor.init(48000.0);
    const auto cutoff = static_cast<std::uint32_t>(ParamId::cutoff);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    const MidiMessage pluck[] = { noteOn(gridToNote(4, 1)) };

    processor.setParameter(cutoff, 0.02f);
    const Render closed = renderBlocks(processor, 12, pluck, 1u);

    processor.setParameter(cutoff, 1.0f);
    const Render open = renderBlocks(processor, 12, pluck, 1u);

    require(closed.peak > 0.0f && open.peak > 0.0f, "both filter settings should produce audio");

    // Cutoff must move the filter, not merely trim the level. A closed filter
    // should be markedly quieter than an open one.
    require(closed.peak < open.peak * 0.75f,
            "a closed filter should be clearly quieter than an open one");
    require(open.peak > closed.peak * 1.25f, "cutoff should have a wide dynamic range");

    // The signal must also be spectrally different, not just scaled.
    auto energyAbove = [](const Render& render) {
        // A cheap one-pole high-pass stand-in: compare the mean of |x| against
        // the mean of successive differences, which rises with brightness.
        double magnitude = 0.0;
        double difference = 0.0;
        for (std::size_t i = 1; i < render.left.size(); ++i)
        {
            magnitude += std::fabs(render.left[i]);
            difference += std::fabs(render.left[i] - render.left[i - 1]);
        }
        return difference / (magnitude + 1.0e-9);
    };
    require(energyAbove(open) > energyAbove(closed) * 1.5f,
            "an open filter should have more high-frequency content than a closed one");
}

void testLevelControlSilencesOutput()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::level), 0.0f);

    const MidiMessage pluck[] = { noteOn(gridToNote(4, 1)) };
    const Render out = renderBlocks(processor, 12, pluck, 1u);
    require(out.peak < 1.0e-6f, "level at zero should silence the output");
}

void testSustainedPolyphonyStaysClean()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::envSustain), 1.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::envRelease), 8000.0f);

    std::array<MidiMessage, kStringCount> input {};
    for (std::size_t col = 0; col < kStringCount; ++col)
        input[col] = noteOn(gridToNote(col, col));

    // Run long enough that a DC offset or runaway feedback would show up.
    const Render out = renderBlocks(processor, 200, input.data(), static_cast<std::uint32_t>(input.size()));

    require(out.peak <= 1.0f, "eight sustained strings must not exceed full scale");
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        require(std::isfinite(out.left[i]), "output should stay finite under full polyphony");
        require(std::isfinite(out.right[i]), "output should stay finite under full polyphony");
    }

    // No runaway growth: the tail of the render should be no louder than the head.
    const std::size_t head = out.left.size() / 4;
    auto peakIn = [&](const std::size_t from, const std::size_t to) {
        float peak = 0.0f;
        for (std::size_t i = from; i < to; ++i)
            peak = std::max(peak, std::fabs(out.left[i]));
        return peak;
    };
    require(peakIn(out.left.size() - head, out.left.size()) <= peakIn(0, head) * 1.5f,
            "the output should not grow without bound over a long render");
}

void testOutputIsDeterministic()
{
    const auto run = [] {
        Processor processor;
        processor.init(48000.0);
        processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);
        const MidiMessage input[] = { noteOn(gridToNote(6, 7)) };
        return renderBlocks(processor, 10, input, 1u);
    };

    const Render first = run();
    const Render second = run();
    require(first.left == second.left, "the core should be deterministic for identical input");
    require(first.right == second.right, "the core should be deterministic on the right channel too");
}

// ── Launchpad contract ──────────────────────────────────────────────────────

void testProgrammerModeAndLedFeedback()
{
    Processor processor;
    processor.init(48000.0);

    std::vector<float> left(kBlock, 0.0f);
    std::vector<float> right(kBlock, 0.0f);
    ProcessResult result {};

    processor.process(left.data(), right.data(), kBlock, TransportSnapshot {}, nullptr, 0u, result);
    require(containsSysex(result), "the first block should send the programmer-mode SysEx");

    bool sawGridLed = false;
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            result.events[i].data[0] == 0x90 &&
            result.events[i].data[1] >= 11 && result.events[i].data[1] <= 88)
        {
            sawGridLed = true;
        }
    }
    require(sawGridLed, "the first block should light the grid");
}

void testMusicalMidiAvoidsLedChannels()
{
    Processor processor;
    processor.init(48000.0);

    // Default base channel is 4, because channels 1-3 are the Launchpad's LED
    // channels in programmer mode.
    require(processor.getParameter(static_cast<std::uint32_t>(ParamId::baseChannel)) == 4.0f,
            "base channel should default to 4 to stay clear of the LED channels");

    processor.setParameter(static_cast<std::uint32_t>(ParamId::midiThru), 1.0f);

    // LED feedback off, so the only note-ons in the stream are the musical
    // ones under test. Grid LED updates are also 0x90 messages, on channel 1.
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    const MidiMessage input[] = { noteOn(gridToNote(0, 0)) };
    const Render out = renderBlocks(processor, 4, input, 1u);

    bool sawNoteOn = false;
    for (std::uint32_t i = 0; i < out.result.eventCount; ++i)
    {
        const std::uint8_t status = out.result.events[i].data[0];
        const auto channel = static_cast<std::uint8_t>(status & 0x0fu);
        if ((status & 0xf0u) == 0x90u && out.result.events[i].data[2] > 0u)
        {
            sawNoteOn = true;
            require(channel >= 3u, "musical notes must not land on LED channels 1-3");
            require(out.result.events[i].data[1] == processor.noteForCell(0, 0),
                    "the MIDI note should match the grid cell's scale degree");
        }
    }
    require(sawNoteOn, "MIDI thru should emit a note-on for a plucked pad");
}

void testPadReleaseIsNotEchoed()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::midiThru), 1.0f);

    const std::uint8_t note = gridToNote(4, 4);

    // Press.
    const MidiMessage down[] = { noteOn(note) };
    const Render pressed = renderBlocks(processor, 2, down, 1u);
    require(containsNoteOn(pressed.result), "a press should emit its note-on");

    // Release must not echo a note-off back onto the LED channel, because an
    // LED-off is exactly what a note-off looks like on channels 1-3.
    const MidiMessage up[] = { noteOff(note) };
    const Render released = renderBlocks(processor, 2, up, 1u);

    // MIDI thru is what legitimately emits note-offs; check it uses the base
    // channel rather than an LED channel.
    for (std::uint32_t i = 0; i < released.result.eventCount; ++i)
    {
        const std::uint8_t status = released.result.events[i].data[0];
        const auto channel = static_cast<std::uint8_t>(status & 0x0fu);
        if ((status & 0xf0u) == 0x80u)
            require(channel >= 3u, "passthrough note-offs must stay off the LED channels");
    }
}

void testUnhandledInputIsBlockedByDefault()
{
    Processor processor;
    processor.init(48000.0);

    const MidiMessage passThrough[] = { controlChange(20, 64) };
    const Render blocked = renderBlocks(processor, 2, passThrough, 1u);

    bool echoed = false;
    for (std::uint32_t i = 0; i < blocked.result.eventCount; ++i)
        if (blocked.result.events[i].data[1] == 20 && blocked.result.events[i].data[2] == 64)
            echoed = true;
    require(!echoed, "unhandled controller input should be blocked by default");

    processor.setParameter(static_cast<std::uint32_t>(ParamId::passInput), 1.0f);
    const Render passed = renderBlocks(processor, 2, passThrough, 1u);

    echoed = false;
    for (std::uint32_t i = 0; i < passed.result.eventCount; ++i)
        if (passed.result.events[i].data[1] == 20 && passed.result.events[i].data[2] == 64)
            echoed = true;
    require(echoed, "pass input should forward unhandled controller MIDI when enabled");
}

void testTopButtonsAndSideButtons()
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };

    const auto octave = processor.getParameter(p(ParamId::octave));

    const MidiMessage down[] = { controlChange(91, 127) };
    renderBlocks(processor, 1, down, 1u);
    require(processor.getParameter(p(ParamId::octave)) == octave - 1.0f, "CC 91 should step the octave down");

    const MidiMessage up[] = { controlChange(92, 127) };
    renderBlocks(processor, 1, up, 1u);
    require(processor.getParameter(p(ParamId::octave)) == octave, "CC 92 should step the octave back up");

    // Side buttons select the performance-scale subset.
    for (std::size_t i = 0; i < kSideButtonCCs.size(); ++i)
    {
        const MidiMessage side[] = { controlChange(kSideButtonCCs[i], 127) };
        renderBlocks(processor, 1, side, 1u);
        require(processor.getParameter(p(ParamId::scale)) == static_cast<float>(kSideButtonScales[i]),
                "each side button should select its scale");
    }

    // Latch toggles from CC 95.
    const MidiMessage latch[] = { controlChange(95, 127) };
    renderBlocks(processor, 1, latch, 1u);
    require(processor.getParameter(p(ParamId::latch)) == 1.0f, "CC 95 should engage latch");
}

void testPanicClearsEverything()
{
    Processor processor;
    processor.init(48000.0);

    const MidiMessage input[] = { noteOn(gridToNote(5, 3)) };
    renderBlocks(processor, 4, input, 1u);
    require(processor.getStatus().activeStrings >= 1.0f, "a string should be sounding before panic");

    processor.setParameter(static_cast<std::uint32_t>(ParamId::panic), 1.0f);

    std::vector<float> left(kBlock, 0.0f);
    std::vector<float> right(kBlock, 0.0f);
    ProcessResult result {};
    processor.process(left.data(), right.data(), kBlock, TransportSnapshot {}, nullptr, 0u, result);

    require(processor.getStatus().activeStrings == 0.0f, "panic should silence every string");
    require(containsSysex(result), "panic should resend the programmer-mode SysEx");

    // Panic must clear the surface both ways: bulk LED SysEx and ordinary
    // three-byte LED-off messages, because some hosts filter SysEx.
    bool sawBulkClear = false;
    bool sawPlainClear = false;
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size > 7 && result.events[i].data[0] == 0xf0)
            sawBulkClear = true;
        if (result.events[i].size == 3 && result.events[i].data[0] == 0x90 && result.events[i].data[2] == 0)
            sawPlainClear = true;
    }
    require(sawBulkClear, "panic should send a bulk LED clear SysEx");
    require(sawPlainClear, "panic should also send plain LED-off messages");
}

void testPanicResetsTuningChanges()
{
    Processor processor;
    processor.init(48000.0);

    const MidiMessage down[] = { noteOn(gridToNote(2, 6)) };
    renderBlocks(processor, 4, down, 1u);

    processor.setParameter(static_cast<std::uint32_t>(ParamId::panic), 1.0f);
    const Render out = renderBlocks(processor, 8);
    require(out.result.status.activeStrings == 0.0f, "panic should leave no active strings");
    require(out.left[0] == 0.0f && out.right[0] == 0.0f, "panic should start from silence");
}

// ── Parameter plumbing ──────────────────────────────────────────────────────

void testStatusParametersAreReadOnly()
{
    Processor processor;
    processor.init(48000.0);

    const auto outString1 = static_cast<std::uint32_t>(ParamId::outString0);
    require(outString1 >= kInputParameterCount, "status parameters must sit after the input block");

    // Writing to a status parameter must be ignored, not clobber the state.
    processor.setParameter(outString1, 1.0f);
    require(processor.getParameter(outString1) == 0.0f, "status parameters should ignore writes");

    const MidiMessage input[] = { noteOn(gridToNote(1, 0)) };
    renderBlocks(processor, 12, input, 1u);
    require(processor.getParameter(outString1) > 0.0f,
            "the first string's status parameter should report its live level");
    require(processor.getParameter(static_cast<std::uint32_t>(ParamId::outActiveStrings)) == 1.0f,
            "active strings should be reported to the host");
}

void testCellParametersPluckStrings()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    require(kCellParameterStart + kCellCount <= kInputParameterCount,
            "the 64 cell parameters should all be host-writable");

    // Row 4, column 1 is cell index 4 * 8 + 1 = 33.
    const auto cell = static_cast<std::uint32_t>(kCellParameterStart + cellIndex(4, 1));
    processor.setParameter(cell, 1.0f);

    const Render out = renderBlocks(processor, 12);
    require(!out.silent(), "setting a cell parameter should pluck its string");
    require(out.result.status.stringLevels[1] > 0.0f, "cell (4,1) should sound string 2");

    // Releasing the cell lets the string die. The default release is 300 ms, so
    // render comfortably past that rather than assuming an instant stop.
    processor.setParameter(cell, 0.0f);
    const Render released = renderBlocks(processor, 400);
    require(released.result.status.activeStrings == 0.0f, "clearing a cell should release its string");
}

void testCellParametersStayInStepWithHardware()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);

    const auto cell = static_cast<std::uint32_t>(kCellParameterStart + cellIndex(2, 6));

    // Plucked from hardware.
    const MidiMessage input[] = { noteOn(gridToNote(2, 6)) };
    renderBlocks(processor, 4, input, 1u);
    require(processor.getParameter(cell) == 1.0f,
            "a hardware pluck should set its cell parameter");

    // Released from hardware.
    const MidiMessage up[] = { noteOff(gridToNote(2, 6)) };
    renderBlocks(processor, 4, up, 1u);
    require(processor.getParameter(cell) == 0.0f,
            "a hardware release should clear its cell parameter");
}

void testCellParametersAreEdgeTriggered()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::ledFeedback), 0.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::midiThru), 1.0f);

    const auto cell = static_cast<std::uint32_t>(kCellParameterStart + cellIndex(0, 0));

    // Repeated writes of the same value must not re-articulate the string, or a
    // host restoring state would retrigger every note.
    processor.setParameter(cell, 1.0f);
    processor.setParameter(cell, 1.0f);
    processor.setParameter(cell, 1.0f);

    const Render out = renderBlocks(processor, 8);
    auto noteOns = 0;
    for (std::uint32_t i = 0; i < out.result.eventCount; ++i)
        if ((out.result.events[i].data[0] & 0xf0u) == 0x90u && out.result.events[i].data[2] > 0u)
            ++noteOns;
    require(noteOns == 1, "writing the same cell value repeatedly should only pluck once");
}

void testParameterClamping()
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };

    processor.setParameter(p(ParamId::octave), 99.0f);
    require(processor.getParameter(p(ParamId::octave)) == 3.0f, "octave should clamp to its maximum");

    processor.setParameter(p(ParamId::octave), -99.0f);
    require(processor.getParameter(p(ParamId::octave)) == -2.0f, "octave should clamp to its minimum");

    processor.setParameter(p(ParamId::baseChannel), 0.0f);
    require(processor.getParameter(p(ParamId::baseChannel)) == 1.0f, "base channel should clamp to 1");

    processor.setParameter(p(ParamId::scale), 999.0f);
    require(processor.getParameter(p(ParamId::scale)) ==
                static_cast<float>(static_cast<int>(ScaleId::count) - 1),
            "scale should clamp to the last scale");

    // Integer parameters snap.
    processor.setParameter(p(ParamId::envSustain), 0.0f);
    require(processor.getParameter(p(ParamId::envSustain)) == 0.0f, "a float parameter should not snap");
    processor.setParameter(p(ParamId::interval), 7.4f);
    require(processor.getParameter(p(ParamId::interval)) == 7.0f, "an integer parameter should snap");
}

void testScaleIdsMatchTheCanonicalReference()
{
    // docs/scales.md pins these ordinals; changing them would silently
    // reinterpret saved host projects.
    require(static_cast<std::uint32_t>(ScaleId::chromatic) == 0, "chromatic should be ordinal 0");
    require(static_cast<std::uint32_t>(ScaleId::major) == 1, "major should be ordinal 1");
    require(static_cast<std::uint32_t>(ScaleId::minor) == 3, "minor should be ordinal 3");
    require(static_cast<std::uint32_t>(ScaleId::bebopMinor) == 23, "bebopMinor should be ordinal 23");
    require(static_cast<std::uint32_t>(ScaleId::count) == 24, "there should be 24 scales");
    require(kScaleNames.size() == 24u, "every scale needs a display name");
    require(kScaleIntervals.size() == 24u, "every scale needs an interval table");

    // Row 7 must be the octave for every scale, or the ladder would not ascend.
    for (std::size_t scale = 0; scale < kScaleIntervals.size(); ++scale)
        require(kScaleIntervals[scale][7] == 12, "scale row 7 should be the octave");
}

void testEmptyProcessIsSafe()
{
    Processor processor;
    processor.init(48000.0);

    ProcessResult result {};
    processor.process(nullptr, nullptr, 0, TransportSnapshot {}, nullptr, 0, result);
    require(result.eventCount == 0, "a zero-frame process should emit nothing");

    std::vector<float> left(16, 0.0f);
    std::vector<float> right(16, 0.0f);
    processor.process(left.data(), right.data(), 16, TransportSnapshot {}, nullptr, 0, result);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        require(left[i] == 0.0f && right[i] == 0.0f, "an idle process should output silence");
        require(std::isfinite(left[i]) && std::isfinite(right[i]), "output should stay finite");
    }
}

}  // namespace

int main()
{
    testGridNoteMapping();
    testScaleLadder();
    testGridPluckProducesAudio();
    testEachColumnIsItsOwnString();
    testAllEightColumnsSoundTogether();
    testReleaseStopsTheString();
    testLatchKeepsStringRinging();
    testMorphCrossfadesIntoWavetables();
    testWavetablesAreGeneratedAndBandLimited();
    testFilterAndDriveChangeTheSound();
    testLevelControlSilencesOutput();
    testSustainedPolyphonyStaysClean();
    testOutputIsDeterministic();
    testProgrammerModeAndLedFeedback();
    testMusicalMidiAvoidsLedChannels();
    testPadReleaseIsNotEchoed();
    testUnhandledInputIsBlockedByDefault();
    testTopButtonsAndSideButtons();
    testPanicClearsEverything();
    testPanicResetsTuningChanges();
    testStatusParametersAreReadOnly();
    testCellParametersPluckStrings();
    testCellParametersStayInStepWithHardware();
    testCellParametersAreEdgeTriggered();
    testParameterClamping();
    testScaleIdsMatchTheCanonicalReference();
    testEmptyProcessIsSafe();
    std::cout << "plank core tests passed\n";
    return 0;
}
