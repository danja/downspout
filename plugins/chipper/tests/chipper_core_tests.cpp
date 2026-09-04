#include "chipper_core.hpp"

#include <array>
#include <cassert>
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

// ── Silence in → silence out ────────────────────────────────────────────────

static void testSilence()
{
    using namespace downspout::chipper;

    constexpr uint32_t kFrames = 128;
    std::array<float, kFrames> inL{}, inR{}, outL{}, outR{};
    const float* ins[]  = { inL.data(), inR.data() };
    float*       outs[] = { outL.data(), outR.data() };

    Parameters p;
    EngineState state;
    processBlock(state, p, kFrames, ins, outs);

    for (uint32_t n = 0; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "silence L[%u]", n);
        check(name, nearlyEqual(outL[n], 0.0f));
        std::snprintf(name, sizeof(name), "silence R[%u]", n);
        check(name, nearlyEqual(outR[n], 0.0f));
    }
}

// ── Mix=0 passes dry signal unchanged ───────────────────────────────────────

static void testDryPassthrough()
{
    using namespace downspout::chipper;

    constexpr uint32_t kFrames = 64;
    std::array<float, kFrames> inL{}, inR{}, outL{}, outR{};
    for (uint32_t i = 0; i < kFrames; ++i)
        inL[i] = inR[i] = 0.5f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 16.0f);

    const float* ins[]  = { inL.data(), inR.data() };
    float*       outs[] = { outL.data(), outR.data() };

    Parameters p;
    p.mix = 0.0f;
    EngineState state;
    processBlock(state, p, kFrames, ins, outs);

    for (uint32_t n = 0; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "dry passthrough L[%u]", n);
        check(name, nearlyEqual(outL[n], inL[n]));
    }
}

// ── Clamp round-trip ─────────────────────────────────────────────────────────

static void testClamp()
{
    using namespace downspout::chipper;

    Parameters extreme;
    extreme.bitDepth   = 99.0f;
    extreme.rateDiv    = -5.0f;
    extreme.jitter     = 5.0f;
    extreme.mix        = 200.0f;
    extreme.outputGain = 100.0f;

    const Parameters c = clampParameters(extreme);

    check("clamp bitDepth max",   c.bitDepth   <= 16.0f);
    check("clamp rateDiv min",    c.rateDiv    >= 1.0f);
    check("clamp jitter max",     c.jitter     <= 1.0f);
    check("clamp mix max",        c.mix        <= 100.0f);
    check("clamp outputGain max", c.outputGain <= 12.0f);
}

// ── Bit depth 16 passes through unmodified ──────────────────────────────────

static void testBitDepth16Passthrough()
{
    using namespace downspout::chipper;

    constexpr uint32_t kFrames = 32;
    std::array<float, kFrames> inL{}, inR{}, outL{}, outR{};
    for (uint32_t i = 0; i < kFrames; ++i)
        inL[i] = inR[i] = static_cast<float>(i) / static_cast<float>(kFrames) * 0.8f - 0.4f;

    const float* ins[]  = { inL.data(), inR.data() };
    float*       outs[] = { outL.data(), outR.data() };

    Parameters p;
    p.bitDepth = 16.0f;
    p.rateDiv  = 1.0f;   // no sample-rate reduction
    p.mix      = 100.0f;
    EngineState state;
    processBlock(state, p, kFrames, ins, outs);

    // With rateDiv=1 and bitDepth=16, output should equal input exactly
    for (uint32_t n = 0; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "16-bit passthrough L[%u]", n);
        check(name, nearlyEqual(outL[n], inL[n], 1e-6f));
    }
}

// ── Sample-hold: output stays constant for rateDiv frames ───────────────────

static void testSampleHold()
{
    using namespace downspout::chipper;

    // Use a ramp input so held output must be constant per segment
    constexpr uint32_t kFrames = 32;
    constexpr int      kDiv    = 8;
    std::array<float, kFrames> inL{}, inR{}, outL{}, outR{};
    for (uint32_t i = 0; i < kFrames; ++i)
        inL[i] = inR[i] = static_cast<float>(i) * 0.01f;

    const float* ins[]  = { inL.data(), inR.data() };
    float*       outs[] = { outL.data(), outR.data() };

    Parameters p;
    p.bitDepth = 16.0f;  // no quantisation noise
    p.rateDiv  = static_cast<float>(kDiv);
    p.jitter   = 0.0f;
    p.mix      = 100.0f;
    EngineState state;
    processBlock(state, p, kFrames, ins, outs);

    // Each segment of kDiv frames must be constant
    for (int seg = 0; seg < static_cast<int>(kFrames) / kDiv; ++seg) {
        const float ref = outL[static_cast<std::size_t>(seg * kDiv)];
        for (int f = 1; f < kDiv; ++f) {
            const int idx = seg * kDiv + f;
            char name[64];
            std::snprintf(name, sizeof(name), "hold constant seg %d frame %d", seg, f);
            check(name, nearlyEqual(outL[static_cast<std::size_t>(idx)], ref, 1e-6f));
        }
    }
}

// ── Output gain scales output ────────────────────────────────────────────────

static void testOutputGain()
{
    using namespace downspout::chipper;

    constexpr uint32_t kFrames = 16;
    std::array<float, kFrames> inL{}, inR{}, out0{}, out6{};
    for (uint32_t i = 0; i < kFrames; ++i)
        inL[i] = inR[i] = 0.25f;

    const float* ins[] = { inL.data(), inR.data() };
    float* outs0[]     = { out0.data(), out0.data() };
    float* outs6[]     = { out6.data(), out6.data() };

    Parameters p;
    p.bitDepth = 16.0f;
    p.rateDiv  = 1.0f;
    p.mix      = 100.0f;

    p.outputGain = 0.0f;
    { EngineState s; processBlock(s, p, kFrames, ins, outs0); }

    p.outputGain = 6.0206f;  // ≈ ×2
    { EngineState s; processBlock(s, p, kFrames, ins, outs6); }

    for (uint32_t n = 0; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "gain ×2 frame %u", n);
        check(name, nearlyEqual(out6[n], out0[n] * 2.0f, 1e-4f));
    }
}

// ── Serialization round-trip ─────────────────────────────────────────────────

static void testSerialization()
{
    using namespace downspout::chipper;

    Parameters p;
    p.bitDepth   = 4.0f;
    p.rateDiv    = 16.0f;
    p.jitter     = 0.3f;
    p.mix        = 75.0f;
    p.outputGain = -3.0f;

    const std::string text     = serializeParameters(p);
    const auto        restored = deserializeParameters(text);

    check("serialization round-trip valid", restored.has_value());
    if (restored.has_value()) {
        check("rt bitDepth",   nearlyEqual(restored->bitDepth,   p.bitDepth));
        check("rt rateDiv",    nearlyEqual(restored->rateDiv,    p.rateDiv));
        check("rt jitter",     nearlyEqual(restored->jitter,     p.jitter));
        check("rt mix",        nearlyEqual(restored->mix,        p.mix));
        check("rt outputGain", nearlyEqual(restored->outputGain, p.outputGain));
    }
}

int main()
{
    testSilence();
    testDryPassthrough();
    testClamp();
    testBitDepth16Passthrough();
    testSampleHold();
    testOutputGain();
    testSerialization();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
