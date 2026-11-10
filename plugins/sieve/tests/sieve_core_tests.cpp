#include "downspout/test_assert.h"

#include "sieve_engine.hpp"
#include "sieve_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::sieve;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;
constexpr double kSineRms = 0.5 / std::sqrt(2.0);

// Long enough to skip the one-frame start-up delay, then measure a steady state.
constexpr int kTotal = 16384;
constexpr int kBlock = 128;
constexpr int kStart = 4096;
constexpr int kTail = 512;

struct Buffers {
    std::vector<float> inL = std::vector<float>(std::size_t(kTotal));
    std::vector<float> inR = std::vector<float>(std::size_t(kTotal));
    std::vector<float> outL = std::vector<float>(std::size_t(kTotal));
    std::vector<float> outR = std::vector<float>(std::size_t(kTotal));
};

double rms(const std::vector<float>& v)
{
    double sum = 0.0;
    const int count = kTotal - kStart - kTail;
    for (int i = 0; i < count; ++i) {
        const double x = v[std::size_t(kStart + i)];
        sum += x * x;
    }
    return std::sqrt(sum / count);
}

double db(const double ratio)
{
    return 20.0 * std::log10(ratio);
}

double toneGain(const Buffers& b)
{
    return db(rms(b.outL) / kSineRms);
}

double correlationAt(const std::vector<float>& a, const std::vector<float>& b, const int lag)
{
    double aa = 0.0, bb = 0.0, ab = 0.0;
    const int count = kTotal - kStart - kTail - lag;
    for (int i = 0; i < count; ++i) {
        const double x = a[std::size_t(kStart + i)];
        const double y = b[std::size_t(kStart + i + lag)];
        aa += x * x;
        bb += y * y;
        ab += x * y;
    }
    return ab / std::sqrt(aa * bb);
}

Params unityMask()
{
    Params p;
    p.scale = 0.0f;  // chromatic
    p.source = 0.0f;
    p.width = 50.0f;
    p.floor = 0.0f;  // every bin's gain is exactly 1
    p.tilt = 0.0f;
    p.mix = 1.0f;
    p.level = 0.0f;
    return p;
}

Params toneParams()
{
    Params p;
    p.scale = 1.0f;  // major
    p.root = 0.0f;
    p.source = 0.0f;
    p.width = 50.0f;
    p.floor = -60.0f;
    p.tilt = 0.0f;
    p.mix = 1.0f;
    p.level = 0.0f;
    return p;
}

Buffers renderTone(const double freq, const Params& p, Engine& e, const int block = kBlock)
{
    Buffers b;
    for (int i = 0; i < kTotal; ++i) {
        const float x = static_cast<float>(0.5 * std::sin(2.0 * kPi * freq * i / kSampleRate));
        b.inL[std::size_t(i)] = x;
        b.inR[std::size_t(i)] = x;
    }
    for (int i = 0; i < kTotal; i += block) {
        const int n = std::min(block, kTotal - i);
        processBlock(e, p, b.inL.data() + i, b.inR.data() + i, b.outL.data() + i, b.outR.data() + i,
                     static_cast<std::uint32_t>(n));
    }
    return b;
}

Buffers renderTone(const double freq, const Params& p, const int block = kBlock)
{
    Engine e;
    activate(e, kSampleRate);
    return renderTone(freq, p, e, block);
}

void testActivateAndSilence()
{
    Engine e;
    activate(e, kSampleRate);
    assert(e.allocated);
    assert(latencySamples(e) == kFftSize);

    std::vector<float> silence(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 1.0f), outR(kBlock, 1.0f);
    Params p = toneParams();
    processBlock(e, p, silence.data(), silence.data(), outL.data(), outR.data(), std::uint32_t(kBlock));
    for (int i = 0; i < kBlock; ++i) {
        assert(std::isfinite(outL[std::size_t(i)]) && std::isfinite(outR[std::size_t(i)]));
        assert(std::fabs(outL[std::size_t(i)]) < 1.0e-7f);
        assert(std::fabs(outR[std::size_t(i)]) < 1.0e-7f);
    }

    reset(e);
    assert(e.heldCount == 0);
}

