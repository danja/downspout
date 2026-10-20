#include "sinevox_core_types.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace downspout::sinevox;

namespace {

constexpr double kPi = 3.14159265358979323846;

using Signal = std::vector<float>;
using FormantSpec = std::vector<std::pair<double, double>>;  // {centre Hz, bandwidth Hz}

// A /a/-like vowel: formants 700, 1200 and 2600 Hz.
const FormantSpec kVowel = {{700.0, 90.0}, {1200.0, 100.0}, {2600.0, 140.0}};

// Glottal-ish pulse train through a cascade of two-pole resonators, scaled to
// a fixed RMS so every test sees the same level.
Signal makeVowel(const std::size_t n, const double sr, const FormantSpec& formants,
                 const double f0 = 110.0, const double rms = 0.1)
{
    Signal x(n, 0.0f);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        phase += f0 / sr;
        if (phase >= 1.0) {
            phase -= 1.0;
            x[i] = 1.0f;
        }
    }
    // Real speech falls about 6 dB/oct once glottal slope and lip radiation
    // are combined; a bare impulse train would be flat and bias the analysis.
    {
        double y1 = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            y1 = x[i] + 0.98 * y1;
            x[i] = static_cast<float>(y1);
        }
    }
    for (const auto& [fc, bw] : formants) {
        const double r = std::exp(-kPi * bw / sr);
        const double c1 = 2.0 * r * std::cos(2.0 * kPi * fc / sr);
        const double c2 = -r * r;
        double y1 = 0.0, y2 = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double y = x[i] + c1 * y1 + c2 * y2;
            y2 = y1;
            y1 = y;
            x[i] = static_cast<float>(y);
        }
    }
    double ss = 0.0;
    for (const float v : x)
        ss += static_cast<double>(v) * v;
    const double gain = rms / std::sqrt(ss / static_cast<double>(n) + 1.0e-30);
    for (float& v : x)
        v = static_cast<float>(v * gain);
    return x;
}

Signal makeNoise(const std::size_t n, const float amp)
{
    Signal x(n);
    std::uint32_t s = 12345u;
    for (float& v : x) {
        s = s * 1664525u + 1013904223u;
        v = amp * (static_cast<float>(s >> 8) / 8388608.0f - 1.0f);
    }
    return x;
}

Signal silence(const std::size_t n) { return Signal(n, 0.0f); }

Signal concat(Signal a, const Signal& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Signal scaled(Signal a, const float g)
{
    for (float& v : a)
        v *= g;
    return a;
}

struct Rig {
    explicit Rig(const double rate = 48000.0) : sr(rate)
    {
        state.sampleRate = rate;
        activate(state);
    }

    double sr;
    EngineState state {};
    Parameters p {};
    Signal outL, outR;
    std::vector<MidiOutEvent> midi;

    // Runs the signals through the engine in 256-frame blocks, appending to
    // outL/outR. A missing carrier is silence.
    void run(const Signal& speech, const Signal& carrier = {})
    {
        constexpr std::size_t kBlock = 256;
        const Signal quiet(kBlock, 0.0f);
        for (std::size_t at = 0; at < speech.size(); at += kBlock) {
            const std::size_t n = std::min(kBlock, speech.size() - at);
            Signal block(speech.begin() + static_cast<long>(at), speech.begin() + static_cast<long>(at + n));
            Signal car = quiet;
            if (!carrier.empty()) {
                car.assign(n, 0.0f);
                for (std::size_t i = 0; i < n && at + i < carrier.size(); ++i)
                    car[i] = carrier[at + i];
            }
            std::array<float, kBlock> l {}, r {};
            const float* ins[] = {block.data(), block.data(), car.data(), car.data()};
            float* outs[] = {l.data(), r.data()};
            processBlock(state, p, static_cast<std::uint32_t>(n), sr, ins, outs);
            outL.insert(outL.end(), l.begin(), l.begin() + static_cast<long>(n));
            outR.insert(outR.end(), r.begin(), r.begin() + static_cast<long>(n));

            MidiOutBlock mb;
            collectMidiOut(state, p, static_cast<std::uint32_t>(n), mb);
            for (int i = 0; i < mb.count; ++i)
                midi.push_back(mb.events[static_cast<std::size_t>(i)]);
        }
    }

    void clearOutput() { outL.clear(); outR.clear(); midi.clear(); }
};

double rmsOf(const Signal& x, std::size_t from, std::size_t to)
{
    double ss = 0.0;
    for (std::size_t i = from; i < to; ++i)
        ss += static_cast<double>(x[i]) * x[i];
    return std::sqrt(ss / static_cast<double>(std::max<std::size_t>(1, to - from)));
}

double tailRms(const Signal& x, const std::size_t count)
{
    return rmsOf(x, x.size() - count, x.size());
}

// Hann-windowed Goertzel magnitude over the last `count` samples.
double goertzel(const Signal& x, const std::size_t count, const double hz, const double sr)
{
    const std::size_t from = x.size() - count;
    const double w = 2.0 * kPi * hz / sr;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(count));
        const double s = x[from + i] * win + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    return std::sqrt(re * re + im * im) / static_cast<double>(count);
}

