#include "bassops_engine.hpp"

#include <array>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace downspout::bassops {
namespace {

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Build Hann-windowed sinc LP/HP coefficients.
// The LP kernel is normalised to unity DC gain. A 129-tap Hann sinc at a low
// cutoff is truncated well short of its first zero crossings and sums to about
// 0.5, which made the old HP = delta - LP pass half of every bass note.
void recomputeFir(FirBank& fir, float cutoffHz, float sampleRate)
{
    const float fc = clampf(cutoffHz / sampleRate, 0.001f, 0.499f);
    const auto  M  = static_cast<int>(kFirOrder);
    const float pi = static_cast<float>(M_PI);

    // LP coefficients
    for (int n = 0; n <= M; ++n) {
        const float x = static_cast<float>(n) - static_cast<float>(M) * 0.5f;
        float sinc = (std::fabs(x) < 1e-6f)
            ? 2.0f * fc
            : std::sin(2.0f * pi * fc * x) / (pi * x);
        const float window = 0.5f * (1.0f - std::cos(2.0f * pi * static_cast<float>(n) / static_cast<float>(M)));
        fir.lp[static_cast<std::size_t>(n)] = sinc * window;
    }
    float sum = 0.0f;
    for (std::uint32_t n = 0; n < kFirTaps; ++n)
        sum += fir.lp[n];
    if (sum > 1.0e-6f) {
        for (std::uint32_t n = 0; n < kFirTaps; ++n)
            fir.lp[n] /= sum;
    }

    // HP = delta[M/2] - LP
    for (std::uint32_t n = 0; n < kFirTaps; ++n) {
        fir.hp[n] = -fir.lp[n];
    }
    fir.hp[kFirOrder / 2] += 1.0f;

    fir.lastCutoffHz   = cutoffHz;
    fir.lastSampleRate = sampleRate;
}

// Circular-buffer FIR convolution; buf[pos] holds the newest sample.
[[nodiscard]] float firConvolve(const std::array<float, kFirTaps>& coeffs,
                                 const std::array<float, kFirTaps>& buf,
                                 std::uint32_t pos) noexcept
{
    float acc = 0.0f;
    for (std::uint32_t k = 0; k < kFirTaps; ++k) {
        acc += coeffs[k] * buf[(pos + kFirTaps - k) % kFirTaps];
    }
    return acc;
}

// Waveshaper applied to the side channel before the HP filter.
// drive and hardMix are precomputed per block from sideShape.
// drive = exp(shape * ln8): 1 at shape=0, 8 at shape=1.
// hardMix = shape^2: blends tanh (soft) toward clamp (hard) as shape rises.
// Dividing by drive after clipping normalises small-signal gain back to unity.
[[nodiscard]] float shapeSide(float x, float drive, float hardMix) noexcept
{
    const float driven  = x * drive;
    const float soft    = std::tanh(driven);
    const float hard    = clampf(driven, -1.0f, 1.0f);
    const float clipped = soft + hardMix * (hard - soft);
    return clipped / drive;
}

// Hilbert-pair coefficients (Niemitalo, 12 poles, +-0.7 deg from about 0.0002 to 0.4998 of
// the sample rate). Each section is a first-order allpass in z^-2:
// y[n] = a^2 (x[n] + y[n-2]) - x[n-2].
constexpr std::array<float, kQuadSections> kQuadA = {0.6923878f, 0.9360654322959f,
                                                     0.9882295226860f, 0.9987488452737f};
constexpr std::array<float, kQuadSections> kQuadB = {0.4021921162426f, 0.8561710882420f,
                                                     0.9722909545651f, 0.9952884791278f};

[[nodiscard]] float allpassStep(AllpassSection& s, float a, float x) noexcept
{
    const float c = a * a;
    const float y = c * (x + s.y2) - s.x2;
    s.x2 = s.x1; s.x1 = x;
    s.y2 = s.y1; s.y1 = y;
    return y;
}

void quadStep(BassQuad& q, float x, float& outA, float& outB) noexcept
{
    float a = x;
    for (std::uint32_t k = 0; k < kQuadSections; ++k)
        a = allpassStep(q.pathA[k], kQuadA[k], a);
    float b = q.delayB;
    q.delayB = x;
    for (std::uint32_t k = 0; k < kQuadSections; ++k)
        b = allpassStep(q.pathB[k], kQuadB[k], b);
    outA = a;
    outB = b;
}

constexpr float kMainEnvCoeff = 0.001f;  // about 20 ms at 48 kHz
constexpr float kSilentEnv    = 1.0e-6f; // -120 dB

}  // namespace

