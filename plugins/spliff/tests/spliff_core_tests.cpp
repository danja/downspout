#include "spliff_core.hpp"

#include <array>
#include <cmath>
#include <cstdio>

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

static bool nearlyEqual(float a, float b, float eps = 1e-5f)
{
    return std::abs(a - b) <= eps;
}

using namespace downspout::spliff;

static constexpr std::uint32_t kFrames = 512;
static constexpr double kSr = 48000.0;

struct Block {
    std::array<float, kFrames> inL {}, inR {}, outL {}, outR {};
};

// 64-sample 880 Hz onset burst, then a quieter sustained tone, else silence.
static void fillBurst(Block& b, float scale = 1.0f)
{
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        float v = 0.0f;
        if (i < 64)
            v = std::sin(2.0f * 3.14159f * 880.0f * static_cast<float>(i) / 48000.0f);
        else if (i < 400)
            v = 0.3f * std::sin(2.0f * 3.14159f * 220.0f * static_cast<float>(i) / 48000.0f);
        b.inL[i] = b.inR[i] = v * scale;
    }
}

static void run(EngineState& s, const Parameters& p, Block& b)
{
    const float* ins[] = { b.inL.data(), b.inR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    processBlock(s, p, kFrames, kSr, ins, outs, p.depth, p.sensitivity, p.decay, p.mix);
}

static Parameters designerParams(float attackDb, float sustainDb)
{
    Parameters p;
    p.mode = 2.0f;
    p.attackDb = attackDb;
    p.sustainDb = sustainDb;
    return p;
}

static float gainDbAt(const Block& dry, const Block& wet, std::uint32_t i)
{
    if (std::fabs(dry.inL[i]) < 1e-12f) return 0.0f;
    return 20.0f * std::log10(std::fabs(wet.outL[i] / dry.inL[i]));
}

static float maxGainDb(const Block& dry, const Block& wet,
                       std::uint32_t from, std::uint32_t to)
{
    float peak = -1.0e9f;
    for (std::uint32_t i = from; i < to && i < kFrames; ++i)
        if (std::fabs(dry.inL[i]) > 1e-7f)
            peak = std::max(peak, gainDbAt(dry, wet, i));
    return peak;
}

static float minGainDb(const Block& dry, const Block& wet,
                       std::uint32_t from, std::uint32_t to)
{
    float floor = 1.0e9f;
    for (std::uint32_t i = from; i < to && i < kFrames; ++i)
        if (std::fabs(dry.inL[i]) > 1e-7f)
            floor = std::min(floor, gainDbAt(dry, wet, i));
    return floor;
}

static void fillConstant(Block& b, float value)
{
    for (std::uint32_t i = 0; i < kFrames; ++i)
        b.inL[i] = b.inR[i] = value;
}

// Settle the designer envelopes on a loud signal so the next block can expose
// a genuine fast < slow tail without relying on hand-set internal state.
static void settleDesigner(EngineState& s)
{
    Block loud;
    fillConstant(loud, 0.8f);
    const Parameters p = designerParams(0.0f, 0.0f);
    for (int n = 0; n < 32; ++n)
        run(s, p, loud);
}

static float peakAfter(const Block& b, std::uint32_t from, std::uint32_t to)
{
    float peak = 0.0f;
    for (std::uint32_t i = from; i < to && i < kFrames; ++i)
        peak = std::max(peak, std::fabs(b.outL[i]));
    return peak;
}

static float meanAbs(const Block& b, std::uint32_t from, std::uint32_t to)
{
    double sum = 0.0;
    std::uint32_t n = 0;
    for (std::uint32_t i = from; i < to && i < kFrames; ++i) { sum += std::fabs(b.outL[i]); ++n; }
    return n ? static_cast<float>(sum / n) : 0.0f;
}

static void fillBroadband(Block& b)
{
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        float v = 0.0f;
        if (i < 128) {
            v = 0.5f * std::sin(2.0f * 3.14159f * 120.0f * i / kSr)
              + 0.5f * std::sin(2.0f * 3.14159f * 900.0f * i / kSr)
              + 0.5f * std::sin(2.0f * 3.14159f * 6000.0f * i / kSr);
        }
        b.inL[i] = b.inR[i] = v;
    }
}