// Strongest bin in [lo, hi].
double bandPeak(const Signal& x, const std::size_t count, const double lo, const double hi, const double sr)
{
    double best = 0.0;
    for (double hz = lo; hz <= hi; hz += 4.0)
        best = std::max(best, goertzel(x, count, hz, sr));
    return best;
}

// Mean power per bin in [lo, hi]: robust for noise-excited output.
double bandPower(const Signal& x, const std::size_t count, const double lo, const double hi, const double sr)
{
    double sum = 0.0;
    int n = 0;
    for (double hz = lo; hz <= hi; hz += 10.0, ++n) {
        const double m = goertzel(x, count, hz, sr);
        sum += m * m;
    }
    return sum / std::max(1, n);
}

std::vector<float> activeFreqs(const EngineState& st)
{
    std::vector<float> f;
    for (int i = 0; i < kMaxFormants; ++i) {
        if (st.outFreq[static_cast<std::size_t>(i)] > 0.0f && st.outAmpDb[static_cast<std::size_t>(i)] > -60.0f)
            f.push_back(st.outFreq[static_cast<std::size_t>(i)]);
    }
    std::sort(f.begin(), f.end());
    return f;
}

bool hasFormantNear(const std::vector<float>& found, const double target, const double tolerance)
{
    for (const float f : found) {
        if (std::fabs(f - target) / target < tolerance)
            return true;
    }
    return false;
}

Parameters sws()
{
    Parameters p;
    p.mode = 1.0f;
    p.formants = 3.0f;
    return p;
}

// ── Tests ──────────────────────────────────────────────────────────────────

void testParameterClampAndSpecs()
{
    Parameters p;
    p.formants = 99.0f;
    p.mode = -4.0f;
    p.qScale = 0.0f;
    p.shift = 1000.0f;
    p.outChannel = 40.0f;
    p.gateDb = NAN;
    const Parameters c = clampParameters(p);
    assert(c.formants == 8.0f);
    assert(c.mode == 0.0f);
    assert(c.qScale == 0.25f);
    assert(c.shift == 12.0f);
    assert(c.outChannel == 16.0f);
    assert(c.gateDb == -55.0f);

    assert(kParameterSpecs.size() == kParameterCount);
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        const ParamSpec& s = kParameterSpecs[i];
        assert(s.minimum <= s.defaultValue && s.defaultValue <= s.maximum);
        assert(s.output == (i >= kInputParameterCount));
    }
    // The default Parameters must be the spec defaults.
    const Parameters d;
    assert(d.formants == kParameterSpecs[index(ParamId::formants)].defaultValue);
    assert(d.qScale == kParameterSpecs[index(ParamId::qScale)].defaultValue);
    assert(d.ccMode == kParameterSpecs[index(ParamId::ccMode)].defaultValue);
}

void testSerializationRoundTrip()
{
    Parameters p;
    p.mode = 2.0f;
    p.formants = 6.0f;
    p.shift = -3.5f;
    p.qScale = 2.5f;
    p.midiOut = 2.0f;
    p.ccTilt = 74.0f;
    const auto restored = deserializeParameters(serializeParameters(p));
    assert(restored.has_value());
    assert(restored->mode == 2.0f);
    assert(restored->formants == 6.0f);
    assert(std::fabs(restored->shift + 3.5f) < 1.0e-4f);
    assert(std::fabs(restored->qScale - 2.5f) < 1.0e-4f);
    assert(restored->midiOut == 2.0f);
    assert(restored->ccTilt == 74.0f);
    assert(!deserializeParameters("garbage without equals").has_value());
}