void testUnityMaskIsAPureDelay()
{
    Engine e;
    activate(e, kSampleRate);
    const Params p = unityMask();
    Buffers b;

    unsigned state = 123456789u;
    for (int i = 0; i < kTotal; ++i) {
        state = state * 1664525u + 1013904223u;
        b.inL[std::size_t(i)] = static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
        b.inR[std::size_t(i)] = b.inL[std::size_t(i)];
    }
    for (int i = 0; i < kTotal; i += kBlock) {
        processBlock(e, p, b.inL.data() + i, b.inR.data() + i, b.outL.data() + i, b.outR.data() + i,
                     std::uint32_t(kBlock));
    }

    const double inputRms = std::sqrt([&] {
        double sum = 0.0;
        const int count = kTotal - kStart - kTail;
        for (int i = 0; i < count; ++i) {
            const double x = b.inL[std::size_t(kStart + i)];
            sum += x * x;
        }
        return sum / count;
    }());
    assert(std::fabs(db(rms(b.outL) / inputRms)) < 0.05);
    assert(correlationAt(b.inL, b.outL, kFftSize) > 0.9999);

    int bestLag = -1;
    double best = -1.0;
    for (int lag = 0; lag <= 2048; ++lag) {
        const double c = correlationAt(b.inL, b.outL, lag);
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }
    assert(bestLag == kFftSize);
    assert(best > 0.9999);
}

void testUnityMaskImpulse()
{
    Engine e;
    activate(e, kSampleRate);
    const Params p = unityMask();
    const int n = 4096;
    std::vector<float> in(n, 0.0f), outL(n), outR(n);
    in[100] = 1.0f;
    processBlock(e, p, in.data(), in.data(), outL.data(), outR.data(), std::uint32_t(n));

    for (int i = 0; i < n; ++i) {
        if (i == 100 + kFftSize) {
            assert(std::fabs(outL[std::size_t(i)] - 1.0f) < 1.0e-3f);
            assert(std::fabs(outR[std::size_t(i)] - 1.0f) < 1.0e-3f);
        } else {
            assert(std::fabs(outL[std::size_t(i)]) < 1.0e-4f);
            assert(std::fabs(outR[std::size_t(i)]) < 1.0e-4f);
        }
    }
}

void testBlockSizeDoesNotChangeTheResult()
{
    const Params p = toneParams();
    const Buffers reference = renderTone(440.0, p, 128);
    for (const int block : {1, 7, 64, 255, 256, 512, 1024, 4096}) {
        const Buffers rendered = renderTone(440.0, p, block);
        for (int i = 0; i < kTotal; ++i) {
            assert(std::fabs(rendered.outL[std::size_t(i)] - reference.outL[std::size_t(i)]) < 1.0e-6f);
            assert(std::fabs(rendered.outR[std::size_t(i)] - reference.outR[std::size_t(i)]) < 1.0e-6f);
        }
    }
}

void testOnScaleTonesPass()
{
    Params chromatic = toneParams();
    chromatic.scale = 0.0f;
    const double a4 = toneGain(renderTone(440.0, chromatic));
    assert(a4 > -1.5);

    // 984.375 Hz is exactly bin 21, so a 50-cent width around B5 passes it cleanly.
    const double b5 = toneGain(renderTone(984.375, toneParams()));
    assert(b5 > -1.5);

    // The same A4 is outside C major; a narrow sieve must reject it.
    const double a4Major = toneGain(renderTone(440.0, toneParams()));
    assert(a4Major < -10.0);
    assert(a4Major < a4 - 10.0);

    // Widening the band recovers most of an off-scale tone.
    Params wide = toneParams();
    wide.width = 400.0f;
    const double a4Wide = toneGain(renderTone(440.0, wide));
    assert(a4Wide > a4Major + 10.0);
}

void testFloorIsTheMinimumGain()
{
    // Width 1 cent makes every bin far from a semitone fall all the way to the floor.
    Params p = toneParams();
    p.scale = 0.0f;
    p.width = 1.0f;
    for (const float floorDb : {-20.0f, -12.0f, -6.0f}) {
        p.floor = floorDb;
        const double gain = toneGain(renderTone(450.0, p));
        assert(std::fabs(gain - double(floorDb)) < 0.1);
    }
}

