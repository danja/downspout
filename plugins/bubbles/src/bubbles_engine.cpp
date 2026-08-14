#include "bubbles_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace downspout::bubbles {
namespace {

// ---------------------------------------------------------------------------
// Mode mixing table
// ---------------------------------------------------------------------------

struct ModeMix {
    float flowGain;
    float bubbleGain;
    float dripGain;
    float waveGain;
    float bodyGain;
    float bubbleSpawnMult;  // per-second spawn rate multiplier for bubbles
    float dripSpawnMult;    // per-second spawn rate multiplier for drips
    float depthBias;        // added to depth param for underwater character
    float turbBias;         // turbulence boost factor
    float waveFreqHz;       // dominant wave oscillator frequency
};

static constexpr std::array<ModeMix, static_cast<int>(WaterMode::Count)> kModeMixTable = {{
    // Stream: rushing water, some small bubbles, gentle ripple rhythm
    {0.80f, 0.30f, 0.05f, 0.15f, 0.40f,  8.0f,  0.5f, 0.00f, 0.40f, 0.55f},
    // River: strong flow, cascade bubbles, rhythmic pulses
    {0.95f, 0.45f, 0.12f, 0.30f, 0.50f, 14.0f,  1.2f, 0.05f, 0.60f, 0.30f},
    // Ocean: wave dominates, deep body resonance, sparse foam
    {0.45f, 0.08f, 0.00f, 0.90f, 0.65f,  2.0f,  0.0f, 0.15f, 0.20f, 0.08f},
    // Bubbles: dense bubble events, body resonance, minimal flow
    {0.15f, 1.00f, 0.25f, 0.00f, 0.70f, 30.0f,  3.0f, 0.30f, 0.10f, 0.12f},
    // Drips: sparse drip impacts, quiet ambient noise
    {0.10f, 0.15f, 1.00f, 0.00f, 0.50f,  2.0f,  4.0f, 0.00f, 0.05f, 0.20f},
    // Rain: dense drip rain, turbulent ambient noise
    {0.40f, 0.12f, 0.80f, 0.10f, 0.35f,  5.0f, 28.0f, 0.00f, 0.50f, 0.50f},
    // Custom: balanced blend of all layers
    {0.50f, 0.50f, 0.50f, 0.30f, 0.55f, 10.0f,  3.0f, 0.10f, 0.30f, 0.20f},
}};

// ---------------------------------------------------------------------------
// Filter helpers
// ---------------------------------------------------------------------------

static void calcOnePoleLP(OnePole& f, float freq, float sr)
{
    const float x = std::exp(-2.0f * 3.14159265f * freq / sr);
    f.a0 = 1.0f - x;
    f.b1 = x;
}

static float processOnePole(OnePole& f, float in)
{
    f.z1 = f.a0 * in + f.b1 * f.z1;
    return f.z1;
}

static void calcBiquadBP(BiquadBP& f, float freq, float Q, float sr)
{
    freq = std::max(10.0f, std::min(freq, sr * 0.499f));
    Q    = std::max(0.1f, Q);
    const float w0    = 2.0f * 3.14159265f * freq / sr;
    const float sinW0 = std::sin(w0);
    const float cosW0 = std::cos(w0);
    const float alpha = sinW0 / (2.0f * Q);
    const float a0inv = 1.0f / (1.0f + alpha);
    f.b0 = alpha * a0inv;
    f.a1 = -2.0f * cosW0 * a0inv;
    f.a2 = (1.0f - alpha) * a0inv;
    // Leave z1/z2 untouched to avoid discontinuities during parameter updates
}

// Direct Form II transposed (b1=0, b2=-b0 for ideal bandpass)
static float processBiquadBP(BiquadBP& f, float in)
{
    const float out = f.b0 * in + f.z1;
    f.z1 = -f.a1 * out + f.z2;
    f.z2 = -f.b0 * in - f.a2 * out;
    return out;
}

// DC blocker (r = 0.9985)
static float dcBlock(float& xm1, float& ym1, float x)
{
    const float y = x - xm1 + 0.9985f * ym1;
    xm1 = x;
    ym1 = y;
    return y;
}

// ---------------------------------------------------------------------------
// RNG helpers
// ---------------------------------------------------------------------------

static float nextWhiteNoise(uint32_t& rng)
{
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<int32_t>(rng)) * (1.0f / 2147483648.0f);
}

