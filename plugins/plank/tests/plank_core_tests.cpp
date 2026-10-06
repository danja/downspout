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
#include <string>
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

// Mirrors Processor::scaleStep for an explicit scale, so the tables themselves
// can be checked without constructing a Processor.
[[nodiscard]] int scaleStepOf(const std::size_t scale, const int degree)
{
    return scaleStepAt(scale, degree);
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

    // In unison the column is ignored, so every column shares one ladder. This
    // is the fallback behaviour, not the default: the default Spread is Scale,
    // which makes the columns different the way Plinky's strings are.
    processor.setParameter(scale(ParamId::spread), static_cast<float>(SpreadId::unison));
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        for (std::size_t row = 0; row < kGridHeight; ++row)
            require(processor.noteForCell(row, col) == processor.noteForCell(row, 0),
                    "unison spread should give every column the same ladder");
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

    // Chromatic must stay a twelve-note ladder past row 7, or stride and rotate
    // would wrap an octave early.
    processor.setParameter(scale(ParamId::scale), static_cast<float>(ScaleId::chromatic));
    require(processor.scaleStep(11) == 11, "chromatic degree 11 should be eleven semitones");
    require(processor.scaleStep(12) == 12, "chromatic degree 12 should be the octave");
    processor.setParameter(scale(ParamId::scale), static_cast<float>(ScaleId::major));

    // scaleInterval wraps per octave, not every eight steps: the ladder keeps
    // climbing rather than resetting, which is what makes the 8-row and 64-cell
    // surfaces work.
    require(processor.scaleInterval(0) == 0, "scale interval 0 should be zero semitones");
    require(processor.scaleInterval(7) == 12, "scale interval 7 should be the octave");
    require(processor.scaleInterval(14) == 24, "scale interval 14 should be two octaves up");
    require(processor.scaleInterval(-1) == -1,
            "a negative degree should fall below the root, not wrap to the top");

    for (int degree = 0; degree < 40; ++degree)
    {
        require(processor.scaleInterval(degree + 1) > processor.scaleInterval(degree),
                "the ladder must ascend without limit");
    }
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
    require(kScaleDegreeCount.size() == 24u, "every scale needs a degree count");

    for (std::size_t scale = 0; scale < kScaleIntervals.size(); ++scale)
    {
        const std::size_t degrees = kScaleDegreeCount[scale];
        const bool chromatic = scale == static_cast<std::size_t>(ScaleId::chromatic);
        require(chromatic ? degrees == 12 : (degrees >= 5 && degrees <= 8),
                "a scale needs between five and eight degrees (chromatic has twelve)");
        require(kScaleIntervals[scale][0] == 0, "a scale must start on its root");
        if (!chromatic)
            require(kScaleIntervals[scale][degrees - 1] < 12,
                    "the last degree of an octave must stay below the octave");

        // The ladder must strictly ascend across all eight grid rows for every
        // scale. Pentatonic, blues and whole-tone failed this when the tables
        // stored a fixed octave at index 7 instead of wrapping per octave.
        for (int row = 1; row < static_cast<int>(kGridHeight); ++row)
        {
            require(scaleStepOf(scale, row) > scaleStepOf(scale, row - 1),
                    "every scale ladder must ascend across all eight rows");
        }

        if (degrees == 7)
        {
            require(scaleStepOf(scale, 7) == 12,
                    "a seven-note scale's eighth row should be the octave");
        }
    }
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

// ── String spread ───────────────────────────────────────────────────────────

void testMicrotuneIsCentsNotSemitones()
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::chromatic));
    processor.setParameter(p(ParamId::root), 60.0f);
    processor.setParameter(p(ParamId::octave), 0.0f);
    processor.setParameter(p(ParamId::rotate), 0.0f);
    processor.setParameter(p(ParamId::stride), 0.0f);

    processor.setParameter(p(ParamId::microtune), 0.0f);
    require(processor.noteForCell(0, 0) == 60, "zero microtune should leave the root alone");

    // Microtune is in cents. 100 cents is one semitone; 50 cents is a quarter
    // tone. It used to be applied as whole semitones, so the 8-cent default
    // pushed every note up by eight.
    processor.setParameter(p(ParamId::microtune), 50.0f);
    require(processor.noteForCell(0, 0) == 61, "50 cents should be a quarter tone");

    processor.setParameter(p(ParamId::microtune), 100.0f);
    require(processor.noteForCell(0, 0) == 61, "100 cents should be one semitone");

    // The parameter spans a quarter tone either side, so it clamps rather than
    // allowing whole-semitone transposition.
    processor.setParameter(p(ParamId::microtune), 5000.0f);
    require(processor.getParameter(p(ParamId::microtune)) == 50.0f,
            "microtune should clamp to its maximum");
    require(processor.noteForCell(0, 0) == 61, "the maximum microtune should be a quarter tone");

    // The default is a small detune, not a transposition.
    Processor defaults;
    defaults.init(48000.0);
    require(defaults.getParameter(p(ParamId::microtune)) < 20.0f,
            "the default microtune should be a few cents");
    defaults.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::chromatic));
    defaults.setParameter(p(ParamId::root), 60.0f);
    require(defaults.noteForCell(0, 0) == 60, "the default microtune must not shift the pitch");
}