void testMixZeroIsDelayedDry()
{
    Engine e;
    activate(e, kSampleRate);
    Params p = toneParams();
    p.mix = 0.0f;
    Buffers b;
    for (int i = 0; i < kTotal; ++i) {
        const float x = static_cast<float>(0.5 * std::sin(0.017 * i));
        b.inL[std::size_t(i)] = x;
        b.inR[std::size_t(i)] = x;
    }
    for (int i = 0; i < kTotal; i += 64) {
        processBlock(e, p, b.inL.data() + i, b.inR.data() + i, b.outL.data() + i, b.outR.data() + i, 64);
    }

    for (int i = 0; i < kFftSize; ++i) {
        assert(std::fabs(b.outL[std::size_t(i)]) < 1.0e-7f);
    }
    for (int i = kFftSize; i < kTotal; ++i) {
        assert(std::fabs(b.outL[std::size_t(i)] - b.inL[std::size_t(i - kFftSize)]) < 1.0e-6f);
    }
}

void testLevelScalesTheOutput()
{
    Params p = unityMask();
    p.scale = 0.0f;
    p.level = 0.0f;
    const double unity = toneGain(renderTone(440.0, p));
    p.level = -6.0f;
    assert(std::fabs(toneGain(renderTone(440.0, p)) - (unity - 6.0)) < 0.05);
    p.level = 6.0f;
    assert(std::fabs(toneGain(renderTone(440.0, p)) - (unity + 6.0)) < 0.05);
}

void testRootAndScaleChangeThePassband()
{
    // B5 is bin-centred and in C major; C# major does not contain B, so it is rejected.
    Params cMajor = toneParams();
    cMajor.root = 0.0f;
    Params cSharpMajor = toneParams();
    cSharpMajor.root = 1.0f;
    const double inScale = toneGain(renderTone(984.375, cMajor));
    const double outOfScale = toneGain(renderTone(984.375, cSharpMajor));
    assert(inScale > -1.5);
    assert(outOfScale < inScale - 5.0);
}

void testTiltChangesOddAndEvenBins()
{
    // 468.75 Hz is bin 10 (even), 515.625 Hz is bin 11 (odd).
    Params p = toneParams();
    p.scale = 0.0f;
    p.width = 200.0f;
    p.tilt = -1.0f;
    const double evenDown = toneGain(renderTone(468.75, p));
    const double oddDown = toneGain(renderTone(515.625, p));
    p.tilt = 1.0f;
    const double evenUp = toneGain(renderTone(468.75, p));
    const double oddUp = toneGain(renderTone(515.625, p));
    assert(evenUp < evenDown - 0.3);
    assert(oddUp > oddDown + 0.3);
}

void testHeldMidiAndChannelFilter()
{
    assert(midiChannelMatches(0x90, 0.0f));
    assert(midiChannelMatches(0x80, 0.0f));
    assert(midiChannelMatches(0x90, 1.0f));
    assert(midiChannelMatches(0x91, 2.0f));
    assert(!midiChannelMatches(0x91, 1.0f));
    assert(!midiChannelMatches(0x90, 3.0f));
    assert(!midiChannelMatches(0xB0, 0.0f));

    Engine e;
    activate(e, kSampleRate);
    Params p = toneParams();
    p.source = 1.0f;  // held MIDI, with the scale as the fallback
    p.midiCh = 2.0f;

    // 421.875 Hz is bin 9, exactly G#4. C major does not contain G#, so with no
    // held notes the scale fallback rejects it.
    const double fallback = toneGain(renderTone(421.875, p, e));

    // G#4 on channel 1 is ignored; the same note on channel 2 is the passband.
    assert(!handleMidiNote(e, 0x90, 68, 100, p.midiCh));
    const double ignored = toneGain(renderTone(421.875, p, e));
    assert(std::fabs(ignored - fallback) < 0.01);

    assert(handleMidiNote(e, 0x91, 68, 100, p.midiCh));
    const double held = toneGain(renderTone(421.875, p, e));
    assert(held > -5.0);
    assert(held > fallback + 15.0);

    // Note-on with velocity 0, then a real note-off, both release it.
    assert(handleMidiNote(e, 0x91, 68, 0, p.midiCh));
    const double releasedByZero = toneGain(renderTone(421.875, p, e));
    assert(std::fabs(releasedByZero - fallback) < 0.01);

    assert(handleMidiNote(e, 0x91, 68, 100, p.midiCh));
    assert(toneGain(renderTone(421.875, p, e)) > -5.0);
    assert(handleMidiNote(e, 0x81, 68, 0, p.midiCh));
    assert(std::fabs(toneGain(renderTone(421.875, p, e)) - fallback) < 0.01);
}

