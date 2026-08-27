/*
 * Skream engine: SVF filter + ADAA2 anti-aliased tanh feedback loop.
 *
 * DSP topology ported from the Scream LV2 plugin (cureaudio/Scream).
 * Li2 dilogarithm from Alexander Voigt's Polylogarithm library (MIT).
 * ADAA2 tanh from jatinchowdhury18 (BSD-3).
 */

#include "skream_core.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

// ---------------------------------------------------------------------------
// Li2 dilogarithm – MIT License, Alexander Voigt (polylogarithm library)
// ---------------------------------------------------------------------------
static double li2(double x)
{
    const double PI = 3.1415926535897932;
    const double P[] = {
         9.999999999999999502e-1,
        -2.688392681856542343e+0,
         2.647722269947310969e+0,
        -1.153855960788741635e+0,
         2.088607779502060784e-1,
        -1.085977713415246308e-2
    };
    const double Q[] = {
         1.000000000000000000e+0,
        -2.938392681856563549e+0,
         3.271209329301863539e+0,
        -1.707670217395428942e+0,
         4.159601722840060384e-1,
        -3.980134375408448296e-2,
         8.274366897446665904e-4
    };

    double y = 0, r = 0, s = 1;

    if (x < -1) {
        const double l = log(1 - x);
        y = 1 / (1 - x);
        r = -PI * PI / 6 + l * (0.5 * l - log(-x));
        s = 1;
    } else if (x == -1) {
        return -PI * PI / 12;
    } else if (x < 0) {
        const double l = log1p(-x);
        y = x / (x - 1);
        r = -0.5 * l * l;
        s = -1;
    } else if (x == 0) {
        return x;
    } else if (x < 0.5) {
        y = x;
        r = 0;
        s = 1;
    } else if (x < 1) {
        y = 1 - x;
        r = PI * PI / 6 - log(x) * log1p(-x);
        s = -1;
    } else if (x == 1) {
        return PI * PI / 6;
    } else if (x < 2) {
        const double l = log(x);
        y = 1 - 1 / x;
        r = PI * PI / 6 - l * (log(y) + 0.5 * l);
        s = 1;
    } else {
        const double l = log(x);
        y = 1 / x;
        r = PI * PI / 3 - 0.5 * l * l;
        s = -1;
    }

    const double y2 = y * y;
    const double y4 = y2 * y2;
    const double p = P[0] + y * P[1] + y2 * (P[2] + y * P[3]) + y4 * (P[4] + y * P[5]);
    const double q = Q[0] + y * Q[1] + y2 * (Q[2] + y * Q[3]) + y4 * (Q[4] + y * Q[5] + y2 * Q[6]);

    return r + s * y * p / q;
}

// ---------------------------------------------------------------------------

