#include "helterskelter_core.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>

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

using namespace downspout::helterskelter;

static constexpr std::uint32_t kFrames = 512;
static constexpr double kSr = 48000.0;

struct Block {
    std::array<float, kFrames> inL {}, inR {}, outL {}, outR {};
};

static Transport playing(double barBeat = 0.0, double bpm = 120.0)
{
    Transport t;
    t.valid = true;
    t.playing = true;
    t.bar = 0.0;
    t.barBeat = barBeat;
    t.beatsPerBar = 4.0;
    t.beatType = 4.0;
    t.bpm = bpm;
    return t;
}

static void run(EngineState& s, const Parameters& p, Block& b, const Transport& t)
{
    const float* ins[] = { b.inL.data(), b.inR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    processBlock(s, p, t, kFrames, kSr, ins, outs,
                 p.sensitivity, p.depth, p.resonance, p.mix);
}

// Level-normalised brightness: mean |diff| over mean |x|.
static float brightness(const float* data, std::uint32_t from, std::uint32_t to)
{
    double diff = 0.0, level = 0.0;
    for (std::uint32_t i = from + 1; i < to; ++i) {
        diff += std::fabs(data[i] - data[i - 1]);
        level += std::fabs(data[i]);
    }
    return static_cast<float>(diff / (level + 1e-6));
}

static void testSilence()
{
    Block b;
    EngineState s;
    Parameters p;
    run(s, p, b, playing());
    bool silent = true;
    for (std::uint32_t i = 0; i < kFrames && silent; ++i)
        silent = b.outL[i] == 0.0f && b.outR[i] == 0.0f;
    check("silence in gives silence out", silent);
}

static void testMixZeroDry()
{
    Block b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        b.inL[i] = b.inR[i] = 0.5f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 16.0f);
    EngineState s;
    Parameters p;
    p.mix = 0.0f;
    run(s, p, b, playing());
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(b.outL[i], b.inL[i], 1e-6f);
    check("mix 0 passes dry unchanged", same);
}

static void testBypassDry()
{
    Block b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        b.inL[i] = b.inR[i] = 0.5f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 16.0f);
    EngineState s;
    Parameters p;
    p.bypass = 1.0f;
    p.depth = 1.0f;
    run(s, p, b, playing());
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(b.outL[i], b.inL[i], 1e-6f);
    check("bypass passes dry unchanged", same);
}

static void testDeterminism()
{
    auto runOnce = []() {
        Block b;
        for (std::uint32_t i = 0; i < kFrames; ++i)
            b.inL[i] = b.inR[i] = 0.4f * std::sin(2.0f * 3.14159f * 880.0f * i / 48000.0f);
        EngineState s;
        Parameters p;
        p.mode = 2.0f;
        Transport t = playing();
        run(s, p, b, t);
        run(s, p, b, playing(0.0107));
        return b;
    };
    const Block a = runOnce();
    const Block b = runOnce();
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(a.outL[i], b.outL[i], 1e-6f) && nearlyEqual(a.outR[i], b.outR[i], 1e-6f);
    check("identical input gives identical output", same);
}

static void testBlockSplitDeterminism()
{
    Block a, b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        a.inL[i] = a.inR[i] = b.inL[i] = b.inR[i]
            = 0.4f * std::sin(2.0f * 3.14159f * 330.0f * i / 48000.0f);
    Parameters p;
    p.mode = 1.0f;
    { EngineState s; run(s, p, a, playing()); }
    {
        EngineState s;
        constexpr std::uint32_t kHalf = kFrames / 2;
        const float* ins[] = { b.inL.data(), b.inR.data() };
        float* outs[] = { b.outL.data(), b.outR.data() };
        processBlock(s, p, playing(), kHalf, kSr, ins, outs,
                     p.sensitivity, p.depth, p.resonance, p.mix);
        const double dq = kHalf * (120.0 / 60.0 / kSr);
        const float* ins2[] = { b.inL.data() + kHalf, b.inR.data() + kHalf };
        float* outs2[] = { b.outL.data() + kHalf, b.outR.data() + kHalf };
        processBlock(s, p, playing(dq), kFrames - kHalf, kSr, ins2, outs2,
                     p.sensitivity, p.depth, p.resonance, p.mix);
    }
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(a.outL[i], b.outL[i], 1e-6f);
    check("split blocks match single block", same);
}

// Louder input drives the envelope, which opens the filter: the loud half
// must be brighter per unit level than the quiet half.
static void testEnvelopeOpensFilter()
{
    // Harmonically rich buzz: the lowpass cutoff audibly changes its
    // level-normalised brightness (a pure sine would not discriminate).
    Block b;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        const float amp = (i < kFrames / 2) ? 0.1f : 0.8f;
        const float ph = 2.0f * 3.14159f * 220.0f * i / 48000.0f;
        b.inL[i] = b.inR[i] = amp * (std::sin(ph) + 0.5f * std::sin(2.0f * ph)
            + 0.33f * std::sin(3.0f * ph) + 0.25f * std::sin(4.0f * ph)) * 0.5f;
    }
    EngineState s;
    Parameters p;
    p.mode = 0.0f;
    p.depth = 1.0f;
    p.sensitivity = 0.8f;
    p.baseFreq = 400.0f;
    run(s, p, b, playing());
    const float quiet = brightness(b.outL.data(), 100, 240);
    const float loud = brightness(b.outL.data(), 300, 500);
    check("envelope opens filter on loud input", loud > quiet * 1.3f);
}

