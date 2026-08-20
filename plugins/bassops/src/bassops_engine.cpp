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

// Hann-windowed sinc LP; complementary HP = impulse@centre - LP
void recomputeFir(FirBank& fir, float cutoffHz, float sampleRate)
{
    const float fc = clampf(cutoffHz / sampleRate, 0.001f, 0.499f);
    const auto  M  = static_cast<int>(kFirOrder);
    const float pi = static_cast<float>(M_PI);

    for (int n = 0; n <= M; ++n) {
        const float x = static_cast<float>(n) - static_cast<float>(M) * 0.5f;
        float sinc;
        if (std::fabs(x) < 1e-6f) {
            sinc = 2.0f * fc;
        } else {
            sinc = std::sin(2.0f * pi * fc * x) / (pi * x);
        }
        const float window = 0.5f * (1.0f - std::cos(2.0f * pi * static_cast<float>(n) / static_cast<float>(M)));
        fir.lp[static_cast<std::size_t>(n)] = sinc * window;
    }

    // HP = delta[n - M/2] - LP
    for (std::uint32_t n = 0; n < kFirTaps; ++n) {
        fir.hp[n] = -fir.lp[n];
    }
    fir.hp[kFirOrder / 2] += 1.0f;

    fir.lastCutoffHz   = cutoffHz;
    fir.lastSampleRate = sampleRate;
}

// Circular-buffer FIR: buf[pos] holds the newest sample
[[nodiscard]] float firConvolve(const std::array<float, kFirTaps>& coeffs,
                                 const std::array<float, kFirTaps>& buf,
                                 std::uint32_t pos) noexcept
{
    float acc = 0.0f;
    for (std::uint32_t k = 0; k < kFirTaps; ++k) {
        // k=0 accesses buf[pos] (newest); each step back one sample
        const std::uint32_t idx = (pos + kFirTaps - k) % kFirTaps;
        acc += coeffs[k] * buf[idx];
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
    state.fir.bufMid.fill(0.0f);
    state.fir.bufSide.fill(0.0f);
    state.fir.pos          = 0;
    state.fir.lastCutoffHz = -1.0f;  // force FIR recompute on first block
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

    // One-pole envelope follower coefficients
    const float attackCoeff  = 1.0f - std::exp(-1.0f / (sr * p.attackMs  * 0.001f));
    const float releaseCoeff = 1.0f - std::exp(-1.0f / (sr * p.releaseMs * 0.001f));
    const float duckScale    = p.duckDepth * 0.01f;

    // Ballistic decay on peak meters (~0.85 per block keeps peaks visible)
    state.meters.inputPeak  *= 0.85f;
    state.meters.outputPeak *= 0.85f;
    float blockInPeak  = 0.0f;
    float blockOutPeak = 0.0f;
    float lastDuckGain = state.meters.duckGain;

    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        // Peak of sidechain stereo pair drives the envelope
        const float ctrlL = audio.controlL ? audio.controlL[frame] : 0.0f;
        const float ctrlR = audio.controlR ? audio.controlR[frame] : 0.0f;
        const float envIn = std::fabs(ctrlL) > std::fabs(ctrlR) ? std::fabs(ctrlL) : std::fabs(ctrlR);

        float& env        = state.env.envelope;
        const float coeff = envIn > env ? attackCoeff : releaseCoeff;
        env += (envIn - env) * coeff;

        // Gain reduction: 1 when env=0, (1-duckScale) when env=1
        lastDuckGain = 1.0f - duckScale * env;

        const float inL = audio.mainL ? audio.mainL[frame] : 0.0f;
        const float inR = audio.mainR ? audio.mainR[frame] : 0.0f;

        // Track input peak before ducking
        const float absinL = std::fabs(inL);
        const float absinR = std::fabs(inR);
        if (absinL > blockInPeak) { blockInPeak = absinL; }
        if (absinR > blockInPeak) { blockInPeak = absinR; }

        const float dL = inL * lastDuckGain;
        const float dR = inR * lastDuckGain;

        // M/S encode (encode with /2 so passthrough is unity when LP+HP=identity)
        const float mid  = (dL + dR) * 0.5f;
        const float side = (dL - dR) * 0.5f;

        // Write into circular FIR buffers at current position
        state.fir.bufMid [state.fir.pos] = mid;
        state.fir.bufSide[state.fir.pos] = side;

        // LP on mid, complementary HP on side
        const float midF  = firConvolve(state.fir.lp, state.fir.bufMid,  state.fir.pos);
        const float sideF = firConvolve(state.fir.hp, state.fir.bufSide, state.fir.pos);

        state.fir.pos = (state.fir.pos + 1) % kFirTaps;

        // M/S decode (decode restores unity for passthrough with /2 encode)
        const float outL = midF + sideF;
        const float outR = midF - sideF;
        if (audio.outL) { audio.outL[frame] = outL; }
        if (audio.outR) { audio.outR[frame] = outR; }

        // Track output peak
        const float absoutL = std::fabs(outL);
        const float absoutR = std::fabs(outR);
        if (absoutL > blockOutPeak) { blockOutPeak = absoutL; }
        if (absoutR > blockOutPeak) { blockOutPeak = absoutR; }
    }

    // Update meters with block results (peak-hold with decay applied above)
    if (blockInPeak  > state.meters.inputPeak)  { state.meters.inputPeak  = blockInPeak;  }
    if (blockOutPeak > state.meters.outputPeak) { state.meters.outputPeak = blockOutPeak; }
    state.meters.scLevel  = state.env.envelope;
    state.meters.duckGain = lastDuckGain;
}

}  // namespace downspout::bassops
