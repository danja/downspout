// midiscribe_core_tests.cpp — deterministic unit tests for the Midiscribe
// portable core.  No audio hardware or DPF dependency required.

#include "midiscribe_core.hpp"
#include "midiscribe_params.hpp"
#include "SmfWriter.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace downspout::midiscribe;

// --------------------------------------------------------------------------
// Helpers

static void require(bool condition, const char* msg)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

// Check that buf[offset..offset+len] matches bytes.
static bool hasMagic(const std::vector<uint8_t>& buf, std::size_t offset,
                     const char* bytes, std::size_t len)
{
    if (buf.size() < offset + len) return false;
    return std::memcmp(buf.data() + offset, bytes, len) == 0;
}

// --------------------------------------------------------------------------
// Test 1: captureAndSerialize
// Record a few synthetic note-on/off events at known beat positions, call
// serializeBuffer, verify MThd and MTrk magic bytes and non-zero length.

static void test_captureAndSerialize()
{
    MidiscribeCore core;

    // Arm the plugin.
    Controls c = core.controls();
    c.armed = true;
    core.setControls(c);

    // Inject three events at known beat positions.
    const std::vector<CapturedEvent> events = {
        {0.0,  0x90, 60, 100},   // Note On  ch1 C4 vel=100
        {0.5,  0x80, 60,   0},   // Note Off ch1 C4
        {1.0,  0x90, 64,  80},   // Note On  ch1 E4
    };
    core.injectEvents(events);

    require(core.bufferSize() == 3, "captureAndSerialize: buffer should hold 3 events");

    const auto smf = core.serializeBuffer(120.0);
    require(!smf.empty(), "captureAndSerialize: SMF output must not be empty");

    // MThd at offset 0
    require(hasMagic(smf, 0, "MThd", 4), "captureAndSerialize: MThd magic not found");

    // MTrk must follow after 14-byte MThd chunk (4 tag + 4 len + 6 data)
    require(hasMagic(smf, 14, "MTrk", 4), "captureAndSerialize: MTrk magic not found");

    // Sanity: file should be longer than both headers combined
    require(smf.size() > 22, "captureAndSerialize: SMF output too short");

    printf("PASS: captureAndSerialize\n");
}

// --------------------------------------------------------------------------
// Test 2: passThrough
// Verify that events are returned in outEvents even when not armed.

static void test_passThrough()
{
    MidiscribeCore core;

    // Default: not armed.
    const std::vector<CapturedEvent> inEvents = {
        {0.0, 0x90, 48, 64},
        {1.0, 0x80, 48,  0},
    };

    std::vector<CapturedEvent> outEvents;
    const bool fired = core.processBlock(inEvents, outEvents, 0.0, 120.0, 0.0f);

    require(!fired, "passThrough: write trigger should not have fired");
    require(outEvents.size() == 2, "passThrough: all events must be forwarded");
    require(outEvents[0].data1 == 48, "passThrough: first event note should be 48");
    require(core.bufferSize() == 0,   "passThrough: buffer must be empty when not armed");

    printf("PASS: passThrough\n");
}

// --------------------------------------------------------------------------
// Test 3: bufferRollover
// Fill past captureBeats, verify only the last N beats are retained.

static void test_bufferRollover()
{
    MidiscribeCore core;

    Controls c = core.controls();
    c.armed = true;
    c.captureBeatIndex = 0;   // 8 beats
    core.setControls(c);

    // Inject events spread over 20 beats (only last 8 should survive rollover).
    std::vector<CapturedEvent> batch;
    for (int i = 0; i <= 20; ++i) {
        batch.push_back({static_cast<double>(i), 0x90, static_cast<uint8_t>(60 + i), 80});
    }

    // Drive processBlock at absoluteBeat=20 so the window is [12, 20].
    std::vector<CapturedEvent> out;
    core.processBlock(batch, out, 20.0, 120.0, 0.0f);

    // Events with beatPosition < 12.0 should have been pruned.
    for (const auto& ev : core.buffer()) {
        require(ev.beatPosition >= 12.0,
                "bufferRollover: event before window start survived pruning");
    }

    // At least the events from beat 12..20 (9 events) should be present.
    require(core.bufferSize() >= 9, "bufferRollover: expected at least 9 events in window");

    printf("PASS: bufferRollover\n");
}

// --------------------------------------------------------------------------
// Test 4: writeTriggerEdge
// Verify that write only fires on the 0→1 edge.

static void test_writeTriggerEdge()
{
    MidiscribeCore core;

    Controls c = core.controls();
    c.armed = true;
    core.setControls(c);

    std::vector<CapturedEvent> out;

    // Trigger already high at start of block — should NOT fire (no 0→1).
    c.writeTrigger = 1.0f;
    core.setControls(c);
    float prev = 1.0f;   // previous value was already 1
    bool fired = core.processBlock({}, out, 0.0, 120.0, prev);
    require(!fired, "writeTriggerEdge: should not fire when trigger stays high");

    // Now simulate 0→1 edge.
    prev = 0.0f;
    fired = core.processBlock({}, out, 0.0, 120.0, prev);
    require(fired, "writeTriggerEdge: should fire on 0→1 edge");

    printf("PASS: writeTriggerEdge\n");
}

// --------------------------------------------------------------------------
// Test 5: smfVlqEncoding
// Check that the VLQ encoder round-trips a set of values correctly.

static void test_smfVlqEncoding()
{
    // Spot-check a few VLQ values against known encodings from the SMF spec.
    struct Case { uint32_t value; std::vector<uint8_t> expected; };
    const std::vector<Case> cases = {
        {0x00,      {0x00}},
        {0x40,      {0x40}},
        {0x7F,      {0x7F}},
        {0x80,      {0x81, 0x00}},
        {0x2000,    {0xC0, 0x00}},
        {0x3FFF,    {0xFF, 0x7F}},
        {0x100000,  {0xC0, 0x80, 0x00}},
        {0x1FFFFF,  {0xFF, 0xFF, 0x7F}},
    };

    for (const auto& tc : cases) {
        std::vector<uint8_t> buf;
        writeVlq(buf, tc.value);
        require(buf == tc.expected, "smfVlqEncoding: VLQ mismatch");
    }

    printf("PASS: smfVlqEncoding\n");
}

// --------------------------------------------------------------------------
// Test 6: serializeControls round-trip

static void test_controlsSerialisation()
{
    Controls c {};
    c.armed = true;
    c.writeTrigger = 0.0f;   // trigger not serialised (momentary)
    c.captureBeatIndex = 2;

    const std::string text = serializeControls(c);
    const Controls c2 = deserializeControls(text);

    require(c2.armed            == c.armed,            "controlsSerialisation: armed");
    require(c2.captureBeatIndex == c.captureBeatIndex, "controlsSerialisation: captureBeatIndex");

    printf("PASS: controlsSerialisation\n");
}

// --------------------------------------------------------------------------
// main

int main()
{
    test_captureAndSerialize();
    test_passThrough();
    test_bufferRollover();
    test_writeTriggerEdge();
    test_smfVlqEncoding();
    test_controlsSerialisation();

    printf("\nAll midiscribe core tests passed.\n");
    return 0;
}