namespace downspout::skream {

namespace {

constexpr double kPid = 3.14159265358979323846;
constexpr float  kPif = 3.14159265358979323846f;
constexpr float  kSqrtHalf = 0.70710678118654752440f;  // sqrt(0.5) – Butterworth Q
constexpr float  kSqrt2    = 1.41421356237309504880f;

static float dbToGain(float dB) noexcept { return std::pow(10.0f, dB * 0.05f); }

static float gainToDb(float gain) noexcept {
    return gain <= 0.0f ? -300.0f : 20.0f * std::log10(gain);
}

static float lerpf(float t, float a, float b) noexcept { return a + t * (b - a); }

// Analog-style envelope time constant (Pirkle, fxobjects.cpp)
static float compressorTC(double numSamples) noexcept {
    return static_cast<float>(std::exp(-0.99967234081320612 / numSamples));
}

static float detectPeak(float x, float yn1, float attack, float release) noexcept {
    float y = (x > yn1) ? attack * yn1 + (1.0f - attack) * x : release * yn1;
    if (y < 1.0e-6f) y = 0.0f;
    return y;
}

static float hardKneeExpander(float x_dB, float threshold_dB, float ratio) noexcept {
    return x_dB >= threshold_dB ? x_dB : threshold_dB + ratio * (x_dB - threshold_dB);
}

// --- ADAA2 tanh (BSD-3, jatinchowdhury18) ------------------------------------

static double adaa2TanhAD1(double x) noexcept { return std::log(std::cosh(x)); }

static double adaa2TanhAD2(double x) noexcept {
    const double expVal = std::exp(-2.0 * x);
    return 0.5 * (li2(-expVal) - x * (x + 2.0 * std::log(expVal + 1.0) - 2.0 * adaa2TanhAD1(x)))
           + (kPid * kPid / 24.0);
}

static double tanhAdaa2Process(TanhAdaa2& t, double x) noexcept
{
    constexpr double kTol = 1.0e-5;

    const bool illCond1 = std::abs(x - t.x1) < kTol;
    t.ad2x0 = adaa2TanhAD2(x);

    double d1;
    if (illCond1)
        d1 = adaa2TanhAD1(0.5 * (x + t.x1));
    else
        d1 = (t.ad2x0 - t.ad2x1) / (x - t.x1);

    double y;
    const bool illCond2 = std::abs(x - t.x2) < kTol;
    if (illCond2) {
        const double xBar  = 0.5 * (x + t.x2);
        const double delta = xBar - t.x1;
        if (std::abs(delta) < kTol)
            y = std::tanh(0.5 * (xBar + t.x1));
        else
            y = (2.0 / delta) * (adaa2TanhAD1(xBar) + (t.ad2x1 - adaa2TanhAD2(xBar)) / delta);
    } else {
        y = (2.0 / (x - t.x2)) * (d1 - t.d2);
    }

    t.d2    = d1;
    t.x2    = t.x1;
    t.x1    = x;
    t.ad2x1 = t.ad2x0;

    return y;
}

// --- Cytomic SVF (linear trap, optimised form 2) ----------------------------

struct SvfCoeffs { float a1, a2, a3, m0, m1, m2; };

static SvfCoeffs svfLP(float fc, float Q, double sampleRate) noexcept
{
    const float g  = std::tan(kPif * fc / static_cast<float>(sampleRate));
    const float k  = 1.0f / Q;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    return { a1, a2, a3, 0.0f, 0.0f, 1.0f };
}

static SvfCoeffs svfHP(float fc, float Q, double sampleRate) noexcept
{
    const float g  = std::tan(kPif * fc / static_cast<float>(sampleRate));
    const float k  = 1.0f / Q;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    return { a1, a2, a3, 1.0f, -k, -1.0f };
}

static float svfProcess(float v0, const SvfCoeffs& c, float* s) noexcept
{
    const float v3 = v0 - s[1];
    const float v1 = c.a1 * s[0] + c.a2 * v3;
    const float v2 = s[1] + c.a2 * s[0] + c.a3 * v3;
    s[0] = 2.0f * v1 - s[0];
    s[1] = 2.0f * v2 - s[1];
    return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
}

}  // namespace

// ---------------------------------------------------------------------------

Parameters clampParameters(const Parameters& p) noexcept
{
    auto safe = [](float v, float lo, float hi, float def) noexcept {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
    };
    Parameters out = p;
    out.inputGain  = safe(p.inputGain,  -24.0f,  24.0f,   0.0f);
    out.cutoff     = safe(p.cutoff,       0.0f, 100.0f,  85.0f);
    out.scream     = safe(p.scream,       0.0f, 100.0f,  46.5f);
    out.resonance  = safe(p.resonance,    0.0f, 100.0f, 100.0f);
    out.mix        = safe(p.mix,          0.0f, 100.0f, 100.0f);
    out.outputGain = safe(p.outputGain, -24.0f,   0.0f,  -6.0f);
    out.track      = safe(p.track,        0.0f, 100.0f,   0.0f);
    out.ccCutoff   = safe(p.ccCutoff,    0.0f, 127.0f,   0.0f);
    out.ccScream   = safe(p.ccScream,    0.0f, 127.0f,   0.0f);
    out.ccChannel  = safe(p.ccChannel,   1.0f,  16.0f,   1.0f);
    return out;
}

void processBlock(EngineState&      state,
                  const Parameters& params,
                  uint32_t          frames,
                  double            sampleRate,
                  const AudioBlock& audio,
                  float             effectiveCutoff,
                  float             effectiveScream) noexcept
{
    const float inGain  = dbToGain(params.inputGain);
    const float outGain = dbToGain(params.outputGain);
    const float wet     = params.mix / 100.0f;
    const float dry     = 1.0f - wet;
    const float nyq     = static_cast<float>(sampleRate * 0.45);

    // Frequency mapping matching the original Scream plugin's MIDI-note-space scheme.
    // LP maps 0-100% linearly across the MIDI note range 20Hz-20kHz.
    // HP is computed relative to LP via the same offset formula Scream uses, so the
    // HP cutoff tracks far below LP (sub-bass range at default settings) rather than
    // landing in the midrange. Without this coupling, the feedback resonance rings
    // at a fixed audible frequency even at the plugin's default settings.
    constexpr float kMidi20Hz  = 15.49f;  // 440*2^((n-69)/12) ≈ 20 Hz
    constexpr float kMidi20kHz = 135.07f; // ≈ 20 kHz
    constexpr float kMidiRange = kMidi20kHz - kMidi20Hz;    // 119.58 semitones
    constexpr float kHpCutMin  = kMidi20Hz - 12.0f;        // 3.49 (HP lower bound)
    constexpr float kHpRange   = kMidi20kHz - kHpCutMin;   // 131.58 semitones

    const float lpMidi = kMidi20Hz + (effectiveCutoff / 100.0f) * kMidiRange;
    // HP tracks LP: as LP moves below max, HP shifts down by the same number of semitones.
    float hpMidi = kHpCutMin + (effectiveScream / 100.0f) * kHpRange - (kMidi20kHz - lpMidi);
    hpMidi = std::max(hpMidi, kHpCutMin);

    auto midiToHz = [](float note) noexcept {
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    };

    const float lpHz = std::clamp(midiToHz(lpMidi), 5.0f, nyq);
    const float hpHz = std::clamp(midiToHz(hpMidi), 5.0f, lpHz);

    // Resonance → Q and feedback gain.
    // LP Q rises with resonance (Butterworth at 0 → high-resonance peak at 100 %) so the
    // filter itself provides formant character via its internal state.  The external
    // feedback gain is kept below unity (max –6 dB = 0.5×) so the loop cannot sustain
    // free self-oscillation regardless of preset.
    const float res          = params.resonance / 100.0f;
    const float lpQ          = lerpf(res, kSqrtHalf, 8.0f);
    const float hpQ          = lerpf(res, kSqrtHalf, kSqrt2);
    const float feedbackGain = dbToGain(lerpf(res, -18.0f, -6.0f));

    // SVF coefficients (constant per block)
    const SvfCoeffs lpC = svfLP(lpHz, lpQ, sampleRate);
    const SvfCoeffs hpC = svfHP(hpHz, hpQ, sampleRate);

    // Feedback gate time constants (1-sample attack, 1ms release)
    const float expAtk = compressorTC(1.0);
    const float expRel = compressorTC(sampleRate * 0.001);

    for (int ch = 0; ch < 2; ++ch) {
        auto& cs = state.channels[static_cast<std::size_t>(ch)];

        for (uint32_t i = 0; i < frames; ++i) {
            const float xDry = audio.inputs[ch][i];
            const float x    = xDry * inGain;

            // Feedforward: (input + feedback) → tanh → LP
            float y = x + cs.fbYn1;
            y = static_cast<float>(tanhAdaa2Process(cs.tanh1, static_cast<double>(y)));
            y = svfProcess(y, lpC, cs.lpState);
            if (y != y) y = 0.0f;  // NaN guard

            audio.outputs[ch][i] = (wet * y + dry * xDry) * outGain;

            // Feedback path: scale → [+ input coupling] → HP → tanh
            // Injecting a fraction of the post-gain input nudges the resonator toward
            // the input's harmonic content (injection locking).
            const float trackGain = params.track / 100.0f;
            float feed = y * feedbackGain + trackGain * x;
            feed = svfProcess(feed, hpC, cs.hpState);
            feed = static_cast<float>(tanhAdaa2Process(cs.tanh2, static_cast<double>(feed)));

            // Expander gate: silences feedback when input is absent
            cs.peakXn1 = detectPeak(std::abs(x), cs.peakXn1, expAtk, expRel);
            const float peakDb = gainToDb(cs.peakXn1);
            const float redDb  = hardKneeExpander(peakDb, -120.0f, 2.0f) - peakDb;
            cs.fbYn1 = (redDb < -140.0f) ? 0.0f : feed * dbToGain(redDb);
        }

        // Round subnormals to zero
        auto flush = [](float& v) noexcept { if (std::abs(v) < 1.0e-8f) v = 0.0f; };
        flush(cs.lpState[0]); flush(cs.lpState[1]);
        flush(cs.hpState[0]); flush(cs.hpState[1]);
        flush(cs.fbYn1);      flush(cs.peakXn1);
    }
}

}  // namespace downspout::skream
