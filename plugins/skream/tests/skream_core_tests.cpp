#include "skream_core.hpp"
#include "skream_presets.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

static int gPassed = 0;
static int gFailed = 0;

static void check(const char* name, bool condition)
{
    if (condition) { ++gPassed; }
    else { std::printf("FAIL: %s\n", name); ++gFailed; }
}

static bool nearlyEqual(float a, float b, float eps = 1e-4f)
{
    return std::abs(a - b) <= eps;
}

// ── clampParameters ─────────────────────────────────────────────────────────

static void testClamp()
{
    using namespace downspout::skream;

    Parameters nan;
    nan.cutoff = std::numeric_limits<float>::quiet_NaN();
    nan.scream = -9999.0f;
    nan.resonance = 999.0f;
    const Parameters c = clampParameters(nan);

    check("clamp: NaN cutoff  → default",  nearlyEqual(c.cutoff,    85.0f));
    check("clamp: out-of-range scream",     nearlyEqual(c.scream,     0.0f));
    check("clamp: out-of-range resonance",  nearlyEqual(c.resonance, 100.0f));
    check("clamp: inputGain stays clamped", c.inputGain >= -24.0f && c.inputGain <= 24.0f);
    check("clamp: outputGain stays clamped", c.outputGain >= -24.0f && c.outputGain <= 0.0f);
    check("clamp: ccChannel min", nearlyEqual(c.ccChannel, 1.0f));
}

// ── Silence passthrough ──────────────────────────────────────────────────────

static void testSilence()
{
    using namespace downspout::skream;

    constexpr uint32_t kFrames = 128;
    std::array<float, kFrames> inL{}, inR{}, outL{}, outR{};

    AudioBlock audio;
    audio.inputs[0]  = inL.data();
    audio.inputs[1]  = inR.data();
    audio.outputs[0] = outL.data();
    audio.outputs[1] = outR.data();

    Parameters p;
    EngineState state;

    processBlock(state, p, kFrames, 44100.0, audio, p.cutoff, p.scream);

    for (uint32_t n = 0; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "silence L[%u] finite", n);
        check(name, std::isfinite(outL[n]));
        std::snprintf(name, sizeof(name), "silence R[%u] finite", n);
        check(name, std::isfinite(outR[n]));
        std::snprintf(name, sizeof(name), "silence L[%u] ~0", n);
        check(name, nearlyEqual(outL[n], 0.0f, 1e-5f));
        std::snprintf(name, sizeof(name), "silence R[%u] ~0", n);
        check(name, nearlyEqual(outR[n], 0.0f, 1e-5f));
    }
}

// ── Serialization round-trip ─────────────────────────────────────────────────

static void testSerialize()
{
    using namespace downspout::skream;

    Parameters p;
    p.inputGain  =  6.5f;
    p.cutoff     = 72.3f;
    p.scream     = 55.0f;
    p.resonance  = 80.0f;
    p.mix        = 90.0f;
    p.outputGain = -9.0f;
    p.ccCutoff   = 74.0f;
    p.ccScream   = 20.0f;
    p.ccChannel  =  3.0f;

    const std::string text = serializeParameters(p);
    const auto result = deserializeParameters(text);

    check("serialize: result present", result.has_value());
    if (!result) return;

    check("serialize: inputGain",  nearlyEqual(result->inputGain,  p.inputGain));
    check("serialize: cutoff",     nearlyEqual(result->cutoff,     p.cutoff));
    check("serialize: scream",     nearlyEqual(result->scream,     p.scream));
    check("serialize: resonance",  nearlyEqual(result->resonance,  p.resonance));
    check("serialize: mix",        nearlyEqual(result->mix,        p.mix));
    check("serialize: outputGain", nearlyEqual(result->outputGain, p.outputGain));
    check("serialize: ccCutoff",   nearlyEqual(result->ccCutoff,   p.ccCutoff));
    check("serialize: ccScream",   nearlyEqual(result->ccScream,   p.ccScream));
    check("serialize: ccChannel",  nearlyEqual(result->ccChannel,  p.ccChannel));
}

// ── Preset count and validity ────────────────────────────────────────────────

static void testPresets()
{
    using namespace downspout::skream;

    check("presets: count == 10", kPresetCount == 10);

    for (int i = 0; i < kPresetCount; ++i) {
        const Parameters& p = kPresets[i].params;
        const Parameters  c = clampParameters(p);
        char name[64];
        std::snprintf(name, sizeof(name), "preset %d '%s' valid after clamp", i, kPresets[i].name);
        check(name,
              nearlyEqual(c.inputGain,  p.inputGain)  &&
              nearlyEqual(c.cutoff,     p.cutoff)      &&
              nearlyEqual(c.scream,     p.scream)      &&
              nearlyEqual(c.resonance,  p.resonance)   &&
              nearlyEqual(c.mix,        p.mix)         &&
              nearlyEqual(c.outputGain, p.outputGain));
    }
}

// ── Output bounded with tone input ──────────────────────────────────────────

static void testBounded()
{
    using namespace downspout::skream;

    constexpr uint32_t kFrames = 256;
    std::array<float, kFrames> inL, inR, outL{}, outR{};

    // Sine wave at 440Hz, 44100Hz sample rate, 0dBFS
    for (uint32_t i = 0; i < kFrames; ++i) {
        inL[i] = inR[i] = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
    }

    AudioBlock audio;
    audio.inputs[0]  = inL.data();
    audio.inputs[1]  = inR.data();
    audio.outputs[0] = outL.data();
    audio.outputs[1] = outR.data();

    // Test with max resonance (most aggressive settings)
    Parameters p;
    p.resonance = 100.0f;
    p.cutoff    = 50.0f;
    p.scream    = 50.0f;
    EngineState state;

    // Process several blocks to let state settle
    for (int b = 0; b < 4; ++b)
        processBlock(state, p, kFrames, 44100.0, audio, p.cutoff, p.scream);

    for (uint32_t n = 0; n < kFrames; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "bounded L[%u] finite", n);
        check(name, std::isfinite(outL[n]));
        std::snprintf(name, sizeof(name), "bounded R[%u] finite", n);
        check(name, std::isfinite(outR[n]));
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    testClamp();
    testSilence();
    testSerialize();
    testPresets();
    testBounded();

    std::printf("%d passed, %d failed\n", gPassed, gFailed);
    return gFailed > 0 ? 1 : 0;
}
