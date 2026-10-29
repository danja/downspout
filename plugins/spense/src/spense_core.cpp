#include "spense_core.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace downspout::spense {

namespace {

constexpr int kChunk = 64;                    // samples whose coefficients are computed together
constexpr float kRampSeconds = 0.01f;         // every control glides over 10 ms, as in the source
constexpr float kMinStageHz = 20.0f;
constexpr float kMaxStageFraction = 0.49f;    // of the sample rate
constexpr float kDriftOctaves = 6.37f;        // a Drift of 1 moves a stage by table x 6.37 octaves
constexpr float kFlushBelow = 1.0e-30f;

// Fixed per-stage frequency offsets in octaves per unit of Drift. Left and right differ.
constexpr std::array<float, kStagesPerCascade> kDriftLeft  = {-0.12f, -0.08f, 0.11f, -0.03f, 0.15f, -0.07f, 0.02f, -0.07f};
constexpr std::array<float, kStagesPerCascade> kDriftRight = {-0.12f, 0.05f, -0.08f, 0.15f, -0.03f, 0.11f, -0.07f, 0.02f};

const std::array<float, kStagesPerCascade>& driftTable(const int channel) noexcept
{
    return channel == 0 ? kDriftLeft : kDriftRight;
}

// 2^x from a degree-5 polynomial and an exponent-field add. The source's, kept so the drift
// offsets match it exactly.
float fastExp2(const float x) noexcept
{
    const float i = std::floor(x);
    const float f = x - i;
    const float r = 1.0000000f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013333f))));
    std::int32_t bits = std::bit_cast<std::int32_t>(r);
    bits += static_cast<std::int32_t>(i) << 23;
    return std::bit_cast<float>(bits);
}

float stageFrequency(const float centreHz, const float drift, const float offset, const float maxHz) noexcept
{
    return std::clamp(centreHz * fastExp2(drift * offset * kDriftOctaves), kMinStageHz, maxHz);
}

// The source's integrator gain: tan(pi f / sr) replaced by x + x^3/3. It is close at the low
// frequencies that matter and runs well short of tan near Nyquist; the filter uses it
// consistently, so the analysis below uses it too.
float integratorGain(const float hz, const float sampleRate) noexcept
{
    const float x = hz * (3.14159265358979f / sampleRate);
    return x * (1.0f + (x * x) * 0.333333f);
}

int rampSteps(const float sampleRate) noexcept
{
    return std::max(1, static_cast<int>(std::lround(kRampSeconds * sampleRate)));
}

float flushed(const float v) noexcept
{
    return std::fabs(v) < kFlushBelow ? 0.0f : v;
}

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters q = p;
    q.stack       = std::clamp(p.stack, 1.0f, static_cast<float>(kMaxStack));
    q.frequencyHz = std::clamp(p.frequencyHz, 40.0f, 20000.0f);
    q.resonance   = std::clamp(p.resonance, 0.1f, 10.0f);
    q.drift       = std::clamp(p.drift, 0.0f, 1.0f);
    q.preGainDb   = std::clamp(p.preGainDb, kMinGainDb, 12.0f);
    q.postGainDb  = std::clamp(p.postGainDb, kMinGainDb, 12.0f);
    q.clip        = std::clamp(p.clip, 0.0f, 1.0f);
    return q;
}

int stackCount(const Parameters& p) noexcept
{
    return std::clamp(static_cast<int>(std::lround(p.stack)), 1, kMaxStack);
}

int latencySamples(const Parameters& p) noexcept
{
    return stackCount(p) * kLatencyPerCascade;
}

float gainFromDb(const float db) noexcept
{
    return db <= kMinGainDb ? 0.0f : std::pow(10.0f, db * 0.05f);
}

void Ramp::setTarget(const float value, const int steps) noexcept
{
    if (value == target)
        return;
    target = value;
    if (steps <= 0) {
        current = value;
        remaining = 0;
        return;
    }
    remaining = steps;
    step = (value - current) / static_cast<float>(steps);
}