void testUnisonSpreadIgnoresTheColumn()
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::major));
    processor.setParameter(p(ParamId::root), 45.0f);
    processor.setParameter(p(ParamId::spread), static_cast<float>(SpreadId::unison));

    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 1; col < kGridWidth; ++col)
        {
            require(processor.noteForCell(row, col) == processor.noteForCell(row, 0),
                    "unison spread should ignore the column entirely");
        }
    }
}

void testScaleSpreadMakesColumnsDifferent()
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::major));
    processor.setParameter(p(ParamId::root), 45.0f);
    processor.setParameter(p(ParamId::octave), 0.0f);
    processor.setParameter(p(ParamId::rotate), 0.0f);
    processor.setParameter(p(ParamId::spread), static_cast<float>(SpreadId::scale));

    // This is the whole point of the change: eight columns must be eight
    // different notes, or the grid is eight copies of one instrument.
    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 1; col < kGridWidth; ++col)
        {
            require(processor.noteForCell(row, col) != processor.noteForCell(row, 0),
                    "scale spread should make every column a different note");
            require(processor.noteForCell(row, col) > processor.noteForCell(row, col - 1),
                    "scale spread columns should ascend left to right");
        }
    }

    // Column c is degree c above column 0, so column 7 row 0 is the octave of
    // the root and the grid spans two octaves.
    require(processor.noteForCell(0, 7) == 45 + 12,
            "scale spread column 8 should start an octave up");
    require(processor.noteForCell(0, 0) == 45, "scale spread column 1 should start on the root");

    // Rows still ascend within a column.
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        for (std::size_t row = 1; row < kGridHeight; ++row)
        {
            require(processor.noteForCell(row, col) > processor.noteForCell(row - 1, col),
                    "rows must still ascend within each column");
        }
    }
}

void testFixedIntervalSpreads()
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::chromatic));
    processor.setParameter(p(ParamId::root), 45.0f);
    processor.setParameter(p(ParamId::octave), 0.0f);

    processor.setParameter(p(ParamId::spread), static_cast<float>(SpreadId::fourths));
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        const int expected = 45 + static_cast<int>(col) * kFourthSemitones;
        require(processor.noteForCell(0, col) == expected, "fourths spread should step 5 semitones");
        require(processor.noteForCell(4, col) == expected + 4, "rows should still move by scale degree");
    }

    processor.setParameter(p(ParamId::spread), static_cast<float>(SpreadId::fifths));
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        const int expected = 45 + static_cast<int>(col) * kFifthSemitones;
        require(processor.noteForCell(0, col) == expected, "fifths spread should step 7 semitones");
    }
}

void testSpreadClampsAndSurvivesTuningChanges()
{
    Processor clampCheck;
    clampCheck.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };

    clampCheck.setParameter(p(ParamId::spread), 99.0f);
    require(clampCheck.getParameter(p(ParamId::spread)) ==
                static_cast<float>(static_cast<int>(SpreadId::count) - 1),
            "spread should clamp to its last mode");

    // A sounding string must follow a tuning change in every spread mode.
    for (const SpreadId mode : {SpreadId::unison, SpreadId::scale, SpreadId::fourths, SpreadId::fifths,
                                SpreadId::stride})
    {
        Processor processor;
        processor.init(48000.0);
        processor.setParameter(p(ParamId::spread), static_cast<float>(mode));

        const MidiMessage pluck[] = { noteOn(gridToNote(3, 4)) };
        renderBlocks(processor, 6, pluck, 1u);

        const std::uint8_t before = processor.noteForCell(3, 4);
        processor.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::blues));
        processor.setParameter(p(ParamId::octave), 1.0f);

        const std::uint8_t after = processor.noteForCell(3, 4);
        require(after != before, "a sounding string should follow a tuning change in every spread mode");
    }
}

