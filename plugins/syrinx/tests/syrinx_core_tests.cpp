#include "syrinx_engine.hpp"
#include "syrinx_params.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace downspout::syrinx;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

static void testParameterDefaults()
{
    Engine engine(48000.0f);
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto spec = getParameterSpec(i);
        const float v = engine.getParameter(i);
        CHECK(v >= spec.minimum && v <= spec.maximum);
    }
}

static void testParameterRoundtrip()
{
    Engine engine(48000.0f);
    engine.setParameter(presetParam(0, kPresetParamNoise), 0.42f);
    CHECK(std::abs(engine.getParameter(presetParam(0, kPresetParamNoise)) - 0.42f) < 1e-5f);

    engine.setParameter(kParamMasterGain, 0.55f);
    CHECK(std::abs(engine.getParameter(kParamMasterGain) - 0.55f) < 1e-5f);
}

static void testParameterClamping()
{
    Engine engine(48000.0f);
    engine.setParameter(presetParam(0, kPresetParamLevel), 9999.0f);
    const auto spec = getParameterSpec(presetParam(0, kPresetParamLevel));
    CHECK(engine.getParameter(presetParam(0, kPresetParamLevel)) <= spec.maximum);

    engine.setParameter(presetParam(0, kPresetParamBend), -99.0f);
    CHECK(engine.getParameter(presetParam(0, kPresetParamBend)) >= -1.0f);
}

static void testSilenceWithNoNotes()
{
    Engine engine(48000.0f);
    for (int i = 0; i < 512; ++i) {
        const auto frame = engine.processStereo();
        CHECK(frame.left  == 0.0f);
        CHECK(frame.right == 0.0f);
    }
}

static void testNoteOnProducesSound()
{
    Engine engine(48000.0f);
    const std::uint8_t noteOn[] = { 0x90, 69, 100 }; // A4, vel 100
    engine.handleMidi(noteOn, 3);

    bool gotSound = false;
    for (int i = 0; i < 4096 && !gotSound; ++i) {
        const auto frame = engine.processStereo();
        if (std::abs(frame.left) > 1e-6f || std::abs(frame.right) > 1e-6f)
            gotSound = true;
    }
    CHECK(gotSound);
}

static void testNoteOffEntersRelease()
{
    Engine engine(48000.0f);
    const std::uint8_t noteOn[]  = { 0x90, 60, 100 };
    const std::uint8_t noteOff[] = { 0x80, 60,   0 };
    engine.handleMidi(noteOn, 3);
    for (int i = 0; i < 2048; ++i) engine.processStereo();
    engine.handleMidi(noteOff, 3);
    // After a long silence the voice should die
    for (int i = 0; i < 48000; ++i) engine.processStereo();
    const auto frame = engine.processStereo();
    CHECK(std::abs(frame.left) < 1e-3f);
}

static void testAllPanic()
{
    Engine engine(48000.0f);
    for (int n = 36; n < 44; ++n) {
        const std::uint8_t noteOn[] = { 0x90, static_cast<std::uint8_t>(n), 100 };
        engine.handleMidi(noteOn, 3);
    }
    const std::uint8_t allOff[] = { 0xb0, 123, 0 };
    engine.handleMidi(allOff, 3);
    const auto frame = engine.processStereo();
    CHECK(frame.left  == 0.0f);
    CHECK(frame.right == 0.0f);
}

static void testOutputBounded()
{
    Engine engine(48000.0f);
    for (int note = 36; note <= 84; note += 12) {
        engine.reset();
        const std::uint8_t noteOn[] = { 0x90, static_cast<std::uint8_t>(note), 127 };
        engine.handleMidi(noteOn, 3);
        for (int i = 0; i < 2048; ++i) {
            const auto frame = engine.processStereo();
            CHECK(frame.left  >= -1.0f && frame.left  <= 1.0f);
            CHECK(frame.right >= -1.0f && frame.right <= 1.0f);
        }
    }
}

static void testPresetDecode()
{
    float params[kParameterCount];
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        params[i] = getParameterSpec(i).defaultValue;

    for (std::uint32_t p = 0; p < kPresetCount; ++p) {
        const auto pp = decodePreset(params, p);
        CHECK(pp.level >= 0.0f && pp.level <= 1.4f);
        CHECK(pp.noise >= 0.0f && pp.noise <= 1.0f);
        CHECK(pp.vibratoRateHz >= 0.0f && pp.vibratoRateHz <= 20.0f);
        CHECK(pp.bend >= -1.0f && pp.bend <= 1.0f);
        CHECK(pp.pitchSemitones >= -12.0f && pp.pitchSemitones <= 12.0f);
        CHECK(pp.durationSec >= 0.05f && pp.durationSec <= 2.0f);
        CHECK(pp.respiration >= 0.0f && pp.respiration <= 1.0f);
        CHECK(pp.amDepth >= 0.0f && pp.amDepth <= 1.0f);
        CHECK(pp.formant1Hz >= 200.0f && pp.formant1Hz <= 8000.0f);
        CHECK(pp.formant2Hz >= 200.0f && pp.formant2Hz <= 8000.0f);
        CHECK(pp.formantQ >= 0.7f && pp.formantQ <= 20.0f);
        CHECK(pp.coupling >= 0.0f && pp.coupling <= 1.0f);
    }
}

int main()
{
    testParameterDefaults();
    testParameterRoundtrip();
    testParameterClamping();
    testSilenceWithNoNotes();
    testNoteOnProducesSound();
    testNoteOffEntersRelease();
    testAllPanic();
    testOutputBounded();
    testPresetDecode();

    if (failures == 0) {
        std::printf("All Syrinx core tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) FAILED.\n", failures);
    return 1;
}
