// CrashVoice.hpp
// Crash cymbal synthesis with filtered noise cascade
// Parameters: Brightness (BP center shift), Decay

#ifndef CRASH_VOICE_HPP
#define CRASH_VOICE_HPP

#include "../modules/NoiseGenerator.hpp"
#include "../modules/BiquadFilter.hpp"
#include "../modules/ADEnvelope.hpp"
#include "../modules/Distortion.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

class CrashVoice {
private:
    float sampleRate;

    NoiseGenerator noise;
    BiquadFilter bp1, bp2, bp3;  // Cascade of bandpass filters
    BiquadFilter metalBand;
    ADEnvelope env;
    Distortion softClipper;

    float brightnessParam;
    float metalParam;
    float level;

    static float expoMap(float value, float min, float max) {
        return min * std::pow(max / min, std::clamp(value, 0.0f, 1.0f));
    }

public:
    explicit CrashVoice(float sampleRate = 48000.0f)
        : sampleRate(sampleRate)
        , noise(321321321u)
        , bp1(sampleRate, BiquadFilter::Type::Bandpass)
        , bp2(sampleRate, BiquadFilter::Type::Bandpass)
        , bp3(sampleRate, BiquadFilter::Type::Bandpass)
        , metalBand(sampleRate, BiquadFilter::Type::Bandpass)
        , env(sampleRate)
        , softClipper()
        , brightnessParam(0.65f)
        , metalParam(0.0f)
        , level(1.0f)
    {
        // Initial bandpass cascade (2.5kHz, 5kHz, 8kHz)
        bp1.setParameters(2500.0f, 2.0f);
        bp2.setParameters(5000.0f, 1.5f);
        bp3.setParameters(8000.0f, 1.2f);
        metalBand.setParameters(9600.0f, 5.0f);

        env.setAttackTime(0.005f);  // 5ms attack
        env.setDecayTime(1.0f);     // 1s decay

        softClipper.setDrive(1.3f);  // Light soft clipping
    }

    void setBrightness(float value) {
        brightnessParam = std::clamp(value, 0.0f, 1.0f);

        // Shift bandpass centers (1.5kHz-10kHz range)
        const float baseShift = expoMap(brightnessParam, 1.5f, 10.0f);
        bp1.setFrequency(baseShift);
        bp2.setFrequency(baseShift * 1.8f);
        bp3.setFrequency(baseShift * 2.6f);
        metalBand.setParameters(5200.0f + brightnessParam * 6200.0f, 4.5f + brightnessParam * 3.0f);
    }

    void setDecay(float value) {
        const float decayTime = expoMap(value, 0.3f, 2.5f);
        env.setDecayTime(decayTime);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.5f);
    }

    void setMetal(float value) {
        metalParam = std::clamp(value, 0.0f, 1.0f);
    }

    void trigger(float vel = 1.0f) {
        env.trigger();
        bp1.reset();
        bp2.reset();
        bp3.reset();
        metalBand.reset();
    }

    float process() {
        if (!env.isActive()) {
            return 0.0f;
        }

        // White noise through bandpass cascade
        const float rawNoise = noise.process();
        float sample = bp1.process(rawNoise);
        sample = bp2.process(sample);
        sample = bp3.process(sample);
        if (metalParam > 0.0f) {
            const float shimmer = metalBand.process(rawNoise) * 0.42f;
            sample = sample * (1.0f - metalParam * 0.16f) + shimmer * metalParam;
        }

        // Light soft clipping for edge
        sample = softClipper.process(sample);

        // Envelope
        sample *= env.process();

        return sample * 0.5f * level;
    }

    bool isActive() const { return env.isActive(); }

    void reset() {
        env.reset();
        bp1.reset();
        bp2.reset();
        bp3.reset();
        metalBand.reset();
    }
};

} // namespace downspout::drumkit

#endif // CRASH_VOICE_HPP
