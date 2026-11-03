#include "downspout/test_assert.h"
#include "spindown_core.hpp"
#include "spindown_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::spindown;

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

constexpr float kSr = 48000.0f;
constexpr float kRampStep = 1.0e-4f;   // the test input: x[n] = (n + 1) * kRampStep, so slope = tape speed * kRampStep

struct Result {
    std::vector<float> in, out;   // left channel
    std::vector<float> outR;
    EngineState state;
};

struct TimedEvent { std::uint32_t frame; MidiEvent ev; };

static TimedEvent noteOn(std::uint32_t frame, std::uint8_t note = 60) { return {frame, {0, 0x90, note, 100}}; }
static TimedEvent noteOff(std::uint32_t frame, std::uint8_t note = 60) { return {frame, {0, 0x80, note, 0}}; }
static TimedEvent cc(std::uint32_t frame, std::uint8_t number, std::uint8_t value, std::uint8_t channel = 0)
{
    return {frame, {0, static_cast<std::uint8_t>(0xB0 | channel), number, value}};
}

// Renders `seconds` of the ramp input in blocks. The transport advances with the blocks.
static Result render(const Parameters& p, float seconds, std::vector<TimedEvent> events = {}, bool playing = true, double bpm = 120.0,
                     double startBeat = 0.0, std::uint32_t block = 128, float sr = kSr, Result* continueFrom = nullptr)
{
    Result res;
    if (continueFrom) res.state = std::move(continueFrom->state);
    else prepare(res.state, sr);
    const auto total = static_cast<std::size_t>(seconds * sr);
    res.in.resize(total);
    res.out.resize(total);
    res.outR.resize(total);
    for (std::size_t n = 0; n < total; ++n) res.in[n] = static_cast<float>(n + 1) * kRampStep;
    for (std::size_t pos = 0; pos < total; pos += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, total - pos));
        Transport t;
        t.playing = playing; t.valid = true; t.bpm = bpm; t.beatsPerBar = 4.0;
        t.beatPosition = startBeat + static_cast<double>(pos) / sr * bpm / 60.0;
        std::vector<MidiEvent> here;
        for (const auto& te : events)
            if (te.frame >= pos && te.frame < pos + n) { MidiEvent e = te.ev; e.frame = static_cast<std::uint32_t>(te.frame - pos); here.push_back(e); }
        const float* ins[2] = {res.in.data() + pos, res.in.data() + pos};
        float* outs[2] = {res.out.data() + pos, res.outR.data() + pos};
        processBlock(res.state, p, sr, n, t, here.data(), static_cast<std::uint32_t>(here.size()), ins, outs);
    }
    return res;
}

// Mean tape speed over [start, start + len) samples, read from the slope of the ramp.
static double slope(const Result& r, std::size_t start, std::size_t len)
{
    return static_cast<double>(r.out[start + len] - r.out[start]) / (static_cast<double>(len) * static_cast<double>(kRampStep));
}

// Independent reference: mean of (1 - t/T)^curve over a window, by brute-force sampling.
static double expectedMeanSpeed(double t0, double t1, double stopSeconds, double curve)
{
    double sum = 0.0;
    constexpr int kN = 2000;
    for (int i = 0; i < kN; ++i) {
        const double t = t0 + (t1 - t0) * (i + 0.5) / kN;
        sum += std::pow(std::max(0.0, 1.0 - t / stopSeconds), curve);
    }
    return sum / kN;
}

static Parameters manualStop()
{
    Parameters p;
    p.stop = 1.0f;
    p.stopMs = 1000.0f;
    p.curve = 1.0f;
    return p;
}