float Ramp::next() noexcept
{
    if (remaining <= 0)
        return target;
    --remaining;
    current = remaining == 0 ? target : current + step;
    return current;
}

void resetState(EngineState& state) noexcept
{
    for (auto& channel : state.channels)
        for (auto& cascade : channel)
            cascade = CascadeState {};
}

void processBlock(EngineState&      state,
                  const Parameters& params,
                  const float       sampleRate,
                  const uint32_t    frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept
{
    const Parameters p = clampParameters(params);
    const int stack = stackCount(p);
    const int steps = rampSteps(sampleRate);
    const float maxHz = sampleRate * kMaxStageFraction;
    const float preGain = gainFromDb(p.preGainDb), postGain = gainFromDb(p.postGainDb);

    if (!state.primed) {
        state.frequency.snap(p.frequencyHz);
        state.resonance.snap(p.resonance);
        state.drift.snap(p.drift);
        state.preGain.snap(preGain);
        state.postGain.snap(postGain);
        state.primed = true;
    } else {
        state.frequency.setTarget(p.frequencyHz, steps);
        state.resonance.setTarget(p.resonance, steps);
        state.drift.setTarget(p.drift, steps);
        state.preGain.setTarget(preGain, steps);
        state.postGain.setTarget(postGain, steps);
    }

    // A cascade that was switched off keeps no memory, so switching it back on starts clean.
    if (stack < state.activeStack)
        for (auto& channel : state.channels)
            for (int c = stack; c < kMaxStack; ++c)
                channel[static_cast<size_t>(c)] = CascadeState {};
    state.activeStack = stack;

    // Per-sample coefficients for one chunk. The cascades share them: every cascade in the source
    // computed identical values from identical smoothers, so they are computed once here.
    alignas(32) float a1[2][kChunk][kStagesPerCascade];
    alignas(32) float a2[2][kChunk][kStagesPerCascade];
    alignas(32) float a3[2][kChunk][kStagesPerCascade];
    float k[kChunk], pre[kChunk], post[kChunk];

    const float* const left = inputs[0];
    const float* const right = inputs[1] != nullptr ? inputs[1] : inputs[0];

    for (uint32_t pos = 0; pos < frames; pos += kChunk) {
        const int n = static_cast<int>(std::min<uint32_t>(kChunk, frames - pos));

        for (int s = 0; s < n; ++s) {
            const float drift = state.drift.next();
            const float freq = state.frequency.next();
            const float res = state.resonance.next();
            pre[s] = state.preGain.next();
            post[s] = state.postGain.next();
            const float damping = 1.0f / std::max(res, 0.001f);
            k[s] = damping;
            for (int ch = 0; ch < 2; ++ch) {
                const auto& table = driftTable(ch);
                for (int i = 0; i < kStagesPerCascade; ++i) {
                    const float g = integratorGain(stageFrequency(freq, drift, table[static_cast<size_t>(i)], maxHz), sampleRate);
                    const float inv = 1.0f / (1.0f + g * (g + damping));
                    a1[ch][s][i] = inv;
                    a2[ch][s][i] = g * inv;
                    a3[ch][s][i] = g * g * inv;
                }
            }
        }

        // Right first: with a null right input it reads the left, which an in-place run overwrites.
        for (int ch = 1; ch >= 0; --ch) {
            const float* in = (ch == 0 ? left : right) + pos;
            float* out = outputs[ch] + pos;
            for (int s = 0; s < n; ++s)
                out[s] = in[s] * pre[s];

            for (int c = 0; c < stack; ++c) {
                CascadeState& st = state.channels[static_cast<size_t>(ch)][static_cast<size_t>(c)];
                for (int s = 0; s < n; ++s) {
                    // Each stage reads the previous stage's output from the previous sample.
                    float stageIn[kStagesPerCascade];
                    stageIn[0] = out[s];
                    for (int i = 1; i < kStagesPerCascade; ++i)
                        stageIn[i] = st.out[static_cast<size_t>(i - 1)];
                    for (int i = 0; i < kStagesPerCascade; ++i) {
                        const size_t u = static_cast<size_t>(i);
                        const float v3 = stageIn[i] - st.s2[u];
                        const float v1 = a1[ch][s][i] * st.s1[u] + a2[ch][s][i] * v3;
                        const float v2 = st.s2[u] + a2[ch][s][i] * st.s1[u] + a3[ch][s][i] * v3;
                        st.s1[u] = 2.0f * v1 - st.s1[u];
                        st.s2[u] = 2.0f * v2 - st.s2[u];
                        st.out[u] = stageIn[i] - 2.0f * k[s] * v1;
                    }
                    out[s] = st.out[kStagesPerCascade - 1];
                }
            }

            for (int s = 0; s < n; ++s)
                out[s] *= post[s];
            if (p.clip > 0.5f)
                for (int s = 0; s < n; ++s)
                    out[s] = std::clamp(out[s], -1.0f, 1.0f);
        }
    }

    // Idle recursive filters decay into denormals, which are slow; snap them to zero.
    for (auto& channel : state.channels)
        for (int c = 0; c < stack; ++c) {
            CascadeState& st = channel[static_cast<size_t>(c)];
            for (size_t i = 0; i < kStagesPerCascade; ++i) {
                st.s1[i] = flushed(st.s1[i]);
                st.s2[i] = flushed(st.s2[i]);
                st.out[i] = flushed(st.out[i]);
            }
        }
}

float stageFrequencyHz(const Parameters& params, const int stage, const int channel, const float sampleRate) noexcept
{
    const Parameters p = clampParameters(params);
    return stageFrequency(p.frequencyHz, p.drift, driftTable(channel)[static_cast<size_t>(std::clamp(stage, 0, kStagesPerCascade - 1))],
                          sampleRate * kMaxStageFraction);
}

namespace {

// One stage as the analysis sees it: nu = tan(w/2)/g puts the bilinear-mapped all-pass
// (nu^2 - k nu j ... ) in the form H = (1 - nu^2 - j k nu) / (1 - nu^2 + j k nu).
struct StageTerms {
    double k, nu, g, t;
};

StageTerms stageTerms(const Parameters& p, const int stage, const int channel, const double hz, const double sampleRate) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * std::min(hz, sampleRate * 0.4999) / sampleRate;
    const double t = std::tan(0.5 * w);
    const double g = integratorGain(stageFrequencyHz(p, stage, channel, static_cast<float>(sampleRate)), static_cast<float>(sampleRate));
    return {1.0 / std::max(static_cast<double>(p.resonance), 0.001), t / g, g, t};
}

}  // namespace

