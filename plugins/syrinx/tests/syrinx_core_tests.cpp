#include "downspout/test_assert.h"
#include "syrinx_engine.hpp"
#include "syrinx_analyzer.hpp"
#include "syrinx_params.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

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
    engine.setParameter(kParamDistance, 0.0f);   // the voice must die; a reverb tail is not its concern
    const std::uint8_t noteOn[]  = { 0x90, 60, 100 };
    const std::uint8_t noteOff[] = { 0x80, 60,   0 };
    engine.handleMidi(noteOn, 3);
    for (int i = 0; i < 2048; ++i) (void)engine.processStereo();
    engine.handleMidi(noteOff, 3);
    // After a long silence the voice should die
    for (int i = 0; i < 48000; ++i) (void)engine.processStereo();
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
        CHECK(pp.regime >= 0.0f && pp.regime <= 1.0f);
        CHECK(pp.tracheaCm >= 0.0f && pp.tracheaCm <= 10.0f);
    }
}

// Air-sac ODE produces non-trivial pressure under constant forcing
static void testAirSacOdePressure()
{
    float rx = 0.0f, rp = 0.0f;
    const float dt = 1.0f / 48000.0f;
    const float kRespTauX   = 0.25f;
    const float kRespTauP   = 0.20f;
    const float kRespAlphaI = 0.05f;
    const float kRespAlphaR = 0.125f;
    const float kRespF0     = 35.0f;
    float maxP = 0.0f;
    for (int i = 0; i < 2400; ++i) {
        const float aP = (rp <= 0.0f) ? kRespAlphaI : kRespAlphaI / kRespAlphaR;
        const float dx = (-(1.0f + rx*rx)*rx - rp + kRespF0) / kRespTauX;
        const float dp = (-(1.0f + rx*rx)*rx - (1.0f + aP)*rp + kRespF0) / kRespTauP;
        rx += dx * dt;
        rp += dp * dt;
        if (rp > maxP) maxP = rp;
    }
    CHECK(maxP > 0.5f);
}

// regime=0 → gammaScale=1; the ODE regime shift is decoupled from harmonic band level
static void testRegimeIndependentOfHarmonic()
{
    // With regime=0, gammaScale must equal 1, so betaClean = 1/(1^2) = 1.
    // Verify a voice at regime=0, harmonic=0.5 produces sound (vs old gammaScale=1.5).
    Engine engine(48000.0f);
    engine.setParameter(presetParam(0, kPresetParamRegime),   0.0f);
    engine.setParameter(presetParam(0, kPresetParamHarmonic), 0.5f);
    const std::uint8_t noteOn[] = { 0x90, 69, 100 };
    engine.handleMidi(noteOn, 3);
    bool gotSound = false;
    for (int i = 0; i < 4096 && !gotSound; ++i) {
        const auto frame = engine.processStereo();
        if (std::abs(frame.left) > 1e-6f) gotSound = true;
    }
    CHECK(gotSound);

    // Also check that regime param decodes correctly
    float params[kParameterCount];
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        params[i] = getParameterSpec(i).defaultValue;
    params[presetParam(0, kPresetParamRegime)] = 0.0f;
    const auto pp = decodePreset(params, 0);
    CHECK(pp.regime == 0.0f);
}

// tracheaCm=0 with coupling>0 uses simple mix (no crash, produces sound)
static void testTracheaCmZeroSimpleMix()
{
    Engine engine(48000.0f);
    engine.setParameter(presetParam(0, kPresetParamCoupling),    0.3f);
    engine.setParameter(presetParam(0, kPresetParamTracheaCm),   0.0f);
    engine.setParameter(presetParam(0, kPresetParamVoiceOffset), 0.1f);
    const std::uint8_t noteOn[] = { 0x90, 60, 100 };
    engine.handleMidi(noteOn, 3);
    bool gotSound = false;
    for (int i = 0; i < 4096 && !gotSound; ++i) {
        const auto frame = engine.processStereo();
        if (std::abs(frame.left) > 1e-6f) gotSound = true;
    }
    CHECK(gotSound);
}

static float estimateHz(Engine& engine)
{
    constexpr int warm = 6000, length = 16000;
    std::vector<float> mono(length);
    for (int i = 0; i < warm; ++i) (void)engine.processStereo();
    for (int i = 0; i < length; ++i) {
        const auto f = engine.processStereo();
        mono[static_cast<std::size_t>(i)] = 0.5f * (f.left + f.right);
    }
    int bestLag = 24;
    double best = -1.0e30;
    for (int lag = 24; lag <= 480; ++lag) {
        double sum = 0.0;
        for (int i = 0; i + lag < length; ++i)
            sum += static_cast<double>(mono[static_cast<std::size_t>(i)]) * mono[static_cast<std::size_t>(i + lag)];
        if (sum > best) { best = sum; bestLag = lag; }
    }
    return 48000.0f / static_cast<float>(bestLag);
}