static bool exactlyEqual(const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i < to; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// The defaults leave the signal alone: the output is the input, sample for sample, at any block size.
static void testDefaultsAreInert()
{
    const Parameters p;
    check("default Stop is off", p.stop == 0.0f && p.trigger == 0.0f);
    for (const std::uint32_t block : {1u, 64u, 127u, 1024u}) {
        const Result r = render(p, 0.5f, {}, true, 120.0, 0.0, block);
        check("idle output equals input", exactlyEqual(r.in, r.out, 0, r.in.size()));
        check("idle right channel equals input", exactlyEqual(r.in, r.outR, 0, r.in.size()));
    }
    check("idle meters", render(p, 0.1f).state.meters.speed == 1.0f);
}

// The tape speed follows (1 - t/T)^curve: read from the slope of a ramp, against a brute-force average of the formula.
static void testSpeedFollowsTheCurve()
{
    for (const float curve : {1.0f, 2.0f, 0.6f, 3.5f}) {
        Parameters p = manualStop();
        p.stopMs = 1000.0f;
        p.curve = curve;
        const Result r = render(p, 1.0f);
        bool ok = true;
        for (const double t : {0.05, 0.2, 0.4, 0.6}) {
            const auto start = static_cast<std::size_t>(t * kSr);
            const std::size_t len = 480;
            const double want = expectedMeanSpeed(t, t + 480.0 / kSr, 1.0, curve);
            if (want < 0.12) continue;   // the fade-out near standstill is checked separately
            if (std::fabs(slope(r, start, len) - want) > 0.02) ok = false;
        }
        check("speed follows (1 - t/T)^curve", ok);
    }
}

// Curve and stop time are audible: a steeper curve is slower at the midpoint, a longer stop time is faster at a given time.
static void testControlsHaveAnEffect()
{
    Parameters a = manualStop(); a.curve = 1.0f;
    Parameters b = manualStop(); b.curve = 3.0f;
    const double sa = slope(render(a, 0.6f), static_cast<std::size_t>(0.5 * kSr), 480);
    const double sb = slope(render(b, 0.6f), static_cast<std::size_t>(0.5 * kSr), 480);
    check("curve changes the speed at the midpoint", sa > sb + 0.2);
    Parameters c = manualStop(); c.stopMs = 2000.0f;
    const double sc = slope(render(c, 0.6f), static_cast<std::size_t>(0.5 * kSr), 480);
    check("a longer Stop Time is still faster at 0.5 s", sc > sa + 0.15);

    // Start Time: restart from standstill and compare the speed 100 ms after release.
    Parameters s1 = manualStop(); s1.stopMs = 200.0f;
    Parameters s2 = s1;
    s1.startMs = 200.0f; s2.startMs = 1000.0f;
    std::vector<TimedEvent> release = {cc(static_cast<std::uint32_t>(0.5 * kSr), kCcStop, 0)};
    s1.ccChannel = 1.0f; s2.ccChannel = 1.0f;
    const auto at = static_cast<std::size_t>(0.6 * kSr);
    const double v1 = slope(render(s1, 0.75f, release), at, 480);
    const double v2 = slope(render(s2, 0.75f, release), at, 480);
    check("a shorter Start Time spins up faster", v1 > v2 + 0.15);
}

// Standing still is silent, with no DC step left behind.
static void testStandstillIsSilent()
{
    const Result r = render(manualStop(), 1.5f);
    bool quiet = true;
    for (std::size_t n = static_cast<std::size_t>(1.1 * kSr); n < r.out.size(); ++n)
        if (std::fabs(r.out[n]) > 1.0e-4f) quiet = false;
    check("silent at standstill", quiet);
    bool finite = true;
    for (float v : r.out) if (!std::isfinite(v)) finite = false;
    check("output stays finite", finite);
}

// The head lags by the time it spent slow: stopping linearly over T leaves T/2 of lag (independent integral).
static void testLagAtStandstill()
{
    for (const float sr : {44100.0f, 48000.0f, 96000.0f}) {
        Parameters p = manualStop();
        p.stopMs = 500.0f;
        p.curve = 1.0f;
        const Result r = render(p, 0.5f, {}, true, 120.0, 0.0, 128, sr);
        check("lag at standstill is half the stop time", std::fabs(r.state.meters.delayMs - 250.0f) < 2.5f);
        check("speed is zero at standstill", r.state.meters.speed < 0.01f);
    }
}

// After the release the tape spins up and the output is the live input again, exactly.
static void testReturnsToLive()
{
    Parameters p = manualStop();
    p.stopMs = 400.0f;
    p.startMs = 300.0f;
    const auto release = static_cast<std::uint32_t>(0.8 * kSr);
    const Result r = render(p, 2.0f, {cc(release, kCcStop, 0)});
    const auto settled = static_cast<std::size_t>((0.8 + 0.3 + 0.05) * kSr);
    check("live again after spin-up", exactlyEqual(r.in, r.out, settled, r.in.size()));
    check("lag is cleared", r.state.meters.delayMs == 0.0f && r.state.meters.speed == 1.0f);
    // While spinning up the output lags the input.
    check("lagging during spin-up", r.out[static_cast<std::size_t>(0.95 * kSr)] < r.in[static_cast<std::size_t>(0.95 * kSr)] - 0.1f);
}

// Releasing early restarts from the speed reached, not from standstill, and stopping again continues smoothly.
static void testInterruptedStopIsContinuous()
{
    Parameters p = manualStop();
    p.stopMs = 1000.0f; p.startMs = 1000.0f; p.curve = 1.0f;
    const auto release = static_cast<std::uint32_t>(0.4 * kSr);
    const auto again = static_cast<std::uint32_t>(0.6 * kSr);
    const Result r = render(p, 1.0f, {cc(release, kCcStop, 0), cc(again, kCcStop, 127)});
    // Progress 0.4 at the release, back to 0.2 after 0.2 s, then forwards again: speeds 0.6, 0.8, 0.8.
    check("speed just before release", std::fabs(slope(r, release - 480, 480) - (1.0 - 0.4 + 0.005)) < 0.03);
    check("speed rises after release", slope(r, release + 100, 480) > slope(r, release - 480, 480));
    check("speed at the second stop", std::fabs(slope(r, again - 480, 480) - 0.8) < 0.03);
    check("falls again after the second stop", slope(r, again + 2400, 480) < 0.75);
    double worst = 0.0;
    for (std::size_t n = release - 200; n + 1 < again + 2000; ++n) worst = std::max(worst, static_cast<double>(std::fabs(r.out[n + 1] - r.out[n] - kRampStep * 1.0f)));
    check("no jumps in the output while the transport changes", worst < static_cast<double>(kRampStep) * 1.05);
}

// A stop longer than the buffer cannot overrun it, and the tape still comes back.
static void testVeryLongStop()
{
    Parameters p = manualStop();
    p.stopMs = 20.0f; p.startMs = 100.0f;
    const auto release = static_cast<std::uint32_t>(14.0 * kSr);
    const Result r = render(p, 15.0f, {cc(release, kCcStop, 0)}, true, 120.0, 0.0, 512);
    bool finite = true;
    for (float v : r.out) if (!std::isfinite(v)) finite = false;
    check("finite over a stop longer than the buffer", finite);
    check("lag is capped at the buffer", r.state.meters.delayMs <= kBufferSeconds * 1000.0f + 1.0f);
}

// Not prepared: the effect passes the input through and does not touch memory.
static void testUnpreparedPassesThrough()
{
    EngineState s;
    std::vector<float> in(256, 0.5f), out(256, 0.0f);
    const float* ins[2] = {in.data(), in.data()};
    float* outs[2] = {out.data(), out.data()};
    Transport t;
    processBlock(s, manualStop(), kSr, 256, t, nullptr, 0, ins, outs);
    check("unprepared passes through", exactlyEqual(in, out, 0, 256));
}

// MIDI trigger: held notes stop the tape, and the first sample affected is the note-on's own frame.
static void testMidiTrigger()
{
    Parameters p;
    p.trigger = 1.0f;
    p.stopMs = 500.0f;
    p.startMs = 200.0f;
    const std::uint32_t on = 12345, off = static_cast<std::uint32_t>(1.2 * kSr);
    const Result r = render(p, 2.0f, {noteOn(on), noteOn(on + 10, 64), noteOff(off), noteOff(off + 10, 64)}, true, 120.0, 0.0, 100);
    check("untouched before the note", exactlyEqual(r.in, r.out, 0, on));
    // Lag after 40 ms of a 500 ms stop with curve 1.5 is 1.5 t^2 / (2 * 0.5) = 2.4 ms = 115 samples = 0.0115 of ramp.
    check("changed within 40 ms of the note", std::fabs(r.out[on + 1920] - r.in[on + 1920]) > 5.0e-3f);
    check("slowed during the note", slope(r, on + 4800, 480) < 0.9);
    check("live again after the notes end", exactlyEqual(r.in, r.out, static_cast<std::size_t>(1.2 * kSr + 0.2 * kSr + 0.05 * kSr + 1000), r.in.size()));

    // One of two notes released: the tape stays stopped.
    const Result q = render(p, 1.5f, {noteOn(100), noteOn(110, 64), noteOff(200, 60)}, true, 120.0, 0.0, 64);
    check("tape stays stopped while a note is held", q.state.meters.speed < 0.01f);

    // All-notes-off clears hanging notes.
    const Result z = render(p, 2.0f, {noteOn(100), cc(static_cast<std::uint32_t>(0.8 * kSr), 123, 0)});
    check("all-notes-off releases the tape", z.state.meters.speed == 1.0f);

    // A note does nothing in Manual mode.
    Parameters m;
    check("notes are ignored in Manual mode", exactlyEqual(render(m, 0.5f, {noteOn(100)}).in, render(m, 0.5f, {noteOn(100)}).out, 0, 24000));
}

// Cycle trigger: the tape stops during the last `length` beats of each cycle. Expected values are written out by hand.
static void testCycleSchedule()
{
    Parameters p;
    p.trigger = 2.0f;
    p.cycle = 0.0f;     // 1 bar of 4 beats
    p.length = 1.0f;
    check("beat 0 plays", !cycleStopped(p, 0.0, 4.0));
    check("beat 2.99 plays", !cycleStopped(p, 2.99, 4.0));
    check("beat 3 stops", cycleStopped(p, 3.0, 4.0));
    check("beat 3.99 stops", cycleStopped(p, 3.99, 4.0));
    check("the next bar plays", !cycleStopped(p, 4.0, 4.0));
    check("bar 100 beat 3.5 stops", cycleStopped(p, 403.5, 4.0));
    p.cycle = 2.0f;     // 4 bars
    p.length = 2.0f;
    check("4-bar cycle plays at beat 13", !cycleStopped(p, 13.0, 4.0));
    check("4-bar cycle stops at beat 14", cycleStopped(p, 14.0, 4.0));
    check("4-bar cycle plays at beat 16", !cycleStopped(p, 16.0, 4.0));
    p.length = 16.0f;   // longer than the cycle is capped at the cycle
    check("a length of a full cycle or more is always stopped", cycleStopped(p, 0.0, 4.0) && cycleStopped(p, 15.99, 4.0));
    p.cycle = 0.0f; p.length = 1.0f;
    check("3/4 time: the last beat of a 3-beat bar", cycleStopped(p, 2.0, 3.0) && !cycleStopped(p, 1.99, 3.0));
    check("unknown beats per bar falls back to 4", cycleStopped(p, 3.5, 0.0) && !cycleStopped(p, 2.5, 0.0));
    check("cycle sizes are 1, 2, 4, 8 bars", cycleBars(0) == 1 && cycleBars(1) == 2 && cycleBars(2) == 4 && cycleBars(3) == 8);

    // Rendered: at 120 BPM a beat is 24000 samples. Start 1 beat before the stop, stopped from 1.0 s in.
    Parameters q = p;
    q.stopMs = 250.0f; q.startMs = 100.0f; q.curve = 1.0f;
    const Result r = render(q, 1.4f, {}, true, 120.0, 2.0);
    check("plays until the stop beat", exactlyEqual(r.in, r.out, 0, 24000));
    check("stopped within the last beat", slope(r, 24000 + 14000, 480) < 0.05);
    // The next bar begins at 1.0 s + 1 beat = 2.0 s from the start: not reached in 1.4 s, so check a longer run.
    const Result r2 = render(q, 2.5f, {}, true, 120.0, 2.0);
    check("back to live in the next bar", exactlyEqual(r2.in, r2.out, static_cast<std::size_t>(2.0 * kSr + 0.1 * kSr + 0.05 * kSr), r2.in.size()));
    // A stopped transport never engages the cycle.
    const Result idle = render(q, 1.5f, {}, false, 120.0, 3.5);
    check("cycle ignores a stopped transport", exactlyEqual(idle.in, idle.out, 0, idle.in.size()));
    // Length and Cycle change the result.
    Parameters q2 = q; q2.length = 0.25f;
    check("Length changes when the tape stops", !exactlyEqual(render(q, 1.0f, {}, true, 120.0, 2.0).out, render(q2, 1.0f, {}, true, 120.0, 2.0).out, 0, 48000));
    Parameters q3 = q; q3.cycle = 1.0f;
    check("Cycle changes when the tape stops", !exactlyEqual(render(q, 1.0f, {}, true, 120.0, 2.0).out, render(q3, 1.0f, {}, true, 120.0, 2.0).out, 0, 48000));
}

// CC takeover: controls on the CC channel, ignored elsewhere, released when the parameter moves.
static void testControllers()
{
    Parameters p;
    p.ccChannel = 3.0f;
    const Result on = render(p, 0.5f, {cc(100, kCcStop, 127, 2)});
    check("CC 4 on the CC channel stops the tape", on.state.meters.speed < 0.3f);   // (1 - 0.5/0.8)^1.5 = 0.23 at the default Stop Time
    check("CC 4 on another channel is ignored", render(p, 0.5f, {cc(100, kCcStop, 127, 1)}).state.meters.speed == 1.0f);
    Parameters off = p; off.ccChannel = 0.0f;
    check("CC channel 0 ignores CC", render(off, 0.5f, {cc(100, kCcStop, 127, 2)}).state.meters.speed == 1.0f);
    check("CC 4 below 64 does not stop", render(p, 0.5f, {cc(100, kCcStop, 63, 2)}).state.meters.speed == 1.0f);

    // CC 1 sets Stop Time on a log scale: 0 -> 20 ms (stopped within 40 ms), 127 -> 4000 ms (still moving at 1 s).
    Parameters q = p; q.stop = 1.0f; q.stopMs = 1000.0f;
    check("CC 1 = 0 gives the shortest stop", render(q, 0.2f, {cc(0, kCcStopTime, 0, 2)}).state.meters.speed < 0.01f);
    check("CC 1 = 127 gives the longest stop", render(q, 1.0f, {cc(0, kCcStopTime, 127, 2)}).state.meters.speed > 0.6f);
    // CC 3 sets the curve: 127 is 4.0, steeper than the default 1.5.
    check("CC 3 changes the curve", render(q, 0.5f, {cc(0, kCcCurve, 127, 2)}).state.meters.speed < render(q, 0.5f, {}).state.meters.speed - 0.05f);

    // Releasing a takeover returns control to the parameter.
    Result held = render(p, 0.3f, {cc(0, kCcStop, 127, 2)});
    check("takeover is active", held.state.overrideActive[3]);
    releaseOverride(held.state, 3);
    check("takeover released", !held.state.overrideActive[3]);
    check("releaseOverride ignores bad indices", (releaseOverride(held.state, -1), releaseOverride(held.state, 99), true));
    check("the parameter map names the takeover slots", overrideIndex(kStopTime) == 0 && overrideIndex(kStartTime) == 1
          && overrideIndex(kCurve) == 2 && overrideIndex(kStop) == 3 && overrideIndex(kTrigger) == -1);
}

// Resetting for host activation clears the runtime state and keeps the buffer.
static void testReset()
{
    Result r = render(manualStop(), 0.5f);
    check("stopped before reset", r.state.meters.speed < 0.6f);
    resetState(r.state);
    check("reset clears the tape", r.state.progress == 0.0 && r.state.delay == 0.0 && r.state.live == 1.0f && r.state.heldNotes == 0);
    check("reset keeps the buffer", !r.state.ring[0].empty());
}

static void testParameters()
{
    Parameters wild;
    wild.trigger = 9.0f; wild.stop = 3.0f; wild.stopMs = -5.0f; wild.startMs = 1e9f; wild.curve = 0.0f;
    wild.cycle = 7.0f; wild.length = 100.0f; wild.ccChannel = 40.0f;
    const Parameters c = clampParameters(wild);
    check("clamp trigger", c.trigger == 2.0f);
    check("clamp stop", c.stop == 1.0f);
    check("clamp times", c.stopMs == kMinStopMs && c.startMs == kMaxStopMs);
    check("clamp curve", c.curve == kMinCurve);
    check("clamp cycle and length", c.cycle == 3.0f && c.length == kMaxLengthBeats);
    check("clamp cc channel", c.ccChannel == 16.0f);
    // The parameter table and the core defaults agree.
    const Parameters d;
    bool ok = true;
    for (std::uint32_t i = 0; i < kControlCount; ++i) {
        const auto& s = specs()[i];
        if (field(d, i) != s.defaultValue || s.defaultValue < s.minimum || s.defaultValue > s.maximum) ok = false;
    }
    check("table defaults match the core and sit in range", ok);
    check("outputs are flagged", isOutput(kStatusSpeed) && isOutput(kStatusDelay) && !isOutput(kCcChannel));
    check("speedFor endpoints", speedFor(0.0, 2.0f) == 1.0f && speedFor(1.0, 2.0f) == 0.0f);
}

int main()
{
    testDefaultsAreInert();
    testSpeedFollowsTheCurve();
    testControlsHaveAnEffect();
    testStandstillIsSilent();
    testLagAtStandstill();
    testReturnsToLive();
    testInterruptedStopIsContinuous();
    testVeryLongStop();
    testUnpreparedPassesThrough();
    testMidiTrigger();
    testCycleSchedule();
    testControllers();
    testReset();
    testParameters();
    std::printf("spindown core tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
