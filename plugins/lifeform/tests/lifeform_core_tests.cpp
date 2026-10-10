#include "lifeform_processor.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>

namespace {

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool containsNoteOn(const downspout::lifeform::ProcessResult& result)
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

std::uint32_t countNoteOns(const downspout::lifeform::ProcessResult& result)
{
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            (result.events[i].data[0] & 0xf0u) == 0x90u &&
            result.events[i].data[2] > 0u)
        {
            ++count;
        }
    }
    return count;
}

bool containsMidi(const downspout::lifeform::ProcessResult& result,
                  const std::uint8_t status,
                  const std::uint8_t data1,
                  const std::uint8_t data2)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            result.events[i].data[0] == status &&
            result.events[i].data[1] == data1 &&
            result.events[i].data[2] == data2)
        {
            return true;
        }
    }
    return false;
}

bool containsSysexPrefix(const downspout::lifeform::ProcessResult& result, const std::initializer_list<std::uint8_t> prefix)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size < prefix.size())
            continue;

        std::size_t offset = 0;
        bool matches = true;
        for (const std::uint8_t byte : prefix)
        {
            if (result.events[i].data[offset++] != byte)
            {
                matches = false;
                break;
            }
        }
        if (matches)
            return true;
    }
    return false;
}

bool containsLargeClearSysex(const downspout::lifeform::ProcessResult& result)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 251 &&
            result.events[i].data[0] == 0xf0u &&
            result.events[i].data[6] == 0x03u &&
            result.events[i].data[250] == 0xf7u)
        {
            return true;
        }
    }
    return false;
}

bool containsLedChannelMusicNote(const downspout::lifeform::ProcessResult& result)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            result.events[i].data[0] >= 0x90u &&
            result.events[i].data[0] <= 0x92u &&
            result.events[i].data[2] > 0u)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// ── Session state ───────────────────────────────────────────────────────────

std::array<bool, 64> cellsOf(const downspout::lifeform::Processor& processor)
{
    std::array<bool, 64> cells {};
    for (std::uint32_t i = 0; i < 64; ++i)
        cells[i] = processor.getParameter(downspout::lifeform::kParamStatusCellStart + i) > 0.5f;
    return cells;
}

void testStateRoundTripsSettingsAndPattern()
{
    using namespace downspout::lifeform;

    Processor saved;
    saved.init(48000.0);
    saved.setParameter(kParamRootNote, 55.0f);
    saved.setParameter(kParamScale, 7.0f);
    saved.setParameter(kParamGate, 0.81f);
    saved.setParameter(kParamDensity, 0.12f);
    saved.setParameter(kParamBaseChannel, 9.0f);
    saved.setParameter(kParamSeed, 3.0f);
    // A hand-drawn pattern that is not any preset.
    for (std::uint32_t i = 0; i < 64; ++i)
        saved.setParameter(kParamCellStart + i, (i % 5 == 0 || i == 63) ? 1.0f : 0.0f);

    const std::string text = saved.serializeParameters();
    require(text.find("version=") != std::string::npos, "state should carry a version line");

    Processor reopened;
    reopened.init(48000.0);
    require(reopened.deserializeParameters(text), "a saved state should load");
    require(reopened.getParameter(kParamRootNote) == 55.0f, "root should restore");
    require(reopened.getParameter(kParamScale) == 7.0f, "scale should restore");
    require(reopened.getParameter(kParamBaseChannel) == 9.0f, "base channel should restore");
    require(std::abs(reopened.getParameter(kParamGate) - 0.81f) < 1.0e-5f, "gate should restore");
    require(reopened.getParameter(kParamSeed) == 3.0f, "seed should restore");
    require(cellsOf(reopened) == cellsOf(saved), "the live cell pattern should restore exactly");
}

void testRestoringSeedDoesNotClobberThePattern()
{
    // Setting the seed parameter normally replaces the pattern with the preset;
    // restoring state must not, or every reopened project loses its drawing.
    using namespace downspout::lifeform;

    Processor saved;
    saved.init(48000.0);
    saved.setParameter(kParamSeed, 5.0f);
    for (std::uint32_t i = 0; i < 64; ++i)
        saved.setParameter(kParamCellStart + i, i == 10 || i == 20 ? 1.0f : 0.0f);

    Processor reopened;
    reopened.init(48000.0);
    require(reopened.deserializeParameters(saved.serializeParameters()), "state should load");
    require(cellsOf(reopened) == cellsOf(saved), "restoring the seed must not reseed the grid");
}