static void midi3(Engine& engine, int status, int a, int b)
{
    const std::uint8_t msg[3] = {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)};
    engine.handleMidi(msg, 3);
}

static void testPitchBendIsPerChannel()
{
    Engine plain(48000.0f);
    midi3(plain, 0x91, 72, 100);
    const float base = estimateHz(plain);

    Engine bent(48000.0f);
    midi3(bent, 0xE2, 0x7F, 0x7F);  // full up on channel 3 before its note
    midi3(bent, 0x92, 72, 100);
    CHECK(std::fabs(estimateHz(bent) / base - std::pow(2.0f, 2.0f / 12.0f)) < 0.06f);

    Engine other(48000.0f);
    midi3(other, 0x91, 72, 100);
    midi3(other, 0xE5, 0x7F, 0x7F);  // a different channel
    CHECK(std::fabs(estimateHz(other) / base - 1.0f) < 0.03f);

}


// ---- helpers for the chirp and audio-mode tests ----
static constexpr float kSr = 48000.0f;

static float goertzel(const float* x, int n, float hz)
{
    const double w = 2.0 * M_PI * hz / kSr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        const double s = x[i] * hann + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return static_cast<float>(s1 * s1 + s2 * s2 - c * s1 * s2);
}

// Strongest component between lo and hi, searched in 1/24-octave steps.
static float peakHz(const float* x, int n, float lo = 400.0f, float hi = 9000.0f)
{
    float best = 0.0f, bestHz = 0.0f;
    for (float f = lo; f < hi; f *= 1.029302f) {
        const float p = goertzel(x, n, f);
        if (p > best) { best = p; bestHz = f; }
    }
    return bestHz;
}

static float rms(const float* x, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(sum / std::max(1, n)));
}

struct Syllable { int begin; int end; float sweepOct; };

// Splits a render into syllables (10 ms RMS above 15% of the peak, split by 6 ms of quiet) and
// measures each one's pitch change from its first to last fifth.
static std::vector<Syllable> findSyllables(const std::vector<float>& x)
{
    const int hop = 240, win = 480;
    std::vector<float> level;
    float peak = 0.0f;
    for (int s = 0; s + win <= static_cast<int>(x.size()); s += hop) {
        level.push_back(rms(&x[static_cast<std::size_t>(s)], win));
        peak = std::max(peak, level.back());
    }
    std::vector<Syllable> out;
    int start = -1, quiet = 0;
    auto close = [&](int endHop) {
        const int a = start * hop, b = endHop * hop + win;
        const int part = std::max(win, (b - a) / 5);
        if (b - a > 3 * win) {
            const float f0 = peakHz(&x[static_cast<std::size_t>(a)], part);
            const float f1 = peakHz(&x[static_cast<std::size_t>(b - part)], part);
            out.push_back({a, b, std::log2(f1 / f0)});
        }
        start = -1;
    };
    for (std::size_t i = 0; i < level.size(); ++i) {
        if (level[i] > 0.15f * peak) {
            if (start < 0) start = static_cast<int>(i);
            quiet = 0;
        } else if (start >= 0 && ++quiet >= 3) {
            close(static_cast<int>(i) - quiet);
        }
    }
    if (start >= 0) close(static_cast<int>(level.size()) - 1);
    return out;
}

static std::vector<float> renderNote(Engine& e, int note, float seconds)
{
    e.setParameter(kParamDistance, 0.0f);
    e.setParameter(kParamMasterGain, 1.0f);
    midi3(e, 0x90, note, 100);
    std::vector<float> x(static_cast<std::size_t>(seconds * kSr));
    for (auto& v : x) v = e.processStereo().left;
    return x;
}

