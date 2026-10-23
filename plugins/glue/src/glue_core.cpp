#include "glue_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::glue {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDbToNeper = 0.115129255f;  // ln(10) / 20
constexpr float kFloorLevel = 1.0e-6f;      // -120 dB

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

// Second-order Butterworth high-pass.
void setHighPass(Biquad& b, float hz, float sampleRate) noexcept
{
    const float fc = std::min(hz, sampleRate * 0.45f);
    const float k = std::tan(kPi * fc / sampleRate);
    const float q = 0.70710678f;
    const float norm = 1.0f / (1.0f + k / q + k * k);
    b.b0 = norm;
    b.b1 = -2.0f * norm;
    b.b2 = norm;
    b.a1 = 2.0f * (k * k - 1.0f) * norm;
    b.a2 = (1.0f - k / q + k * k) * norm;
}

inline float runBiquad(Biquad& b, float x) noexcept
{
    const float y = b.b0 * x + b.z1;
    b.z1 = b.b1 * x - b.a1 * y + b.z2;
    b.z2 = b.b2 * x - b.a2 * y;
    return y;
}

inline float onePoleCoefficient(float ms, float sampleRate) noexcept
{
    return std::exp(-1.0f / (std::max(ms, 0.01f) * 0.001f * sampleRate));
}

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters o = p;
    o.thresholdDb = clampf(p.thresholdDb, -60.0f, 0.0f);
    o.ratio       = clampf(p.ratio, 1.1f, 20.0f);
    o.kneeDb      = clampf(p.kneeDb, 0.0f, 24.0f);
    o.attackMs    = clampf(p.attackMs, 0.1f, 100.0f);
    o.releaseMs   = clampf(p.releaseMs, 20.0f, 2000.0f);
    o.autoRelease = p.autoRelease >= 0.5f ? 1.0f : 0.0f;
    o.scHpfHz     = clampf(p.scHpfHz, kHpfOffHz, 400.0f);
    o.rms         = p.rms >= 0.5f ? 1.0f : 0.0f;
    o.rangeDb     = clampf(p.rangeDb, 1.0f, 40.0f);
    o.makeupDb    = clampf(p.makeupDb, 0.0f, 24.0f);
    o.softClip    = p.softClip >= 0.5f ? 1.0f : 0.0f;
    o.mix         = clampf(p.mix, 0.0f, 100.0f);
    return o;
}

float staticGainDb(const Parameters& raw, float levelDb) noexcept
{
    const Parameters p = clampParameters(raw);
    const float slope = 1.0f - 1.0f / p.ratio;
    const float over = levelDb - p.thresholdDb;
    float gr;
    if (2.0f * over < -p.kneeDb)
        gr = 0.0f;
    else if (2.0f * std::fabs(over) <= p.kneeDb)
        gr = -slope * (over + p.kneeDb * 0.5f) * (over + p.kneeDb * 0.5f) / (2.0f * p.kneeDb);
    else
        gr = -slope * over;
    return std::max(gr, -p.rangeDb);
}

float softClipSample(float x) noexcept
{
    constexpr float k = 0.5f;
    const float a = std::fabs(x);
    if (a <= k) return x;
    const float y = k + (1.0f - k) * std::tanh((a - k) / (1.0f - k));
    return x < 0.0f ? -y : y;
}

float autoReleaseMs(float depthDb) noexcept
{
    // Short after a transient, long once the compression has been sustained.
    return 40.0f + 760.0f * std::min(std::max(depthDb / 8.0f, 0.0f), 1.0f);
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
}

