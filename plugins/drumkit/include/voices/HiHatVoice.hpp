// HiHatVoice.hpp
// Hi-hat synthesis with inharmonic oscillators and noise
// Parameters: Brightness (HPF cutoff), Decay (note-dependent: closed/open)

#ifndef HIHAT_VOICE_HPP
#define HIHAT_VOICE_HPP

#include "../modules/NoiseGenerator.hpp"
#include "../modules/BiquadFilter.hpp"
#include "../modules/ADEnvelope.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class HiHatVoice {
private:
    static constexpr float TWO_PI = 6.28318530718f;

    float sampleRate;
    NoiseGenerator noise;
    BiquadFilter hpf;
    BiquadFilter sizzleBand;
    ADEnvelope env;

    // 6 square wave oscillators (inharmonic ratios)
    float phases[6];
    const float ratios[6] = {1.0f, 1.34f, 1.71f, 2.08f, 2.56f, 3.01f};
    const float baseFreq = 320.0f;  // Base frequency

    float brightnessParam;
    float metalParam;
    float decayTime;
    bool isClosed;
    float level;
    float velocity;

    static float expoMap(float value, float min, float max) {
        return min * std::pow(max / min, std::clamp(value, 0.0f, 1.0f));
    }

public:
    explicit HiHatVoice(float sampleRate = 48000.0f, bool closed = true)
        : sampleRate(sampleRate)
        , noise(123123123u)
        , hpf(sampleRate, BiquadFilter::Type::Highpass)
        , sizzleBand(sampleRate, BiquadFilter::Type::Bandpass)
        , env(sampleRate)
        , brightnessParam(0.6f)
        , metalParam(0.0f)
        , decayTime(0.1f)
        , isClosed(closed)
        , level(1.0f)
        , velocity(1.0f)
    {
        for (int i = 0; i < 6; ++i) {
            phases[i] = 0.0f;
        }

        hpf.setParameters(7000.0f, 0.85f);
        sizzleBand.setParameters(9000.0f, 1.3f);
        env.setAttackTime(0.0001f);
        env.setDecayTime(closed ? 0.1f : 0.5f);
    }

    void setClosed(bool closed) {
        isClosed = closed;
        decayTime = closed ? 0.1f : 0.5f;
        env.setDecayTime(decayTime);
    }

    void setBrightness(float value) {
        brightnessParam = std::clamp(value, 0.0f, 1.0f);
        const float cutoff = expoMap(brightnessParam, 5500.0f, 14500.0f);
        hpf.setParameters(cutoff, 0.85f + brightnessParam * 0.35f);
        sizzleBand.setParameters(std::min(cutoff * 1.12f, sampleRate * 0.42f), 1.15f + brightnessParam * 0.9f);
    }

    void setDecay(float value) {
        const float minDecay = isClosed ? 0.05f : 0.2f;
        const float maxDecay = isClosed ? 0.2f : 1.2f;
        decayTime = expoMap(value, minDecay, maxDecay);
        env.setDecayTime(decayTime);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.5f);
    }

    void setMetal(float value) {
        metalParam = std::clamp(value, 0.0f, 1.0f);
    }

    void trigger(float vel = 1.0f) {
        velocity = std::clamp(vel, 0.0f, 1.0f);
        for (int i = 0; i < 6; ++i) {
            phases[i] = 0.0f;
        }
        env.trigger();
        hpf.reset();
        sizzleBand.reset();
    }

    float process() {
        if (!env.isActive()) {
            return 0.0f;
        }

        // Build a more metallic source than a plain summed square stack.
        float oscSum = 0.0f;
        float ring = 0.0f;
        float previous = 0.0f;
        for (int i = 0; i < 6; ++i) {
            const float freq = (baseFreq * 1.35f) * ratios[i];
            phases[i] += freq / sampleRate;
            if (phases[i] >= 1.0f) phases[i] -= 1.0f;

            const float square = (phases[i] < 0.5f) ? 1.0f : -1.0f;
            oscSum += square * (0.85f / (i + 1));
            if (i > 0) {
                ring += square * previous * (0.34f / (float)i);
            }
            previous = square;
        }

        const float noiseSample = noise.process();
        float sample = oscSum * (0.19f + metalParam * 0.08f)
            + ring * (0.42f + metalParam * 0.48f)
            + noiseSample * (0.95f - metalParam * 0.24f);
        const float filtered = hpf.process(sample);
        const float sizzle = sizzleBand.process(noise.process() * 0.75f + ring * 0.28f);
        sample = filtered * 0.92f + sizzle * (0.48f + metalParam * 0.28f);
        sample = std::tanh(sample * (1.08f + 0.45f * brightnessParam + metalParam * 0.28f));
        sample *= env.process();

        return sample * (isClosed ? 0.66f : 0.74f) * level * (0.9f + 0.1f * velocity);
    }

    bool isActive() const { return env.isActive(); }

    void reset() {
        env.reset();
        hpf.reset();
        sizzleBand.reset();
        for (int i = 0; i < 6; ++i) {
            phases[i] = 0.0f;
        }
    }

    void kill() {
        reset();
    }
};

} // namespace downspout::drumkit

#endif // HIHAT_VOICE_HPP