// A phrase is Syllables separate chirps, each sweeping; the legacy setting is one steady tone.
static void testChirpPhrase()
{
    Engine chirpy(kSr);
    chirpy.setParameter(kParamSyllables, 4.0f);
    chirpy.setParameter(kParamChirp, 0.8f);
    chirpy.setParameter(kParamVariation, 0.0f);
    chirpy.setParameter(kParamGap, 0.5f);
    const auto syl = findSyllables(renderNote(chirpy, 84, 1.5f));
    CHECK(syl.size() == 4);
    float sweep = 0.0f, longest = 0.0f;
    for (const auto& s : syl) {
        sweep += std::fabs(s.sweepOct) / static_cast<float>(std::max<std::size_t>(1, syl.size()));
        longest = std::max(longest, static_cast<float>(s.end - s.begin) / kSr);
    }
    CHECK(sweep > 0.6f);       // octaves per syllable; the old contour managed 0.0-0.35 across 0.5 s
    CHECK(longest < 0.12f);    // a chirp, not a held note

    Engine legacy(kSr);
    legacy.setParameter(kParamSyllables, 1.0f);
    legacy.setParameter(kParamChirp, 0.0f);
    const auto one = findSyllables(renderNote(legacy, 84, 1.5f));
    CHECK(one.size() == 1);
    CHECK(std::fabs(one[0].sweepOct) < 0.4f);
}

// Variation spreads the syllable lengths; Gap stretches the phrase. Both must be audible.
static void testVariationAndGapHaveAnEffect()
{
    auto lengths = [](float variation, float gap) {
        Engine e(kSr);
        e.setParameter(kParamSyllables, 6.0f);
        e.setParameter(kParamChirp, 0.7f);
        e.setParameter(kParamVariation, variation);
        e.setParameter(kParamGap, gap);
        return findSyllables(renderNote(e, 84, 2.5f));
    };
    auto spread = [](const std::vector<Syllable>& v) {
        float lo = 1e9f, hi = 0.0f;
        for (const auto& s : v) {
            lo = std::min(lo, static_cast<float>(s.end - s.begin));
            hi = std::max(hi, static_cast<float>(s.end - s.begin));
        }
        return (hi - lo) / kSr;
    };
    const auto flat = lengths(0.0f, 0.3f);
    const auto varied = lengths(1.0f, 0.3f);
    CHECK(flat.size() >= 5 && varied.size() >= 5);
    CHECK(spread(flat) < 0.006f);
    CHECK(spread(varied) > 0.015f);

    const auto tight = lengths(0.0f, 0.0f);
    const auto loose = lengths(0.0f, 1.0f);
    CHECK(!tight.empty() && !loose.empty());
    CHECK(tight.back().end < loose.back().end - static_cast<int>(0.2f * kSr));
}

// With the ODE locked to f0 the sound sits at the note, not wherever Phi(alpha) drifts to.
static void testPitchLockHoldsTheNote()
{
    for (std::uint32_t preset = 0; preset < 5; ++preset) {
        Engine e(kSr);
        e.setParameter(kParamSelectedPreset, static_cast<float>(preset));
        e.setParameter(kParamSyllables, 1.0f);
        e.setParameter(kParamChirp, 0.0f);
        e.setParameter(presetParam(preset, kPresetParamBend), 0.0f);        // a flat contour, so the
        e.setParameter(presetParam(preset, kPresetParamVibDepth), 0.0f);    // pitch is the note's
        const auto pp = decodePreset([&] {
            static float v[kParameterCount];
            for (std::uint32_t i = 0; i < kParameterCount; ++i) v[i] = e.getParameter(i);
            return v;
        }(), preset);
        auto x = renderNote(e, 84, 0.6f);
        const float expect = 1046.5f * std::pow(2.0f, pp.pitchSemitones / 12.0f);
        const float got = peakHz(&x[static_cast<std::size_t>(0.1f * kSr)], 4800, 500.0f, 4000.0f);
        CHECK(std::fabs(got / expect - 1.0f) < 0.04f);
    }
}

static std::vector<float> voiceSignal(float hz, int frames, float amp = 0.3f)
{
    std::vector<float> x(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const double ph = 2.0 * M_PI * hz * i / kSr;
        x[static_cast<std::size_t>(i)] = amp * static_cast<float>(std::sin(ph) + 0.5 * std::sin(2 * ph) + 0.3 * std::sin(3 * ph)) / 1.8f;
    }
    return x;
}

