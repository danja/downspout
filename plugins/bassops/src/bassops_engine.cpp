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

    // LP coefficients (scratch)
    std::array<float, kFirTaps> lp {};
    for (int n = 0; n <= M; ++n) {
        const float x = static_cast<float>(n) - static_cast<float>(M) * 0.5f;
        float sinc = (std::fabs(x) < 1e-6f)
            ? 2.0f * fc
            : std::sin(2.0f * pi * fc * x) / (pi * x);
        const float window = 0.5f * (1.0f - std::cos(2.0f * pi * static_cast<float>(n) / static_cast<float>(M)));
        lp[static_cast<std::size_t>(n)] = sinc * window;
    }

    // HP = delta[M/2] - LP
    for (std::uint32_t n = 0; n < kFirTaps; ++n) {
        fir.hp[n] = -lp[n];
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

}  // namespace

Parameters clampParameters(const Parameters& raw)
{
    Parameters p = raw;
    p.duckDepth = clampf(p.duckDepth, 0.0f, 100.0f);
    p.attackMs  = clampf(p.attackMs,  1.0f, 500.0f);
    p.releaseMs = clampf(p.releaseMs, 10.0f, 2000.0f);
    p.cutoffHz  = clampf(p.cutoffHz,  50.0f, 5000.0f);
    return p;
}

void activate(EngineState& state)
{
    state.env.envelope = 0.0f;
    state.fir.bufSide.fill(0.0f);
    state.fir.midDelay.fill(0.0f);
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

    state.meters.inputPeak  *= 0.85f;
    state.meters.outputPeak *= 0.85f;
    float blockInPeak  = 0.0f;
    float blockOutPeak = 0.0f;
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

        // M/S encode
        const float mid  = (dL + dR) * 0.5f;
        const float side = (dL - dR) * 0.5f;

        // Delay mid by kMidDelayLen samples to match HP FIR group delay
        const float delayedMid = state.fir.midDelay[state.fir.delayPos];
        state.fir.midDelay[state.fir.delayPos] = mid;
        state.fir.delayPos = (state.fir.delayPos + 1) % kMidDelayLen;

        // HP filter on side only: bass in side is suppressed, treble passes
        state.fir.bufSide[state.fir.pos] = side;
        const float sideF = firConvolve(state.fir.hp, state.fir.bufSide, state.fir.pos);
        state.fir.pos = (state.fir.pos + 1) % kFirTaps;

        // Decode: delayed_mid ± HP(side)
        // Below cutoff: HP(side)≈0 → both channels = mid (mono bass)
        // Above cutoff: HP(side)≈side → channels = mid±side = L,R (full stereo)
        const float outL = delayedMid + sideF;
        const float outR = delayedMid - sideF;
        if (audio.outL) { audio.outL[frame] = outL; }
        if (audio.outR) { audio.outR[frame] = outR; }

        const float absOL = std::fabs(outL);
        const float absOR = std::fabs(outR);
        if (absOL > blockOutPeak) { blockOutPeak = absOL; }
        if (absOR > blockOutPeak) { blockOutPeak = absOR; }
    }

    if (blockInPeak  > state.meters.inputPeak)  { state.meters.inputPeak  = blockInPeak;  }
    if (blockOutPeak > state.meters.outputPeak) { state.meters.outputPeak = blockOutPeak; }
    state.meters.scLevel  = state.env.envelope;
    state.meters.duckGain = lastDuckGain;
}

}  // namespace downspout::bassops