void testFormantTracking(const double sr)
{
    Rig rig(sr);
    rig.p = sws();
    rig.run(makeVowel(static_cast<std::size_t>(sr), sr, kVowel));
    const auto found = activeFreqs(rig.state);
    assert(found.size() >= 3);
    assert(hasFormantNear(found, 700.0, 0.08));
    assert(hasFormantNear(found, 1200.0, 0.08));
    assert(hasFormantNear(found, 2600.0, 0.08));

    // Q is f / bandwidth; the vowel's resonances are 8 - 20 wide in Q terms.
    for (int i = 0; i < kMaxFormants; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        if (std::fabs(rig.state.outFreq[idx] - 700.0f) < 50.0f) {
            assert(rig.state.outQ[idx] > 3.0f && rig.state.outQ[idx] < 25.0f);
        }
    }
    // The level reading is the analysed input level (rms 0.1 is about -20 dB).
    assert(rig.state.outLevelDb > -30.0f && rig.state.outLevelDb < -10.0f);
}

void testSineWaveSpeechSpectrum()
{
    Rig rig;
    rig.p = sws();
    rig.run(makeVowel(48000, 48000.0, kVowel));
    const std::size_t n = 12000;
    const double f1 = bandPeak(rig.outL, n, 650, 750, 48000.0);
    const double f2 = bandPeak(rig.outL, n, 1150, 1250, 48000.0);
    const double f3 = bandPeak(rig.outL, n, 2500, 2700, 48000.0);
    const double gap1 = bandPeak(rig.outL, n, 380, 480, 48000.0);
    const double gap2 = bandPeak(rig.outL, n, 1850, 1950, 48000.0);
    assert(f1 > 6.0 * gap1);
    assert(f2 > 6.0 * gap1);
    assert(f3 > 6.0 * gap2);
    // Pure sines: there is no energy at the harmonics of the source.
    const double harmonic = bandPeak(rig.outL, n, 880, 960, 48000.0);
    assert(f1 > 6.0 * harmonic);
}

void testVocoderShapesCarrier()
{
    Rig rig;
    rig.p.mode = 0.0f;
    rig.p.formants = 3.0f;
    rig.run(makeVowel(48000, 48000.0, kVowel), makeNoise(48000, 0.3f));
    const std::size_t n = 12000;
    const double on1 = bandPower(rig.outL, n, 660, 740, 48000.0);
    const double off1 = bandPower(rig.outL, n, 380, 460, 48000.0);
    const double on2 = bandPower(rig.outL, n, 1160, 1240, 48000.0);
    const double off2 = bandPower(rig.outL, n, 900, 980, 48000.0);
    assert(on1 > 4.0 * off1);
    assert(on2 > 4.0 * off2);
    assert(tailRms(rig.outL, n) > 1.0e-3);

    // Speech stops: the formants fade and the output falls to nothing.
    rig.clearOutput();
    rig.run(silence(48000), makeNoise(48000, 0.3f));
    assert(tailRms(rig.outL, 4800) < 1.0e-6);
}

void testVocoderIsStereo()
{
    Rig rig;
    rig.p.formants = 3.0f;
    const Signal vowel = makeVowel(24000, 48000.0, kVowel);
    // Different carrier on each side: the outputs must follow their own side.
    Signal noiseL = makeNoise(24000, 0.3f);
    constexpr std::size_t kBlock = 256;
    for (std::size_t at = 0; at + kBlock <= vowel.size(); at += kBlock) {
        const Signal sp(vowel.begin() + static_cast<long>(at), vowel.begin() + static_cast<long>(at + kBlock));
        const Signal cl(noiseL.begin() + static_cast<long>(at), noiseL.begin() + static_cast<long>(at + kBlock));
        const Signal cr = silence(kBlock);
        std::array<float, kBlock> l {}, r {};
        const float* ins[] = {sp.data(), sp.data(), cl.data(), cr.data()};
        float* outs[] = {l.data(), r.data()};
        processBlock(rig.state, rig.p, kBlock, 48000.0, ins, outs);
        rig.outL.insert(rig.outL.end(), l.begin(), l.end());
        rig.outR.insert(rig.outR.end(), r.begin(), r.end());
    }
    assert(tailRms(rig.outL, 6000) > 1.0e-3);
    assert(tailRms(rig.outR, 6000) == 0.0);
}