static void testAnalyzerTracksPitch()
{
    // The search covers 0.5x to 2.5x the range, so each voice sits inside its own.
    for (const float hz : {90.0f, 140.0f, 220.0f, 330.0f}) {
        InputAnalyzer a(kSr);
        a.setRange(hz < 100.0f ? 150.0f : 220.0f);
        for (const float v : voiceSignal(hz, 24000)) a.push(v);
        CHECK(a.hz() > 0.0f);
        CHECK(std::fabs(a.hz() / hz - 1.0f) < 0.02f);
    }

    InputAnalyzer silent(kSr);
    for (int i = 0; i < 24000; ++i) silent.push(0.0f);
    CHECK(silent.hz() == 0.0f);

    // White noise has no pitch (xorshift: the same generator the voice uses).
    InputAnalyzer hiss(kSr);
    Xorshift32 rng(5u);
    int voicedFrames = 0;
    for (int i = 0; i < 48000; ++i) {
        hiss.push(0.3f * rng.next());
        if (i > 12000 && i % 480 == 0 && hiss.hz() > 0.0f) ++voicedFrames;
    }
    CHECK(voicedFrames < 8);   // of ~75 checks
}

static void testAnalyzerSeesOnsetsAndSibilance()
{
    InputAnalyzer a(kSr);
    for (int i = 0; i < 12000; ++i) a.push(0.0f);
    CHECK(!a.takeOnset());
    int onsets = 0;
    const auto burst = voiceSignal(200.0f, 4800, 0.4f);
    for (const float v : burst) { a.push(v); if (a.takeOnset()) ++onsets; }
    CHECK(onsets == 1);
    const float tonalSib = a.sibilance();

    InputAnalyzer s(kSr);
    Xorshift32 rng(9u);
    float prev = 0.0f;
    for (int i = 0; i < 12000; ++i) {
        const float n = 0.3f * rng.next();
        s.push(n - prev);   // first difference: a highpass-tilted hiss
        prev = n;
    }
    CHECK(s.sibilance() > tonalSib + 0.3f);
}

static Engine makeAudioEngine()
{
    Engine e(kSr);
    e.setParameter(kParamSource, 1.0f);
    e.setParameter(kParamDistance, 0.0f);
    e.setParameter(kParamMasterGain, 1.0f);
    e.setParameter(kParamInPitchMult, 6.0f);
    e.setParameter(kParamInResponse, 0.0f);
    return e;
}

static std::vector<float> runAudio(Engine& e, const std::vector<float>& in)
{
    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) out[i] = e.processStereo(in[i], in[i]).left;
    return out;
}

// The bird voice follows the input pitch times the multiplier, and moves when the input moves.
static void testAudioModeFollowsInputPitch()
{
    Engine e = makeAudioEngine();
    const auto low = runAudio(e, voiceSignal(200.0f, 36000));
    const float fLow = peakHz(&low[24000], 9600, 500.0f, 5000.0f);
    CHECK(std::fabs(fLow / 1200.0f - 1.0f) < 0.08f);

    const auto high = runAudio(e, voiceSignal(300.0f, 36000));
    const float fHigh = peakHz(&high[24000], 9600, 500.0f, 5000.0f);
    CHECK(std::fabs(fHigh / fLow - 1.5f) < 0.12f);

    // Pitch Multiplier scales the result.
    Engine d = makeAudioEngine();
    d.setParameter(kParamInPitchMult, 3.0f);
    const auto half = runAudio(d, voiceSignal(200.0f, 36000));
    const float fHalf = peakHz(&half[24000], 9600, 300.0f, 5000.0f);
    CHECK(std::fabs(fHalf / 600.0f - 1.0f) < 0.1f);
}

static void testAudioModeIsSilentOnSilenceAndIgnoresMidi()
{
    Engine e = makeAudioEngine();
    midi3(e, 0x90, 84, 100);
    const auto quiet = runAudio(e, std::vector<float>(24000, 0.0f));
    CHECK(rms(quiet.data(), 24000) < 1.0e-4f);

    // After a phrase of speech, the voice shuts when the input stops.
    (void)runAudio(e, voiceSignal(200.0f, 24000));
    const auto after = runAudio(e, std::vector<float>(48000, 0.0f));
    CHECK(rms(&after[24000], 24000) < 1.0e-3f);
}

static void testGateSilencesQuietInput()
{
    auto level = [](float gate) {
        Engine e = makeAudioEngine();
        e.setParameter(kParamInGate, gate);
        // loud, then 12 dB down: relative to the recent peak the second half is quiet
        auto in = voiceSignal(200.0f, 24000, 0.5f);
        const auto soft = voiceSignal(200.0f, 24000, 0.12f);
        in.insert(in.end(), soft.begin(), soft.end());
        const auto out = runAudio(e, in);
        return rms(&out[40000], 7000);
    };
    CHECK(level(0.0f) > 3.0f * level(1.0f) + 1.0e-4f);
}