void testStateNeverStoresTriggers()
{
    using namespace downspout::lifeform;

    Processor processor;
    processor.init(48000.0);
    const std::string text = processor.serializeParameters();
    for (const char* trigger : {"randomize", "clear", "step", "panic", "status"})
        require(text.find(trigger) == std::string::npos, "triggers and status must not be persisted");

    // A hand-edited state naming one is refused outright rather than firing it.
    Processor target;
    target.init(48000.0);
    require(!target.deserializeParameters("version=1\npanic=1\n"), "a trigger in state must be rejected");
    require(!target.deserializeParameters("version=1\nrandomize=1\n"), "a trigger in state must be rejected");
}

void testStateRejectsGarbage()
{
    using namespace downspout::lifeform;

    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamRootNote, 61.0f);
    const auto before = cellsOf(processor);

    require(!processor.deserializeParameters(""), "empty state is rejected");
    require(!processor.deserializeParameters("root=70\n"), "state without a version is rejected");
    require(!processor.deserializeParameters("version=99\nroot=70\n"), "an unknown version is rejected");
    require(!processor.deserializeParameters("version=1\nnonsense=1\n"), "an unknown key is rejected");
    require(!processor.deserializeParameters("version=1\nroot=abc\n"), "a non-numeric value is rejected");
    require(!processor.deserializeParameters("version=1\ncells=0101\n"), "a short bitmap is rejected");
    require(!processor.deserializeParameters(std::string("version=1\ncells=") + std::string(64, '2') + "\n"),
            "a bitmap with other characters is rejected");

    require(processor.getParameter(kParamRootNote) == 61.0f, "a rejected state must leave the patch alone");
    require(cellsOf(processor) == before, "a rejected state must leave the pattern alone");
}

void testHostActivationKeepsThePatch()
{
    // Regression: activate() and sampleRateChanged() both reset to defaults, so
    // the settings and the drawn pattern were lost whenever a host started
    // playback or changed rate.
    using namespace downspout::lifeform;

    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamRootNote, 60.0f);
    processor.setParameter(kParamScale, 4.0f);
    for (std::uint32_t i = 0; i < 64; ++i)
        processor.setParameter(kParamCellStart + i, i % 7 == 0 ? 1.0f : 0.0f);
    const auto pattern = cellsOf(processor);

    processor.activate();
    require(processor.getParameter(kParamRootNote) == 60.0f, "activate must not reset the root");
    require(processor.getParameter(kParamScale) == 4.0f, "activate must not reset the scale");
    require(cellsOf(processor) == pattern, "activate must not reset the pattern");

    processor.setSampleRate(96000.0);
    require(processor.getParameter(kParamRootNote) == 60.0f, "a rate change must not reset the patch");
    require(cellsOf(processor) == pattern, "a rate change must not reset the pattern");
}


