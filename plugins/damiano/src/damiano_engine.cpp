#include "damiano_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::damiano {

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters out = p;
    out.mode       = std::clamp(out.mode,       0.0f, static_cast<float>(kModeCount - 1));
    out.drive      = std::clamp(out.drive,      1.0f, 10.0f);
    out.tone       = std::clamp(out.tone,       0.0f, 100.0f);
    out.foldCount  = std::clamp(out.foldCount,  1.0f, 8.0f);
    out.mix        = std::clamp(out.mix,        0.0f, 100.0f);
    out.outputGain = std::clamp(out.outputGain, -24.0f, 24.0f);
    out.ccDrive    = std::clamp(out.ccDrive,    0.0f, 127.0f);
    out.ccChannel  = std::clamp(out.ccChannel,  1.0f, 16.0f);
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

// Asymptotic fuzz — approaches ±1, asymmetric positive bias
float fuzzShape(float x, float drive) noexcept
{
    float pre = x * drive * 4.0f;
    // Asymmetric: positive half slightly harder
    if (pre >= 0.0f) {
        return 1.0f - std::exp(-pre * 1.1f);
    } else {
        return -(1.0f - std::exp(pre));
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

// Triangle wavefolder: maps any float into [-1, 1] with triangle reflection
float triangleFold(float u) noexcept
{
    u += 1.0f;
    float t = std::fmod(u, 4.0f);
    if (t < 0.0f) t += 4.0f;
    return (t < 2.0f) ? (t - 1.0f) : (3.0f - t);
}

// Wavefold: apply triangle fold `count` times, each time driving the signal
float wavefoldShape(float x, float drive, int count) noexcept
{
    for (int i = 0; i < count; ++i) {
        x = triangleFold(x * drive);
    }
    return x;
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
    const float outGain = std::pow(10.0f, params.outputGain / 20.0f);

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

        float lp = state.toneLp[c];

        for (uint32_t n = 0; n < frames; ++n) {
            float dry = in[n];

            // High-shelf tone: lp subtraction gives high band
            lp = toneAlpha * lp + (1.0f - toneAlpha) * dry;
            float toned = dry + toneAmount * (dry - lp);

            float wet = processSample(toned, mode, effectiveDrive,
                                      state.tanhComp, foldCount);
            out[n] = (dry * dryMix + wet * wetMix) * outGain;
        }

        state.toneLp[c] = lp;
    }
}

}  // namespace downspout::damiano