void testHostActivationKeepsThePatch()
{
    // Regression: activate() and sampleRateChanged() both called init(), which
    // put every parameter back to its default, so the engine and the rest of the
    // patch were lost whenever a host started playback or changed rate.
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::engine), static_cast<float>(EngineId::marimba));
    processor.setParameter(p(ParamId::damping), 0.9f);

    processor.activate();
    require(processor.getParameter(p(ParamId::engine)) == static_cast<float>(EngineId::marimba),
            "activate must not reset the engine");

    processor.setSampleRate(96000.0);
    require(processor.getParameter(p(ParamId::damping)) == 0.9f, "a rate change must not reset the patch");

    const MidiMessage down[] = { noteOn(gridToNote(2, 0)) };
    const Render out = renderBlocks(processor, 20, down, 1u);
    require(!out.silent(), "the plugin must still sound after a rate change");
    require(std::isfinite(out.peak), "output must stay finite after a rate change");
}

void testIdleResonatorsAreSilentAndCheap()
{
    // After the note has released, the string must produce exact silence rather
    // than a decaying tail of denormals.
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::ledFeedback), 0.0f);
    processor.setParameter(p(ParamId::engine), static_cast<float>(EngineId::drumhead));
    processor.setParameter(p(ParamId::delaySend), 0.0f);
    processor.setParameter(p(ParamId::reverbSend), 0.0f);
    processor.setParameter(p(ParamId::envRelease), 5.0f);
    processor.setParameter(p(ParamId::envSustain), 0.0f);

    const MidiMessage down[] = { noteOn(gridToNote(2, 0)) };
    renderBlocks(processor, 4, down, 1u);
    const MidiMessage up[] = { noteOff(gridToNote(2, 0)) };
    renderBlocks(processor, 2, up, 1u);
    renderBlocks(processor, 40);  // let the 5 ms release finish
    const Render later = renderBlocks(processor, 400);
    require(later.silent(), "a released resonator must fall to exact silence");
}

// ── Every pad stays in the chosen scale ────────────────────────────────────