void processBlock(EngineState& state, const Parameters& rawParams, float sampleRate, uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept
{
    const Parameters p = clampParameters(rawParams);
    const float sr = sampleRate > 0.0f ? sampleRate : 44100.0f;

    const bool hpfOn = p.scHpfHz > kHpfOffHz + 0.5f;
    if (hpfOn) {
        setHighPass(state.hpf[0], p.scHpfHz, sr);
        // Keep the coefficients identical across channels; the memory stays per channel.
        const Biquad c = state.hpf[0];
        state.hpf[1].b0 = c.b0; state.hpf[1].b1 = c.b1; state.hpf[1].b2 = c.b2;
        state.hpf[1].a1 = c.a1; state.hpf[1].a2 = c.a2;
    }

    const float makeupGoal = std::exp(p.makeupDb * kDbToNeper);
    const float wetGoal = p.mix * 0.01f;
    if (!state.primed) {
        state.makeupLinear = makeupGoal;
        state.wet = wetGoal;
        state.primed = true;
    }
    const float makeupFrom = state.makeupLinear, wetFrom = state.wet;
    const float invFrames = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;

    const float attackCoeff = onePoleCoefficient(p.attackMs, sr);
    const float releaseCoeff = onePoleCoefficient(p.releaseMs, sr);
    const float rmsCoeff = onePoleCoefficient(10.0f, sr);
    const float depthCoeff = onePoleCoefficient(400.0f, sr);
    const bool useRms = p.rms >= 0.5f;
    const bool autoRel = p.autoRelease >= 0.5f;
    const bool clip = p.softClip >= 0.5f;

    const float* in0 = inputs[0];
    const float* in1 = inputs[1] ? inputs[1] : inputs[0];
    float* out0 = outputs[0];
    float* out1 = outputs[1];

    float level = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float x0 = in0[i], x1 = in1[i];
        if (!std::isfinite(x0)) x0 = 0.0f;
        if (!std::isfinite(x1)) x1 = 0.0f;

        float s0 = x0, s1 = x1;
        if (hpfOn) {
            s0 = runBiquad(state.hpf[0], s0);
            s1 = runBiquad(state.hpf[1], s1);
        }
        float a0, a1;
        if (useRms) {
            state.meanSquare[0] = state.meanSquare[0] * rmsCoeff + s0 * s0 * (1.0f - rmsCoeff);
            state.meanSquare[1] = state.meanSquare[1] * rmsCoeff + s1 * s1 * (1.0f - rmsCoeff);
            a0 = std::sqrt(state.meanSquare[0]);
            a1 = std::sqrt(state.meanSquare[1]);
        } else {
            a0 = std::fabs(s0);
            a1 = std::fabs(s1);
        }
        level = std::max(std::max(a0, a1), kFloorLevel);
        const float levelDb = 20.0f * std::log10(level);

        const float target = staticGainDb(p, levelDb);
        float coeff = releaseCoeff;
        if (target < state.gainDb)
            coeff = attackCoeff;
        else if (autoRel)
            coeff = onePoleCoefficient(autoReleaseMs(state.autoDepth), sr);
        state.gainDb = coeff * state.gainDb + (1.0f - coeff) * target;
        // Depth follows what the detector demands, not the lagging gain: otherwise a slow release
        // keeps the depth up, which keeps the release slow.
        state.autoDepth = depthCoeff * state.autoDepth + (1.0f - depthCoeff) * -target;

        const float t = static_cast<float>(i + 1) * invFrames;
        const float makeup = makeupFrom + (makeupGoal - makeupFrom) * t;
        const float wet = wetFrom + (wetGoal - wetFrom) * t;
        const float gain = std::exp(state.gainDb * kDbToNeper) * makeup;

        float w0 = x0 * gain, w1 = x1 * gain;
        if (clip) {
            w0 = softClipSample(w0);
            w1 = softClipSample(w1);
        }
        out0[i] = x0 + (w0 - x0) * wet;
        out1[i] = x1 + (w1 - x1) * wet;
    }

    // Recursive memory on a silent input decays into denormals; zero what is negligible.
    constexpr float kTiny = 1.0e-20f;
    for (Biquad& b : state.hpf) {
        if (std::fabs(b.z1) < kTiny) b.z1 = 0.0f;
        if (std::fabs(b.z2) < kTiny) b.z2 = 0.0f;
    }
    for (float& m : state.meanSquare)
        if (m < kTiny) m = 0.0f;
    if (std::fabs(state.gainDb) < 1.0e-6f) state.gainDb = 0.0f;
    if (state.autoDepth < 1.0e-6f) state.autoDepth = 0.0f;

    state.makeupLinear = makeupGoal;
    state.wet = wetGoal;
    state.meters.reductionDb = -state.gainDb;
    state.meters.levelDb = 20.0f * std::log10(std::max(level, kFloorLevel));
}

}  // namespace downspout::glue