void testConductorCcSet()
{
    using namespace downspout::lifeform;

    const auto send = [](Processor& processor, const int status, const int d1, const int d2) {
        MidiMessage m {};
        m.size = 3;
        m.data[0] = static_cast<std::uint8_t>(status);
        m.data[1] = static_cast<std::uint8_t>(d1);
        m.data[2] = static_cast<std::uint8_t>(d2);
        TransportSnapshot transport {};
        return processor.processBlock(64, transport, &m, 1);
    };
    const auto liveCells = [](const Processor& processor) {
        int count = 0;
        for (const bool c : cellsOf(processor))
            count += c ? 1 : 0;
        return count;
    };

    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);

    // Off by default: the CCs do nothing.
    require(processor.getParameter(kParamConductorCh) == 0.0f, "conductor channel defaults to off");
    const float density = processor.getParameter(kParamDensity);
    send(processor, 0xbf, 21, 127);
    require(processor.getParameter(kParamDensity) == density, "conductor CC ignored while off");

    processor.setParameter(kParamConductorCh, 16.0f);
    send(processor, 0xbf, 21, 127);  // Density
    require(processor.getParameter(kParamDensity) == 1.0f, "CC 21 sets density");
    send(processor, 0xbf, 21, 0);
    require(processor.getParameter(kParamDensity) == 0.0f, "CC 21 spans 0 to 1");
    send(processor, 0xbf, 22, 127);  // Energy -> Velocity
    require(processor.getParameter(kParamVelocity) == 1.0f, "CC 22 sets velocity");
    send(processor, 0xbf, 23, 0);    // Mutation
    require(processor.getParameter(kParamMutation) == 0.0f, "CC 23 sets mutation");
    send(processor, 0xbf, 23, 127);
    require(processor.getParameter(kParamMutation) == 1.0f, "CC 23 reaches 1");

    // Other channels and unused CCs are ignored.
    const float before = processor.getParameter(kParamVelocity);
    send(processor, 0xb0, 22, 0);   // channel 1
    send(processor, 0xbf, 20, 0);   // scene is unused
    require(processor.getParameter(kParamVelocity) == before, "other channels and CC 20 do nothing");

    // CC 24 only acts at 127, and then draws a fresh random pattern at the current density.
    processor.setParameter(kParamClear, 1.0f);
    require(liveCells(processor) == 0, "cleared");
    send(processor, 0xbf, 21, 127);
    send(processor, 0xbf, 24, 100);
    require(liveCells(processor) == 0, "CC 24 below 127 does nothing");
    send(processor, 0xbf, 24, 127);
    require(liveCells(processor) > 0, "CC 24 = 127 randomises the pattern");

    // Conductor CCs still pass through when Pass Input is on.
    processor.setParameter(kParamPassInput, 1.0f);
    const auto passed = send(processor, 0xbf, 21, 50);
    require(containsMidi(passed, 0xbf, 21, 50), "conductor CC is passed on with Pass Input");

    // The channel is saved with the settings.
    processor.setParameter(kParamConductorCh, 7.0f);
    Processor restored;
    restored.init(48000.0);
    require(restored.deserializeParameters(processor.serializeParameters()), "state restores");
    require(restored.getParameter(kParamConductorCh) == 7.0f, "conductor channel is saved");
}

void testGliderTravelsAcrossEveryEdge()
{
    // Independent torus reference: a glider must cross all four edges and keep matching it.
    using namespace downspout::lifeform;
    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    std::array<bool, 64> ref {};
    for (std::uint32_t i = 0; i < 64; ++i)
        processor.setParameter(kParamCellStart + i, 0.0f);
    const int glider[5][2] = {{0, 1}, {1, 2}, {2, 0}, {2, 1}, {2, 2}};
    for (const auto& c : glider)
    {
        processor.setParameter(kParamCellStart + static_cast<std::uint32_t>(cellIndex(c[0], c[1])), 1.0f);
        ref[cellIndex(c[0], c[1])] = true;
    }
    processor.setParameter(kParamRunning, 1.0f);
    processor.setParameter(kParamClockMode, 2.0f);
    TransportSnapshot transport {};
    unsigned refGeneration = 0;
    for (int block = 0; block < 4000 && refGeneration < 80; ++block)
    {
        processor.processBlock(512, transport, nullptr, 0);
        const auto generation = static_cast<unsigned>(processor.getParameter(kParamStatusGeneration));
        for (; refGeneration < generation; ++refGeneration)
        {
            std::array<bool, 64> next {};
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c)
                {
                    int n = 0;
                    for (int dr = -1; dr <= 1; ++dr)
                        for (int dc = -1; dc <= 1; ++dc)
                            if (dr != 0 || dc != 0)
                                n += ref[static_cast<std::size_t>(((r + dr + 8) % 8) * 8 + (c + dc + 8) % 8)] ? 1 : 0;
                    const bool alive = ref[static_cast<std::size_t>(r * 8 + c)];
                    next[static_cast<std::size_t>(r * 8 + c)] = alive ? (n == 2 || n == 3) : n == 3;
                }
            ref = next;
        }
        for (std::uint32_t i = 0; i < 64; ++i)
            require((processor.getParameter(kParamStatusCellStart + i) >= 0.5f) == ref[i],
                    "lifeform glider should match a wrapping 8x8 reference across every edge");
    }
    require(refGeneration >= 80, "lifeform glider test should run 80 generations");
}