// BBT ADSR mode on a midrange tone: block RMS must breathe with the cycle.
static void testBbtCycleBreathes()
{
    constexpr std::uint32_t kBig = 4096;
    std::array<float, kBig> in {}, out {};
    for (std::uint32_t i = 0; i < kBig; ++i)
        in[i] = 0.5f * std::sin(2.0f * 3.14159f * 800.0f * i / 48000.0f);
    EngineState s;
    Parameters p;
    p.mode = 1.0f;
    p.depth = 1.0f;
    p.baseFreq = 400.0f;
    p.division = 2.0f;  // 4-beat cycle
    p.gateBeats = 2.0f;
    p.attack = 5.0f;
    p.decay = 100.0f;
    p.sustain = 0.1f;
    p.release = 150.0f;
    Transport t = playing();
    const double dq = kBig * (120.0 / 60.0 / kSr);
    float lo = 1e9f, hi = 0.0f;
    for (int k = 0; k < 48; ++k) {  // ~4.1 s, just over two 4-beat cycles
        const float* ins[] = { in.data(), in.data() };
        float* outs[] = { out.data(), out.data() };
        processBlock(s, p, t, kBig, kSr, ins, outs,
                     p.sensitivity, p.depth, p.resonance, p.mix);
        double sum = 0.0;
        for (float v : out) sum += v * v;
        const float rms = static_cast<float>(std::sqrt(sum / kBig));
        lo = std::min(lo, rms);
        hi = std::max(hi, rms);
        t.barBeat += dq;
    }
    check("bbt cycle breathes", hi - lo > 0.08f);
}

// Invert flips the gate: the two patterns must differ somewhere.
static void testInvertFlipsPattern()
{
    constexpr std::uint32_t kBig = 1024;
    std::array<float, kBig> in {}, outA {}, outB {};
    for (std::uint32_t i = 0; i < kBig; ++i)
        in[i] = 0.5f * std::sin(2.0f * 3.14159f * 800.0f * i / 48000.0f);
    auto runMode = [&](float invert, std::array<float, kBig>& out) {
        EngineState s;
        Parameters p;
        p.mode = 1.0f;
        p.depth = 1.0f;
        p.invert = invert;
        Transport t = playing();
        const double dq = kBig * (120.0 / 60.0 / kSr);
        for (int k = 0; k < 8; ++k) {
            const float* ins[] = { in.data(), in.data() };
            float* outs[] = { out.data(), out.data() };
            processBlock(s, p, t, kBig, kSr, ins, outs,
                         p.sensitivity, p.depth, p.resonance, p.mix);
            t.barBeat += dq;
        }
    };
    runMode(0.0f, outA);
    runMode(1.0f, outB);
    bool differ = false;
    for (std::uint32_t i = 0; i < kBig && !differ; ++i)
        differ = !nearlyEqual(outA[i], outB[i], 1e-4f);
    check("invert flips the pattern", differ);
}

// Stopped transport: ADSR releases to a static filter; DC passes at unity.
static void testStoppedTransportSettles()
{
    Block b;
    for (auto& v : b.inL) v = 0.3f;
    for (auto& v : b.inR) v = 0.3f;
    EngineState s;
    Parameters p;
    p.mode = 1.0f;
    Transport t;
    double mean = 0.0;
    for (int k = 0; k < 60; ++k) {
        run(s, p, b, t);
        mean = 0.0;
        for (std::uint32_t i = kFrames / 2; i < kFrames; ++i)
            mean += b.outL[i];
        mean /= (kFrames / 2);
        bool finite = true;
        for (std::uint32_t i = 0; i < kFrames && finite; ++i)
            finite = std::isfinite(b.outL[i]) && std::isfinite(b.outR[i])
                && std::fabs(b.outL[i]) < 2.0f;
        if (!finite) {
            check("stopped transport stays bounded", false);
            return;
        }
    }
    check("stopped transport stays bounded", true);
    check("stopped transport settles to static filter", nearlyEqual(static_cast<float>(mean), 0.3f, 0.05f));
}