void testEveryPadStaysInTheChosenScale()
{
    // Independent pitch-class sets, written out here rather than read from
    // kScaleIntervals, so a wrong or truncated table cannot agree with itself.
    // The 8-note diminished and bebop scales were once stored as 7-note scales,
    // which dropped their last note and put out-of-scale pitches on the grid.
    const std::vector<std::vector<int>> reference = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},  // chromatic
        {0, 2, 4, 5, 7, 9, 11},                  // major
        {0, 2, 4, 5, 7, 9, 11},                  // ionian
        {0, 2, 3, 5, 7, 8, 10},                  // minor
        {0, 2, 3, 5, 7, 8, 11},                  // harmonic minor
        {0, 2, 3, 5, 7, 9, 11},                  // melodic minor
        {0, 2, 3, 5, 7, 9, 10},                  // dorian
        {0, 1, 3, 5, 7, 8, 10},                  // phrygian
        {0, 2, 4, 6, 7, 9, 11},                  // lydian
        {0, 2, 4, 5, 7, 9, 10},                  // mixolydian
        {0, 1, 3, 5, 6, 8, 10},                  // locrian
        {0, 1, 4, 5, 7, 8, 10},                  // phrygian dominant
        {0, 1, 3, 5, 7, 9, 11},                  // neapolitan major
        {0, 1, 3, 5, 7, 8, 11},                  // neapolitan minor
        {0, 2, 4, 7, 9},                         // pentatonic major
        {0, 3, 5, 7, 10},                        // pentatonic minor
        {0, 3, 5, 6, 7, 10},                     // blues
        {0, 2, 4, 6, 8, 10},                     // whole tone
        {0, 1, 3, 4, 6, 8, 10},                  // altered
        {0, 1, 3, 4, 6, 7, 9, 10},               // half-whole diminished
        {0, 2, 3, 5, 6, 8, 9, 11},               // whole-half diminished
        {0, 2, 4, 5, 7, 9, 10, 11},              // bebop dominant
        {0, 2, 4, 5, 7, 8, 9, 11},               // bebop major
        {0, 2, 3, 4, 5, 7, 9, 10},               // bebop minor
    };
    require(reference.size() == static_cast<std::size_t>(ScaleId::count), "one reference set per scale");

    const auto inSet = [](const std::vector<int>& set, const int pitchClass) {
        for (const int value : set)
        {
            if (value == pitchClass)
                return true;
        }
        return false;
    };

    constexpr int kRoot = 45;
    for (std::size_t scale = 0; scale < reference.size(); ++scale)
    {
        // The scale must contain every one of its notes, and nothing else.
        Processor probe;
        probe.init(48000.0);
        const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
        probe.setParameter(p(ParamId::scale), static_cast<float>(scale));
        std::vector<int> seen;
        for (int degree = 0; degree < 24; ++degree)
        {
            const int pitchClass = ((probe.scaleStep(degree) % 12) + 12) % 12;
            require(inSet(reference[scale], pitchClass), "a scale degree must belong to the scale");
            if (!inSet(seen, pitchClass))
                seen.push_back(pitchClass);
        }
        require(seen.size() == reference[scale].size(), "a scale must contain every one of its notes");

        // Every pad, in every column and every spread that is meant to stay in
        // key, must land on a note of the scale.
        for (const SpreadId spread : {SpreadId::unison, SpreadId::scale, SpreadId::stride})
        {
            for (const int stride : {0, 3, 5, 7, 12})
            {
                Processor processor;
                processor.init(48000.0);
                processor.setParameter(p(ParamId::scale), static_cast<float>(scale));
                processor.setParameter(p(ParamId::root), static_cast<float>(kRoot));
                processor.setParameter(p(ParamId::octave), 0.0f);
                processor.setParameter(p(ParamId::microtune), 0.0f);
                processor.setParameter(p(ParamId::spread), static_cast<float>(spread));
                processor.setParameter(p(ParamId::stride), static_cast<float>(stride));
                for (const float rotate : {0.0f, 3.0f})
                {
                    processor.setParameter(p(ParamId::rotate), rotate);
                    for (std::size_t row = 0; row < kGridHeight; ++row)
                    {
                        for (std::size_t col = 0; col < kGridWidth; ++col)
                        {
                            const int pitchClass = (((processor.noteForCell(row, col) - kRoot) % 12) + 12) % 12;
                            require(inSet(reference[scale], pitchClass),
                                    "every pad must play a note of the chosen scale");
                        }
                    }
                }
            }
        }

        // Down a column the notes must ascend through the scale.
        Processor column;
        column.init(48000.0);
        column.setParameter(p(ParamId::scale), static_cast<float>(scale));
        column.setParameter(p(ParamId::microtune), 0.0f);
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            for (std::size_t row = 1; row < kGridHeight; ++row)
                require(column.noteForCell(row, col) > column.noteForCell(row - 1, col),
                        "notes must ascend up a column");
        }
    }
}

// ── Session state ───────────────────────────────────────────────────────────

void testStateRoundTrips()
{
    Processor saved;
    saved.init(48000.0);

    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    saved.setParameter(p(ParamId::morph), 0.42f);
    saved.setParameter(p(ParamId::cutoff), 0.17f);
    saved.setParameter(p(ParamId::drive), 0.88f);
    saved.setParameter(p(ParamId::resonance), 0.63f);
    saved.setParameter(p(ParamId::envAttack), 123.0f);
    saved.setParameter(p(ParamId::envRelease), 777.0f);
    saved.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::blues));
    saved.setParameter(p(ParamId::root), 52.0f);
    saved.setParameter(p(ParamId::octave), -1.0f);
    saved.setParameter(p(ParamId::width), 0.29f);
    saved.setParameter(p(ParamId::level), 0.91f);
    saved.setParameter(p(ParamId::latch), 1.0f);
    saved.setParameter(p(ParamId::midiThru), 1.0f);
    saved.setParameter(p(ParamId::ledFeedback), 0.0f);
    saved.setParameter(p(ParamId::lfoATarget), static_cast<float>(ModTarget::morph));

    const std::string state = saved.serializeParameters();
    require(!state.empty(), "serialize should produce a non-empty state");
    require(state.find("version=") != std::string::npos, "state should carry a version line");

    // A reopened plugin starts from defaults, then restores.
    Processor reopened;
    reopened.init(48000.0);
    require(reopened.getParameter(p(ParamId::cutoff)) != 0.17f,
            "a fresh plugin should not already have the saved values");

    require(reopened.deserializeParameters(state), "a well-formed state should load");

    for (const ParamId id : {ParamId::morph, ParamId::cutoff, ParamId::drive, ParamId::resonance,
                             ParamId::envAttack, ParamId::envRelease, ParamId::scale, ParamId::root,
                             ParamId::octave, ParamId::width, ParamId::level, ParamId::latch,
                             ParamId::midiThru, ParamId::ledFeedback, ParamId::lfoATarget})
    {
        require(std::fabs(reopened.getParameter(p(id)) - saved.getParameter(p(id))) < 1.0e-4f,
                "every persisted parameter should survive a save and reload");
    }
}

