// SnareVoice.hpp
// Snare drum synthesis with dual resonators and noise burst
// Parameters: Tone (body/shell mix + Q), Snap (noise level + HPF cutoff)

#ifndef SNARE_VOICE_HPP
#define SNARE_VOICE_HPP

#include "../modules/BiquadFilter.hpp"
#include "../modules/NoiseGenerator.hpp"
#include "../modules/ADEnvelope.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class SnareVoice {
private:
    float sampleRate;

    // Modules
    BiquadFilter bodyResonator;    // 180 Hz resonator
    BiquadFilter shellResonator;   // 330 Hz resonator
    BiquadFilter noiseFilter;      // HPF for snap
    BiquadFilter crackResonator;   // Upper-mid crack band
    BiquadFilter metalResonator;   // Narrow inharmonic ping
    NoiseGenerator noise;
    ADEnvelope ampEnv;             // Main amplitude envelope
    ADEnvelope noiseEnv;           // Noise burst envelope
    ADEnvelope crackEnv;           // Short crack envelope

    // Parameters
    float toneParam;       // Body/shell mix and Q
    float snapParam;       // Noise level and filter frequency
    float metalParam;      // Inharmonic resonant component
    float level;           // Output level
    float velocity;

    /**
     * Exponential parameter mapping
     */
    static float expoMap(float value, float min, float max) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return min * std::pow(max / min, clamped);
    }

public:
    explicit SnareVoice(float sampleRate = 48000.0f)
        : sampleRate(sampleRate)
        , bodyResonator(sampleRate, BiquadFilter::Type::Bandpass)
        , shellResonator(sampleRate, BiquadFilter::Type::Bandpass)
        , noiseFilter(sampleRate, BiquadFilter::Type::Highpass)
        , crackResonator(sampleRate, BiquadFilter::Type::Bandpass)
        , metalResonator(sampleRate, BiquadFilter::Type::Bandpass)
        , noise(111222333u)
        , ampEnv(sampleRate)
        , noiseEnv(sampleRate)
        , crackEnv(sampleRate)
        , toneParam(0.5f)
        , snapParam(0.6f)
        , metalParam(0.0f)
        , level(1.0f)
        , velocity(1.0f)
    {
        // Configure resonators (fixed frequencies)
        bodyResonator.setParameters(185.0f, 8.0f);
        shellResonator.setParameters(360.0f, 9.0f);
        crackResonator.setParameters(2200.0f, 1.8f);
        metalResonator.setParameters(4200.0f, 7.0f);

        // Configure noise filter
        noiseFilter.setParameters(2200.0f, 0.85f);

        // Configure envelopes
        ampEnv.setAttackTime(0.0004f);
        ampEnv.setDecayTime(0.17f);

        noiseEnv.setAttackTime(0.0001f);
        noiseEnv.setDecayTime(0.12f);

        crackEnv.setAttackTime(0.0001f);
        crackEnv.setDecayTime(0.032f);
    }

    /**
     * Set tone parameter (normalized 0-1)
     * Controls body/shell mix and resonator Q
     * 0 = more body (low), 1 = more shell (high)
     */
    void setTone(float value) {
        toneParam = std::clamp(value, 0.0f, 1.0f);

        const float bodyFreq = 165.0f + toneParam * 65.0f;
        const float shellFreq = 300.0f + toneParam * 140.0f;
        const float bodyQ = 4.5f + toneParam * 8.0f;
        const float shellQ = 6.0f + toneParam * 10.0f;
        const float crackFreq = 1500.0f + toneParam * 2200.0f;

        bodyResonator.setParameters(bodyFreq, bodyQ);
        shellResonator.setParameters(shellFreq, shellQ);
        crackResonator.setParameters(crackFreq, 1.3f + toneParam * 1.4f);
        metalResonator.setParameters(std::min(crackFreq * 1.72f, sampleRate * 0.43f), 7.0f + toneParam * 3.0f);
    }

    /**
     * Set snap parameter (normalized 0-1)
     * Controls noise level and HPF cutoff
     */
    void setSnap(float value) {
        snapParam = std::clamp(value, 0.0f, 1.0f);

        const float cutoff = expoMap(snapParam, 700.0f, 6500.0f);
        noiseFilter.setParameters(cutoff, 0.75f + snapParam * 0.45f);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.5f);
    }

    void setMetal(float value) {
        metalParam = std::clamp(value, 0.0f, 1.0f);
    }

    /**
     * Trigger the snare
     * @param vel - Velocity 0-1
     */
    void trigger(float vel = 1.0f) {
        velocity = std::clamp(vel, 0.0f, 1.0f);

        ampEnv.trigger();
        noiseEnv.trigger();
        crackEnv.trigger();

        bodyResonator.reset();
        shellResonator.reset();
        noiseFilter.reset();
        crackResonator.reset();
        metalResonator.reset();
    }

    /**
     * Process one sample
     */
    float process() {
        if (!ampEnv.isActive() && !noiseEnv.isActive() && !crackEnv.isActive()) {
            return 0.0f;
        }

        const float bodyEnv = ampEnv.process();
        const float noiseAmt = 0.32f + snapParam * 0.88f;
        const float rawNoise = noise.process();
        const float tonalExcite = rawNoise * (0.65f + 0.18f * velocity);

        const float bodyOut = bodyResonator.process(tonalExcite);
        const float shellOut = shellResonator.process(tonalExcite * 0.92f);

        const float bodyMix = 1.0f - toneParam;
        const float shellMix = toneParam;
        float tonal = (bodyOut * bodyMix + shellOut * shellMix) * bodyEnv;

        const float filteredNoise = noiseFilter.process(rawNoise);
        const float noiseOut = filteredNoise * noiseEnv.process() * noiseAmt;
        const float crackEnvValue = crackEnv.process();
        const float crack = crackResonator.process(filteredNoise * (0.9f + snapParam * 0.5f)) * crackEnvValue;

        float sample = tonal * 0.95f + noiseOut * 0.72f + crack * 0.58f;
        if (metalParam > 0.0f) {
            const float metal = metalResonator.process(filteredNoise + tonalExcite * 0.35f) * crackEnvValue;
            sample = sample * (1.0f - metalParam * 0.14f) + metal * (0.75f * metalParam);
        }
        sample = std::tanh(sample * (1.15f + snapParam * 0.55f));
        sample *= velocity;

        return sample * 0.72f * level;
    }

    /**
     * Check if voice is active
     */
    bool isActive() const {
        return ampEnv.isActive() || noiseEnv.isActive() || crackEnv.isActive();
    }

    /**
     * Force stop the voice
     */
    void reset() {
        ampEnv.reset();
        noiseEnv.reset();
        crackEnv.reset();
        bodyResonator.reset();
        shellResonator.reset();
        noiseFilter.reset();
        crackResonator.reset();
        metalResonator.reset();
    }
};

} // namespace downspout::drumkit

#endif // SNARE_VOICE_HPP