static void runWithDelta(EngineState& s, const Parameters& p, Block& b,
                         std::array<float, kFrames>& deltaL,
                         std::array<float, kFrames>& deltaR)
{
    const float* ins[] = { b.inL.data(), b.inR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    float* deltas[] = { deltaL.data(), deltaR.data() };
    processBlock(s, p, kFrames, kSr, ins, outs,
                 p.depth, p.sensitivity, p.decay, p.mix, deltas);
}

static float rmsDiff(const Block& a, const Block& b)
{
    double sum = 0.0;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        const float d = a.outL[i] - b.outL[i];
        sum += d * d;
    }
    return static_cast<float>(std::sqrt(sum / kFrames));
}

static void testSilence()
{
    Block b;
    EngineState s;
    Parameters p;
    run(s, p, b);
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        if (b.outL[i] != 0.0f || b.outR[i] != 0.0f) {
            check("silence in gives silence out", false);
            return;
        }
    }
    check("silence in gives silence out", true);
}

static void testMixZeroDry()
{
    Block b;
    fillBurst(b);
    EngineState s;
    Parameters p;
    p.mix = 0.0f;
    run(s, p, b);
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(b.outL[i], b.inL[i], 1e-6f);
    check("mix 0 passes dry unchanged", same);
}

static void testBypassDry()
{
    Block b;
    fillBurst(b);
    EngineState s;
    Parameters p;
    p.bypass = 1.0f;
    p.depth = 1.0f;
    run(s, p, b);
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(b.outL[i], b.inL[i], 1e-6f);
    check("bypass passes dry unchanged", same);
}

static void testDepthZeroTransparent()
{
    Block b;
    fillBurst(b);
    EngineState s;
    Parameters p;
    p.depth = 0.0f;
    run(s, p, b);
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(b.outL[i], b.inL[i], 1e-4f);
    check("depth 0 is transparent", same);
}

static void testCutReducesTransient()
{
    Block dry, wet;
    fillBurst(dry);
    fillBurst(wet);
    { EngineState s; Parameters p; p.depth = 0.0f; run(s, p, dry); }
    { EngineState s; Parameters p; p.mode = 0.0f; p.depth = 1.0f; p.sensitivity = 1.0f; run(s, p, wet); }
    const float dryPeak = peakAfter(dry, 32, 64);
    const float wetPeak = peakAfter(wet, 32, 64);
    check("cut burst peak is lower", wetPeak < dryPeak * 0.9f);
}

static void testBoostRaisesTransient()
{
    Block dry, wet;
    fillBurst(dry);
    fillBurst(wet);
    { EngineState s; Parameters p; p.depth = 0.0f; run(s, p, dry); }
    { EngineState s; Parameters p; p.mode = 1.0f; p.depth = 1.0f; p.sensitivity = 1.0f; run(s, p, wet); }
    const float dryPeak = peakAfter(dry, 32, 64);
    const float wetPeak = peakAfter(wet, 32, 64);
    check("boost burst peak is higher", wetPeak > dryPeak * 1.1f);
}

static void testSensitivityGate()
{
    // Settle the detector on a tone first so the burst ratio is moderate;
    // otherwise any onset from silence saturates the weight at any threshold.
    auto runSettled = [](float sens) {
        EngineState s;
        Parameters p;
        p.mode = 0.0f;
        p.depth = 1.0f;
        p.sensitivity = sens;
        Block pre;
        for (std::uint32_t i = 0; i < kFrames; ++i)
            pre.inL[i] = pre.inR[i] =
                0.3f * std::sin(2.0f * 3.14159f * 220.0f * static_cast<float>(i) / 48000.0f);
        run(s, p, pre);
        Block b;
        for (std::uint32_t i = 0; i < 96; ++i)
            b.inL[i] = b.inR[i] =
                std::sin(2.0f * 3.14159f * 880.0f * static_cast<float>(i) / 48000.0f);
        for (std::uint32_t i = 96; i < kFrames; ++i)
            b.inL[i] = b.inR[i] =
                0.3f * std::sin(2.0f * 3.14159f * 220.0f * static_cast<float>(i) / 48000.0f);
        run(s, p, b);
        return peakAfter(b, 8, 96);
    };
    check("higher sensitivity cuts deeper", runSettled(1.0f) < runSettled(0.0f));
}

static void testDecayTail()
{
    Block fast, slow;
    fillBurst(fast);
    fillBurst(slow);
    { EngineState s; Parameters p; p.mode = 0.0f; p.depth = 1.0f; p.decay = 0.0f; run(s, p, fast); }
    { EngineState s; Parameters p; p.mode = 0.0f; p.depth = 1.0f; p.decay = 1.0f; run(s, p, slow); }
    check("longer decay suppresses tail more",
          meanAbs(slow, 200, 400) < meanAbs(fast, 200, 400));
}