void testSilenceIsSilent()
{
    for (const float mode : {0.0f, 1.0f, 2.0f}) {
        Rig rig;
        rig.p.mode = mode;
        rig.run(silence(24000), makeNoise(24000, 0.5f));
        assert(tailRms(rig.outL, 12000) == 0.0);
        assert(rig.state.outActive == 0);
    }
}

void testGateSuppressesQuietSpeech()
{
    const Signal quiet = makeVowel(48000, 48000.0, kVowel, 110.0, 0.002);  // about -54 dB
    Rig open;
    open.p = sws();
    open.p.gateDb = -80.0f;
    open.run(quiet);
    assert(open.state.outActive >= 2);

    Rig shut;
    shut.p = sws();
    shut.p.gateDb = -40.0f;
    shut.run(quiet);
    assert(shut.state.outActive == 0);
    assert(tailRms(shut.outL, 6000) < 1.0e-6);
}

void testFormantCountControlsTracks()
{
    Rig one;
    one.p = sws();
    one.p.formants = 1.0f;
    one.run(makeVowel(48000, 48000.0, kVowel));
    assert(one.state.outActive == 1);

    Rig four;
    four.p = sws();
    four.p.formants = 4.0f;
    four.run(makeVowel(48000, 48000.0, kVowel));
    assert(four.state.outActive >= 3);
}

void testBandwidthLimit()
{
    const FormantSpec broad = {{700.0, 450.0}, {1500.0, 500.0}, {2600.0, 600.0}};
    const Signal sig = makeVowel(48000, 48000.0, broad);
    Rig wide;
    wide.p = sws();
    wide.p.bwLimit = 3000.0f;
    wide.run(sig);
    assert(wide.state.outActive >= 1);

    Rig narrow;
    narrow.p = sws();
    narrow.p.bwLimit = 100.0f;
    narrow.run(sig);
    assert(narrow.state.outActive == 0);
}

void testShiftTransposesFormants()
{
    Rig rig;
    rig.p = sws();
    rig.p.shift = 12.0f;
    rig.run(makeVowel(48000, 48000.0, kVowel));
    const std::size_t n = 12000;
    assert(bandPeak(rig.outL, n, 1350, 1450, 48000.0) > 6.0 * bandPeak(rig.outL, n, 650, 750, 48000.0));
    assert(bandPeak(rig.outL, n, 2300, 2500, 48000.0) > 6.0 * bandPeak(rig.outL, n, 1150, 1250, 48000.0));
}

void testLevelAndDry()
{
    const Signal vowel = makeVowel(24000, 48000.0, kVowel);
    Rig a, b;
    a.p = sws();
    b.p = sws();
    b.p.levelDb = 6.0206f;
    a.run(vowel);
    b.run(vowel);
    const double ratio = tailRms(b.outL, 6000) / tailRms(a.outL, 6000);
    assert(std::fabs(ratio - 2.0) < 0.05);

    // Speech passes through at unity with no carrier to vocode.
    Rig d;
    d.p.mode = 0.0f;
    d.p.dry = 100.0f;
    d.run(vowel);
    for (std::size_t i = 0; i < vowel.size(); ++i)
        assert(std::fabs(d.outL[i] - vowel[i]) < 1.0e-6f);
}

void testBypassPassesSpeech()
{
    const Signal vowel = makeVowel(12000, 48000.0, kVowel);
    Rig rig;
    rig.p = sws();
    rig.p.bypass = 1.0f;
    rig.p.dry = 0.0f;
    rig.run(vowel);
    for (std::size_t i = 0; i < vowel.size(); ++i) {
        assert(rig.outL[i] == vowel[i]);
        assert(rig.outR[i] == vowel[i]);
    }
}

