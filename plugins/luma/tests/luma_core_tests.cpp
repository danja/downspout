#include "luma_processor.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
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

bool hasMessage(const downspout::luma::ProcessResult& result,
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

bool hasNoteOn(const downspout::luma::ProcessResult& result)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            (result.events[i].data[0] & 0xf0u) == 0x90u &&
            result.events[i].data[2] > 0u &&
            result.events[i].data[0] != 0x90u)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// ── Session state ───────────────────────────────────────────────────────────

std::array<bool, 64> cellsOf(const downspout::luma::Processor& processor)
{
    std::array<bool, 64> cells {};
    for (std::uint32_t i = 0; i < 64; ++i)
        cells[i] = processor.getParameter(downspout::luma::kParamStatusCellStart + i) > 0.5f;
    return cells;
}

void testStateRoundTripsSettingsAndPattern()
{
    using namespace downspout::luma;

    Processor saved;
    saved.init(48000.0);
    saved.setParameter(kParamRootNote, 55.0f);
    saved.setParameter(kParamScale, 7.0f);
    saved.setParameter(kParamGate, 0.81f);
    saved.setParameter(kParamDensity, 0.12f);
    saved.setParameter(kParamBaseChannel, 9.0f);
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
    require(cellsOf(reopened) == cellsOf(saved), "the live cell pattern should restore exactly");
}

void testStateNeverStoresTriggers()
{
    using namespace downspout::luma;

    Processor processor;
    processor.init(48000.0);
    const std::string text = processor.serializeParameters();
    for (const char* trigger : {"randomize", "clear", "status"})
        require(text.find(trigger) == std::string::npos, "triggers and status must not be persisted");

    // A hand-edited state naming one is refused outright rather than firing it.
    Processor target;
    target.init(48000.0);
    require(!target.deserializeParameters("version=1\nclear=1\n"), "a trigger in state must be rejected");
    require(!target.deserializeParameters("version=1\nrandomize=1\n"), "a trigger in state must be rejected");
}

void testStateRejectsGarbage()
{
    using namespace downspout::luma;

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
    using namespace downspout::luma;

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
    using namespace downspout::luma;
    const auto send = [](Processor& processor, const int status, const int d1, const int d2) {
        MidiMessage m {};
        m.size = 3;
        m.data[0] = static_cast<std::uint8_t>(status);
        m.data[1] = static_cast<std::uint8_t>(d1);
        m.data[2] = static_cast<std::uint8_t>(d2);
        TransportSnapshot transport {};
        return processor.processBlock(64, transport, &m, 1);
    };
    const auto live = [](const Processor& processor) {
        int count = 0;
        for (std::uint32_t i = 0; i < kCellCount; ++i)
            count += processor.getParameter(kParamCellStart + i) >= 0.5f ? 1 : 0;
        return count;
    };

    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamLedFeedback, 0.0f);

    require(processor.getParameter(kParamConductorCh) == 0.0f, "luma conductor channel defaults to off");
    const float density = processor.getParameter(kParamDensity);
    send(processor, 0xbf, 21, 127);
    require(processor.getParameter(kParamDensity) == density, "luma conductor CC ignored while off");

    processor.setParameter(kParamConductorCh, 16.0f);
    send(processor, 0xbf, 21, 127);
    require(processor.getParameter(kParamDensity) == 1.0f, "luma CC 21 sets density");
    send(processor, 0xbf, 21, 0);
    require(processor.getParameter(kParamDensity) == 0.0f, "luma CC 21 spans 0 to 1");
    send(processor, 0xbf, 22, 127);
    require(processor.getParameter(kParamEnergy) == 1.0f, "luma CC 22 sets energy");

    // Other channels and unused CCs are ignored.
    const float energy = processor.getParameter(kParamEnergy);
    send(processor, 0xb0, 22, 0);
    require(processor.getParameter(kParamEnergy) == energy, "luma conductor CC on another channel is ignored");
    send(processor, 0xbf, 23, 0);
    send(processor, 0xbf, 20, 0);
    require(processor.getParameter(kParamEnergy) == energy, "luma CC 20 and 23 are unused");

    // CC 24 scatters only at 127.
    processor.setParameter(kParamClear, 1.0f);
    require(live(processor) == 0, "luma clear empties the grid");
    send(processor, 0xbf, 24, 100);
    require(live(processor) == 0, "luma CC 24 below 127 does nothing");
    for (int i = 0; i < 4 && live(processor) == 0; ++i)
        send(processor, 0xbf, 24, 127);
    require(live(processor) > 0, "luma CC 24 = 127 scatters a new pattern");

    // The channel is clamped, saved, and the setting round-trips.
    processor.setParameter(kParamConductorCh, 99.0f);
    require(processor.getParameter(kParamConductorCh) == 16.0f, "luma conductor channel clamps to 16");
    processor.setParameter(kParamConductorCh, 5.0f);
    Processor restored;
    restored.init(48000.0);
    require(restored.deserializeParameters(processor.serializeParameters()), "luma state with conductor channel loads");
    require(restored.getParameter(kParamConductorCh) == 5.0f, "luma conductor channel survives state");
}