static void testDecayTiltDirection()
{
    Block bLo, bHi;
    fillBurst(bLo);
    fillBurst(bHi);
    // Low-frequency burst: tilt -1 (LF long) should suppress its tail more.
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        const float v = (i < 64) ? std::sin(2.0f * 3.14159f * 90.0f * i / 48000.0f)
                                 : 0.3f * std::sin(2.0f * 3.14159f * 90.0f * i / 48000.0f);
        bLo.inL[i] = bLo.inR[i] = (i < 400) ? v : 0.0f;
        bHi.inL[i] = bHi.inR[i] = (i < 400) ? v : 0.0f;
    }
    { EngineState s; Parameters p; p.mode = 0.0f; p.depth = 1.0f; p.decay = 1.0f; p.decayTilt = -1.0f; run(s, p, bLo); }
    { EngineState s; Parameters p; p.mode = 0.0f; p.depth = 1.0f; p.decay = 1.0f; p.decayTilt = 1.0f; run(s, p, bHi); }
    check("tilt -1 holds LF reduction longer",
          meanAbs(bLo, 200, 400) <= meanAbs(bHi, 200, 400));
}

static void testDeltaIsWetMinusDry()
{
    // Depth 0 -> wet == dry, so delta must be (near) silence.
    Block idle;
    fillBurst(idle);
    { EngineState s; Parameters p; p.depth = 0.0f; p.delta = 1.0f; run(s, p, idle); }
    check("delta idle is silence", peakAfter(idle, 0, kFrames) < 1e-3f);

    // Depth 1 cut -> delta carries real removal energy, and the wet path
    // it came from is quieter than dry over the burst.
    Block dry, wet, delta;
    fillBurst(dry);
    fillBurst(wet);
    fillBurst(delta);
    { EngineState s; Parameters p; p.depth = 0.0f; run(s, p, dry); }
    { EngineState s; Parameters p; p.mode = 0.0f; p.depth = 1.0f; p.sensitivity = 1.0f; run(s, p, wet); }
    { EngineState s; Parameters p; p.mode = 0.0f; p.depth = 1.0f; p.sensitivity = 1.0f; p.delta = 1.0f; run(s, p, delta); }
    const float dryPeak = peakAfter(dry, 32, 64);
    check("delta cut removes energy", peakAfter(wet, 32, 64) < dryPeak * 0.9f);
    check("delta monitor is nonzero at transient", peakAfter(delta, 0, 128) > 1e-3f);
}

static void testTrimGain()
{
    Block flat, loud;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        flat.inL[i] = flat.inR[i] = loud.inL[i] = loud.inR[i] = 0.25f;
    { EngineState s; Parameters p; p.depth = 0.0f; p.trim = 0.0f; run(s, p, flat); }
    { EngineState s; Parameters p; p.depth = 0.0f; p.trim = 6.0206f; run(s, p, loud); }
    bool doubled = true;
    for (std::uint32_t i = 100; i < kFrames && doubled; ++i)
        doubled = nearlyEqual(loud.outL[i], flat.outL[i] * 2.0f, 1e-4f);
    check("trim +6 dB doubles wet", doubled);
}

static void testBlockSplitDeterminism()
{
    Block a, b;
    fillBurst(a);
    fillBurst(b);
    Parameters p;
    p.mode = 0.0f;
    p.depth = 0.8f;
    { EngineState s; run(s, p, a); }
    {
        EngineState s;
        constexpr std::uint32_t kHalf = kFrames / 2;
        const float* ins[] = { b.inL.data(), b.inR.data() };
        float* outs[] = { b.outL.data(), b.outR.data() };
        processBlock(s, p, kHalf, kSr, ins, outs, p.depth, p.sensitivity, p.decay, p.mix);
        const float* ins2[] = { b.inL.data() + kHalf, b.inR.data() + kHalf };
        float* outs2[] = { b.outL.data() + kHalf, b.outR.data() + kHalf };
        processBlock(s, p, kFrames - kHalf, kSr, ins2, outs2, p.depth, p.sensitivity, p.decay, p.mix);
    }
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(a.outL[i], b.outL[i], 1e-6f) && nearlyEqual(a.outR[i], b.outR[i], 1e-6f);
    check("split blocks match single block", same);
}