void testSpreadMakesStereo()
{
    const Signal vowel = makeVowel(48000, 48000.0, kVowel);
    Rig mono;
    mono.p = sws();
    mono.p.spread = 0.0f;
    mono.run(vowel);
    for (std::size_t i = 0; i < mono.outL.size(); ++i)
        assert(std::fabs(mono.outL[i] - mono.outR[i]) < 1.0e-6f);

    Rig wide;
    wide.p = sws();
    wide.p.spread = 100.0f;
    wide.run(vowel);
    // F1 is panned left of F3, so the channels hold them in different balance.
    const std::size_t n = 12000;
    const double lowBalance = bandPeak(wide.outL, n, 650, 750, 48000.0) / bandPeak(wide.outR, n, 650, 750, 48000.0);
    const double highBalance = bandPeak(wide.outL, n, 2500, 2700, 48000.0) / bandPeak(wide.outR, n, 2500, 2700, 48000.0);
    assert(lowBalance > 2.0 * highBalance);
}

void testTiltChangesBalance()
{
    const Signal vowel = makeVowel(48000, 48000.0, kVowel);
    Rig flat;
    flat.p = sws();
    flat.run(vowel);
    Rig tilted;
    tilted.p = sws();
    tilted.p.tilt = 12.0f;
    tilted.run(vowel);
    const std::size_t n = 12000;
    auto balance = [&](const Rig& r) {
        return bandPeak(r.outL, n, 2500, 2700, 48000.0) / bandPeak(r.outL, n, 650, 750, 48000.0);
    };
    // +12 dB/oct over the ~1.9 octaves between F1 and F3 is about 22 dB.
    assert(balance(tilted) > 8.0 * balance(flat));
}

void testQScaleNarrowsVocoderBands()
{
    const Signal vowel = makeVowel(48000, 48000.0, kVowel);
    const Signal noise = makeNoise(48000, 0.3f);
    auto contrast = [&](const float qScale) {
        Rig rig;
        rig.p.formants = 3.0f;
        rig.p.qScale = qScale;
        rig.run(vowel, noise);
        const std::size_t n = 12000;
        return bandPower(rig.outL, n, 660, 740, 48000.0) / bandPower(rig.outL, n, 900, 980, 48000.0);
    };
    assert(contrast(4.0f) > 2.0 * contrast(0.25f));
}

void testCarrierNormalisation()
{
    const Signal vowel = makeVowel(48000, 48000.0, kVowel);
    const Signal noise = makeNoise(48000, 0.1f);
    auto level = [&](const float norm, const float carrierGain) {
        Rig rig;
        rig.p.formants = 3.0f;
        rig.p.carrierNorm = norm;
        rig.run(vowel, scaled(noise, carrierGain));
        return tailRms(rig.outL, 12000);
    };
    // Normalised: a 4x hotter carrier gives about the same output.
    assert(std::fabs(level(100.0f, 4.0f) / level(100.0f, 1.0f) - 1.0) < 0.3);
    // Not normalised: the output follows the carrier.
    assert(level(0.0f, 4.0f) / level(0.0f, 1.0f) > 3.0);
}

void testModeSelectsEngine()
{
    const Signal vowel = makeVowel(24000, 48000.0, kVowel);
    const Signal noise = makeNoise(24000, 0.3f);
    Rig voc, sine, both;
    voc.p.mode = 0.0f;
    sine.p.mode = 1.0f;
    both.p.mode = 2.0f;
    voc.run(vowel, noise);
    sine.run(vowel, noise);
    both.run(vowel, noise);
    // SWS ignores the carrier entirely: same output with or without it.
    Rig sineNoCarrier;
    sineNoCarrier.p.mode = 1.0f;
    sineNoCarrier.run(vowel);
    for (std::size_t i = 0; i < vowel.size(); ++i)
        assert(sine.outL[i] == sineNoCarrier.outL[i]);
    // Vocoder output has no energy without a carrier.
    Rig vocNoCarrier;
    vocNoCarrier.p.mode = 0.0f;
    vocNoCarrier.run(vowel);
    assert(tailRms(vocNoCarrier.outL, 6000) == 0.0);
    // Both is the sum.
    for (std::size_t i = 0; i < vowel.size(); ++i)
        assert(std::fabs(both.outL[i] - (voc.outL[i] + sine.outL[i])) < 1.0e-5f);
}

void testGlideSlowsFrequencyChange()
{
    const Signal a = makeVowel(24000, 48000.0, kVowel);
    const FormantSpec moved = {{1000.0, 90.0}, {1500.0, 100.0}, {2600.0, 140.0}};
    const Signal b = makeVowel(4000, 48000.0, moved);  // about 80 ms
    auto lowest = [&](const float glide) {
        Rig rig;
        rig.p = sws();
        rig.p.glideMs = glide;
        rig.run(concat(a, b));
        const auto f = activeFreqs(rig.state);
        return f.empty() ? 0.0f : f.front();
    };
    assert(lowest(1.0f) > 900.0f);
    assert(lowest(200.0f) < 850.0f);
}

