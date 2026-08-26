#include "damiano_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::damiano {

Parameters clampParameters(const Parameters& p) noexcept
{
    // std::clamp is UB for NaN inputs — use explicit isfinite guard first
    auto safe = [](float v, float lo, float hi, float def) noexcept {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
    };
    Parameters out = p;
    out.mode       = safe(out.mode,       0.0f,  5.0f,  1.0f);
    out.drive      = safe(out.drive,      1.0f, 10.0f,  2.0f);
    out.tone       = safe(out.tone,       0.0f, 100.0f, 50.0f);
    out.foldCount  = safe(out.foldCount,  1.0f,  8.0f,  2.0f);
    out.mix        = safe(out.mix,        0.0f, 100.0f, 100.0f);
    out.outputGain = safe(out.outputGain, -24.0f, 24.0f, 0.0f);
    out.ccDrive    = safe(out.ccDrive,    0.0f, 127.0f, 0.0f);
    out.ccChannel  = safe(out.ccChannel,  1.0f,  16.0f, 1.0f);
    return out;
}

namespace {

// Polynomial soft clip: x*(1.5 - 0.5*x²), clamped to [-1,1]
float softClip(float x, float drive) noexcept
{
    x *= drive;
    if (x > 1.0f)  return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

// Tanh waveshaper with drive and output normalization
float tanhShape(float x, float drive, float comp) noexcept
{
    return std::tanh(x * drive) * comp;
}

// Asymptotic fuzz — approaches ±1, slight positive-half asymmetry for even harmonics
// Small-signal gain ≈ drive, consistent with other modes
float fuzzShape(float x, float drive) noexcept
{
    if (x >= 0.0f) {
        return 1.0f - std::exp(-x * drive * 1.2f);
    } else {
        return -(1.0f - std::exp(x * drive));
    }
}

// Overdrive: biased tanh, DC-cancelled, normalized per half to ±1
// Bias asymmetry produces even harmonics (Tube-Screamer-style character)
float overdriveShape(float x, float drive) noexcept
{
    constexpr float kBias = 0.08f;
    float g = drive * 2.0f;
    float dc = std::tanh(kBias * g);
    float out = std::tanh((x + kBias) * g) - dc;
    // Normalize: positive peak = 1-dc, negative peak = 1+dc
    if (out >= 0.0f) {
        return out / (1.0f - dc);
    } else {
        return out / (1.0f + dc);
    }
}

// Tube: asymmetric — tanh for positive half, x/sqrt(1+x²) for negative half
// Adds even harmonics characteristic of class A topology
float tubeShape(float x, float drive) noexcept
{
    if (x >= 0.0f) {
        return std::tanh(x * drive) / std::tanh(drive);
    } else {
        float neg = -x * drive;
        return -(neg / std::sqrt(1.0f + neg * neg));
    }
}

// Sine wavefolder: smooth fold with no derivative discontinuities.
// Normalized by 2/π so that f'(0) = 1, matching the triangle fold's small-signal
// gain. Without normalization sin(πu/2) has gain π/2 ≈ 1.57 per stage, which
// amplifies noise floors (d × π/2)^n times — the "latches into noise" behaviour.
// With normalization the per-stage gain = d, identical to the triangle fold.
// Peak output is 2/π ≈ 0.637 instead of 1.0; use Output Gain to compensate.
float sineFold(float u) noexcept
{
    constexpr float kHalfPi  = static_cast<float>(M_PI) * 0.5f;
    constexpr float kNorm    = 2.0f / static_cast<float>(M_PI);  // restores f'(0)=1
    return kNorm * std::sin(u * kHalfPi);
}

// Wavefold: parallel sum of `count` sine folds at linearly spaced depths.
// Depths: d*(1/count), d*(3/count), ..., d*(2count-1)/count.
// Small-signal gain = d (Σ(2i-1) for i=1..count = count², avg/count = d).
// Drive sets the maximum fold depth; count adds harmonic variety. Both are
// clearly audible. No serial gain compounding, so no noise-latch at high settings.
float wavefoldShape(float x, float drive, int count) noexcept
{
    if (count <= 0) return x;
    const float d = 1.0f + (drive - 1.0f) * (2.0f / 9.0f);
    float sum = 0.0f;
    for (int i = 1; i <= count; ++i) {
        const float di = d * static_cast<float>(2 * i - 1) / static_cast<float>(count);
        sum += sineFold(x * di);
    }
    return sum / static_cast<float>(count);
}

float processSample(float x, Mode mode, float drive, float tanhComp, int foldCount) noexcept
{
    switch (mode) {
    case kModeSoft:     return softClip(x, drive);
    case kModeTanh:     return tanhShape(x, drive, tanhComp);
    case kModeFuzz:     return fuzzShape(x, drive);
    case kModeOverdrive: return overdriveShape(x, drive);
    case kModeTube:     return tubeShape(x, drive);
    case kModeWavefold: return wavefoldShape(x, drive, foldCount);
    default:            return x;
    }
}

}  // namespace

void processBlock(EngineState&      state,
                  const Parameters& params,
                  std::uint32_t     frames,
                  double            sampleRate,
                  const AudioBlock& audio,
                  float             effectiveDrive) noexcept
{
    effectiveDrive = std::clamp(effectiveDrive, 1.0f, 10.0f);

    // Update tanh compensation cache when drive changes
    if (effectiveDrive != state.cachedDrive) {
        state.tanhComp = (effectiveDrive > 0.001f)
                            ? 1.0f / std::tanh(effectiveDrive)
                            : 1.0f;
        state.cachedDrive = effectiveDrive;
    }

    const auto mode = static_cast<Mode>(static_cast<int>(std::round(params.mode)));
    const int  foldCount = static_cast<int>(std::round(params.foldCount));
    const float wetMix  = params.mix / 100.0f;
    const float dryMix  = 1.0f - wetMix;
    float outGain = std::pow(10.0f, params.outputGain / 20.0f);
    if (!std::isfinite(outGain)) outGain = 1.0f;

    // Tone shelf: one-pole high-shelf via low-pass subtraction
    // toneAmount: -0.8 (dark) to +0.8 (bright), 0 at 50
    const float toneAmount = ((params.tone - 50.0f) / 50.0f) * 0.8f;
    // Crossover ~3 kHz
    const float toneAlpha = std::exp(-2.0f * static_cast<float>(M_PI) * 3000.0f
                                     / static_cast<float>(sampleRate));

    const uint32_t ch = std::min(audio.channelCount, kMaxChannels);

    for (uint32_t c = 0; c < ch; ++c) {
        const float* in  = audio.inputs[c];
        float*       out = audio.outputs[c];
        if (!in || !out) continue;

        // Reset LP state if it became non-finite (NaN/Inf from a bad input block)
        float lp = state.toneLp[c];
        if (!std::isfinite(lp)) lp = 0.0f;

        for (uint32_t n = 0; n < frames; ++n) {
            float dry = in[n];
            // Treat non-finite input as silence rather than poisoning filter state
            if (!std::isfinite(dry)) dry = 0.0f;

            lp = toneAlpha * lp + (1.0f - toneAlpha) * dry;
            float toned = dry + toneAmount * (dry - lp);

            float wet = processSample(toned, mode, effectiveDrive,
                                      state.tanhComp, foldCount);
            // Final guard: no mode should produce non-finite output, but be safe
            if (!std::isfinite(wet)) wet = 0.0f;

            out[n] = (dry * dryMix + wet * wetMix) * outGain;
        }

        state.toneLp[c] = lp;
    }
}

}  // namespace downspout::damiano
