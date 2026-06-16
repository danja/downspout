// ClapVoice.hpp
// Clap synthesis with multi-impulse burst and bandpass filtering
// Parameters: Density (impulse count + spacing), Tone (BP frequency)

#ifndef CLAP_VOICE_HPP
#define CLAP_VOICE_HPP

#include "../modules/NoiseGenerator.hpp"
#include "../modules/BiquadFilter.hpp"
#include "../modules/ADEnvelope.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class ClapVoice {
private:
    float sampleRate;

    NoiseGenerator noise;
    BiquadFilter bandpass;
    BiquadFilter metalBand;
    ADEnvelope env;

    float densityParam;
    float toneParam;
    float metalParam;
    float level;

    // Multi-impulse state
    int impulseCount;
    int currentImpulse;
    int samplesUntilNext;
    int impulseSpacing;
    int burstLength;
    int burstSamples;

    static float expoMap(float value, float min, float max) {
        return min * std::pow(max / min, std::clamp(value, 0.0f, 1.0f));
    }

public:
    explicit ClapVoice(float sampleRate = 48000.0f)
        : sampleRate(sampleRate)
        , noise(444555666u)
        , bandpass(sampleRate, BiquadFilter::Type::Bandpass)
        , metalBand(sampleRate, BiquadFilter::Type::Bandpass)
        , env(sampleRate)
        , densityParam(0.55f)
        , toneParam(0.45f)
        , metalParam(0.0f)
        , level(1.0f)
        , impulseCount(5)
        , currentImpulse(0)
        , samplesUntilNext(0)
        , impulseSpacing(480)  // 10ms at 48kHz
        , burstLength(240)     // 5ms burst at 48kHz
        , burstSamples(0)
    {
        bandpass.setParameters(1500.0f, 6.0f);  // Higher Q for more resonance
        metalBand.setParameters(3600.0f, 8.0f);
        env.setAttackTime(0.001f);  // Very fast attack
        env.setDecayTime(0.3f);     // Longer decay
    }

    void setDensity(float value) {
        densityParam = std::clamp(value, 0.0f, 1.0f);
        impulseCount = static_cast<int>(3.0f + densityParam * 4.0f);  // 3-7 impulses
        impulseSpacing = static_cast<int>(sampleRate * (0.025f - densityParam * 0.015f));  // 25-10ms
        burstLength = static_cast<int>(sampleRate * (0.003f + densityParam * 0.004f));  // 3-7ms bursts
    }

    void setTone(float value) {
        toneParam = std::clamp(value, 0.0f, 1.0f);
        const float freq = expoMap(toneParam, 1000.0f, 4500.0f);  // Higher frequency range
        const float q = 4.0f + toneParam * 4.0f;  // Q from 4-8 (more resonance at high tone)
        bandpass.setParameters(freq, q);
        metalBand.setParameters(std::min(freq * 2.35f, sampleRate * 0.43f), 8.0f + toneParam * 4.0f);
    }

    void setMetal(float value) {
        metalParam = std::clamp(value, 0.0f, 1.0f);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.5f);
    }

    void trigger(float vel = 1.0f) {
        currentImpulse = 0;
        samplesUntilNext = 0;
        burstSamples = 0;
        env.trigger();
        bandpass.reset();
        metalBand.reset();
    }

    float process() {
        if (!env.isActive()) {
            return 0.0f;
        }

        float sample = 0.0f;

        // Generate impulse bursts
        if (currentImpulse < impulseCount) {
            if (burstSamples > 0) {
                // Continue current burst
                sample = noise.process() * 1.2f;  // Louder bursts
                burstSamples--;
            } else if (samplesUntilNext <= 0) {
                // Start new burst
                sample = noise.process() * 1.2f;
                burstSamples = burstLength;
                currentImpulse++;
                samplesUntilNext = impulseSpacing;
            } else {
                samplesUntilNext--;
            }
        }

        const float rawBurst = sample;
        sample = bandpass.process(rawBurst);
        if (metalParam > 0.0f) {
            const float ping = metalBand.process(rawBurst) * (0.35f + densityParam * 0.25f);
            sample = sample * (1.0f - metalParam * 0.18f) + ping * metalParam;
        }
        sample *= env.process();

        return sample * 0.8f * level;  // Higher output level
    }

    bool isActive() const { return env.isActive(); }
    void reset() { env.reset(); bandpass.reset(); metalBand.reset(); }
};

} // namespace downspout::drumkit

#endif // CLAP_VOICE_HPP
