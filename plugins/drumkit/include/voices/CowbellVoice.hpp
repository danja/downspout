// CowbellVoice.hpp
// Metallic cowbell voice using dual oscillators and resonant bandpass
// Parameters: Tone, Decay

#ifndef COWBELL_VOICE_HPP
#define COWBELL_VOICE_HPP

#include "../modules/ADEnvelope.hpp"
#include "../modules/BiquadFilter.hpp"
#include "../modules/Distortion.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class CowbellVoice {
private:
    static constexpr float TWO_PI = 6.28318530718f;

    float sampleRate;

    ADEnvelope env;
    BiquadFilter bandpass;
    Distortion saturator;

    float phaseA;
    float phaseB;

    float baseFreq;
    float decayTime;
    float metalParam;
    float velocity;
    float level;

    static float expoMap(float value, float min, float max) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return min * std::pow(max / min, clamped);
    }

public:
    explicit CowbellVoice(float sampleRate = 48000.0f)
        : sampleRate(sampleRate)
        , env(sampleRate)
        , bandpass(sampleRate, BiquadFilter::Type::Bandpass)
        , saturator()
        , phaseA(0.0f)
        , phaseB(0.0f)
        , baseFreq(540.0f)
        , decayTime(0.25f)
        , metalParam(0.0f)
        , velocity(1.0f)
        , level(1.0f)
    {
        env.setAttackTime(0.0015f);
        setTone(0.45f);
        setDecay(0.35f);
        saturator.setDrive(2.0f);
    }

    void setTone(float value) {
        baseFreq = expoMap(value, 380.0f, 1400.0f);
        bandpass.setParameters(baseFreq * 1.15f, 6.0f);
    }

    void setDecay(float value) {
        decayTime = expoMap(value, 0.05f, 1.0f);
        env.setDecayTime(decayTime);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.5f);
    }

    void setMetal(float value) {
        metalParam = std::clamp(value, 0.0f, 1.0f);
        saturator.setDrive(2.0f + metalParam * metalParam * 4.0f);
    }

    void trigger(float vel = 1.0f) {
        velocity = std::clamp(vel, 0.0f, 1.0f);
        phaseA = 0.0f;
        phaseB = 0.0f;
        env.trigger();
        bandpass.reset();
    }

    float process() {
        if (!env.isActive()) {
            return 0.0f;
        }

        const float envValue = env.process();
        const float freqA = baseFreq;
        const float freqB = baseFreq * 1.45f;

        phaseA += TWO_PI * freqA / sampleRate;
        phaseB += TWO_PI * freqB / sampleRate;
        if (phaseA >= TWO_PI) phaseA -= TWO_PI;
        if (phaseB >= TWO_PI) phaseB -= TWO_PI;

        const float oscA = std::sin(phaseA);
        const float oscB = std::sin(phaseB);
        const float squareA = oscA >= 0.0f ? 1.0f : -1.0f;
        const float squareB = oscB >= 0.0f ? 1.0f : -1.0f;

        const float clang = squareA * squareB + (oscA * squareB - oscB * squareA) * 0.32f;
        float sample = 0.55f * squareA + 0.45f * squareB + 0.2f * (oscA + oscB);
        if (metalParam > 0.0f) {
            const float m = metalParam * metalParam;
            sample = sample * (1.0f - m * 0.52f) + clang * (1.7f * m);
        }
        sample = bandpass.process(sample * (0.8f + metalParam * 0.35f));
        sample = saturator.process(sample);

        return sample * envValue * velocity * 0.6f * level;
    }

    bool isActive() const { return env.isActive(); }

    void reset() {
        env.reset();
        bandpass.reset();
        phaseA = 0.0f;
        phaseB = 0.0f;
    }
};

} // namespace downspout::drumkit

#endif // COWBELL_VOICE_HPP