static void testDesignerDefaultInert()
{
    Block b;
    fillBurst(b);
    EngineState s;
    run(s, designerParams(0.0f, 0.0f), b);
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(b.outL[i], b.inL[i], 1e-5f) &&
               nearlyEqual(b.outR[i], b.inR[i], 1e-5f);
    check("designer 0 dB is transparent", same);
}

static void testDesignerAttackAndSustain()
{
    Block dry, attackUp, attackDown;
    fillBurst(dry);
    fillBurst(attackUp);
    fillBurst(attackDown);
    { EngineState s; run(s, designerParams(15.0f, 0.0f), attackUp); }
    { EngineState s; run(s, designerParams(-15.0f, 0.0f), attackDown); }

    check("designer attack + lifts onsets",
          maxGainDb(dry, attackUp, 0, 64) > 10.0f);
    check("designer attack - cuts onsets",
          minGainDb(dry, attackDown, 0, 64) < -10.0f);
    check("designer attack stays inside +/-15 dB",
          maxGainDb(dry, attackUp, 0, kFrames) <= 15.01f &&
          minGainDb(dry, attackDown, 0, kFrames) >= -15.01f);

    Block quiet, sustainUp, sustainDown;
    fillConstant(quiet, 0.01f);
    fillConstant(sustainUp, 0.01f);
    fillConstant(sustainDown, 0.01f);
    EngineState upState, downState;
    settleDesigner(upState);
    settleDesigner(downState);
    run(upState, designerParams(0.0f, 24.0f), sustainUp);
    run(downState, designerParams(0.0f, -24.0f), sustainDown);

    check("designer sustain + lifts body/tail",
          maxGainDb(quiet, sustainUp, 64, kFrames) > 2.0f);
    check("designer sustain - cuts body/tail",
          minGainDb(quiet, sustainDown, 64, kFrames) < -2.0f);
    check("designer sustain stays inside +/-24 dB",
          maxGainDb(quiet, sustainUp, 0, kFrames) <= 24.01f &&
          minGainDb(quiet, sustainDown, 0, kFrames) >= -24.01f);
}

static void testDesignerLevelIndependence()
{
    Block loud, quiet;
    fillBurst(loud, 1.0f);
    fillBurst(quiet, 1.0e-10f);  // 200 dB down: the envelope guard must be relative
    const Parameters p = designerParams(12.0f, -18.0f);
    EngineState loudState, quietState;
    run(loudState, p, loud);
    run(quietState, p, quiet);

    float worst = 0.0f;
    for (std::uint32_t i = 0; i < 64; ++i) {
        if (std::fabs(loud.inL[i]) < 1e-7f) continue;
        worst = std::max(worst, std::abs(gainDbAt(loud, loud, i) -
                                        gainDbAt(quiet, quiet, i)));
    }
    check("designer gain is level independent", worst < 0.02f);
}

static void testDesignerNoThreshold()
{
    Block b;
    fillConstant(b, 1.0f);
    EngineState s;
    s.designer.fast = 1.001f;  // a small, non-zero envelope difference
    s.designer.slow = 1.0f;
    run(s, designerParams(15.0f, 0.0f), b);
    check("designer has no threshold/deadzone",
          maxGainDb(b, b, 0, kFrames) > 0.0005f);
}

static void testDesignerBlockSplitDeterminism()
{
    Block a, b;
    fillBurst(a);
    fillBurst(b);
    const Parameters p = designerParams(9.0f, -12.0f);
    { EngineState s; run(s, p, a); }
    {
        EngineState s;
        constexpr std::uint32_t kHalf = kFrames / 2;
        const float* ins[] = { b.inL.data(), b.inR.data() };
        float* outs[] = { b.outL.data(), b.outR.data() };
        processBlock(s, p, kHalf, kSr, ins, outs, p.depth, p.sensitivity, p.decay, p.mix);
        const float* ins2[] = { b.inL.data() + kHalf, b.inR.data() + kHalf };
        float* outs2[] = { b.outL.data() + kHalf, b.outR.data() + kHalf };
        processBlock(s, p, kFrames - kHalf, kSr, ins2, outs2,
                     p.depth, p.sensitivity, p.decay, p.mix);
    }
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(a.outL[i], b.outL[i], 1e-6f) &&
               nearlyEqual(a.outR[i], b.outR[i], 1e-6f);
    check("designer split blocks match single block", same);
}