void testStateRestoresTheMusicalLadder()
{
    Processor saved;
    saved.init(48000.0);
    saved.setParameter(static_cast<std::uint32_t>(ParamId::scale), static_cast<float>(ScaleId::wholeTone));
    saved.setParameter(static_cast<std::uint32_t>(ParamId::root), 40.0f);
    saved.setParameter(static_cast<std::uint32_t>(ParamId::octave), 2.0f);

    const auto before = saved.noteForCell(3, 0);

    Processor reopened;
    reopened.init(48000.0);
    require(reopened.deserializeParameters(saved.serializeParameters()),
            "the ladder state should load");

    require(reopened.noteForCell(3, 0) == before,
            "the grid's pitches should be identical after a reload");
}

void testStateRejectsGarbage()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::cutoff), 0.6f);
    const float before = processor.getParameter(static_cast<std::uint32_t>(ParamId::cutoff));

    require(!processor.deserializeParameters(""), "an empty state should be rejected");
    require(!processor.deserializeParameters("nonsense"), "a state with no version should be rejected");
    require(!processor.deserializeParameters("version=999\ncutoff=0.1\n"),
            "an unknown version should be rejected");
    require(!processor.deserializeParameters("version=1\nnot_a_parameter=0.5\n"),
            "an unknown parameter should be rejected");
    require(!processor.deserializeParameters("version=1\ncutoff\n"),
            "a line without a separator should be rejected");

    require(processor.getParameter(static_cast<std::uint32_t>(ParamId::cutoff)) == before,
            "a rejected state must not partially change the patch");
}

void testStateExcludesGridCellsAndPanic()
{
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::morph), 0.3f);

    const std::string state = processor.serializeParameters();

    // Restoring cells would re-pluck every string on load, and restoring a
    // panic value of 1 would fire the panic.
    require(state.find("cell_1=") == std::string::npos, "grid cells should not be persisted");
    require(state.find("panic=") == std::string::npos, "the panic trigger should not be persisted");
    require(state.find("morph=") != std::string::npos, "real settings should be persisted");

    // Even if a hand-edited state names them, they must not re-pluck.
    const std::string tampered =
        "version=1\nmorph=0.5\ncell_1=1\ncell_9=1\npanic=1\n";
    Processor other;
    other.init(48000.0);
    require(other.deserializeParameters(tampered), "a state naming cells should still parse");

    std::vector<float> left(kBlock * 4, 0.0f);
    std::vector<float> right(kBlock * 4, 0.0f);
    ProcessResult result {};
    other.process(left.data(), right.data(), kBlock * 4, TransportSnapshot {}, nullptr, 0, result);
    require(other.getStatus().activeStrings == 0.0f,
            "a restored state must not leave strings sounding");
}

}  // namespace

// ── Plinky string stride ────────────────────────────────────────────────────