void testReleaseSetsTail()
{
    const Signal vowel = makeVowel(24000, 48000.0, kVowel);
    auto tail = [&](const float release) {
        Rig rig;
        rig.p = sws();
        rig.p.releaseMs = release;
        rig.run(concat(vowel, silence(7200)));
        return rmsOf(rig.outL, 24000 + 4800, 24000 + 7200);
    };
    assert(tail(500.0f) > 20.0 * tail(5.0f));
}

void testAttackSetsRise()
{
    const Signal vowel = makeVowel(7200, 48000.0, kVowel);
    auto early = [&](const float attack) {
        Rig rig;
        rig.p = sws();
        rig.p.attackMs = attack;
        rig.run(concat(silence(4800), vowel));
        return rmsOf(rig.outL, 4800 + 1200, 4800 + 2400);
    };
    assert(early(1.0f) > 2.0 * early(100.0f));
}

void testSampleRates()
{
    testFormantTracking(44100.0);
    testFormantTracking(96000.0);
}

void testMidiCcAndChannel()
{
    EngineState st;
    Parameters p;
    const std::uint8_t shiftTop[] = {0xb0, 1, 127};
    assert(handleMidi(st, p, shiftTop, 3));
    assert(p.shift == 12.0f);
    const std::uint8_t shiftMid[] = {0xb0, 1, 0};
    assert(handleMidi(st, p, shiftMid, 3));
    assert(p.shift == -12.0f);

    const std::uint8_t qTop[] = {0xb0, 2, 127};
    handleMidi(st, p, qTop, 3);
    assert(std::fabs(p.qScale - 4.0f) < 1.0e-4f);
    const std::uint8_t qBottom[] = {0xb0, 2, 0};
    handleMidi(st, p, qBottom, 3);
    assert(std::fabs(p.qScale - 0.25f) < 1.0e-4f);

    const std::uint8_t formants[] = {0xb0, 3, 127};
    handleMidi(st, p, formants, 3);
    assert(p.formants == 8.0f);
    const std::uint8_t mode[] = {0xb0, 5, 127};
    handleMidi(st, p, mode, 3);
    assert(p.mode == 2.0f);
    const std::uint8_t modeLow[] = {0xb0, 5, 0};
    handleMidi(st, p, modeLow, 3);
    assert(p.mode == 0.0f);

    // Another channel is ignored.
    p.shift = 0.0f;
    const std::uint8_t other[] = {0xb1, 1, 127};
    assert(!handleMidi(st, p, other, 3));
    assert(p.shift == 0.0f);

    // CC number 0 disables an override.
    p.ccShift = 0.0f;
    assert(!handleMidi(st, p, shiftTop, 3));
    assert(p.shift == 0.0f);

    // A reassigned controller number follows.
    p.ccShift = 74.0f;
    const std::uint8_t moved[] = {0xb0, 74, 127};
    assert(handleMidi(st, p, moved, 3));
    assert(p.shift == 12.0f);

    // Short or non-controller messages are harmless.
    assert(!handleMidi(st, p, shiftTop, 1));
    const std::uint8_t pitchBend[] = {0xe0, 0, 64};
    assert(!handleMidi(st, p, pitchBend, 3));
}