static void testEffectsAreStrong()
{
    Block dry, cut, boost;
    fillBurst(dry);
    fillBurst(cut);
    fillBurst(boost);
    { EngineState s; Parameters p; p.depth = 0.0f; run(s, p, dry); }
    { EngineState s; Parameters p; p.depth = 1.0f; p.sensitivity = 1.0f; run(s, p, cut); }
    { EngineState s; Parameters p; p.mode = 1.0f; p.depth = 1.0f; p.sensitivity = 1.0f; run(s, p, boost); }

    const float dryPeak = peakAfter(dry, 8, 64);
    check("full Cut reaches at least -8 dB on a transient",
          peakAfter(cut, 8, 64) < dryPeak * 0.4f);
    check("full Boost reaches at least +12 dB on a transient",
          peakAfter(boost, 8, 64) > dryPeak * 4.0f);
    check("Cut and Boost are audibly far apart",
          peakAfter(boost, 8, 64) > peakAfter(cut, 8, 64) * 8.0f);
}

static void testSplitControlsChangeAudio()
{
    Block lowSplit, highSplit;
    fillBroadband(lowSplit);
    fillBroadband(highSplit);

    Parameters p;
    p.depth = 1.0f;
    p.sensitivity = 1.0f;
    p.sharpness = 1.0f;
    p.decay = 0.5f;
    p.splitLow = 20.0f;
    p.splitHigh = 500.0f;
    { EngineState s; run(s, p, lowSplit); }
    p.splitLow = 1500.0f;
    p.splitHigh = 1650.0f;
    { EngineState s; run(s, p, highSplit); }

    check("moving both splits audibly changes the processed audio",
          rmsDiff(lowSplit, highSplit) > 0.02f);

    // A single 500 Hz tone moves from the high band to the low band as Split Low crosses it.
    Block lowTone, highTone;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        const float v = i < 128 ? std::sin(2.0f * 3.14159f * 500.0f * i / kSr) : 0.0f;
        lowTone.inL[i] = lowTone.inR[i] = highTone.inL[i] = highTone.inR[i] = v;
    }
    p.splitLow = 20.0f;
    p.splitHigh = 500.0f;
    { EngineState s; run(s, p, lowTone); }
    p.splitLow = 1500.0f;
    p.splitHigh = 1650.0f;
    { EngineState s; run(s, p, highTone); }
    check("moving Split Low changes a 500 Hz transient's level",
          std::fabs(peakAfter(lowTone, 0, 128) - peakAfter(highTone, 0, 128)) > 0.02f);
}

static void testDeltaBufferIsWetMinusDry()
{
    Block dry, wet;
    std::array<float, kFrames> deltaL {}, deltaR {};
    fillBurst(dry);
    fillBurst(wet);
    { EngineState s; Parameters p; p.depth = 0.0f; run(s, p, dry); }
    {
        EngineState s;
        Parameters p;
        p.depth = 1.0f;
        p.sensitivity = 1.0f;
        runWithDelta(s, p, wet, deltaL, deltaR);
    }

    float worst = 0.0f;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        worst = std::max(worst, std::fabs(deltaL[static_cast<std::size_t>(i)] -
                                         (wet.outL[i] - dry.inL[i])));
    check("the analyser Delta buffer is wet minus dry", worst < 1e-5f);
    check("the Delta buffer carries real energy",
          peakAfter(wet, 0, 128) > 1e-3f);
}

static void testClamp()
{
    Parameters extreme;
    extreme.mode = 9.0f;
    extreme.depth = 5.0f;
    extreme.sensitivity = -1.0f;
    extreme.sharpness = 2.0f;
    extreme.decay = -1.0f;
    extreme.decayTilt = 3.0f;
    extreme.splitLow = 1.0f;
    extreme.splitHigh = 100.0f;  // below low*1.1, must be corrected up
    extreme.mix = 200.0f;
    extreme.trim = 100.0f;
    extreme.attackDb = 100.0f;
    extreme.sustainDb = -100.0f;
    extreme.ccChannel = 0.0f;
    const Parameters c = clampParameters(extreme);
    check("clamp mode", c.mode == 2.0f);
    check("clamp depth", c.depth <= 1.0f);
    check("clamp sensitivity", c.sensitivity >= 0.0f);
    check("clamp sharpness", c.sharpness <= 1.0f);
    check("clamp decay", c.decay >= 0.0f);
    check("clamp tilt", c.decayTilt <= 1.0f);
    check("clamp split order", c.splitHigh >= c.splitLow * 1.1f - 1e-3f);
    check("clamp mix", c.mix <= 100.0f);
    check("clamp trim", c.trim <= 12.0f);
    check("clamp attack", c.attackDb == 15.0f);
    check("clamp sustain", c.sustainDb == -24.0f);
    check("clamp ccChannel", c.ccChannel >= 1.0f);
}