void testStrideSpreadFollowsPlinky()
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::scale), static_cast<float>(ScaleId::major));
    processor.setParameter(p(ParamId::root), 45.0f);
    processor.setParameter(p(ParamId::octave), 0.0f);
    processor.setParameter(p(ParamId::rotate), 0.0f);
    processor.setParameter(p(ParamId::microtune), 0.0f);
    processor.setParameter(p(ParamId::spread), static_cast<float>(SpreadId::stride));

    // Plinky's default: each string a fifth above the last, snapped to the scale.
    processor.setParameter(p(ParamId::stride), 7.0f);
    require(processor.noteForCell(0, 0) == 45, "string 1 sits on the root");
    require(processor.noteForCell(0, 1) == 52, "string 2 is a fifth up");
    require(processor.noteForCell(0, 2) == 59, "string 3 is two fifths up");

    // Stride 0 puts every string on the same ladder, as on Plinky.
    processor.setParameter(p(ParamId::stride), 0.0f);
    for (std::size_t col = 1; col < kGridWidth; ++col)
        require(processor.noteForCell(0, col) == processor.noteForCell(0, 0), "stride 0 is unison");

    // Snapping: a stride of 6 has no major-scale degree at +6, so the result must
    // still be a note of the scale, never a chromatic neighbour.
    processor.setParameter(p(ParamId::stride), 6);
    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        const int semitones = (processor.noteForCell(0, col) - 45) % 12;
        bool inScale = false;
        for (int degree = 0; degree < 7; ++degree)
            inScale = inScale || scaleStepAt(static_cast<std::size_t>(ScaleId::major), degree) == semitones;
        require(inScale, "stride must keep every string in the scale");
    }
}

void testStridePitchReachesTheVoice()
{
    // Regression: setDegree() used to pass column 0 to noteForCell, so every
    // string ignored its column and Spread had no audible effect.
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::ledFeedback), 0.0f);
    processor.setParameter(p(ParamId::midiThru), 1.0f);
    processor.setParameter(p(ParamId::spread), static_cast<float>(SpreadId::scale));

    for (std::size_t col = 0; col < kGridWidth; ++col)
    {
        Processor fresh;
        fresh.init(48000.0);
        fresh.setParameter(p(ParamId::ledFeedback), 0.0f);
        fresh.setParameter(p(ParamId::midiThru), 1.0f);
        fresh.setParameter(p(ParamId::spread), static_cast<float>(SpreadId::scale));

        const MidiMessage down[] = { noteOn(gridToNote(0, col)) };
        const Render out = renderBlocks(fresh, 2, down, 1u);

        bool found = false;
        for (std::uint32_t i = 0; i < out.result.eventCount; ++i)
        {
            if ((out.result.events[i].data[0] & 0xf0u) == 0x90u)
            {
                require(out.result.events[i].data[1] == fresh.noteForCell(0, col),
                        "a string must sound the note its column maps to");
                found = true;
            }
        }
        require(found, "the pluck should emit a note");
    }
}

// ── Resonator engines ───────────────────────────────────────────────────────

Render pluckWith(const EngineId engine, const float damping = 0.55f, const std::uint32_t blocks = 40u)
{
    Processor processor;
    processor.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    processor.setParameter(p(ParamId::ledFeedback), 0.0f);
    processor.setParameter(p(ParamId::engine), static_cast<float>(engine));
    processor.setParameter(p(ParamId::damping), damping);
    const MidiMessage down[] = { noteOn(gridToNote(2, 0)) };
    return renderBlocks(processor, blocks, down, 1u);
}

float tailEnergy(const Render& render)
{
    float energy = 0.0f;
    for (std::size_t i = render.left.size() * 3u / 4u; i < render.left.size(); ++i)
        energy += render.left[i] * render.left[i];
    return energy;
}

void testEngineIdsAreStable()
{
    // Engine ordinals are saved in host state: append only.
    require(static_cast<std::uint32_t>(EngineId::plinky) == 0, "plinky is engine 0");
    require(static_cast<std::uint32_t>(EngineId::beam) == 1, "beam is engine 1");
    require(static_cast<std::uint32_t>(EngineId::string) == 6, "string is engine 6");
    require(kEngineNames.size() == static_cast<std::size_t>(EngineId::count), "every engine needs a name");
    require(kExciterNames.size() == static_cast<std::size_t>(ExciterId::count), "every exciter needs a name");

    Processor processor;
    processor.init(48000.0);
    require(processor.getParameter(static_cast<std::uint32_t>(ParamId::engine)) == 0.0f,
            "the default engine must remain Plinky so existing patches sound the same");
}

void testEveryResonatorEngineSounds()
{
    const Render reference = pluckWith(EngineId::plinky);
    for (std::uint32_t id = 1; id < static_cast<std::uint32_t>(EngineId::count); ++id)
    {
        const Render render = pluckWith(static_cast<EngineId>(id));
        require(!render.silent(), "every resonator engine should produce audio");
        require(render.peak < 1.5f, "resonator output must stay in a sane range");
        require(std::isfinite(render.peak), "resonator output must be finite");

        bool differs = false;
        for (std::size_t i = 0; i < render.left.size() && !differs; ++i)
            differs = std::fabs(render.left[i] - reference.left[i]) > 1.0e-4f;
        require(differs, "a resonator engine must not sound like the Plinky engine");
    }
}