void testNoteShiftTransposes()
{
    const Signal vowel = makeVowel(48000, 48000.0, kVowel);
    Rig rig;
    rig.p = sws();
    rig.p.noteShift = 1.0f;
    const std::uint8_t on[] = {0x90, 72, 100};  // an octave above middle C
    handleMidi(rig.state, rig.p, on, 3);
    rig.run(vowel);
    const std::size_t n = 12000;
    assert(bandPeak(rig.outL, n, 1350, 1450, 48000.0) > 6.0 * bandPeak(rig.outL, n, 650, 750, 48000.0));

    // Releasing the note returns to the plain formants.
    const std::uint8_t off[] = {0x80, 72, 0};
    handleMidi(rig.state, rig.p, off, 3);
    rig.clearOutput();
    rig.run(vowel);
    assert(bandPeak(rig.outL, n, 650, 750, 48000.0) > 6.0 * bandPeak(rig.outL, n, 1350, 1450, 48000.0));

    // With Note Shift off, held notes do nothing.
    Rig inert;
    inert.p = sws();
    handleMidi(inert.state, inert.p, on, 3);
    inert.run(vowel);
    assert(bandPeak(inert.outL, n, 650, 750, 48000.0) > 6.0 * bandPeak(inert.outL, n, 1350, 1450, 48000.0));

    // Last note wins; middle C is no offset.
    Rig stack;
    stack.p = sws();
    stack.p.noteShift = 1.0f;
    const std::uint8_t c4[] = {0x90, 60, 100};
    handleMidi(stack.state, stack.p, c4, 3);
    stack.run(vowel);
    assert(bandPeak(stack.outL, n, 650, 750, 48000.0) > 6.0 * bandPeak(stack.outL, n, 1350, 1450, 48000.0));
}

void testMidiOutOffByDefault()
{
    Rig rig;
    rig.p = sws();
    rig.run(makeVowel(48000, 48000.0, kVowel));
    assert(rig.midi.empty());
}

void testMidiOutControllers()
{
    Rig rig;
    rig.p = sws();
    rig.p.midiOut = 1.0f;
    rig.p.outChannel = 3.0f;
    rig.run(makeVowel(48000, 48000.0, kVowel));
    assert(!rig.midi.empty());

    std::array<int, kMaxFormants> freqCc {};
    std::array<int, kMaxFormants> ampCc {};
    freqCc.fill(-1);
    ampCc.fill(-1);
    for (const MidiOutEvent& ev : rig.midi) {
        assert((ev.data[0] & 0xf0u) == 0xb0u);
        assert((ev.data[0] & 0x0fu) == 2u);  // channel 3
        const int cc = ev.data[1];
        if (cc >= kOutFreqCcBase && cc < kOutFreqCcBase + kMaxFormants)
            freqCc[static_cast<std::size_t>(cc - kOutFreqCcBase)] = ev.data[2];
        else if (cc >= kOutAmpCcBase && cc < kOutAmpCcBase + kMaxFormants)
            ampCc[static_cast<std::size_t>(cc - kOutAmpCcBase)] = ev.data[2];
        else
            assert(false);
    }
    // Every reported frequency decodes to one of the vowel's formants.
    int decoded = 0;
    for (int i = 0; i < kMaxFormants; ++i) {
        if (freqCc[static_cast<std::size_t>(i)] < 0 || ampCc[static_cast<std::size_t>(i)] <= 0)
            continue;
        const double hz = kOutFreqMinHz * std::pow(kOutFreqMaxHz / kOutFreqMinHz,
                                                   freqCc[static_cast<std::size_t>(i)] / 127.0);
        const bool known = hasFormantNear({static_cast<float>(hz)}, 700.0, 0.12)
                        || hasFormantNear({static_cast<float>(hz)}, 1200.0, 0.12)
                        || hasFormantNear({static_cast<float>(hz)}, 2600.0, 0.12);
        assert(known);
        ++decoded;
    }
    assert(decoded >= 3);

    // Silence drops every amplitude controller back to zero.
    rig.clearOutput();
    rig.run(silence(48000));
    std::array<int, kMaxFormants> lastAmp = ampCc;
    for (const MidiOutEvent& ev : rig.midi) {
        if (ev.data[1] >= kOutAmpCcBase)
            lastAmp[static_cast<std::size_t>(ev.data[1] - kOutAmpCcBase)] = ev.data[2];
    }
    for (int i = 0; i < kMaxFormants; ++i)
        assert(lastAmp[static_cast<std::size_t>(i)] <= 0);
}

