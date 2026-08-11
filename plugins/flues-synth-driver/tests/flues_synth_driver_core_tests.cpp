#include "flues_synth_driver_processor.hpp"

#include <cassert>
#include <cstdio>

using namespace downspout::flues_synth_driver;

static int passed = 0, failed = 0;

#define CHECK(cond, msg) \
    do { if (cond) { ++passed; } else { ++failed; std::fprintf(stderr, "FAIL: %s\n", msg); } } while(0)

// ── Helpers ───────────────────────────────────────────────────────────────────

static ProcessResult runBlock(Processor& proc, const MidiMessage* in = nullptr, uint32_t inCount = 0)
{
    return proc.processBlock(512, in, inCount);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void testInit()
{
    Processor p;
    p.init(44100.0);

    // Default params should match spec
    CHECK(p.getSynthParam(kParamAlgorithm)    == 0.0f,  "default algorithm");
    CHECK(p.getSynthParam(kParamInterfaceType)== 2.0f,  "default interface type (Reed)");
    CHECK(p.getSynthParam(kParamGain)         == 0.5f,  "default gain");
    CHECK(p.getSynthParam(kParamAttack)       == 0.01f, "default attack");
}

static void testDirtyEmitsCCs()
{
    Processor p;
    p.init(44100.0);

    // First block should emit all default CCs (all dirty after activate)
    const auto r = runBlock(p);
    // Must have emitted exactly the non-trajectory synth params (trajectory skipped on init)
    const std::size_t trajStart = kParamTrajSides - kParamAlgorithm;
    CHECK(r.eventCount >= trajStart, "first block emits non-trajectory default CCs");

    // All events should be CC messages on channel 1
    bool allCC = true;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].size != 3 || (r.events[i].data[0] & 0xF0) != 0xB0)
            allCC = false;
    }
    CHECK(allCC, "all emitted events are CCs");
}

static void testParamChangeEmitsCC()
{
    Processor p;
    p.init(44100.0);
    runBlock(p);  // consume initial dirty burst

    p.setGain(0.0f);
    const auto r = runBlock(p);

    bool foundGainCC = false;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].data[1] == 7 && r.events[i].data[2] == 0)
            foundGainCC = true;
    }
    CHECK(foundGainCC, "gain=0 emits CC7=0");

    // Second block with no change: no CC for gain
    const auto r2 = runBlock(p);
    bool foundAgain = false;
    for (uint32_t i = 0; i < r2.eventCount; ++i) {
        if (r2.events[i].data[1] == 7)
            foundAgain = true;
    }
    CHECK(!foundAgain, "no redundant CC on unchanged param");
}

static void testPassInput()
{
    Processor p;
    p.init(44100.0);
    p.setPassInput(true);
    runBlock(p);

    // Send a note-on
    MidiMessage noteOn {};
    noteOn.frame   = 0;
    noteOn.size    = 3;
    noteOn.data[0] = 0x90;
    noteOn.data[1] = 60;
    noteOn.data[2] = 100;

    const auto r = runBlock(p, &noteOn, 1);
    bool forwarded = false;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].data[0] == 0x90 && r.events[i].data[1] == 60)
            forwarded = true;
    }
    CHECK(forwarded, "note-on forwarded when pass input on");
}

static void testBlockInput()
{
    Processor p;
    p.init(44100.0);
    p.setPassInput(false);
    runBlock(p);

    MidiMessage noteOn {};
    noteOn.frame = 0; noteOn.size = 3;
    noteOn.data[0] = 0x90; noteOn.data[1] = 61; noteOn.data[2] = 80;

    const auto r = runBlock(p, &noteOn, 1);
    bool forwarded = false;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].data[0] == 0x90 && r.events[i].data[1] == 61)
            forwarded = true;
    }
    CHECK(!forwarded, "note-on blocked when pass input off");
}

static void testConductorChannelFiltered()
{
    Processor p;
    p.init(44100.0);
    p.setConductorCh(3);
    runBlock(p);

    // Conductor Density CC (21) on ch3 should NOT be forwarded
    MidiMessage cc {};
    cc.frame = 0; cc.size = 3;
    cc.data[0] = 0xB2;  // CC on channel 3
    cc.data[1] = 21;    // Density CC
    cc.data[2] = 64;

    const auto r = runBlock(p, &cc, 1);
    bool conductorForwarded = false;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].data[0] == 0xB2 && r.events[i].data[1] == 21)
            conductorForwarded = true;
    }
    CHECK(!conductorForwarded, "conductor CC not forwarded downstream");

    // Should have updated Disyn P1 (param changed → dirty)
    const float p1 = p.getSynthParam(kParamDisynP1);
    CHECK(p1 >= 0.49f && p1 <= 0.51f, "conductor density CC updates disyn P1");
}

static void testPanic()
{
    Processor p;
    p.init(44100.0);
    runBlock(p);

    p.triggerPanic();
    const auto r = runBlock(p);

    // Should contain all-notes-off CCs (CC123) on all 16 channels
    int allNotesOffCount = 0;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if ((r.events[i].data[0] & 0xF0) == 0xB0 && r.events[i].data[1] == 123)
            ++allNotesOffCount;
    }
    CHECK(allNotesOffCount == 16, "panic sends all-notes-off on all 16 channels");
}

static void testOutputChannel()
{
    Processor p;
    p.init(44100.0);
    p.setOutputChannel(5);
    p.setGain(1.0f);

    const auto r = runBlock(p);
    bool onCh5 = false;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].data[1] == 7 && (r.events[i].data[0] & 0x0F) == 4)
            onCh5 = true;
    }
    CHECK(onCh5, "CCs emitted on configured output channel");
}

static void testCCConversionLinear()
{
    Processor p;
    p.init(44100.0);
    runBlock(p);

    p.setGain(0.0f);
    const auto r0 = runBlock(p);
    bool cc7_0 = false;
    for (uint32_t i = 0; i < r0.eventCount; ++i)
        if (r0.events[i].data[1] == 7 && r0.events[i].data[2] == 0) cc7_0 = true;
    CHECK(cc7_0, "gain=0.0 → CC7=0");

    p.setGain(1.0f);
    const auto r1 = runBlock(p);
    bool cc7_127 = false;
    for (uint32_t i = 0; i < r1.eventCount; ++i)
        if (r1.events[i].data[1] == 7 && r1.events[i].data[2] == 127) cc7_127 = true;
    CHECK(cc7_127, "gain=1.0 → CC7=127");
}

// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    testInit();
    testDirtyEmitsCCs();
    testParamChangeEmitsCC();
    testPassInput();
    testBlockInput();
    testConductorChannelFiltered();
    testPanic();
    testOutputChannel();
    testCCConversionLinear();

    std::printf("flues-synth-driver core: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