Parameters clampParameters(const Parameters& raw)
{
    Parameters p = raw;
    p.duckDepth = clampf(p.duckDepth, 0.0f, 100.0f);
    p.attackMs  = clampf(p.attackMs,  0.0f, 500.0f);
    p.releaseMs = clampf(p.releaseMs, 0.0f, 2000.0f);
    p.cutoffHz  = clampf(p.cutoffHz,  50.0f, 5000.0f);
    p.sideShape = clampf(p.sideShape, 0.0f, 100.0f);
    p.wet       = clampf(p.wet,       0.0f, 100.0f);
    p.bassPhase = p.bassPhase < 0 ? 0 : (p.bassPhase > 2 ? 2 : p.bassPhase);
    return p;
}

void activate(EngineState& state)
{
    state.env = {};
    state.fir.bufMid.fill(0.0f);
    state.fir.bufSide.fill(0.0f);
    state.fir.dryDelayL.fill(0.0f);
    state.fir.dryDelayR.fill(0.0f);
    state.fir.pos          = 0;
    state.fir.delayPos     = 0;
    state.fir.lastCutoffHz = -1.0f;
    state.quad             = {};
    state.meters           = {};
}

void handleMidi(EngineState& state, const BusParameters& bus, const MidiControlEvent& ev)
{
    if (!bus.midiEnabled || ev.size < 3) return;

    const std::uint8_t status  = ev.data[0] & 0xF0u;
    const int          channel = (ev.data[0] & 0x0Fu) + 1;  // 1-16

    if (status != 0xB0u) return;  // only CC messages

    if (bus.controlChannel != 0 && channel != bus.controlChannel) return;

    const std::uint8_t cc  = ev.data[1];
    const std::uint8_t val = ev.data[2];

    if (cc == kProducerLifecycleCC) {
        state.bus.producerActive = (val >= 64);
        if (!state.bus.producerActive) {
            state.bus.duckMidiActive = false;
            state.bus.wetMidiActive  = false;
        }
        return;
    }

    if (bus.requireGate && !state.bus.producerActive) return;

    if (cc == kDuckDepthCC) {
        state.bus.duckMidiActive = true;
        state.bus.duckMidiValue  = val / 127.0f * 100.0f;
    } else if (cc == kWetCC) {
        state.bus.wetMidiActive = true;
        state.bus.wetMidiValue  = val / 127.0f * 100.0f;
    }
}

float effectiveDuckDepth(const EngineState& state, const Parameters& p) noexcept
{
    return state.bus.duckMidiActive ? state.bus.duckMidiValue : p.duckDepth;
}

float effectiveWet(const EngineState& state, const Parameters& p) noexcept
{
    return state.bus.wetMidiActive ? state.bus.wetMidiValue : p.wet;
}

