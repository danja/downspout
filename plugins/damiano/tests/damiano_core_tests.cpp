#include "damiano_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

// This suite uses its own CHECK/gFailed harness below, not assert(), so it does
// not depend on -UNDEBUG and does not include downspout/test_assert.h.

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

static bool nearlyEqual(float a, float b, float eps = 1e-4f)
{
    return std::abs(a - b) <= eps;
}

// ── Silence passthrough ──────────────────────────────────────────────────────

static void testSilence()
{
    using namespace downspout::damiano;

    constexpr uint32_t kFrames = 64;
    std::array<float, kFrames> in_L{}, in_R{}, out_L{}, out_R{};

    std::array<const float*, kMaxChannels> ins{};
    std::array<float*, kMaxChannels>       outs{};
    ins[0] = in_L.data(); ins[1] = in_R.data();
    outs[0] = out_L.data(); outs[1] = out_R.data();

    AudioBlock audio;
    audio.inputs       = ins;
    audio.outputs      = outs;
    audio.channelCount = 2;

    for (int mode = 0; mode < kModeCount; ++mode) {
        Parameters p;
        p.mode = static_cast<float>(mode);
        EngineState state;

        processBlock(state, p, kFrames, 44100.0, audio, p.drive);

        for (uint32_t n = 0; n < kFrames; ++n) {
            char name[64];
            std::snprintf(name, sizeof(name), "silence mode %d L[%u]", mode, n);
            check(name, nearlyEqual(out_L[n], 0.0f));
            std::snprintf(name, sizeof(name), "silence mode %d R[%u]", mode, n);
            check(name, nearlyEqual(out_R[n], 0.0f));
        }
    }
}

// ── Output bounded at ±1 (before output gain) with unity output gain ─────────

static void testOutputBounded()
{
    using namespace downspout::damiano;

    constexpr uint32_t kFrames = 128;
    std::array<float, kFrames> in_buf{};
    std::array<float, kFrames> out_buf{};

    // Fill input with samples ranging from -3 to +3 (well above clipping)
    for (uint32_t i = 0; i < kFrames; ++i)
        in_buf[i] = -3.0f + 6.0f * (static_cast<float>(i) / static_cast<float>(kFrames - 1));

    std::array<const float*, kMaxChannels> ins{};
    std::array<float*, kMaxChannels>       outs{};
    ins[0]  = in_buf.data();
    outs[0] = out_buf.data();

    AudioBlock audio;
    audio.inputs       = ins;
    audio.outputs      = outs;
    audio.channelCount = 1;

    constexpr float kMargin = 1.05f; // allow small overshoot from tone shelf transient

    for (int mode = 0; mode < kModeCount; ++mode) {
        Parameters p;
        p.mode       = static_cast<float>(mode);
        p.drive      = 5.0f;
        p.outputGain = 0.0f;
        p.mix        = 100.0f;
        EngineState state;

        processBlock(state, p, kFrames, 44100.0, audio, p.drive);

        for (uint32_t n = 0; n < kFrames; ++n) {
            char name[64];
            std::snprintf(name, sizeof(name), "bounded mode %d frame %u", mode, n);
            check(name, std::abs(out_buf[n]) <= kMargin);
        }
    }
}

// ── Dry mix = 0 passes input unchanged ──────────────────────────────────────

static void testDryPassthrough()
{
    using namespace downspout::damiano;

    constexpr uint32_t kFrames = 32;
    std::array<float, kFrames> in_buf{}, out_buf{};

    for (uint32_t i = 0; i < kFrames; ++i)
        in_buf[i] = 0.5f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 16.0f);

    std::array<const float*, kMaxChannels> ins{};
    std::array<float*, kMaxChannels>       outs{};
    ins[0]  = in_buf.data();
    outs[0] = out_buf.data();

    AudioBlock audio;
    audio.inputs       = ins;
    audio.outputs      = outs;
    audio.channelCount = 1;

    Parameters p;
    p.mix        = 0.0f;   // fully dry
    p.outputGain = 0.0f;
    EngineState state;

    processBlock(state, p, kFrames, 44100.0, audio, p.drive);

    for (uint32_t n = 0; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "dry passthrough frame %u", n);
        check(name, nearlyEqual(out_buf[n], in_buf[n]));
    }
}

