#include "energise_render.hpp"

#include "energise_params.hpp"
#include "energise_rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace downspout::energise {
namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] float softClip(const float x, const float drive)
{
    if (drive <= 0.0f)
        return x;
    const float k = 1.0f + drive * 7.0f;
    return std::tanh(x * k) / std::tanh(k);
}

// Two-times oversampled nonlinearity with a 3-tap downsampling filter. The
// direct path is deliberately available for the alias probe in the core tests.
void saturate(std::vector<float>& left, std::vector<float>& right, const float amount)
{
    if (amount <= 0.0f)
        return;
    const std::size_t frames = std::min(left.size(), right.size());
    std::vector<float> outL(frames);
    std::vector<float> outR(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const float previousL = i > 0 ? left[i - 1] : left[i];
        const float nextL = i + 1 < frames ? left[i + 1] : left[i];
        const float middleL = 0.5f * (left[i] + nextL);
        const float beforeL = 0.5f * (previousL + left[i]);
        const float previousR = i > 0 ? right[i - 1] : right[i];
        const float nextR = i + 1 < frames ? right[i + 1] : right[i];
        const float middleR = 0.5f * (right[i] + nextR);
        const float beforeR = 0.5f * (previousR + right[i]);
        outL[i] = 0.25f * softClip(beforeL, amount) + 0.5f * softClip(middleL, amount)
                + 0.25f * softClip(nextL, amount);
        outR[i] = 0.25f * softClip(beforeR, amount) + 0.5f * softClip(middleR, amount)
                + 0.25f * softClip(nextR, amount);
    }
    left = std::move(outL);
    right = std::move(outR);
}

void addNoiseBurst(std::vector<float>& left, std::vector<float>& right, const int sr,
                   const std::size_t start, const std::size_t length, const float level,
                   const float decay, Rng& rng)
{
    float low = 0.0f;
    for (std::size_t i = 0; i < length && start + i < left.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const float noise = rng.nextSigned();
        low += 0.35f * (noise - low);
        const float bright = noise - low;
        const float value = bright * level * static_cast<float>(std::exp(-t / std::max(0.001, static_cast<double>(decay))));
        left[start + i] += value;
        right[start + i] += value;
    }
}

}  // namespace

double tailSeconds(const DrumClass cls) noexcept
{
    switch (cls) {
    case DrumClass::Kick: return 0.50;
    case DrumClass::Snare: return 0.36;
    case DrumClass::Clap: return 0.30;
    case DrumClass::Rim: return 0.12;
    case DrumClass::ClosedHat: return 0.16;
    case DrumClass::OpenHat: return 0.75;
    case DrumClass::Ride: return 1.40;
    case DrumClass::Crash: return 2.00;
    case DrumClass::LowTom:
    case DrumClass::MidTom:
    case DrumClass::HighTom: return 0.60;
    case DrumClass::Percussion: return 0.30;
    case DrumClass::Unknown:
    default: return 0.24;
    }
}

