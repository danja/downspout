#include "skream_core.hpp"
#include "skream_presets.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

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
    check("clamp: track default", nearlyEqual(c.track, 0.0f));
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
    p.track      = 55.0f;
    p.ccCutoff   = 74.0f;
    p.ccScream   = 20.0f;
    p.ccChannel  =  3.0f;
    p.morph      = -42.5f;

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
    check("serialize: track",      nearlyEqual(result->track,      p.track));
    check("serialize: ccCutoff",   nearlyEqual(result->ccCutoff,   p.ccCutoff));
    check("serialize: ccScream",   nearlyEqual(result->ccScream,   p.ccScream));
    check("serialize: ccChannel",  nearlyEqual(result->ccChannel,  p.ccChannel));
    check("serialize: morph",      nearlyEqual(result->morph,      p.morph));

    // A project saved before Morph existed has no "morph" key: it must load as off.
    const auto legacy = deserializeParameters("version=1\ncutoff=60\nscream=30\n");
    check("serialize: legacy text loads", legacy.has_value());
    if (legacy) check("serialize: legacy morph is off", nearlyEqual(legacy->morph, 0.0f));
}

// ── Morph (LP -> HP shape follows the cutoff) ────────────────────────────────

// RMS of a sine run through the engine after a settling period.
static float sineRms(const downspout::skream::Parameters& params, float hz)
{
    using namespace downspout::skream;
    constexpr double kSr = 48000.0;
    constexpr uint32_t kFrames = 8192;
    std::vector<float> in(kFrames), out(kFrames);
    for (uint32_t n = 0; n < kFrames; ++n)
        in[n] = 0.1f * std::sin(2.0f * 3.14159265f * hz * static_cast<float>(n) / static_cast<float>(kSr));

    AudioBlock audio;
    audio.inputs  = { in.data(), in.data() };
    audio.outputs = { out.data(), out.data() };

    Parameters p = params;
    p.resonance  = 0.0f;   // keep the loop out of the way: this tests the forward filter
    p.outputGain = 0.0f;
    p.mix        = 100.0f;
    p.track      = 0.0f;
    EngineState state;
    processBlock(state, clampParameters(p), kFrames, kSr, audio, p.cutoff, p.scream);

    double sum = 0.0;
    for (uint32_t n = kFrames / 2; n < kFrames; ++n)
        sum += static_cast<double>(out[n]) * out[n];
    return static_cast<float>(std::sqrt(sum / (kFrames / 2)));
}

static void testMorph()
{
    using namespace downspout::skream;
    constexpr float kLow = 100.0f, kHigh = 8000.0f;

    // Cutoff 70 % is about 2.5 kHz: well between the two test tones.
    Parameters plain;
    plain.cutoff = 70.0f;
    const float plainRatio = sineRms(plain, kHigh) / sineRms(plain, kLow);
    check("morph: off is a low-pass (highs well below lows)", plainRatio < 0.2f);

    Parameters up = plain;
    up.morph = 100.0f;   // t = cutoff position = 0.7: mostly high-pass
    const float upRatio = sineRms(up, kHigh) / sineRms(up, kLow);
    check("morph: + at a high cutoff turns the filter towards high-pass", upRatio > 1.0f);

    // Positive morph at a very low cutoff stays low-pass: the shape follows the cutoff.
    Parameters upLow = up;
    upLow.cutoff = 5.0f;   // t = 0.05
    const float upLowRatio = sineRms(upLow, kHigh) / std::max(sineRms(upLow, kLow), 1.0e-9f);
    check("morph: + at a low cutoff is still essentially low-pass", upLowRatio < 0.2f || sineRms(upLow, kHigh) < 0.01f);

    // Negative morph is the mirror image: high-pass at a low cutoff. A high-pass passes
    // both 100 Hz and 8 kHz, so use a tone well below the (about 80 Hz) cutoff: it must
    // be removed, while the low-pass it replaces would have kept it.
    constexpr float kSub = 40.0f;
    Parameters down = plain;
    down.cutoff = 20.0f;   // about 80 Hz
    down.morph  = -100.0f; // t = 1 - 0.2 = 0.8
    Parameters downOff = down;
    downOff.morph = 0.0f;
    const float subOff  = sineRms(downOff, kSub);
    const float subDown = sineRms(down, kSub);
    check("morph: off keeps a tone below the cutoff", subOff > 0.05f);
    check("morph: - at a low cutoff removes a tone below it (high-pass)", subDown < 0.2f * subOff);
    check("morph: - at a low cutoff passes highs the low-pass blocked",
          sineRms(down, kHigh) > 10.0f * std::max(sineRms(downOff, kHigh), 1.0e-6f));

    // Continuity: a whisper of morph must not jump.
    Parameters tiny = plain;
    tiny.morph = 1.0f;
    const float a = sineRms(plain, 1000.0f);
    const float b = sineRms(tiny, 1000.0f);
    check("morph: continuous around zero", std::fabs(a - b) < 0.05f * std::max(a, 1.0e-6f) + 1.0e-4f);

    // Clamping.
    Parameters wild;
    wild.morph = 5000.0f;
    check("morph: clamps high", nearlyEqual(clampParameters(wild).morph, 100.0f));
    wild.morph = -5000.0f;
    check("morph: clamps low", nearlyEqual(clampParameters(wild).morph, -100.0f));
    wild.morph = std::numeric_limits<float>::quiet_NaN();
    check("morph: NaN falls back to off", nearlyEqual(clampParameters(wild).morph, 0.0f));

    // Extreme settings stay finite.
    Parameters extreme;
    extreme.morph = 100.0f;
    extreme.cutoff = 100.0f;
    extreme.resonance = 100.0f;
    check("morph: finite at extremes", std::isfinite(sineRms(extreme, 440.0f)));
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
              nearlyEqual(c.outputGain, p.outputGain)  &&
              nearlyEqual(c.track,      p.track));
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

    // Test with max resonance and full track coupling (most aggressive settings)
    Parameters p;
    p.resonance = 100.0f;
    p.cutoff    = 50.0f;
    p.scream    = 50.0f;
    p.track     = 100.0f;
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
    testMorph();
    testPresets();
    testBounded();

    std::printf("%d passed, %d failed\n", gPassed, gFailed);
    return gFailed > 0 ? 1 : 0;
}