// ── Clamp round-trip ─────────────────────────────────────────────────────────

static void testClamp()
{
    using namespace downspout::damiano;

    Parameters extreme;
    extreme.mode       = 99.0f;
    extreme.drive      = -5.0f;
    extreme.tone       = 200.0f;
    extreme.foldCount  = 0.0f;
    extreme.mix        = 150.0f;
    extreme.outputGain = 100.0f;
    extreme.ccDrive    = 200.0f;
    extreme.ccChannel  = 0.0f;

    const Parameters clamped = clampParameters(extreme);

    check("clamp mode max",        clamped.mode       <= 5.0f);
    check("clamp drive min",       clamped.drive      >= 1.0f);
    check("clamp tone max",        clamped.tone       <= 100.0f);
    check("clamp foldCount min",   clamped.foldCount  >= 1.0f);
    check("clamp mix max",         clamped.mix        <= 100.0f);
    check("clamp outputGain max",  clamped.outputGain <= 24.0f);
    check("clamp ccDrive max",     clamped.ccDrive    <= 127.0f);
    check("clamp ccChannel min",   clamped.ccChannel  >= 1.0f);
}

// ── Serialization round-trip ─────────────────────────────────────────────────

static void testSerialization()
{
    using namespace downspout::damiano;

    Parameters p;
    p.mode       = 3.0f;
    p.drive      = 4.5f;
    p.tone       = 70.0f;
    p.foldCount  = 3.0f;
    p.mix        = 75.0f;
    p.outputGain = -6.0f;
    p.ccDrive    = 11.0f;
    p.ccChannel  = 2.0f;

    const std::string text     = serializeParameters(p);
    const auto        restored = deserializeParameters(text);

    check("serialization round-trip valid", restored.has_value());
    if (restored.has_value()) {
        check("rt mode",       nearlyEqual(restored->mode,       p.mode));
        check("rt drive",      nearlyEqual(restored->drive,      p.drive));
        check("rt tone",       nearlyEqual(restored->tone,       p.tone));
        check("rt foldCount",  nearlyEqual(restored->foldCount,  p.foldCount));
        check("rt mix",        nearlyEqual(restored->mix,        p.mix));
        check("rt outputGain", nearlyEqual(restored->outputGain, p.outputGain));
        check("rt ccDrive",    nearlyEqual(restored->ccDrive,    p.ccDrive));
        check("rt ccChannel",  nearlyEqual(restored->ccChannel,  p.ccChannel));
    }
}

// ── Output gain applied ──────────────────────────────────────────────────────

static void testOutputGain()
{
    using namespace downspout::damiano;

    constexpr uint32_t kFrames = 16;
    std::array<float, kFrames> in_buf{}, out_unity{}, out_boosted{};

    for (uint32_t i = 0; i < kFrames; ++i)
        in_buf[i] = 0.1f; // small signal — stays linear in tanh

    std::array<const float*, kMaxChannels> ins{};
    std::array<float*, kMaxChannels>       outs{};
    ins[0] = in_buf.data();

    AudioBlock audio;
    audio.inputs       = ins;
    audio.outputs      = outs;
    audio.channelCount = 1;

    Parameters p;
    p.mode  = static_cast<float>(kModeSoft);
    p.drive = 1.0f;
    p.mix   = 100.0f;
    p.tone  = 50.0f; // flat

    {
        p.outputGain  = 0.0f;
        outs[0] = out_unity.data();
        EngineState state;
        processBlock(state, p, kFrames, 44100.0, audio, p.drive);
    }
    {
        p.outputGain  = 6.0206f; // ≈ +6 dB = ×2
        outs[0] = out_boosted.data();
        EngineState state;
        processBlock(state, p, kFrames, 44100.0, audio, p.drive);
    }

    // After tone filter settles (skip first few frames)
    for (uint32_t n = 8; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "output gain ×2 frame %u", n);
        check(name, nearlyEqual(out_boosted[n], out_unity[n] * 2.0f, 1e-3f));
    }
}

// ── Stereo split ─────────────────────────────────────────────────────────────

struct StereoRun {
    std::array<float, 256> inL{}, inR{}, outL{}, outR{};
};