double phaseRadians(const Parameters& params, const double hz, const int channel, const double sampleRate) noexcept
{
    const Parameters p = clampParameters(params);
    const int stack = stackCount(p);
    const double pi = 3.14159265358979323846;
    double phase = -2.0 * pi * hz / sampleRate * static_cast<double>(stack * kLatencyPerCascade);
    for (int i = 0; i < kStagesPerCascade; ++i) {
        const StageTerms s = stageTerms(p, i, channel, hz, sampleRate);
        phase += static_cast<double>(stack) * -2.0 * std::atan2(s.k * s.nu, 1.0 - s.nu * s.nu);
    }
    return std::remainder(phase, 2.0 * pi);
}

double groupDelaySamples(const Parameters& params, const double hz, const int channel, const double sampleRate) noexcept
{
    const Parameters p = clampParameters(params);
    const int stack = stackCount(p);
    double total = 0.0;
    for (int i = 0; i < kStagesPerCascade; ++i) {
        const StageTerms s = stageTerms(p, i, channel, hz, sampleRate);
        const double denominator = (1.0 - s.nu * s.nu) * (1.0 - s.nu * s.nu) + s.k * s.k * s.nu * s.nu;
        total += s.k * (1.0 + s.nu * s.nu) * (1.0 + s.t * s.t) / (s.g * denominator);
    }
    return total * static_cast<double>(stack);
}

}  // namespace downspout::spense