float rmsOf(const Render& render)
{
    double sum = 0.0;
    for (const float v : render.left)
        sum += static_cast<double>(v) * static_cast<double>(v);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(render.left.size())));
}

void testResonatorsAreAboutAsLoudAsPlinky()
{
    // Regression: the resonator engines sat about 14 dB below the Plinky voice.
    // A struck sound decays, so it cannot match the sustained voice's RMS, but it
    // must be within a few dB and must not clip.
    const auto render = [](const EngineId engine, const ExciterId exciter) {
        Processor processor;
        processor.init(48000.0);
        const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
        processor.setParameter(p(ParamId::ledFeedback), 0.0f);
        processor.setParameter(p(ParamId::engine), static_cast<float>(engine));
        processor.setParameter(p(ParamId::exciter), static_cast<float>(exciter));
        processor.setParameter(p(ParamId::delaySend), 0.0f);
        processor.setParameter(p(ParamId::reverbSend), 0.0f);
        const MidiMessage down[] = { noteOn(gridToNote(2, 0)) };
        return renderBlocks(processor, 700, down, 1u);
    };

    const float reference = rmsOf(render(EngineId::plinky, ExciterId::mallet));
    for (std::uint32_t id = 1; id < static_cast<std::uint32_t>(EngineId::count); ++id)
    {
        for (const ExciterId exciter : {ExciterId::mallet, ExciterId::noise})
        {
            const Render out = render(static_cast<EngineId>(id), exciter);
            const float ratio = rmsOf(out) / reference;
            require(ratio > 0.4f, "a resonator engine must not be far quieter than the Plinky voice");
            require(ratio < 1.5f, "a resonator engine must not be far louder than the Plinky voice");
            require(out.peak < 1.0f, "a resonator engine must not clip");
        }
    }
}

void testSoftMalletIsAudibleAtEveryPitch()
{
    // Regression: the mallet had a fixed 6 ms contact time at the default Morph,
    // which put its first spectral null below most modes, so the strike was
    // nearly silent above the lowest rows.
    const auto level = [](const std::size_t row) {
        Processor processor;
        processor.init(48000.0);
        const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
        processor.setParameter(p(ParamId::ledFeedback), 0.0f);
        processor.setParameter(p(ParamId::engine), static_cast<float>(EngineId::drumhead));
        processor.setParameter(p(ParamId::exciter), static_cast<float>(ExciterId::mallet));
        processor.setParameter(p(ParamId::morph), 0.0f);
        processor.setParameter(p(ParamId::root), 57.0f);
        processor.setParameter(p(ParamId::delaySend), 0.0f);
        processor.setParameter(p(ParamId::reverbSend), 0.0f);
        const MidiMessage down[] = { noteOn(gridToNote(row, 0)) };
        return rmsOf(renderBlocks(processor, 400, down, 1u));
    };

    const float low = level(0);
    const float high = level(7);
    require(high > low * 0.6f, "a soft mallet must stay about as loud at higher pitches");
    require(low > 0.08f, "a soft mallet must not be nearly silent");
}

void testResonatorEnginesDifferFromEachOther()
{
    const Render beam = pluckWith(EngineId::beam);
    const Render drum = pluckWith(EngineId::drumhead);
    const Render string = pluckWith(EngineId::string);

    float beamVsDrum = 0.0f;
    float drumVsString = 0.0f;
    for (std::size_t i = 0; i < beam.left.size(); ++i)
    {
        beamVsDrum += std::fabs(beam.left[i] - drum.left[i]);
        drumVsString += std::fabs(drum.left[i] - string.left[i]);
    }
    require(beamVsDrum > 0.01f, "beam and drumhead should have different spectra");
    require(drumVsString > 0.01f, "drumhead and string should have different spectra");
}