void testScaleTablesAndNames()
{
    // Independent copy of the 24 canonical semitone sets in docs/scales.md.
    constexpr std::array<std::array<int, 12>, kScaleCount> expected = {{
        {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},  // chromatic
        {{0, 2, 4, 5, 7, 9, 11}},                   // major
        {{0, 2, 4, 5, 7, 9, 11}},                   // ionian
        {{0, 2, 3, 5, 7, 8, 10}},                   // minor
        {{0, 2, 3, 5, 7, 8, 11}},                   // harmonic minor
        {{0, 2, 3, 5, 7, 9, 11}},                   // melodic minor
        {{0, 2, 3, 5, 7, 9, 10}},                   // dorian
        {{0, 1, 3, 5, 7, 8, 10}},                   // phrygian
        {{0, 2, 4, 6, 7, 9, 11}},                   // lydian
        {{0, 2, 4, 5, 7, 9, 10}},                   // mixolydian
        {{0, 1, 3, 5, 6, 8, 10}},                   // locrian
        {{0, 1, 4, 5, 7, 8, 10}},                   // phrygian dominant
        {{0, 1, 3, 5, 7, 9, 11}},                   // neapolitan major
        {{0, 1, 3, 5, 7, 8, 11}},                   // neapolitan minor
        {{0, 2, 4, 7, 9}},                          // pentatonic major
        {{0, 3, 5, 7, 10}},                         // pentatonic minor
        {{0, 3, 5, 6, 7, 10}},                      // blues
        {{0, 2, 4, 6, 8, 10}},                      // whole tone
        {{0, 1, 3, 4, 6, 8, 10}},                   // altered
        {{0, 1, 3, 4, 6, 7, 9, 10}},                // half-whole diminished
        {{0, 2, 3, 5, 6, 8, 9, 11}},                // whole-half diminished
        {{0, 2, 4, 5, 7, 9, 10, 11}},               // bebop dominant
        {{0, 2, 4, 5, 7, 8, 9, 11}},                // bebop major
        {{0, 2, 3, 4, 5, 7, 9, 10}},                // bebop minor
    }};

    assert(kScaleCount == 24);
    assert(kScaleIntervals[0][0] == 0);
    assert(std::string(scaleName(0)) == "Chromatic");
    assert(std::string(scaleName(1)) == "Major");
    assert(std::string(scaleName(3)) == "Minor");
    assert(std::string(scaleName(23)) == "Bebop Minor");
    assert(std::string(scaleName(-5)) == "Chromatic");
    assert(std::string(scaleName(99)) == "Bebop Minor");
    assert(std::string(rootName(0)) == "C");
    assert(std::string(rootName(11)) == "B");
    assert(std::string(rootName(12)) == "C");
    assert(std::string(rootName(-1)) == "B");

    for (int s = 0; s < kScaleCount; ++s) {
        const auto& row = kScaleIntervals[std::size_t(s)];
        assert(row == expected[std::size_t(s)]);
        assert(row[0] == 0);
        int used = 1;
        while (used < 12 && row[std::size_t(used)] != 0) ++used;
        assert(used == kScaleDegreeCount[std::size_t(s)]);
        for (int i = 1; i < used; ++i) {
            assert(row[std::size_t(i)] > row[std::size_t(i - 1)]);
            assert(row[std::size_t(i)] < 12);
        }
    }
}

}  // namespace

int main()
{
    testActivateAndSilence();
    testUnityMaskIsAPureDelay();
    testUnityMaskImpulse();
    testBlockSizeDoesNotChangeTheResult();
    testOnScaleTonesPass();
    testFloorIsTheMinimumGain();
    testMixZeroIsDelayedDry();
    testLevelScalesTheOutput();
    testRootAndScaleChangeThePassband();
    testTiltChangesOddAndEvenBins();
    testHeldMidiAndChannelFilter();
    testScaleTablesAndNames();
    std::printf("sieve core tests passed\n");
    return 0;
}