// Returns [0, 1)
static float nextRandFloat(uint32_t& rng)
{
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(rng >> 1) * (1.0f / 2147483648.0f);
}

// ---------------------------------------------------------------------------
// Filter coefficient update (called every kControlUpdatePeriod samples)
// ---------------------------------------------------------------------------

static void updateCoefficients(EngineState&    state,
                                const Parameters& params,
                                const ModeMix&  mix,
                                float           sr)
{
    // Flow LP chain: two stages, brightness shifts cutoffs upward
    const float brightScale = 0.3f + params.brightness * 1.7f;
    float freq1 = std::clamp(350.0f * brightScale + params.turbulence * 3000.0f,
                             50.0f, sr * 0.40f);
    float freq2 = std::clamp(90.0f  * brightScale + params.turbulence * 1000.0f,
                             40.0f, sr * 0.30f);
    calcOnePoleLP(state.flowLP1, freq1, sr);
    calcOnePoleLP(state.flowLP2, freq2, sr);

    // Turbulence bandpass centres
    float tc1 = std::clamp(90.0f  + params.brightness * 1110.0f, 60.0f, sr * 0.45f);
    float tc2 = std::clamp(250.0f + params.brightness * 3000.0f, 80.0f, sr * 0.45f);
    float tQ1 = 0.45f + params.resonance * 0.75f;
    float tQ2 = 0.55f + params.resonance * 0.65f;
    calcBiquadBP(state.turbBand1, tc1, tQ1, sr);
    calcBiquadBP(state.turbBand2, tc2, tQ2, sr);

    // Body modal resonators: 4 modes, harmonic-series ratios
    static constexpr float kBodyRatios[kNumBodyModes] = {1.0f, 2.41f, 4.37f, 6.96f};
    const float bodyBase = 60.0f + (1.0f - params.size) * 260.0f;  // 60–320 Hz
    const float bodyQ    = 0.5f + params.resonance * 1.7f;
    for (int j = 0; j < kNumBodyModes; ++j) {
        float f = std::clamp(bodyBase * kBodyRatios[j], 30.0f, sr * 0.45f);
        calcBiquadBP(state.bodyModes[j], f, bodyQ, sr);
    }

    // Depth (underwater) LP: high cutoff = low depth, low cutoff = high depth
    float effectiveDepth = std::clamp(params.depth + mix.depthBias, 0.0f, 1.0f);
    float depthFreq = std::clamp(9000.0f * (1.0f - effectiveDepth * 0.88f),
                                 150.0f, sr * 0.45f);
    calcOnePoleLP(state.depthLPL, depthFreq, sr);
    calcOnePoleLP(state.depthLPR, depthFreq, sr);

    // Wave oscillator rate (Hz / sample)
    state.cachedWaveRate = (mix.waveFreqHz * (0.5f + params.flow * 0.5f)) / sr;

    // Stereo delay
    state.cachedDelayFeedback = 0.15f + params.space * 0.42f;
    state.cachedDelayWet      = 0.18f + params.space * 0.47f;
    state.cachedDelayTapL = std::min(static_cast<int>(0.042f * sr), kDelayBufferLen - 1);
    state.cachedDelayTapR = std::min(static_cast<int>(0.064f * sr), kDelayBufferLen - 1);
}

// ---------------------------------------------------------------------------
// Voice spawning
// ---------------------------------------------------------------------------

static void spawnBubble(EngineState&     state,
                        const Parameters& params,
                        float            sr)
{
    for (auto& v : state.bubbles) {
        if (v.active) continue;

        v.active = true;

        // Frequency: large bubbles (low size) → low freq; small bubbles (high size) → high freq
        // Also influenced by MIDI note
        float noteFactor = (static_cast<float>(state.midiNote) - 60.0f) / 48.0f * 0.25f;
        float sizeEff = std::clamp(params.size + noteFactor, 0.0f, 1.0f);
        float baseFreq = std::exp(std::log(1480.0f / 230.0f) * sizeEff) * 230.0f;
        float jitter   = 1.0f + nextWhiteNoise(state.rngState) * 0.35f * params.randomness;
        float freq     = std::clamp(baseFreq * jitter, 80.0f, 8000.0f);

        float Q = 0.7f + params.resonance * 3.1f;
        calcBiquadBP(v.resonator, freq, Q, sr);
        v.resonator.z1 = 0.0f;
        v.resonator.z2 = 0.0f;

        // Impulse excitation (short burst)
        float impMs = 1.2f + nextRandFloat(state.rngState) * 2.3f * params.randomness;
        v.impDecay = std::exp(-1.0f / (impMs * 0.001f * sr));
        v.impAmp   = 1.5f + params.brightness * 3.0f;

        // Envelope duration: larger bubbles sustain longer
        float envMs = 14.0f + (1.0f - params.size) * 150.0f * (1.0f - params.heat * 0.6f);
        envMs = std::max(5.0f, envMs);
        v.envDecay = std::exp(-1.0f / (envMs * 0.001f * sr));
        v.envAmp   = 0.4f + state.midiVelocity * 0.6f;
        return;
    }
    // All voices active — oldest would need eviction, but we skip for simplicity
}