int main()
{
    testConductorCcSet();
    testStateRoundTripsSettingsAndPattern();
    testStateNeverStoresTriggers();
    testStateRejectsGarbage();
    testHostActivationKeepsThePatch();
    using downspout::luma::MidiMessage;
    using downspout::luma::Processor;
    using downspout::luma::TransportSnapshot;
    using downspout::luma::gridToNote;
    using downspout::luma::kParamCellStart;
    using downspout::luma::kParamClear;
    using downspout::luma::kParamDensity;
    using downspout::luma::kParamEnergy;
    using downspout::luma::kParamLedFeedback;
    using downspout::luma::kParamPassInput;
    using downspout::luma::kParamStatusCellStart;
    using downspout::luma::kParamStatusActive;

    Processor processor;
    processor.init(48000.0);

    require(processor.getParameter(kParamStatusActive) == 0.0f, "luma should start with no active pads");

    MidiMessage pad {};
    pad.size = 3;
    pad.data[0] = 0x90;
    pad.data[1] = gridToNote(0, 0);
    pad.data[2] = 127;

    TransportSnapshot stopped {};
    auto result = processor.processBlock(64, stopped, &pad, 1);
    require(processor.getParameter(kParamCellStart) == 1.0f, "luma Launchpad pad should toggle a cell");
    require(processor.getParameter(kParamStatusCellStart) == 1.0f, "luma status pad mirror should track hardware toggles");
    require(processor.getParameter(kParamStatusActive) == 1.0f, "luma active pad status should update");
    require(hasMessage(result, 0x90, gridToNote(0, 0), downspout::luma::kLedBlue),
            "luma LED feedback should light the toggled bass pad");

    processor.setParameter(kParamDensity, 1.0f);
    processor.setParameter(kParamEnergy, 1.0f);

    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;
    result = processor.processBlock(4096, transport, nullptr, 0);
    require(hasNoteOn(result), "luma active cell should emit MIDI while transport runs");

    processor.setParameter(kParamClear, 1.0f);
    require(processor.getParameter(kParamStatusActive) == 0.0f, "luma clear should remove active pads");

    processor.setParameter(kParamLedFeedback, 0.0f);
    result = processor.processBlock(64, stopped, nullptr, 0);
    require(result.eventCount == 0, "luma LED disabled should suppress idle LED output");

    MidiMessage foreignNote {};
    foreignNote.size = 3;
    foreignNote.data[0] = 0x99;
    foreignNote.data[1] = 100;
    foreignNote.data[2] = 64;
    result = processor.processBlock(64, stopped, &foreignNote, 1);
    require(!hasMessage(result, 0x99, 100, 64), "luma should block unhandled input MIDI by default");

    processor.setParameter(kParamPassInput, 1.0f);
    result = processor.processBlock(64, stopped, &foreignNote, 1);
    require(hasMessage(result, 0x99, 100, 64), "luma pass input switch should forward unhandled MIDI");

    return 0;
}