static StereoRun runStereo(const downspout::damiano::Parameters& p,
                           const downspout::damiano::LiveControl& live)
{
    using namespace downspout::damiano;
    StereoRun r;
    for (std::size_t i = 0; i < r.inL.size(); ++i) {
        const float v = 0.6f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 32.0f);
        r.inL[i] = v;
        r.inR[i] = v;  // identical input: any difference is the processing
    }
    std::array<const float*, kMaxChannels> ins{};
    std::array<float*, kMaxChannels>       outs{};
    ins[0] = r.inL.data(); ins[1] = r.inR.data();
    outs[0] = r.outL.data(); outs[1] = r.outR.data();
    AudioBlock audio;
    audio.inputs = ins; audio.outputs = outs; audio.channelCount = 2;
    EngineState state;
    processBlock(state, p, static_cast<std::uint32_t>(r.inL.size()), 44100.0, audio, live);
    return r;
}

static float maxDiff(const std::array<float, 256>& a, const std::array<float, 256>& b)
{
    float d = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

static void testLinkedMirrorsLeft()
{
    using namespace downspout::damiano;
    Parameters p;           // stereo defaults to linked
    p.driveR = 9.0f;        // would be audible if it leaked through
    p.modeR  = static_cast<float>(kModeFuzz);
    LiveControl live;
    live.driveR = 9.0f;
    live.modeR  = 2.0f;
    const StereoRun r = runStereo(p, live);
    check("linked: R equals L despite R settings", maxDiff(r.outL, r.outR) == 0.0f);
}

static void testLegacyOverloadUnchanged()
{
    using namespace downspout::damiano;
    // The short processBlock must give exactly what the live-control form gives
    // with only driveL set (the pre-split behaviour).
    Parameters p;
    p.mode = static_cast<float>(kModeOverdrive);
    StereoRun a = runStereo(p, LiveControl{6.5f, -1.0f, -1.0f, -1.0f});

    StereoRun b;
    for (std::size_t i = 0; i < b.inL.size(); ++i) {
        b.inL[i] = a.inL[i]; b.inR[i] = a.inR[i];
    }
    std::array<const float*, kMaxChannels> ins{};
    std::array<float*, kMaxChannels>       outs{};
    ins[0] = b.inL.data(); ins[1] = b.inR.data();
    outs[0] = b.outL.data(); outs[1] = b.outR.data();
    AudioBlock audio;
    audio.inputs = ins; audio.outputs = outs; audio.channelCount = 2;
    EngineState state;
    processBlock(state, p, 256, 44100.0, audio, 6.5f);
    check("legacy overload L matches", maxDiff(a.outL, b.outL) == 0.0f);
    check("legacy overload R matches", maxDiff(a.outR, b.outR) == 0.0f);
}

static void testSplitIndependentDrive()
{
    using namespace downspout::damiano;
    Parameters p;
    p.stereo = 1.0f;
    p.driveR = 9.0f;        // L stays at 2
    const StereoRun r = runStereo(p, LiveControl{});
    check("split: channels differ", maxDiff(r.outL, r.outR) > 0.01f);

    // Left must be identical to a linked run: the right settings cannot touch it.
    Parameters linked = p;
    linked.stereo = 0.0f;
    const StereoRun l = runStereo(linked, LiveControl{});
    check("split: L unchanged by R settings", maxDiff(r.outL, l.outL) == 0.0f);
    // Right must equal a linked run whose left settings are the right ones.
    Parameters swapped;
    swapped.drive = 9.0f;
    const StereoRun s = runStereo(swapped, LiveControl{});
    check("split: R behaves as L would with R settings", maxDiff(r.outR, s.outL) == 0.0f);
}

static void testSplitIndependentMode()
{
    using namespace downspout::damiano;
    Parameters p;
    p.stereo = 1.0f;
    p.mode   = static_cast<float>(kModeSoft);
    p.modeR  = static_cast<float>(kModeFuzz);
    const StereoRun r = runStereo(p, LiveControl{});
    check("split mode: channels differ", maxDiff(r.outL, r.outR) > 0.01f);
}

static void testLiveControlPerChannel()
{
    using namespace downspout::damiano;
    Parameters p;
    p.stereo = 1.0f;
    // A CC on the right drive must move only the right channel.
    const StereoRun base = runStereo(p, LiveControl{});
    const StereoRun moved = runStereo(p, LiveControl{-1.0f, 10.0f, -1.0f, -1.0f});
    check("live driveR leaves L alone", maxDiff(base.outL, moved.outL) == 0.0f);
    check("live driveR moves R",        maxDiff(base.outR, moved.outR) > 0.01f);
    // And a CC on the left mode only the left.
    const StereoRun shaped = runStereo(p, LiveControl{-1.0f, -1.0f, 2.0f, -1.0f});
    check("live modeL leaves R alone", maxDiff(base.outR, shaped.outR) == 0.0f);
    check("live modeL moves L",        maxDiff(base.outL, shaped.outL) > 0.01f);
}

static void testCcMapping()
{
    using namespace downspout::damiano;
    check("cc 0 -> drive 1",    nearlyEqual(driveFromCc(0),   1.0f));
    check("cc 127 -> drive 10", nearlyEqual(driveFromCc(127), 10.0f));
    check("cc drive clamps",    nearlyEqual(driveFromCc(500), 10.0f));
    check("cc 0 -> mode 0",     modeFromCc(0)   == 0.0f);
    check("cc 127 -> last mode", modeFromCc(127) == static_cast<float>(kModeCount - 1));
    bool monotonic = true, covered[kModeCount] = {};
    float prev = -1.0f;
    for (int v = 0; v < 128; ++v) {
        const float m = modeFromCc(v);
        monotonic = monotonic && m >= prev;
        prev = m;
        covered[static_cast<int>(m)] = true;
    }
    check("cc shape monotonic", monotonic);
    bool all = true;
    for (bool c : covered) all = all && c;
    check("cc shape reaches every mode", all);
}

static void testSplitSerialization()
{
    using namespace downspout::damiano;
    Parameters p;
    p.stereo = 1.0f; p.driveR = 7.5f; p.modeR = 4.0f; p.toneR = 20.0f;
    p.foldCountR = 5.0f; p.ccDriveR = 12.0f; p.ccShape = 13.0f; p.ccShapeR = 14.0f;
    const auto r = deserializeParameters(serializeParameters(p));
    check("split round-trip valid", r.has_value());
    if (r) {
        check("rt stereo",     nearlyEqual(r->stereo, 1.0f));
        check("rt driveR",     nearlyEqual(r->driveR, 7.5f));
        check("rt modeR",      nearlyEqual(r->modeR, 4.0f));
        check("rt toneR",      nearlyEqual(r->toneR, 20.0f));
        check("rt foldCountR", nearlyEqual(r->foldCountR, 5.0f));
        check("rt ccDriveR",   nearlyEqual(r->ccDriveR, 12.0f));
        check("rt ccShape",    nearlyEqual(r->ccShape, 13.0f));
        check("rt ccShapeR",   nearlyEqual(r->ccShapeR, 14.0f));
    }
    // A pre-split document (no R keys) loads with the linked defaults.
    const auto old = deserializeParameters(
        "version=1\nmode=3.0\ndrive=4.0\ntone=50.0\nfold_count=2.0\nmix=100.0\n"
        "output_gain=0.0\ncc_drive=1.0\ncc_channel=1.0\n");
    check("pre-split state loads", old.has_value());
    if (old) {
        check("pre-split is linked", old->stereo == 0.0f);
        check("pre-split keeps drive", nearlyEqual(old->drive, 4.0f));
    }
    Parameters extreme;
    extreme.stereo = 7.0f; extreme.driveR = 99.0f; extreme.modeR = 99.0f;
    extreme.ccShape = 500.0f;
    const Parameters c = clampParameters(extreme);
    check("clamp stereo", c.stereo <= 1.0f);
    check("clamp driveR", c.driveR <= 10.0f);
    check("clamp modeR",  c.modeR <= 5.0f);
    check("clamp ccShape", c.ccShape <= 127.0f);
}

int main()
{
    testSilence();
    testOutputBounded();
    testDryPassthrough();
    testClamp();
    testSerialization();
    testOutputGain();
    testLinkedMirrorsLeft();
    testLegacyOverloadUnchanged();
    testSplitIndependentDrive();
    testSplitIndependentMode();
    testLiveControlPerChannel();
    testCcMapping();
    testSplitSerialization();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