void processBlock(EngineState& state,
                  const Parameters& rawParameters,
                  const BusParameters& /*busParams*/,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio)
{
    if (nframes == 0 || sampleRate <= 0.0) {
        return;
    }

    const Parameters p  = clampParameters(rawParameters);
    const float      sr = static_cast<float>(sampleRate);

    if (p.cutoffHz != state.fir.lastCutoffHz || sr != state.fir.lastSampleRate) {
        recomputeFir(state.fir, p.cutoffHz, sr);
    }

    // 0 ms = instant. With both at 0 the follower is bypassed and the main signal is
    // multiplied by the signed sidechain (ring mod), blended in by Duck Depth.
    const bool  ringMod      = p.attackMs <= 0.0f && p.releaseMs <= 0.0f;
    const float attackCoeff  = p.attackMs  <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / (sr * p.attackMs  * 0.001f));
    const float releaseCoeff = p.releaseMs <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / (sr * p.releaseMs * 0.001f));
    const float duckScale    = effectiveDuckDepth(state, p) * 0.01f;
    const float wetScale     = effectiveWet(state, p) * 0.01f;
    const float dryScale     = 1.0f - wetScale;

    // Precompute waveshaper coefficients (avoid per-sample exp)
    constexpr float kLn8 = 2.0794415f;
    const float sideShapeNorm = p.sideShape * 0.01f;
    const float shapeDrive    = std::exp(sideShapeNorm * kLn8);  // 1 → 8
    const float shapeHardMix  = sideShapeNorm * sideShapeNorm;

    // Bass quadrature is idle unless selected: no filtering, and stale state is dropped
    // so it starts clean when switched on.
    const bool bassOn = p.bassPhase != 0;
    if (!bassOn)
        state.quad = {};
    state.quad.running = bassOn;

    state.meters.inputPeak *= 0.85f;
    float blockInPeak  = 0.0f;
    float blockOutSum  = 0.0f;
    float lastDuckGain = state.meters.duckGain;

    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        const float ctrlL = audio.controlL ? audio.controlL[frame] : 0.0f;
        const float ctrlR = audio.controlR ? audio.controlR[frame] : 0.0f;
        const float envIn = std::fabs(ctrlL) > std::fabs(ctrlR) ? std::fabs(ctrlL) : std::fabs(ctrlR);

        float& env        = state.env.envelope;
        env += (envIn - env) * (envIn > env ? attackCoeff : releaseCoeff);

        // Signed sidechain sample: the larger-magnitude channel, so a mono feed on one side works.
        const float ctrlSigned = std::fabs(ctrlL) > std::fabs(ctrlR) ? ctrlL : ctrlR;
        const float gain = ringMod ? 1.0f - duckScale + duckScale * ctrlSigned
                                   : 1.0f - duckScale * env;
        lastDuckGain = ringMod ? clampf(std::fabs(gain), 0.0f, 1.0f) : gain;

        float inL = audio.mainL ? audio.mainL[frame] : 0.0f;
        float inR = audio.mainR ? audio.mainR[frame] : 0.0f;

        // A mono track only feeds one of the two main inputs. Treat a silent
        // side as a copy of the other, or the mono sum is 6 dB down and the
        // dry path comes out of one channel only.
        state.env.mainEnvL += (std::fabs(inL) - state.env.mainEnvL) * kMainEnvCoeff;
        state.env.mainEnvR += (std::fabs(inR) - state.env.mainEnvR) * kMainEnvCoeff;
        if (state.env.mainEnvR < kSilentEnv && state.env.mainEnvL >= kSilentEnv)
            inR = inL;
        else if (state.env.mainEnvL < kSilentEnv && state.env.mainEnvR >= kSilentEnv)
            inL = inR;

        const float absL = std::fabs(inL);
        const float absR = std::fabs(inR);
        if (absL > blockInPeak) { blockInPeak = absL; }
        if (absR > blockInPeak) { blockInPeak = absR; }

        const float dL = inL * gain;
        const float dR = inR * gain;

        // Routing: the mono sum, delayed by the FIR latency, is the mid. The side is only
        // what the waveshaper adds above the cutoff: HP(shaped - mono). Decoded as
        // L = mid + side, R = mid - side, so the full-range mono signal (bass included)
        // is identical in both channels, and at shape=0 the side is exactly zero.
        const float mono = (dL + dR) * 0.5f;

        state.fir.bufMid[state.fir.pos] = mono;
        const float midF = state.fir.bufMid[(state.fir.pos + kFirTaps - kMidDelayLen) % kFirTaps];

        float sideF = 0.0f;
        if (sideShapeNorm > 0.0f) {
            state.fir.bufSide[state.fir.pos] = shapeSide(mono, shapeDrive, shapeHardMix) - mono;
            sideF = firConvolve(state.fir.hp, state.fir.bufSide, state.fir.pos);
        } else {
            state.fir.bufSide[state.fir.pos] = 0.0f;
        }

        // Bass part after the filter (LP of the mono sum, aligned with midF). Each channel's
        // bass is swapped for one leg of the quadrature pair; the selected channel gets the
        // leg that lags the other by 90 degrees.
        float bassDeltaL = 0.0f;
        float bassDeltaR = 0.0f;
        if (bassOn) {
            const float bass = firConvolve(state.fir.lp, state.fir.bufMid, state.fir.pos);
            float qa = 0.0f, qb = 0.0f;
            quadStep(state.quad, bass, qa, qb);
            bassDeltaL = (p.bassPhase == 1 ? qb : qa) - bass;
            bassDeltaR = (p.bassPhase == 1 ? qa : qb) - bass;
        }

        state.fir.pos = (state.fir.pos + 1) % kFirTaps;

        // Delay dry signal to match FIR group delay (kMidDelayLen samples)
        const float delayedInL = state.fir.dryDelayL[state.fir.delayPos];
        const float delayedInR = state.fir.dryDelayR[state.fir.delayPos];
        state.fir.dryDelayL[state.fir.delayPos] = inL;
        state.fir.dryDelayR[state.fir.delayPos] = inR;
        state.fir.delayPos = (state.fir.delayPos + 1) % kMidDelayLen;

        const float outL = wetScale * (midF + sideF + bassDeltaL) + dryScale * delayedInL;
        const float outR = wetScale * (midF - sideF + bassDeltaR) + dryScale * delayedInR;
        if (audio.outL) { audio.outL[frame] = outL; }
        if (audio.outR) { audio.outR[frame] = outR; }

        blockOutSum += std::fabs(outL) + std::fabs(outR);
    }

    if (blockInPeak > state.meters.inputPeak) { state.meters.inputPeak = blockInPeak; }
    // Mean absolute output with fast-release ballistic so the meter tracks ducking immediately
    const float blockOutMean = blockOutSum / (2.0f * static_cast<float>(nframes));
    state.meters.outputPeak = std::max(state.meters.outputPeak * 0.7f, blockOutMean);
    state.meters.scLevel  = state.env.envelope;
    state.meters.duckGain = lastDuckGain;
}

}  // namespace downspout::bassops