void testDampingSetsTheRingTime()
{
    // Sustain is raised so the envelope does not mask the resonator's own decay.
    const auto ring = [](const float damping) {
        Processor processor;
        processor.init(48000.0);
        const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
        processor.setParameter(p(ParamId::ledFeedback), 0.0f);
        processor.setParameter(p(ParamId::engine), static_cast<float>(EngineId::string));
        processor.setParameter(p(ParamId::damping), damping);
        processor.setParameter(p(ParamId::envSustain), 1.0f);
        const MidiMessage down[] = { noteOn(gridToNote(2, 0)) };
        return tailEnergy(renderBlocks(processor, 60, down, 1u));
    };

    require(ring(0.9f) > ring(0.1f) * 4.0f, "more damping control should ring far longer");
}

void testExcitersAreBothUsable()
{
    for (std::uint32_t kind = 0; kind < static_cast<std::uint32_t>(ExciterId::count); ++kind)
    {
        Processor processor;
        processor.init(48000.0);
        const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
        processor.setParameter(p(ParamId::ledFeedback), 0.0f);
        processor.setParameter(p(ParamId::engine), static_cast<float>(EngineId::marimba));
        processor.setParameter(p(ParamId::exciter), static_cast<float>(kind));
        const MidiMessage down[] = { noteOn(gridToNote(3, 3)) };
        const Render render = renderBlocks(processor, 20, down, 1u);
        require(!render.silent(), "each exciter should excite the resonator");
        require(render.peak < 1.5f, "each exciter must stay in range");
    }
}

void testStrikePositionChangesTheTone()
{
    const auto render = [](const float strike) {
        Processor processor;
        processor.init(48000.0);
        const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
        processor.setParameter(p(ParamId::ledFeedback), 0.0f);
        processor.setParameter(p(ParamId::engine), static_cast<float>(EngineId::string));
        processor.setParameter(p(ParamId::strike), strike);
        const MidiMessage down[] = { noteOn(gridToNote(2, 0)) };
        return renderBlocks(processor, 10, down, 1u);
    };

    const Render centre = render(0.5f);
    const Render edge = render(0.1f);
    float difference = 0.0f;
    for (std::size_t i = 0; i < centre.left.size(); ++i)
        difference += std::fabs(centre.left[i] - edge.left[i]);
    require(difference > 0.01f, "strike position should reshape the harmonic balance");
}

void testResonatorStateRoundTrips()
{
    Processor saved;
    saved.init(48000.0);
    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };
    saved.setParameter(p(ParamId::engine), static_cast<float>(EngineId::plate));
    saved.setParameter(p(ParamId::exciter), static_cast<float>(ExciterId::noise));
    saved.setParameter(p(ParamId::strike), 0.8f);
    saved.setParameter(p(ParamId::damping), 0.2f);
    saved.setParameter(p(ParamId::material), 0.9f);
    saved.setParameter(p(ParamId::stride), 5.0f);

    Processor reopened;
    reopened.init(48000.0);
    require(reopened.deserializeParameters(saved.serializeParameters()), "state should restore");
    require(reopened.getParameter(p(ParamId::engine)) == static_cast<float>(EngineId::plate), "engine restores");
    require(reopened.getParameter(p(ParamId::exciter)) == static_cast<float>(ExciterId::noise), "exciter restores");
    require(reopened.getParameter(p(ParamId::material)) == 0.9f, "material restores");
    require(reopened.getParameter(p(ParamId::stride)) == 5.0f, "stride restores");
}

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
    testMicrotuneIsCentsNotSemitones();
    testUnisonSpreadIgnoresTheColumn();
    testScaleSpreadMakesColumnsDifferent();
    testFixedIntervalSpreads();
    testSpreadClampsAndSurvivesTuningChanges();
    testEveryPadStaysInTheChosenScale();
    testStrideSpreadFollowsPlinky();
    testStridePitchReachesTheVoice();
    testEngineIdsAreStable();
    testEveryResonatorEngineSounds();
    testResonatorsAreAboutAsLoudAsPlinky();
    testSoftMalletIsAudibleAtEveryPitch();
    testResonatorEnginesDifferFromEachOther();
    testDampingSetsTheRingTime();
    testExcitersAreBothUsable();
    testStrikePositionChangesTheTone();
    testResonatorStateRoundTrips();
    testHostActivationKeepsThePatch();
    testIdleResonatorsAreSilentAndCheap();
    testStateRoundTrips();
    testStateRestoresTheMusicalLadder();
    testStateRejectsGarbage();
    testStateExcludesGridCellsAndPanic();
    std::cout << "plank core tests passed\n";
    return 0;
}
