#include "ghost_core.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

static int gPassed = 0;
static int gFailed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        ++gPassed;
    } else {
        std::printf("FAIL: %s\n", name);
        ++gFailed;
    }
}

using namespace downspout::ghost;
using downspout::generative::absoluteQuarter;

static Transport playing(double bar = 0.0, double barBeat = 0.0, double bpm = 120.0)
{
    Transport t;
    t.valid = true;
    t.playing = true;
    t.bar = bar;
    t.barBeat = barBeat;
    t.beatsPerBar = 4.0;
    t.beatType = 4.0;
    t.bpm = bpm;
    return t;
}

struct Block {
    static constexpr std::uint32_t kFrames = 512;
    std::array<float, kFrames> inL {}, inR {}, outL {}, outR {};
};

static MidiBlock run(Block& b, EngineState& s, const Parameters& p, const Transport& t,
                     double sr = 48000.0)
{
    const float* ins[] = { b.inL.data(), b.inR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    return processBlock(s, p, t, Block::kFrames, sr, ins, outs, nullptr, 0,
                        p.sensitivity, p.density, p.velocity, p.drag);
}

static std::uint32_t countNoteOns(const MidiBlock& block)
{
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < block.count; ++i)
        if (block.events[i].size >= 3 && (block.events[i].data[0] & 0xF0) == 0x90
            && block.events[i].data[2] > 0)
            ++n;
    return n;
}

// Silence in, transport running -> no ghosts, silence out.
static void testSilenceNoGhosts()
{
    Block b;
    EngineState s;
    resetState(s);
    Parameters p;
    const MidiBlock out = run(b, s, p, playing());
    check("silence emits no note-ons", countNoteOns(out) == 0);
    bool silent = true;
    for (std::uint32_t i = 0; i < Block::kFrames && silent; ++i)
        silent = b.outL[i] == 0.0f && b.outR[i] == 0.0f;
    check("silence in gives silence out", silent);
}

// Audio output is silent by default, even with loud input.
static void testAudioThruDefaultOff()
{
    Block b;
    for (auto& v : b.inL) v = 0.5f;
    for (auto& v : b.inR) v = -0.5f;
    EngineState s;
    resetState(s);
    Parameters p;
    run(b, s, p, playing());
    bool silent = true;
    for (std::uint32_t i = 0; i < Block::kFrames && silent; ++i)
        silent = b.outL[i] == 0.0f && b.outR[i] == 0.0f;
    check("audio thru defaults to silent output", silent);
}

// Audio Thru on passes input audio to the output.
static void testAudioThruOn()
{
    Block b;
    for (std::uint32_t i = 0; i < Block::kFrames; ++i)
        b.inL[i] = b.inR[i] = 0.3f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 32.0f);
    EngineState s;
    resetState(s);
    Parameters p;
    p.audioThru = 1.0f;
    run(b, s, p, playing());
    bool same = true;
    for (std::uint32_t i = 0; i < Block::kFrames && same; ++i)
        same = b.outL[i] == b.inL[i] && b.outR[i] == b.inR[i];
    check("audio thru passes input when on", same);
}

// Stopped transport -> never emits, even on loud impulses.
static void testStoppedTransportSilent()
{
    Block b;
    for (auto& v : b.inL) v = 0.9f;
    for (auto& v : b.inR) v = 0.9f;
    EngineState s;
    resetState(s);
    Parameters p;
    Transport t = playing();
    t.playing = false;
    const MidiBlock out = run(b, s, p, t);
    check("stopped transport emits no note-ons", countNoteOns(out) == 0);
}

// Invalid transport -> never emits.
static void testInvalidTransportSilent()
{
    Block b;
    for (auto& v : b.inL) v = 0.9f;
    EngineState s;
    resetState(s);
    Parameters p;
    Transport t;
    const MidiBlock out = run(b, s, p, t);
    check("invalid transport emits no note-ons", countNoteOns(out) == 0);
}

// Pump blocks with advancing BBT (a 16th slot is ~6000 frames at 120 bpm,
// so quantised ghosts land several blocks after the trigger).
static std::uint32_t pump(EngineState& s, Block& b, const Parameters& p, Transport t,
                          int blocks, double sr = 48000.0)
{
    const double dq = Block::kFrames * (t.bpm / 60.0 / sr);
    std::uint32_t total = 0;
    for (int k = 0; k < blocks; ++k) {
        total += countNoteOns(run(b, s, p, t, sr));
        t.barBeat += dq;
    }
    return total;
}

