#include "damiano_core.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

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

int main()
{
    testSilence();
    testOutputBounded();
    testDryPassthrough();
    testClamp();
    testSerialization();
    testOutputGain();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