static void testBirdMixBlendsDry()
{
    Engine e = makeAudioEngine();
    e.setParameter(kParamInMix, 0.0f);
    const auto in = voiceSignal(200.0f, 12000);
    const auto out = runAudio(e, in);
    for (std::size_t i = 0; i < in.size(); i += 97)
        CHECK(std::fabs(out[i] - in[i]) < 1.0e-5f);   // fully dry: the input, untouched
}

static void testUnvoicedNoiseTurnsHissIntoTurbulence()
{
    auto tonality = [](float unvoiced) {
        Engine e = makeAudioEngine();
        e.setParameter(kParamInUnvoiced, unvoiced);
        e.setParameter(kParamInGate, 0.0f);
        Xorshift32 rng(3u);
        std::vector<float> in(48000);
        float prev = 0.0f;
        for (auto& v : in) { const float n = 0.4f * rng.next(); v = n - prev; prev = n; }
        const auto out = runAudio(e, in);
        // how much of the energy sits in the single strongest 1/24-oct band
        float best = 0.0f, total = 0.0f;
        for (float f = 500.0f; f < 9000.0f; f *= 1.029302f) {
            const float p = goertzel(&out[24000], 9600, f);
            best = std::max(best, p);
            total += p;
        }
        return best / std::max(total, 1.0e-12f);
    };
    CHECK(tonality(0.0f) > tonality(1.0f) * 1.5f);
}

static void testStatusOutputsReportTheInput()
{
    Engine e = makeAudioEngine();
    (void)runAudio(e, voiceSignal(200.0f, 36000));
    CHECK(std::fabs(e.getParameter(kParamOutInHz) / 200.0f - 1.0f) < 0.03f);
    CHECK(e.getParameter(kParamOutVoiced) == 1.0f);
    CHECK(e.getParameter(kParamOutLevel) > 0.3f);
    CHECK(std::fabs(e.getParameter(kParamOutBirdHz) / 1200.0f - 1.0f) < 0.05f);

    e.setParameter(kParamOutInHz, 999.0f);   // status values cannot be set from outside
    (void)runAudio(e, voiceSignal(200.0f, 480));
    CHECK(e.getParameter(kParamOutInHz) < 400.0f);
}

// Saved projects index parameters by position, so the original block must not move.
static void testOriginalParameterIndicesAreStable()
{
    CHECK(kParamDistance == 210);
    CHECK(kParamMasterGain == 211);
    CHECK(kParamSelectedPreset == 212);
    CHECK(kParamSource == 213);
    CHECK(kParameterCount == 228);
    for (std::uint32_t i = kFirstOutputParam; i < kParameterCount; ++i)
        CHECK(getParameterSpec(i).output);
    for (std::uint32_t i = 0; i < kFirstOutputParam; ++i)
        CHECK(!getParameterSpec(i).output);
}

// Changing the preset knobs while the bird voice runs must not restart or silence it.
static void testLiveParameterEditKeepsTheAudioVoice()
{
    Engine e = makeAudioEngine();
    (void)runAudio(e, voiceSignal(200.0f, 24000));
    e.setParameter(presetParam(0, kPresetParamTimbre), 0.9f);
    e.setParameter(presetParam(0, kPresetParamNoise), 0.3f);
    const auto out = runAudio(e, voiceSignal(200.0f, 12000));
    CHECK(rms(&out[6000], 6000) > 0.02f);
}

int main()
{
    testPitchBendIsPerChannel();
    testParameterDefaults();
    testParameterRoundtrip();
    testParameterClamping();
    testSilenceWithNoNotes();
    testNoteOnProducesSound();
    testNoteOffEntersRelease();
    testAllPanic();
    testOutputBounded();
    testPresetDecode();
    testAirSacOdePressure();
    testRegimeIndependentOfHarmonic();
    testTracheaCmZeroSimpleMix();
    testChirpPhrase();
    testVariationAndGapHaveAnEffect();
    testPitchLockHoldsTheNote();
    testAnalyzerTracksPitch();
    testAnalyzerSeesOnsetsAndSibilance();
    testAudioModeFollowsInputPitch();
    testAudioModeIsSilentOnSilenceAndIgnoresMidi();
    testGateSilencesQuietInput();
    testBirdMixBlendsDry();
    testUnvoicedNoiseTurnsHissIntoTurbulence();
    testStatusOutputsReportTheInput();
    testOriginalParameterIndicesAreStable();
    testLiveParameterEditKeepsTheAudioVoice();

    if (failures == 0) {
        std::printf("All Syrinx core tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) FAILED.\n", failures);
    return 1;
}