RenderedBuffer renderVoice(const DrumEvent& event, const RenderSettings& settings,
                           const std::uint32_t seed, const int sampleRate)
{
    const int sr = sampleRate > 0 ? sampleRate : 48000;
    const auto cls = event.primary;
    const double tail = tailSeconds(cls);
    RenderedBuffer voice {};
    voice.sampleRate = sr;
    voice.left.assign(static_cast<std::size_t>(tail * sr), 0.0f);
    voice.right.assign(voice.left.size(), 0.0f);
    if (voice.left.empty())
        return voice;

    Rng rng {};
    rng.seed(hashSeed(seed, event.id));
    const float velocity = std::clamp(event.velocity, 0.0f, 1.0f);
    const float variation = std::clamp(settings.variation, 0.0f, 1.0f);
    const float detune = 1.0f + rng.nextSigned() * variation * 0.06f;
    const std::size_t frames = voice.left.size();

    // Transient layer: a 1-3 ms class-filtered click.
    addNoiseBurst(voice.left, voice.right, sr, 0, static_cast<std::size_t>(0.003 * sr),
                  settings.transientLevel * velocity * 0.55f, 0.0012f, rng);

    double phase = rng.nextFloat() * kPi * 2.0;
    switch (cls) {
    case DrumClass::Kick: {
        double frequency = 52.0f * detune;
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sr;
            frequency = 48.0 * detune + 140.0 * detune * std::exp(-t * 24.0);
            phase += kPi * 2.0 * frequency / sr;
            const float body = static_cast<float>(std::sin(phase) * std::exp(-t * 8.0));
            voice.left[i] += body * velocity * 0.85f;
            voice.right[i] += body * velocity * 0.85f;
        }
        break;
    }
    case DrumClass::Snare: {
        double frequency = 200.0 * detune;
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sr;
            phase += kPi * 2.0 * frequency / sr;
            const float body = static_cast<float>(std::sin(phase) * std::exp(-t * 28.0));
            voice.left[i] += body * velocity * 0.35f;
            voice.right[i] += body * velocity * 0.35f;
        }
        addNoiseBurst(voice.left, voice.right, sr, 0, frames, velocity * 0.75f, 0.055f, rng);
        break;
    }
    case DrumClass::Clap:
        for (const double offset : {0.0, 0.009, 0.021}) {
            addNoiseBurst(voice.left, voice.right, sr, static_cast<std::size_t>(offset * sr),
                          static_cast<std::size_t>(0.05 * sr), velocity * 0.45f, 0.012f, rng);
        }
        addNoiseBurst(voice.left, voice.right, sr, 0, frames, velocity * 0.16f, 0.07f, rng);
        break;
    case DrumClass::Rim:
        addNoiseBurst(voice.left, voice.right, sr, 0, frames, velocity * 0.8f, 0.018f, rng);
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sr;
            phase += kPi * 2.0 * 740.0 * detune / sr;
            const float tone = static_cast<float>(std::sin(phase) * std::exp(-t * 65.0));
            voice.left[i] += tone * velocity * 0.2f;
            voice.right[i] += tone * velocity * 0.2f;
        }
        break;
    case DrumClass::ClosedHat:
    case DrumClass::OpenHat: {
        const double decay = cls == DrumClass::ClosedHat ? 0.028 : 0.18;
        addNoiseBurst(voice.left, voice.right, sr, 0, frames, velocity * 0.9f, decay, rng);
        static constexpr std::array<double, 4> kPartials = {{6300.0, 8400.0, 10100.0, 12300.0}};
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sr;
            float metal = 0.0f;
            for (const double partial : kPartials) {
                metal += static_cast<float>(std::sin(phase + kPi * 2.0 * partial / sr * static_cast<double>(i)));
            }
            voice.left[i] += metal * velocity * 0.025f * static_cast<float>(std::exp(-t / decay));
            voice.right[i] += metal * velocity * 0.025f * static_cast<float>(std::exp(-t / decay));
        }
        break;
    }
    case DrumClass::Ride:
    case DrumClass::Crash: {
        static constexpr std::array<double, 8> kPartials = {{
            2103.0, 2789.0, 3647.0, 4211.0, 5317.0, 6199.0, 7531.0, 8807.0,
        }};
        const double decay = cls == DrumClass::Crash ? 0.62 : 0.45;
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sr;
            float metal = 0.0f;
            for (std::size_t p = 0; p < kPartials.size(); ++p) {
                metal += static_cast<float>(std::sin(phase + kPi * 2.0 * kPartials[p] * detune / sr * static_cast<double>(i)))
                       / static_cast<float>(kPartials.size());
            }
            const float envelope = static_cast<float>(std::exp(-t / decay));
            voice.left[i] += metal * velocity * 0.45f * envelope;
            voice.right[i] += metal * velocity * 0.45f * envelope;
        }
        addNoiseBurst(voice.left, voice.right, sr, 0, frames, velocity * 0.18f, decay * 0.6, rng);
        break;
    }
    case DrumClass::LowTom:
    case DrumClass::MidTom:
    case DrumClass::HighTom: {
        const double base = cls == DrumClass::LowTom ? 95.0 : (cls == DrumClass::MidTom ? 140.0 : 200.0);
        double frequency = base * detune;
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sr;
            frequency = base * detune * (1.0 + 0.7 * std::exp(-t * 16.0));
            phase += kPi * 2.0 * frequency / sr;
            const float body = static_cast<float>(std::sin(phase) * std::exp(-t * 9.0));
            voice.left[i] += body * velocity * 0.8f;
            voice.right[i] += body * velocity * 0.8f;
        }
        addNoiseBurst(voice.left, voice.right, sr, 0, static_cast<std::size_t>(0.008 * sr), velocity * 0.3f, 0.003f, rng);
        break;
    }
    case DrumClass::Percussion:
        addNoiseBurst(voice.left, voice.right, sr, 0, frames, velocity * 0.6f, 0.045f, rng);
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sr;
            phase += kPi * 2.0 * 310.0 * detune / sr;
            const float tone = static_cast<float>(std::sin(phase) * std::exp(-t * 22.0));
            voice.left[i] += tone * velocity * 0.2f;
            voice.right[i] += tone * velocity * 0.2f;
        }
        break;
    case DrumClass::Unknown:
    default:
        addNoiseBurst(voice.left, voice.right, sr, 0, frames, velocity * 0.5f, 0.035f, rng);
        break;
    }

    for (std::size_t i = 0; i < frames; ++i) {
        voice.left[i] *= settings.bodyLevel;
        voice.right[i] *= settings.bodyLevel;
    }
    saturate(voice.left, voice.right, settings.saturation);
    for (std::size_t i = 0; i < frames; ++i) {
        if (std::fabs(voice.left[i]) < 1e-7f) voice.left[i] = 0.0f;
        if (std::fabs(voice.right[i]) < 1e-7f) voice.right[i] = 0.0f;
    }
    return voice;
}

