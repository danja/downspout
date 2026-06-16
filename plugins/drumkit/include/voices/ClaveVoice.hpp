// ClaveVoice.hpp
// Sharp wooden clave click using filtered noise burst
// Parameters: Tone, Decay

#ifndef CLAVE_VOICE_HPP
#define CLAVE_VOICE_HPP

#include "../modules/NoiseGenerator.hpp"
#include "../modules/ADEnvelope.hpp"
#include "../modules/BiquadFilter.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class ClaveVoice {
private:
    float sampleRate;

    NoiseGenerator noise;
    ADEnvelope env;
    BiquadFilter bandpass;
    BiquadFilter metalBand;

    float toneFreq;
    float decayTime;
    float metalParam;
    float velocity;
    float level;

    static float expoMap(float value, float min, float max) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return min * std::pow(max / min, clamped);
    }

public:
    explicit ClaveVoice(float sampleRate = 48000.0f)
        : sampleRate(sampleRate)
        , noise(910231u)
        , env(sampleRate)
        , bandpass(sampleRate, BiquadFilter::Type::Bandpass)
        , metalBand(sampleRate, BiquadFilter::Type::Bandpass)
        , toneFreq(1800.0f)
        , decayTime(0.08f)
        , metalParam(0.0f)
        , velocity(1.0f)
        , level(1.0f)
    {
        env.setAttackTime(0.0008f);
        setTone(0.5f);
        setDecay(0.25f);
    }

    void setTone(float value) {
        toneFreq = expoMap(value, 900.0f, 4200.0f);
        bandpass.setParameters(toneFreq, 10.0f);
        metalBand.setParameters(std::min(toneFreq * 2.45f, sampleRate * 0.43f), 8.0f);
    }

    void setDecay(float value) {
        decayTime = expoMap(value, 0.02f, 0.4f);
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
        env.trigger();
        bandpass.reset();
        metalBand.reset();
    }

    float process() {
        if (!env.isActive()) {
            return 0.0f;
        }

        const float envValue = env.process();
        const float excitation = noise.process() * (0.8f + envValue * 0.4f);
        float sample = bandpass.process(excitation);
        if (metalParam > 0.0f) {
            const float ping = metalBand.process(excitation) * (0.42f + envValue * 0.28f);
            sample = sample * (1.0f - metalParam * 0.18f) + ping * metalParam;
        }

        return sample * envValue * velocity * 0.5f * level;
    }

    bool isActive() const { return env.isActive(); }

    void reset() {
        env.reset();
        bandpass.reset();
        metalBand.reset();
    }
};

} // namespace downspout::drumkit

#endif // CLAVE_VOICE_HPP