// Effective CC overrides: mix 0 forces dry; sensitivity 0 vs 1 changes tone.
static void testEffectiveOverrides()
{
    Block dry, wet;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        dry.inL[i] = dry.inR[i] = wet.inL[i] = wet.inR[i]
            = 0.4f * std::sin(2.0f * 3.14159f * 660.0f * i / 48000.0f);
    Parameters p;
    p.mode = 0.0f;
    p.depth = 1.0f;
    {
        EngineState s;
        const float* ins[] = { dry.inL.data(), dry.inR.data() };
        float* outs[] = { dry.outL.data(), dry.outR.data() };
        processBlock(s, p, playing(), kFrames, kSr, ins, outs, 0.8f, 1.0f, 4.0f, 0.0f);
    }
    {
        EngineState s;
        const float* ins[] = { wet.inL.data(), wet.inR.data() };
        float* outs[] = { wet.outL.data(), wet.outR.data() };
        processBlock(s, p, playing(), kFrames, kSr, ins, outs, 0.8f, 1.0f, 4.0f, 100.0f);
    }
    bool drySame = true, wetDiff = false;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        drySame = drySame && nearlyEqual(dry.outL[i], dry.inL[i], 1e-6f);
        if (!nearlyEqual(wet.outL[i], wet.inL[i], 1e-4f)) wetDiff = true;
    }
    check("effective mix 0 forces dry", drySame);
    check("effective mix 100 processes", wetDiff);
}

static void testClamp()
{
    Parameters extreme;
    extreme.mode = 9.0f;
    extreme.sensitivity = -1.0f;
    extreme.depth = 5.0f;
    extreme.resonance = 100.0f;
    extreme.baseFreq = 1.0f;
    extreme.division = 9.0f;
    extreme.gateBeats = 99.0f;
    extreme.attack = -5.0f;
    extreme.sustain = 3.0f;
    extreme.invert = 4.0f;
    extreme.mix = 200.0f;
    extreme.trim = 100.0f;
    extreme.bypass = 5.0f;
    extreme.ccChannel = 0.0f;
    const Parameters c = clampParameters(extreme);
    check("clamp mode", c.mode <= 2.0f);
    check("clamp sensitivity", c.sensitivity >= 0.0f);
    check("clamp depth", c.depth <= 1.0f);
    check("clamp resonance", c.resonance <= 12.0f);
    check("clamp baseFreq", c.baseFreq >= 100.0f);
    check("clamp division", c.division <= 3.0f);
    check("clamp gateBeats", c.gateBeats <= 8.0f);
    check("clamp attack", c.attack >= 1.0f);
    check("clamp sustain", c.sustain <= 1.0f);
    check("clamp invert", c.invert <= 1.0f);
    check("clamp mix", c.mix <= 100.0f);
    check("clamp trim", c.trim <= 12.0f);
    check("clamp bypass", c.bypass <= 1.0f);
    check("clamp ccChannel", c.ccChannel >= 1.0f);
}

static void testSerialization()
{
    Parameters p;
    p.mode = 2.0f;
    p.sensitivity = 0.8f;
    p.depth = 0.9f;
    p.resonance = 8.0f;
    p.baseFreq = 600.0f;
    p.division = 3.0f;
    p.gateBeats = 3.0f;
    p.attack = 10.0f;
    p.decay = 200.0f;
    p.sustain = 0.5f;
    p.release = 300.0f;
    p.invert = 1.0f;
    p.mix = 75.0f;
    p.trim = -3.0f;
    p.bypass = 1.0f;

    const std::string text = serializeParameters(p);
    const auto restored = deserializeParameters(text);
    check("serialization round-trip valid", restored.has_value());
    if (restored.has_value()) {
        const float eps = 1e-4f;
        check("rt mode", restored->mode == p.mode);
        check("rt sensitivity", std::abs(restored->sensitivity - p.sensitivity) < eps);
        check("rt depth", std::abs(restored->depth - p.depth) < eps);
        check("rt resonance", std::abs(restored->resonance - p.resonance) < eps);
        check("rt baseFreq", std::abs(restored->baseFreq - p.baseFreq) < eps);
        check("rt division", restored->division == p.division);
        check("rt gateBeats", std::abs(restored->gateBeats - p.gateBeats) < eps);
        check("rt attack", std::abs(restored->attack - p.attack) < eps);
        check("rt decay", std::abs(restored->decay - p.decay) < eps);
        check("rt sustain", std::abs(restored->sustain - p.sustain) < eps);
        check("rt release", std::abs(restored->release - p.release) < eps);
        check("rt invert", restored->invert == p.invert);
        check("rt mix", std::abs(restored->mix - p.mix) < eps);
        check("rt trim", std::abs(restored->trim - p.trim) < eps);
        check("rt bypass", restored->bypass == p.bypass);
    }
    check("garbage deserialises to nullopt", !deserializeParameters("nonsense\n").has_value());
}

int main()
{
    testSilence();
    testMixZeroDry();
    testBypassDry();
    testDeterminism();
    testBlockSplitDeterminism();
    testEnvelopeOpensFilter();
    testBbtCycleBreathes();
    testInvertFlipsPattern();
    testStoppedTransportSettles();
    testEffectiveOverrides();
    testClamp();
    testSerialization();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
