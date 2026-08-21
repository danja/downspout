#include "bassops_engine.hpp"

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

// Build Hann-windowed sinc HP coefficients.
// The mid channel is delayed by kMidDelayLen samples (the FIR group delay) so
// that delayed_mid + HP(side) is time-aligned. This avoids the signal loss that
// LP(mid)+HP(side) causes for mono content above the cutoff.
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

}  // namespace

Parameters clampParameters(const Parameters& raw)
{
    Parameters p = raw;
    p.duckDepth = clampf(p.duckDepth, 0.0f, 100.0f);
    p.attackMs  = clampf(p.attackMs,  1.0f, 500.0f);
    p.releaseMs = clampf(p.releaseMs, 10.0f, 2000.0f);
    p.cutoffHz  = clampf(p.cutoffHz,  50.0f, 5000.0f);
    p.sideShape = clampf(p.sideShape, 0.0f, 100.0f);
    p.wet       = clampf(p.wet,       0.0f, 100.0f);
    return p;
}

void activate(EngineState& state)
{
    state.env.envelope = 0.0f;
    state.fir.bufMid.fill(0.0f);
    state.fir.bufSide.fill(0.0f);
    state.fir.dryDelayL.fill(0.0f);
    state.fir.dryDelayR.fill(0.0f);
    state.fir.pos          = 0;
    state.fir.delayPos     = 0;
    state.fir.lastCutoffHz = -1.0f;
    state.meters           = {};
}

void processBlock(EngineState& state,
                  const Parameters& rawParameters,
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

    const float attackCoeff  = 1.0f - std::exp(-1.0f / (sr * p.attackMs  * 0.001f));
    const float releaseCoeff = 1.0f - std::exp(-1.0f / (sr * p.releaseMs * 0.001f));
    const float duckScale    = p.duckDepth * 0.01f;
    const float wetScale     = p.wet * 0.01f;
    const float dryScale     = 1.0f - wetScale;

    // Precompute waveshaper coefficients (avoid per-sample exp)
    constexpr float kLn8 = 2.0794415f;
    const float sideShapeNorm = p.sideShape * 0.01f;
    const float shapeDrive    = std::exp(sideShapeNorm * kLn8);  // 1 → 8
    const float shapeHardMix  = sideShapeNorm * sideShapeNorm;

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

        lastDuckGain = 1.0f - duckScale * env;

        const float inL = audio.mainL ? audio.mainL[frame] : 0.0f;
        const float inR = audio.mainR ? audio.mainR[frame] : 0.0f;

        const float absL = std::fabs(inL);
        const float absR = std::fabs(inR);
        if (absL > blockInPeak) { blockInPeak = absL; }
        if (absR > blockInPeak) { blockInPeak = absR; }

        const float dL = inL * lastDuckGain;
        const float dR = inR * lastDuckGain;

        // New routing: mono sum → LP for mid (clean bass), distorted mono → HP for side (harmonic width)
        // At shape=0: LP(x)+HP(x) = delayed(x), so outL=outR=delayed mono (perfect reconstruction, phase-coherent)
        // At shape>0: distortion adds harmonics to side, creating stereo width above cutoff
        const float mono = (dL + dR) * 0.5f;

        // LP path (clean mid)
        state.fir.bufMid[state.fir.pos] = mono;
        const float midF = firConvolve(state.fir.lp, state.fir.bufMid, state.fir.pos);

        // Distort + HP path (harmonic side)
        const float shaped = sideShapeNorm > 0.0f ? shapeSide(mono, shapeDrive, shapeHardMix) : mono;
        state.fir.bufSide[state.fir.pos] = shaped;
        const float sideF = firConvolve(state.fir.hp, state.fir.bufSide, state.fir.pos);

        state.fir.pos = (state.fir.pos + 1) % kFirTaps;

        // Delay dry signal to match FIR group delay (kMidDelayLen samples)
        const float delayedInL = state.fir.dryDelayL[state.fir.delayPos];
        const float delayedInR = state.fir.dryDelayR[state.fir.delayPos];
        state.fir.dryDelayL[state.fir.delayPos] = inL;
        state.fir.dryDelayR[state.fir.delayPos] = inR;
        state.fir.delayPos = (state.fir.delayPos + 1) % kMidDelayLen;

        const float outL = wetScale * (midF + sideF) + dryScale * delayedInL;
        const float outR = wetScale * (midF - sideF) + dryScale * delayedInR;
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
