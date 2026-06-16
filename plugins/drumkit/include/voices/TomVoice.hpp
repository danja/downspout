// TomVoice.hpp
// Tom synthesis with resonant body + attack knock and pitch envelope
// Parameters: Pitch (note-dependent), Decay

#ifndef TOM_VOICE_HPP
#define TOM_VOICE_HPP

#include "../modules/PitchEnvelope.hpp"
#include "../modules/ADEnvelope.hpp"
#include "../modules/BiquadFilter.hpp"
#include "../modules/NoiseGenerator.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class TomVoice {
private:
    static constexpr float TWO_PI = 6.28318530718f;

    float sampleRate;
    PitchEnvelope pitchEnv;
    ADEnvelope ampEnv;
    ADEnvelope attackEnv;
    BiquadFilter bodyResonator;
    BiquadFilter knockResonator;
    BiquadFilter metalResonator;
    NoiseGenerator noise;

    float phase;
    float basePitch;     // Note-dependent base frequency
    float pitchParam;
    float metalParam;
    float velocity;
    float level;

    static float expoMap(float value, float min, float max) {
        return min * std::pow(max / min, std::clamp(value, 0.0f, 1.0f));
    }

public:
    explicit TomVoice(float sampleRate = 48000.0f, float baseFreq = 100.0f)
        : sampleRate(sampleRate)
        , pitchEnv(sampleRate)
        , ampEnv(sampleRate)
        , attackEnv(sampleRate)
        , bodyResonator(sampleRate, BiquadFilter::Type::Bandpass)
        , knockResonator(sampleRate, BiquadFilter::Type::Bandpass)
        , metalResonator(sampleRate, BiquadFilter::Type::Bandpass)
        , noise(777888999u)
        , phase(0.0f)
        , basePitch(baseFreq)
        , pitchParam(0.0f)
        , metalParam(0.0f)
        , velocity(1.0f)
        , level(1.0f)
    {
        bodyResonator.setParameters(baseFreq, 10.0f);
        knockResonator.setParameters(2200.0f, 2.4f);
        metalResonator.setParameters(std::clamp(baseFreq * 7.4f, 700.0f, 7200.0f), 6.0f);
        ampEnv.setAttackTime(0.0003f);
        ampEnv.setDecayTime(0.24f);
        attackEnv.setAttackTime(0.0001f);
        attackEnv.setDecayTime(0.022f);
    }

    void setBasePitch(float freq) {
        basePitch = freq;
    }

    void setPitch(float value) {
        pitchParam = std::clamp(value, 0.0f, 1.0f);
        const float startFreq = basePitch * (1.55f + value * 0.65f);
        const float endFreq = basePitch * (0.86f + value * 0.12f);
        pitchEnv.setParameters(startFreq, endFreq, 0.09f);
        metalResonator.setParameters(std::clamp(basePitch * (6.4f + pitchParam * 5.8f), 700.0f, 7600.0f), 6.0f + pitchParam * 2.5f);
    }

    void setDecay(float value) {
        const float decayTime = expoMap(value, 0.08f, 0.8f);
        ampEnv.setDecayTime(decayTime);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.5f);
    }

    void setMetal(float value) {
        metalParam = std::clamp(value, 0.0f, 1.0f);
    }

    void trigger(float vel = 1.0f) {
        velocity = std::clamp(vel, 0.0f, 1.0f);
        phase = 0.0f;
        pitchEnv.trigger();
        ampEnv.trigger();
        attackEnv.trigger();
        bodyResonator.reset();
        knockResonator.reset();
        metalResonator.reset();
    }

    float process() {
        if (!ampEnv.isActive()) {
            return 0.0f;
        }

        const float freq = pitchEnv.process();
        const float amp = ampEnv.process();
        const float attack = attackEnv.process();

        // Mixed sine/triangle body keeps the pitch center but avoids a hollow pure ring.
        phase += freq / sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;
        const float sine = std::sin(TWO_PI * phase);
        const float triangle = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
        const float bodyExcite = sine * 0.78f + triangle * 0.22f + noise.process() * 0.04f;

        bodyResonator.setParameters(freq, 8.0f + velocity * 5.0f);
        const float body = bodyResonator.process(bodyExcite);

        const float knockFreq = std::clamp(freq * 8.5f, 1400.0f, 4200.0f);
        knockResonator.setParameters(knockFreq, 1.7f + velocity * 1.4f);
        const float attackNoise = noise.process();
        const float knockExcite = attackNoise * (0.55f + 0.35f * velocity) + triangle * 0.25f;
        const float knock = knockResonator.process(knockExcite) * attack;

        float sample = body * (0.95f + 0.12f * velocity) + knock * 0.44f;
        if (metalParam > 0.0f) {
            const float clang = metalResonator.process(attackNoise * 0.8f + triangle * 0.45f) * attack;
            sample = sample * (1.0f - metalParam * 0.12f) + clang * (0.58f * metalParam);
        }
        sample = std::tanh(sample * 1.45f);
        sample *= amp * velocity;
        return sample * 0.52f * level;
    }

    bool isActive() const { return ampEnv.isActive(); }
    void reset() {
        ampEnv.reset();
        attackEnv.reset();
        pitchEnv.reset();
        bodyResonator.reset();
        knockResonator.reset();
        metalResonator.reset();
        phase = 0.0f;
    }
};

} // namespace downspout::drumkit

#endif // TOM_VOICE_HPP
