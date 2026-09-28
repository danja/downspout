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
static void fillBurst(Block& b)
{
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        float v = 0.0f;
        if (i < 64)
            v = std::sin(2.0f * 3.14159f * 880.0f * static_cast<float>(i) / 48000.0f);
        else if (i < 400)
            v = 0.3f * std::sin(2.0f * 3.14159f * 220.0f * static_cast<float>(i) / 48000.0f);
        b.inL[i] = b.inR[i] = v;
    }
}

static void run(EngineState& s, const Parameters& p, Block& b)
{
    const float* ins[] = { b.inL.data(), b.inR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    processBlock(s, p, kFrames, kSr, ins, outs, p.depth, p.sensitivity, p.decay, p.mix);
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

static void testClamp()
{
    Parameters extreme;
    extreme.depth = 5.0f;
    extreme.sensitivity = -1.0f;
    extreme.sharpness = 2.0f;
    extreme.decay = -1.0f;
    extreme.decayTilt = 3.0f;
    extreme.splitLow = 1.0f;
    extreme.splitHigh = 100.0f;  // below low*1.1, must be corrected up
    extreme.mix = 200.0f;
    extreme.trim = 100.0f;
    extreme.ccChannel = 0.0f;
    const Parameters c = clampParameters(extreme);
    check("clamp depth", c.depth <= 1.0f);
    check("clamp sensitivity", c.sensitivity >= 0.0f);
    check("clamp sharpness", c.sharpness <= 1.0f);
    check("clamp decay", c.decay >= 0.0f);
    check("clamp tilt", c.decayTilt <= 1.0f);
    check("clamp split order", c.splitHigh >= c.splitLow * 1.1f - 1e-3f);
    check("clamp mix", c.mix <= 100.0f);
    check("clamp trim", c.trim <= 12.0f);
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
    }
    check("garbage deserialises to nullopt", !deserializeParameters("nonsense\n").has_value());
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
    testClamp();
    testSerialization();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