static void spawnDrip(EngineState&     state,
                      const Parameters& params,
                      float            sr)
{
    for (auto& v : state.drips) {
        if (v.active) continue;

        v.active = true;

        float baseFreq = 400.0f + params.size * 1400.0f;
        float jitter   = 1.0f + nextWhiteNoise(state.rngState) * 0.25f * params.randomness;
        float freq1    = std::clamp(baseFreq * jitter, 150.0f, 6000.0f);
        float spread   = 1.28f + nextRandFloat(state.rngState) * 0.45f * params.randomness;
        float freq2    = std::clamp(freq1 * spread, 200.0f, 9000.0f);

        float Q = 1.0f + params.resonance * 3.0f;
        calcBiquadBP(v.resonator1, freq1, Q, sr);
        v.resonator1.z1 = v.resonator1.z2 = 0.0f;
        calcBiquadBP(v.resonator2, freq2, Q * 0.8f, sr);
        v.resonator2.z1 = v.resonator2.z2 = 0.0f;

        float impMs = 2.5f + nextRandFloat(state.rngState) * 7.0f * params.randomness;
        v.impDecay = std::exp(-1.0f / (impMs * 0.001f * sr));
        v.impAmp   = 2.0f + params.brightness * 2.5f;

        float envMs = 24.0f + (1.0f - params.heat) * 120.0f;
        v.envDecay = std::exp(-1.0f / (envMs * 0.001f * sr));
        v.envAmp   = 0.5f + nextRandFloat(state.rngState) * 0.5f;
        return;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Parameters clampParameters(const Parameters& raw)
{
    Parameters p = raw;
    p.mode       = std::clamp(p.mode, 0.0f, static_cast<float>(static_cast<int>(WaterMode::Count) - 1));
    p.flow       = std::clamp(p.flow,       0.0f, 1.0f);
    p.turbulence = std::clamp(p.turbulence, 0.0f, 1.0f);
    p.size       = std::clamp(p.size,       0.0f, 1.0f);
    p.density    = std::clamp(p.density,    0.0f, 1.0f);
    p.heat       = std::clamp(p.heat,       0.0f, 1.0f);
    p.depth      = std::clamp(p.depth,      0.0f, 1.0f);
    p.brightness = std::clamp(p.brightness, 0.0f, 1.0f);
    p.resonance  = std::clamp(p.resonance,  0.0f, 1.0f);
    p.randomness = std::clamp(p.randomness, 0.0f, 1.0f);
    p.space      = std::clamp(p.space,      0.0f, 1.0f);
    p.drive      = std::clamp(p.drive,      0.0f, 1.0f);
    p.output     = std::clamp(p.output,     0.0f, 1.0f);
    return p;
}

void activate(EngineState& state, double sampleRate)
{
    state.sampleRate = sampleRate;
    state.controlCounter = 0;
    state.gateOn   = true;   // continuous generation without MIDI
    state.gateAmp  = 1.0f;
    state.wavePhase  = 0.0f;
    state.wavePhaseR = 0.30f;
    state.bubbleSpawnAccum = 0.0f;
    state.dripSpawnAccum   = 0.0f;
    state.dcXm1L = state.dcYm1L = 0.0f;
    state.dcXm1R = state.dcYm1R = 0.0f;
    state.delayL.fill(0.0f);
    state.delayR.fill(0.0f);
    state.delayWrL = 0;
    state.delayWrR = 0;
    for (auto& v : state.bubbles) { v.active = false; v.envAmp = 0.0f; v.impAmp = 0.0f; }
    for (auto& v : state.drips)   { v.active = false; v.envAmp = 0.0f; v.impAmp = 0.0f; }
}

void processBlock(EngineState&           state,
                  const Parameters&      params,
                  const TransportSnapshot& /*transport*/,
                  uint32_t               nframes,
                  double                 sampleRate,
                  float*                 outL,
                  float*                 outR,
                  const InputMidiEvent*  midiEvents,
                  uint32_t               midiEventCount)
{
    const float sr = static_cast<float>(sampleRate);
    const int modeIdx = std::clamp(static_cast<int>(params.mode),
                                   0,
                                   static_cast<int>(WaterMode::Count) - 1);
    const ModeMix& mix = kModeMixTable[modeIdx];

    // Per-mode body weight constants
    static constexpr float kBodyWeights[kNumBodyModes] = {0.18f, 0.13f, 0.09f, 0.06f};

    uint32_t midiIdx = 0;

    for (uint32_t i = 0; i < nframes; ++i) {

        // --- MIDI event dispatch ---
        while (midiIdx < midiEventCount && midiEvents[midiIdx].frame <= i) {
            const auto& ev = midiEvents[midiIdx++];
            const uint8_t status = ev.data[0] & 0xF0;
            if (status == 0x90 && ev.data[2] > 0) {
                state.midiNote     = ev.data[1];
                state.midiVelocity = ev.data[2] / 127.0f;
                state.gateOn       = true;
            } else if (status == 0x80 || (status == 0x90 && ev.data[2] == 0)) {
                if (ev.data[1] == state.midiNote)
                    state.gateOn = false;
            } else if ((ev.data[0] & 0xF0) == 0xB0 && ev.data[1] == 0x7B) {
                state.gateOn = false;  // all-notes-off
            }
        }

        // Gate amplitude: MIDI note-on opens it; no MIDI → stays at 1.0 (ambient mode)
        // When a note-off arrives the gate fades out; when note-on arrives it fades in.
        if (!state.gateOn && state.gateAmp > 0.0f) {
            state.gateAmp = std::max(0.0f, state.gateAmp - 0.0007f);
        } else if (state.gateOn) {
            state.gateAmp = std::min(1.0f, state.gateAmp + 0.0018f);
        }
        // If gateAmp reaches 0 and no MIDI received, stay at 0 until note-on.
        // Fresh activate() sets gateAmp=1 so it runs without MIDI by default.

        // --- Coefficient updates ---
        if (--state.controlCounter <= 0) {
            state.controlCounter = kControlUpdatePeriod;
            updateCoefficients(state, params, mix, sr);
        }

        // --- White noise source ---
        float noise = nextWhiteNoise(state.rngState);

        // --- Flow signal: two-stage LP, bandpass by subtraction ---
        float lp1 = processOnePole(state.flowLP1, noise);
        float lp2 = processOnePole(state.flowLP2, lp1);
        float flowBand = lp1 - lp2;

        // --- Turbulence bandpass bands ---
        float turb1 = processBiquadBP(state.turbBand1, noise);
        float turb2 = processBiquadBP(state.turbBand2, noise);

        // Combine flow and turbulence
        float flowSig = (flowBand + (turb1 + turb2) * 0.5f * (mix.turbBias + params.turbulence))
                      * params.flow;

        // --- Stochastic voice spawning ---
        float bSpawnRate = params.density * mix.bubbleSpawnMult * (1.0f + params.heat * 4.0f);
        state.bubbleSpawnAccum += bSpawnRate / sr;
        while (state.bubbleSpawnAccum >= 1.0f) {
            state.bubbleSpawnAccum -= 1.0f;
            if (nextRandFloat(state.rngState) < 0.75f)
                spawnBubble(state, params, sr);
        }

        float dSpawnRate = params.density * mix.dripSpawnMult * (1.0f + params.heat * 1.0f);
        state.dripSpawnAccum += dSpawnRate / sr;
        while (state.dripSpawnAccum >= 1.0f) {
            state.dripSpawnAccum -= 1.0f;
            if (nextRandFloat(state.rngState) < 0.65f)
                spawnDrip(state, params, sr);
        }

        // --- Bubble voice synthesis ---
        float bubbleSig = 0.0f;
        float noiseExcite = nextWhiteNoise(state.rngState);
        for (auto& v : state.bubbles) {
            if (!v.active) continue;
            float excite = v.impAmp * noiseExcite;
            v.impAmp *= v.impDecay;
            float vout = processBiquadBP(v.resonator, excite) * v.envAmp;
            v.envAmp *= v.envDecay;
            if (v.envAmp < 1e-6f) { v.active = false; }
            bubbleSig += vout;
        }

        // --- Drip voice synthesis ---
        float dripSig = 0.0f;
        float dripExcite = nextWhiteNoise(state.rngState);
        for (auto& v : state.drips) {
            if (!v.active) continue;
            float excite = v.impAmp * dripExcite;
            v.impAmp *= v.impDecay;
            float o1 = processBiquadBP(v.resonator1, excite);
            float o2 = processBiquadBP(v.resonator2, excite);
            float vout = (o1 + 0.6f * o2) * v.envAmp;
            v.envAmp *= v.envDecay;
            if (v.envAmp < 1e-6f) { v.active = false; }
            dripSig += vout;
        }

        // --- Mix sources ---
        float mixed = mix.flowGain   * flowSig
                    + mix.bubbleGain * bubbleSig
                    + mix.dripGain   * dripSig;

        // --- Physical body resonance ---
        float bodySig = 0.0f;
        for (int j = 0; j < kNumBodyModes; ++j) {
            bodySig += processBiquadBP(state.bodyModes[j], mixed) * kBodyWeights[j];
        }
        mixed += mix.bodyGain * bodySig;

        // --- Wave oscillator (ocean/river amplitude modulation) ---
        state.wavePhase  += state.cachedWaveRate;
        if (state.wavePhase  > 1.0f) state.wavePhase  -= 1.0f;
        state.wavePhaseR += state.cachedWaveRate * 1.031f;
        if (state.wavePhaseR > 1.0f) state.wavePhaseR -= 1.0f;

        float waveMod  = 0.5f + 0.5f * std::sin(2.0f * 3.14159265f * state.wavePhase);
        float waveModR = 0.5f + 0.5f * std::sin(2.0f * 3.14159265f * state.wavePhaseR);

        float sigL = mixed;
        float sigR = mixed;

        if (mix.waveGain > 0.01f) {
            float wGain = mix.waveGain * params.flow;
            sigL *= (1.0f - wGain * 0.5f + wGain * waveMod);
            sigR *= (1.0f - wGain * 0.5f + wGain * waveModR);
        }

        // --- Depth LP (underwater damping) ---
        sigL = processOnePole(state.depthLPL, sigL);
        sigR = processOnePole(state.depthLPR, sigR);

        // --- DC block ---
        sigL = dcBlock(state.dcXm1L, state.dcYm1L, sigL);
        sigR = dcBlock(state.dcXm1R, state.dcYm1R, sigR);

        // --- Pre-drive saturation ---
        float driveAmt   = 1.2f + params.drive * 2.8f;
        float driveNorm  = 1.0f / driveAmt;
        sigL = std::tanh(driveAmt * sigL) * driveNorm;
        sigR = std::tanh(driveAmt * sigR) * driveNorm;

        // --- Stereo delay (cross-coupled for spread) ---
        const int tapL = (state.delayWrL - state.cachedDelayTapL + kDelayBufferLen) & (kDelayBufferLen - 1);
        const int tapR = (state.delayWrR - state.cachedDelayTapR + kDelayBufferLen) & (kDelayBufferLen - 1);
        const float dL = state.delayL[tapL];
        const float dR = state.delayR[tapR];
        state.delayL[state.delayWrL] = sigL + dR * state.cachedDelayFeedback;
        state.delayR[state.delayWrR] = sigR + dL * state.cachedDelayFeedback;
        state.delayWrL = (state.delayWrL + 1) & (kDelayBufferLen - 1);
        state.delayWrR = (state.delayWrR + 1) & (kDelayBufferLen - 1);

        const float wet = state.cachedDelayWet;
        sigL = sigL * (1.0f - wet) + dL * wet;
        sigR = sigR * (1.0f - wet) + dR * wet;

        // --- Output gain (includes gate amplitude) ---
        const float outGain = (0.25f + params.output * 0.75f) * state.gateAmp;
        outL[i] = sigL * outGain;
        outR[i] = sigR * outGain;
    }
}

}  // namespace downspout::bubbles
