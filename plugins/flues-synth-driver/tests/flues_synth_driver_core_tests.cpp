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

static bool hasCC(const ProcessResult& r, uint8_t cc, int value = -1)
{
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].size == 3 && (r.events[i].data[0] & 0xF0) == 0xB0
            && r.events[i].data[1] == cc) {
            if (value < 0 || r.events[i].data[2] == static_cast<uint8_t>(value))
                return true;
        }
    }
    return false;
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

    // First block emits the 9 slider CCs for program 0
    const auto r = runBlock(p);
    CHECK(r.eventCount == 9, "first block emits exactly 9 slider CCs");

    // All events should be CC messages on channel 1
    bool allCC = true;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].size != 3 || (r.events[i].data[0] & 0xF0) != 0xB0)
            allCC = false;
    }
    CHECK(allCC, "all emitted events are CCs");

    // Program 0 slider 6 (CC1) = Delay1 FB should be present
    CHECK(hasCC(r, 1), "prog0 slider CC1 (Delay1 FB) in first block");
    // Program 0 slider 7 (CC27) = Attack should be present
    CHECK(hasCC(r, 27), "prog0 slider CC27 (Attack) in first block");
    // Program 0 slider 8 (CC7) = Release should be present
    CHECK(hasCC(r, 7), "prog0 slider CC7 (Release) in first block");
}

static void testParamChangeEmitsCC()
{
    Processor p;
    p.init(44100.0);
    runBlock(p);  // consume initial dirty burst

    // Delay1 FB is slider 6 in program 0 → CC1
    p.setDelay1Fb(0.0f);
    const auto r = runBlock(p);
    CHECK(hasCC(r, 1, 0), "delay1_fb=0 emits CC1=0 in prog0");

    // Second block with no change: no CC1
    const auto r2 = runBlock(p);
    CHECK(!hasCC(r2, 1), "no redundant CC on unchanged param");
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

    // Panic should also emit slider CCs to re-sync flues-synth
    CHECK(r.eventCount > 16, "panic emits slider CCs after all-notes-off");
}

static void testOutputChannel()
{
    Processor p;
    p.init(44100.0);
    p.setOutputChannel(5);

    // First block emits program-0 slider CCs on channel 5
    const auto r = runBlock(p);
    bool anyCCOnCh5 = false;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].size == 3 && (r.events[i].data[0] & 0xF0) == 0xB0
            && (r.events[i].data[0] & 0x0F) == 4)   // ch5 = index 4
            anyCCOnCh5 = true;
    }
    CHECK(anyCCOnCh5, "slider CCs emitted on configured output channel");
}

static void testCCConversionLinear()
{
    Processor p;
    p.init(44100.0);
    runBlock(p);  // consume initial dirty burst

    // Intensity is slider 4 in prog0 (CC74), range 0-1 linear
    p.setIntensity(0.0f);
    const auto r0 = runBlock(p);
    CHECK(hasCC(r0, 74, 0), "intensity=0.0 → CC74=0");

    p.setIntensity(1.0f);
    const auto r1 = runBlock(p);
    CHECK(hasCC(r1, 74, 127), "intensity=1.0 → CC74=127");
}

static void testProgramChange()
{
    Processor p;
    p.init(44100.0);
    runBlock(p);  // consume initial dirty burst

    p.setProgram(3);  // Formant Voice
    const auto r = runBlock(p);

    // Should have emitted a Program Change message on ch1
    bool foundPC = false;
    for (uint32_t i = 0; i < r.eventCount; ++i) {
        if (r.events[i].size == 2 && r.events[i].data[0] == 0xC0
            && r.events[i].data[1] == 3)
            foundPC = true;
    }
    CHECK(foundPC, "setProgram emits PC message");

    // Should have emitted all 9 slider CCs for program 3
    // Program 3: CC73=F1, CC72=F2, CC28=F3, CC30=F4, CC74=Noise,
    //            CC71=Nasal, CC1=TrajJitter, CC27=Attack, CC7=Release
    const uint8_t expected[] = {73, 72, 28, 30, 74, 71, 1, 27, 7};
    for (int s = 0; s < 9; ++s) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "prog3 slider CC%u emitted", expected[s]);
        CHECK(hasCC(r, expected[s]), msg);
    }

    // Program 3's F1 is slider 0 (CC73) — moving it should emit CC73
    runBlock(p);  // flush
    p.setF1(200.0f);  // minimum
    const auto r2 = runBlock(p);
    CHECK(hasCC(r2, 73, 0), "F1=min → CC73=0 in prog3");
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
    testProgramChange();

    std::printf("flues-synth-driver core: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