static void testSerialization()
{
    Parameters p;
    p.mode = 1.0f;
    p.depth = 0.7f;
    p.sensitivity = 0.6f;
    p.sharpness = 0.8f;
    p.decay = 0.4f;
    p.decayTilt = -0.5f;
    p.splitLow = 120.0f;
    p.splitHigh = 6000.0f;
    p.mix = 75.0f;
    p.trim = -3.0f;
    p.bypass = 1.0f;
    p.delta = 1.0f;
    p.attackDb = -6.5f;
    p.sustainDb = 18.25f;

    const std::string text = serializeParameters(p);
    const auto restored = deserializeParameters(text);
    check("serialization round-trip valid", restored.has_value());
    if (restored.has_value()) {
        const float eps = 1e-4f;
        check("rt mode", restored->mode == p.mode);
        check("rt depth", std::abs(restored->depth - p.depth) < eps);
        check("rt sensitivity", std::abs(restored->sensitivity - p.sensitivity) < eps);
        check("rt sharpness", std::abs(restored->sharpness - p.sharpness) < eps);
        check("rt decay", std::abs(restored->decay - p.decay) < eps);
        check("rt tilt", std::abs(restored->decayTilt - p.decayTilt) < eps);
        check("rt splitLow", std::abs(restored->splitLow - p.splitLow) < eps);
        check("rt splitHigh", std::abs(restored->splitHigh - p.splitHigh) < eps);
        check("rt mix", std::abs(restored->mix - p.mix) < eps);
        check("rt trim", std::abs(restored->trim - p.trim) < eps);
        check("rt bypass", restored->bypass == p.bypass);
        check("rt delta", restored->delta == p.delta);
        check("rt attackDb", std::abs(restored->attackDb - p.attackDb) < eps);
        check("rt sustainDb", std::abs(restored->sustainDb - p.sustainDb) < eps);
    }
    const auto oldState = deserializeParameters("version=1\nmode=2\n");
    check("old state still loads", oldState.has_value());
    if (oldState.has_value()) {
        check("old state attack default", oldState->attackDb == 0.0f);
        check("old state sustain default", oldState->sustainDb == 0.0f);
        check("old state keeps mode 2", oldState->mode == 2.0f);
    }
    check("garbage deserialises to nullopt", !deserializeParameters("nonsense\n").has_value());
}

// A sustained note must be processed at the default settings; the old 0.5/0.5 defaults
// left the detector threshold above a constant envelope, so ordinary material passed dry.
static void testDefaultsAudibleOnSteadyTone()
{
    constexpr std::uint32_t kBlocks = 24;
    Parameters p;
    EngineState s;
    check("default depth is audible", p.depth >= 0.65f);
    check("default sensitivity is audible", p.sensitivity >= 0.65f);

    double inSq = 0.0, outSq = 0.0;
    for (std::uint32_t block = 0; block < kBlocks; ++block) {
        Block b {};
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const float v = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f
                                           * static_cast<float>(block * kFrames + i) / kSr);
            b.inL[i] = b.inR[i] = v;
            inSq += static_cast<double>(v) * v;
        }
        run(s, p, b);
        for (std::uint32_t i = 0; i < kFrames; ++i)
            outSq += static_cast<double>(b.outL[i]) * b.outL[i];
    }
    const double ratio = std::sqrt(outSq / inSq);
    check("steady tone is audibly cut by default", ratio < 0.85);
    check("steady tone is not annihilated by default", ratio > 0.25);
    std::printf("default steady-tone ratio %.4f\n", ratio);
}

int main()
{
    testSilence();
    testMixZeroDry();
    testBypassDry();
    testDepthZeroTransparent();
    testCutReducesTransient();
    testBoostRaisesTransient();
    testSensitivityGate();
    testDecayTail();
    testDecayTiltDirection();
    testDeltaIsWetMinusDry();
    testTrimGain();
    testBlockSplitDeterminism();
    testDesignerDefaultInert();
    testDesignerAttackAndSustain();
    testDesignerLevelIndependence();
    testDesignerNoThreshold();
    testDesignerBlockSplitDeterminism();
    testEffectsAreStrong();
    testSplitControlsChangeAudio();
    testDeltaBufferIsWetMinusDry();
    testClamp();
    testSerialization();
    testDefaultsAudibleOnSteadyTone();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