int main()
{
    testGliderTravelsAcrossEveryEdge();
    testConductorCcSet();
    testStateRoundTripsSettingsAndPattern();
    testRestoringSeedDoesNotClobberThePattern();
    testStateNeverStoresTriggers();
    testStateRejectsGarbage();
    testHostActivationKeepsThePatch();
    using downspout::lifeform::MidiMessage;
    using downspout::lifeform::Processor;
    using downspout::lifeform::TransportSnapshot;
    using downspout::lifeform::gridToNote;
    using downspout::lifeform::kParamCellStart;
    using downspout::lifeform::kParamClockMode;
    using downspout::lifeform::kParamEmitMode;
    using downspout::lifeform::kParamLedFeedback;
    using downspout::lifeform::kParamPanic;
    using downspout::lifeform::kParamPassInput;
    using downspout::lifeform::kParamRunning;
    using downspout::lifeform::kParamSeed;
    using downspout::lifeform::kParamStatusCellStart;
    using downspout::lifeform::kParamStatusGeneration;
    using downspout::lifeform::cellIndex;

    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    for (std::uint32_t i = 0; i < 64; ++i)
        processor.setParameter(kParamCellStart + i, 0.0f);

    processor.setParameter(kParamCellStart + static_cast<std::uint32_t>(cellIndex(3, 2)), 1.0f);
    processor.setParameter(kParamCellStart + static_cast<std::uint32_t>(cellIndex(3, 3)), 1.0f);
    processor.setParameter(kParamCellStart + static_cast<std::uint32_t>(cellIndex(3, 4)), 1.0f);

    TransportSnapshot transport {};
    auto result = processor.processBlock(512, transport, nullptr, 0);
    require(!containsNoteOn(result), "lifeform should not advance while stopped");

    processor.setParameter(kParamRunning, 1.0f);
    processor.setParameter(kParamClockMode, 0.0f);
    result = processor.processBlock(512, transport, nullptr, 0);
    require(!containsNoteOn(result), "lifeform auto clock should not free-run while host transport is stopped");

    processor.setParameter(kParamClockMode, 2.0f);
    result = processor.processBlock(512, transport, nullptr, 0);
    require(containsNoteOn(result), "lifeform should emit MIDI for living cells on a generation beat");
    require(!containsLedChannelMusicNote(result),
            "lifeform default musical MIDI should avoid Launchpad LED channels 1-3");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(2, 3)) == 1.0f,
            "lifeform blinker should rotate to row 2");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(3, 3)) == 1.0f,
            "lifeform blinker should keep center cell");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(4, 3)) == 1.0f,
            "lifeform blinker should rotate to row 4");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(3, 2)) == 0.0f,
            "lifeform blinker should clear left arm");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    for (std::uint32_t i = 0; i < 64; ++i)
        processor.setParameter(kParamCellStart + i, 1.0f);
    processor.setParameter(kParamRunning, 1.0f);
    processor.setParameter(kParamClockMode, 2.0f);
    result = processor.processBlock(512, transport, nullptr, 0);
    require(countNoteOns(result) == 8,
            "lifeform lean emit mode should limit dense patterns to eight musical note-ons");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    processor.setParameter(kParamEmitMode, 1.0f);
    for (std::uint32_t i = 0; i < 64; ++i)
        processor.setParameter(kParamCellStart + i, 1.0f);
    processor.setParameter(kParamRunning, 1.0f);
    processor.setParameter(kParamClockMode, 2.0f);
    result = processor.processBlock(512, transport, nullptr, 0);
    require(countNoteOns(result) == 64,
            "lifeform full emit mode should preserve all-cell musical output");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    for (std::uint32_t i = 0; i < 64; ++i)
        processor.setParameter(kParamCellStart + i, 0.0f);
    processor.setParameter(kParamCellStart + static_cast<std::uint32_t>(cellIndex(0, 7)), 1.0f);
    processor.setParameter(kParamCellStart + static_cast<std::uint32_t>(cellIndex(0, 0)), 1.0f);
    processor.setParameter(kParamCellStart + static_cast<std::uint32_t>(cellIndex(0, 1)), 1.0f);
    processor.setParameter(kParamRunning, 1.0f);
    processor.setParameter(kParamClockMode, 2.0f);
    result = processor.processBlock(512, transport, nullptr, 0);
    require(processor.getParameter(kParamStatusCellStart + cellIndex(7, 0)) == 1.0f,
            "lifeform wrapped blinker should birth through the top edge");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(0, 0)) == 1.0f,
            "lifeform wrapped blinker should keep the edge center");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(1, 0)) == 1.0f,
            "lifeform wrapped blinker should birth through the bottom edge");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(0, 7)) == 0.0f,
            "lifeform wrapped blinker should clear the wrapped left arm");
    require(processor.getParameter(kParamStatusCellStart + cellIndex(0, 1)) == 0.0f,
            "lifeform wrapped blinker should clear the wrapped right arm");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    MidiMessage press {};
    press.size = 3;
    press.data[0] = 0x90;
    press.data[1] = gridToNote(5, 6);
    press.data[2] = 127;
    result = processor.processBlock(128, transport, &press, 1);
    require(processor.getParameter(kParamStatusCellStart + cellIndex(5, 6)) == 1.0f,
            "lifeform Launchpad pad press should flip a cell on");
    press.data[2] = 0;
    result = processor.processBlock(128, transport, &press, 1);
    require(processor.getParameter(kParamStatusCellStart + cellIndex(5, 6)) == 1.0f,
            "lifeform Launchpad pad release should not flip or echo as LED-off");
    require(!containsMidi(result, 0x90, gridToNote(5, 6), 0),
            "lifeform Launchpad pad release should not be echoed to the hardware LED stream");
    press.data[2] = 127;
    result = processor.processBlock(128, transport, &press, 1);
    require(processor.getParameter(kParamStatusCellStart + cellIndex(5, 6)) == 0.0f,
            "lifeform Launchpad pad press should flip a cell off");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    MidiMessage foreignNote {};
    foreignNote.size = 3;
    foreignNote.data[0] = 0x99;
    foreignNote.data[1] = 100;
    foreignNote.data[2] = 64;
    result = processor.processBlock(128, transport, &foreignNote, 1);
    require(!containsMidi(result, 0x99, 100, 64), "lifeform should block unhandled input MIDI by default");

    processor.setParameter(kParamPassInput, 1.0f);
    result = processor.processBlock(128, transport, &foreignNote, 1);
    require(containsMidi(result, 0x99, 100, 64), "lifeform pass input switch should forward unhandled MIDI");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamLedFeedback, 0.0f);
    processor.setParameter(kParamRunning, 0.0f);
    for (std::uint32_t i = 0; i < 64; ++i)
        processor.setParameter(kParamCellStart + i, 0.0f);
    processor.setParameter(kParamRunning, 1.0f);
    processor.setParameter(kParamClockMode, 2.0f);
    press.frame = 0;
    press.data[1] = gridToNote(4, 4);
    result = processor.processBlock(128, transport, &press, 1);
    require(processor.getParameter(kParamStatusCellStart + cellIndex(4, 4)) == 1.0f,
            "lifeform same-frame beat and pad press should leave the pressed cell visible");
    require(processor.getParameter(kParamStatusGeneration) == 1.0f,
            "lifeform same-frame beat should still advance exactly one generation");

    processor.setParameter(kParamPanic, 1.0f);
    result = processor.processBlock(128, transport, nullptr, 0);
    require(processor.getParameter(kParamStatusCellStart + cellIndex(4, 4)) == 0.0f,
            "lifeform panic should clear cells");
    require(processor.getParameter(kParamRunning) == 0.0f,
            "lifeform panic should stop the generator");
    require(containsSysexPrefix(result, {0xf0u, 0x00u, 0x20u, 0x29u, 0x02u, 0x0du, 0x0eu, 0x01u}),
            "lifeform panic should send programmer-mode SysEx");
    require(containsLargeClearSysex(result),
            "lifeform panic should send a full Launchpad clear SysEx");
    require(containsMidi(result, 0x90, gridToNote(0, 0), 0),
            "lifeform panic should also send 3-byte LED-off fallback for grid pads");
    require(containsMidi(result, 0xb0, 99, 0),
            "lifeform panic should also send 3-byte LED-off fallback for the logo");

    processor.setParameter(kParamSeed, 1.0f);
    require(processor.getStatus().activeCells >= 5.0f, "lifeform seed control should load an organism");

    return 0;
}