// A loud impulse while running -> onset accent ghosts appear.
static void testImpulseTriggersGhost()
{
    Block b;
    b.inL[4] = 1.0f;
    b.inR[4] = 1.0f;
    EngineState s;
    resetState(s);
    Parameters p;
    p.density = 0.0f;  // isolate the onset path
    check("impulse triggers onset ghost", pump(s, b, p, playing(), 32) > 0);
}

// Drums mode emits on channel 10 with GM ghost voices.
static void testDrumsChannelTen()
{
    Block b;
    b.inL[4] = 1.0f;
    b.inR[4] = 1.0f;
    EngineState s;
    resetState(s);
    Parameters p;
    p.density = 0.0f;
    Transport t = playing();
    const double dq = Block::kFrames * (t.bpm / 60.0 / 48000.0);
    bool sawCh10 = false, sawVoice = false;
    for (int k = 0; k < 32 && !(sawCh10 && sawVoice); ++k) {
        const MidiBlock out = run(b, s, p, t);
        for (std::uint32_t i = 0; i < out.count; ++i) {
            if (out.events[i].size >= 3 && (out.events[i].data[0] & 0xF0) == 0x90
                && out.events[i].data[2] > 0) {
                if ((out.events[i].data[0] & 0x0F) == 9) sawCh10 = true;
                if (out.events[i].data[1] == 36 || out.events[i].data[1] == 38) sawVoice = true;
            }
        }
        t.barBeat += dq;
    }
    check("drums mode emits on ch 10", sawCh10);
    check("drums mode uses 36/38 voices", sawVoice);
}

// Notes mode honours the channel parameter.
static void testNotesModeChannel()
{
    Block b;
    b.inL[4] = 1.0f;
    b.inR[4] = 1.0f;
    EngineState s;
    resetState(s);
    Parameters p;
    p.density = 0.0f;
    p.mode = 1.0f;
    p.channel = 3.0f;
    p.baseNote = 60.0f;
    Transport t = playing();
    const double dq = Block::kFrames * (t.bpm / 60.0 / 48000.0);
    bool sawCh3 = false;
    for (int k = 0; k < 32 && !sawCh3; ++k) {
        const MidiBlock out = run(b, s, p, t);
        for (std::uint32_t i = 0; i < out.count; ++i) {
            if (out.events[i].size >= 3 && (out.events[i].data[0] & 0xF0) == 0x90
                && out.events[i].data[2] > 0 && (out.events[i].data[0] & 0x0F) == 2)
                sawCh3 = true;
        }
        t.barBeat += dq;
    }
    check("notes mode emits on param channel", sawCh3);
}

// Density 0 vs 1 on sustained audio: fills appear only with density.
static void testDensityGate()
{
    auto runSustained = [](float density) {
        Block b;
        for (auto& v : b.inL) v = 0.4f;
        for (auto& v : b.inR) v = 0.4f;
        EngineState s;
        resetState(s);
        Parameters p;
        p.sensitivity = 0.0f;  // desensitise onsets (threshold max)
        p.density = density;
        Transport t = playing();
        pump(s, b, p, t, 48);  // settle envelope + flush the settle onset
        const double dq = Block::kFrames * (t.bpm / 60.0 / 48000.0);
        t.barBeat += dq * 48;
        return pump(s, b, p, t, 32);
    };
    const std::uint32_t none = runSustained(0.0f);
    const std::uint32_t full = runSustained(1.0f);
    check("density 0 emits no fills", none == 0);
    check("density 1 emits fills", full > 0);
}

// Determinism: identical input twice -> identical MIDI bytes.
static void testDeterminism()
{
    auto runOnce = []() {
        Block b;
        for (std::uint32_t i = 0; i < Block::kFrames; ++i)
            b.inL[i] = b.inR[i] = 0.3f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 32.0f);
        b.inL[4] = 1.0f;
        EngineState s;
        resetState(s);
        Parameters p;
        Transport t = playing();
        MidiBlock first = run(b, s, p, t);
        t.barBeat += 0.5;
        MidiBlock second = run(b, s, p, t);
        return std::array<MidiBlock, 2> { first, second };
    };
    const auto a = runOnce();
    const auto b = runOnce();
    bool same = a[0].count == b[0].count && a[1].count == b[1].count;
    for (int k = 0; k < 2 && same; ++k)
        for (std::uint32_t i = 0; i < a[k].count && same; ++i)
            same = a[k].events[i].frame == b[k].events[i].frame
                && a[k].events[i].size == b[k].events[i].size
                && a[k].events[i].data == b[k].events[i].data;
    check("identical input gives identical MIDI", same);
}