void testMidiOutNotes()
{
    Rig rig;
    rig.p = sws();
    rig.p.midiOut = 2.0f;
    rig.run(makeVowel(48000, 48000.0, kVowel));
    int ons = 0, offs = 0;
    for (const MidiOutEvent& ev : rig.midi) {
        const unsigned status = ev.data[0] & 0xf0u;
        if (status == 0x90) {
            ++ons;
            const int note = ev.data[1];
            // 700 Hz -> 77 and 1200 Hz -> 86. F3 (2600 Hz, note 100) sits at
            // -56 dB here, under the -50 dB note threshold, so it stays silent.
            assert(std::abs(note - 77) <= 1 || std::abs(note - 86) <= 1);
            assert(ev.data[2] >= 1);
        } else if (status == 0x80) {
            ++offs;
        }
    }
    assert(ons >= 2);

    // Silence releases every sounding note; none is left hanging.
    rig.run(silence(48000));
    ons = offs = 0;
    for (const MidiOutEvent& ev : rig.midi) {
        if ((ev.data[0] & 0xf0u) == 0x90) ++ons;
        if ((ev.data[0] & 0xf0u) == 0x80) ++offs;
    }
    assert(ons == offs);

    // Switching the mode off while sounding also releases them.
    Rig held;
    held.p = sws();
    held.p.midiOut = 2.0f;
    held.run(makeVowel(24000, 48000.0, kVowel));
    held.midi.clear();
    held.p.midiOut = 0.0f;
    held.run(makeVowel(1024, 48000.0, kVowel));
    int released = 0;
    for (const MidiOutEvent& ev : held.midi)
        if ((ev.data[0] & 0xf0u) == 0x80) ++released;
    assert(released >= 2);
}

void testActivateClearsRuntimeOnly()
{
    Rig rig;
    rig.p = sws();
    rig.run(makeVowel(24000, 48000.0, kVowel));
    assert(rig.state.outActive >= 2);
    const std::uint8_t on[] = {0x90, 64, 100};
    handleMidi(rig.state, rig.p, on, 3);
    const Parameters before = rig.p;

    activate(rig.state);
    assert(rig.state.outActive == 0);
    assert(rig.state.heldCount == 0);
    assert(rig.state.sampleRate == 48000.0);
    // The patch lives with the caller and is untouched.
    assert(serializeParameters(rig.p) == serializeParameters(before));

    rig.clearOutput();
    rig.run(silence(2048));
    assert(tailRms(rig.outL, 2048) == 0.0);
}

void testIdleVoicesLeaveNoDenormals()
{
    Rig rig;
    rig.p.formants = 4.0f;
    rig.p.mode = 2.0f;
    rig.run(makeVowel(24000, 48000.0, kVowel), makeNoise(24000, 0.3f));
    rig.run(silence(96000), makeNoise(96000, 0.3f));
    for (const Formant& f : rig.state.formants) {
        assert(f.smAmp == 0.0f);
        for (int c = 0; c < 2; ++c) {
            assert(f.ic1[c] == 0.0f);
            assert(f.ic2[c] == 0.0f);
        }
    }
}

void testRealtimeCost()
{
    // Four seconds of speech must analyse and synthesise far faster than real time.
    Rig rig;
    rig.p.mode = 2.0f;
    rig.p.formants = 8.0f;
    const Signal vowel = makeVowel(192000, 48000.0, kVowel);
    const Signal noise = makeNoise(192000, 0.3f);
    const auto start = std::chrono::steady_clock::now();
    rig.run(vowel, noise);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  8 formants, both engines: 4 s of audio in %.3f s\n", seconds);
    assert(seconds < 2.0);
}

}  // namespace

int main()
{
    testParameterClampAndSpecs();
    testSerializationRoundTrip();
    testFormantTracking(48000.0);
    testSampleRates();
    testSineWaveSpeechSpectrum();
    testVocoderShapesCarrier();
    testVocoderIsStereo();
    testSilenceIsSilent();
    testGateSuppressesQuietSpeech();
    testFormantCountControlsTracks();
    testBandwidthLimit();
    testShiftTransposesFormants();
    testLevelAndDry();
    testBypassPassesSpeech();
    testSpreadMakesStereo();
    testTiltChangesBalance();
    testQScaleNarrowsVocoderBands();
    testCarrierNormalisation();
    testModeSelectsEngine();
    testGlideSlowsFrequencyChange();
    testReleaseSetsTail();
    testAttackSetsRise();
    testMidiCcAndChannel();
    testNoteShiftTransposes();
    testMidiOutOffByDefault();
    testMidiOutControllers();
    testMidiOutNotes();
    testActivateClearsRuntimeOnly();
    testIdleVoicesLeaveNoDenormals();
    testRealtimeCost();
    std::printf("sinevox core tests passed\n");
    return 0;
}