RenderedBuffer renderBreak(const CaptureBuffer& capture, const std::vector<DrumEvent>& events,
                           const RenderSettings& settings, const std::uint32_t seed,
                           const std::uint16_t muteMask, const std::uint16_t soloMask,
                           const float outputTrim)
{
    RenderedBuffer out {};
    const int sr = capture.sampleRate > 0 ? capture.sampleRate : 48000;
    out.sampleRate = sr;
    const std::size_t captureFrames = std::min(capture.left.size(), capture.right.size());
    double longestTail = 0.2;
    for (const DrumEvent& event : events)
        longestTail = std::max(longestTail, tailSeconds(event.primary));
    out.left.assign(captureFrames + static_cast<std::size_t>(longestTail * sr), 0.0f);
    out.right.assign(out.left.size(), 0.0f);

    const bool anySolo = soloMask != 0u;
    for (const DrumEvent& event : events) {
        const auto bit = classBit(event.primary);
        if (event.muted || (muteMask & bit) != 0u || (anySolo && (soloMask & bit) == 0u))
            continue;
        const auto cls = static_cast<std::size_t>(event.primary);
        const float gain = std::clamp(settings.classGains[cls], 0.0f, 2.0f);
        const float pan = std::clamp(event.pan + static_cast<float>(settings.classOffsetsMs[cls]) * 0.0f, -1.0f, 1.0f);
        const std::size_t start = static_cast<std::size_t>(std::max(0.0, event.quantisedTime) * sr);

        RenderedBuffer voice = renderVoice(event, settings, seed, sr);
        for (std::size_t i = 0; i < voice.left.size() && start + i < out.left.size(); ++i) {
            const float leftGain = gain * (1.0f - pan) * 0.5f;
            const float rightGain = gain * (1.0f + pan) * 0.5f;
            out.left[start + i] += voice.left[i] * leftGain;
            out.right[start + i] += voice.right[i] * rightGain;
        }

        // Residual/texture layer: a short crossfaded fragment of the original hit.
        if (settings.residualLevel > 0.0f && event.sourceDuration < 10.0) {
            const std::size_t sourceStart = static_cast<std::size_t>(std::max(0.0, event.sourceTime - 0.003) * sr);
            const std::size_t fragment = std::min(static_cast<std::size_t>(0.035 * sr), captureFrames - std::min(sourceStart, captureFrames));
            const std::size_t fade = std::min(static_cast<std::size_t>(0.005 * sr), fragment / 2 + 1);
            for (std::size_t i = 0; i < fragment && start + i < out.left.size(); ++i) {
                const float ramp = std::min(1.0f, std::min(static_cast<float>(i) / static_cast<float>(fade),
                                                           static_cast<float>(fragment - i) / static_cast<float>(fade)));
                const float level = settings.residualLevel * ramp * gain;
                out.left[start + i] += capture.left[sourceStart + i] * level;
                out.right[start + i] += capture.right[sourceStart + i] * level;
            }
        }
    }

    float peak = 0.0f;
    for (std::size_t i = 0; i < out.left.size(); ++i) {
        out.left[i] *= outputTrim;
        out.right[i] *= outputTrim;
        peak = std::max(peak, std::fabs(out.left[i]));
        peak = std::max(peak, std::fabs(out.right[i]));
    }
    if (peak > 1.0f) {
        const float scale = 1.0f / peak;
        for (std::size_t i = 0; i < out.left.size(); ++i) {
            out.left[i] *= scale;
            out.right[i] *= scale;
        }
    }
    for (std::size_t i = 0; i < out.left.size(); ++i) {
        if (std::fabs(out.left[i]) < 1e-7f) out.left[i] = 0.0f;
        if (std::fabs(out.right[i]) < 1e-7f) out.right[i] = 0.0f;
    }
    return out;
}

}  // namespace downspout::energise