// Loop jump backwards does not blow up the per-block emission cap.
static void testLoopJumpBounded()
{
    Block b;
    for (auto& v : b.inL) v = 0.5f;
    for (auto& v : b.inR) v = 0.5f;
    EngineState s;
    resetState(s);
    Parameters p;
    p.density = 1.0f;
    Transport t = playing(4.0, 0.0);
    run(b, s, p, t);
    t.bar = 0.0;  // loop back to the top
    t.barBeat = 0.0;
    const MidiBlock out = run(b, s, p, t);
    check("loop jump stays bounded", countNoteOns(out) <= 8);
}

// Non-finite audio is sanitised, counted, and never emitted as sound.
static void testNonFiniteSanitised()
{
    Block b;
    b.inL[0] = std::numeric_limits<float>::infinity();
    b.inR[1] = std::numeric_limits<float>::quiet_NaN();
    EngineState s;
    resetState(s);
    Parameters p;
    p.audioThru = 1.0f;  // thru on: sanitisation is what keeps outputs finite
    const MidiBlock out = run(b, s, p, playing());
    (void)out;
    check("non-finite inputs counted", s.faults == 2);
    check("non-finite outputs sanitised", std::isfinite(b.outL[0]) && std::isfinite(b.outR[1]));
    check("sanitised outputs are zero", b.outL[0] == 0.0f && b.outR[1] == 0.0f);
}

static void testClamp()
{
    Parameters extreme;
    extreme.sensitivity = 5.0f;
    extreme.density = -1.0f;
    extreme.velocity = 500.0f;
    extreme.drag = -2.0f;
    extreme.mode = 7.0f;
    extreme.channel = 99.0f;
    extreme.baseNote = -4.0f;
    extreme.audioThru = 7.0f;
    extreme.seed = 0.0f;
    extreme.ccChannel = 0.0f;
    const Parameters c = clampParameters(extreme);
    check("clamp sensitivity", c.sensitivity <= 1.0f);
    check("clamp density", c.density >= 0.0f);
    check("clamp velocity", c.velocity <= 127.0f);
    check("clamp drag", c.drag >= 0.0f);
    check("clamp mode", c.mode <= 1.0f);
    check("clamp channel", c.channel <= 16.0f);
    check("clamp baseNote", c.baseNote >= 0.0f);
    check("clamp audioThru", c.audioThru <= 1.0f);
    check("clamp seed", c.seed >= 1.0f);
    check("clamp ccChannel", c.ccChannel >= 1.0f);
}

static void testSerialization()
{
    Parameters p;
    p.sensitivity = 0.7f;
    p.density = 0.5f;
    p.velocity = 100.0f;
    p.drag = 0.3f;
    p.mode = 1.0f;
    p.channel = 5.0f;
    p.baseNote = 60.0f;
    p.audioThru = 1.0f;
    p.seed = 42.0f;
    const std::string text = serializeParameters(p);
    const auto restored = deserializeParameters(text);
    check("serialization round-trip valid", restored.has_value());
    if (restored.has_value()) {
        const float eps = 1e-4f;
        check("rt sensitivity", std::abs(restored->sensitivity - p.sensitivity) < eps);
        check("rt density", std::abs(restored->density - p.density) < eps);
        check("rt velocity", std::abs(restored->velocity - p.velocity) < eps);
        check("rt drag", std::abs(restored->drag - p.drag) < eps);
        check("rt mode", restored->mode == p.mode);
        check("rt channel", restored->channel == p.channel);
        check("rt baseNote", restored->baseNote == p.baseNote);
        check("rt audioThru", restored->audioThru == p.audioThru);
        check("rt seed", restored->seed == p.seed);
    }
    check("garbage deserialises to nullopt", !deserializeParameters("nonsense\n").has_value());
}

int main()
{
    testSilenceNoGhosts();
    testAudioThruDefaultOff();
    testAudioThruOn();
    testStoppedTransportSilent();
    testInvalidTransportSilent();
    testImpulseTriggersGhost();
    testDrumsChannelTen();
    testNotesModeChannel();
    testDensityGate();
    testDeterminism();
    testLoopJumpBounded();
    testNonFiniteSanitised();
    testClamp();
    testSerialization();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
