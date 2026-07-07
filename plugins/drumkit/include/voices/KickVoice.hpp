// KickVoice.hpp
// Kick drum synthesis with pitch envelope, sine oscillator, transient noise, distortion, and punch
// Parameters: Pitch (30-250Hz start), Decay (50-1500ms), Drive (1-10×), Punch (click amount), Transient (pink burst)

#ifndef KICK_VOICE_HPP
#define KICK_VOICE_HPP

#include "../drumkit_params.hpp"
#include "../modules/PitchEnvelope.hpp"
#include "../modules/ADEnvelope.hpp"
#include "../modules/Distortion.hpp"
#include "../modules/DCBlocker.hpp"
#include "../modules/NoiseGenerator.hpp"
#include "../modules/BiquadFilter.hpp"
#include <cmath>
#include <algorithm>

namespace downspout::drumkit {

class KickVoice {
private:
    static constexpr float TWO_PI = 6.28318530718f;

    float sampleRate;

    // Modules
    PitchEnvelope pitchEnv;
    ADEnvelope ampEnv;
    Distortion distortion;
    DCBlocker dcBlocker;
    NoiseGenerator noise;
    BiquadFilter clickFilter;  // HPF for punch

    // Oscillator state
    float phase;

    // Parameters
    float pitchStart;    // Starting pitch (Hz)
    float pitchEnd;      // Ending pitch (Hz)
    float decayTime;     // Envelope decay time (seconds)
    float driveAmount;   // Distortion drive
    float punchAmount;   // Click enhancement amount
    float transientAmount; // Pink-noise transient amount
    float transientEnv;   // Exponential transient envelope
    float transientDecay; // Per-sample transient envelope decay
    float pink0;          // Pink-noise filter state
    float pink1;
    float pink2;
    float level;         // Output level

    // Velocity
    float velocity;

    /**
     * Exponential parameter mapping
     */
    static float expoMap(float value, float min, float max) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return min * std::pow(max / min, clamped);
    }

    float processPinkNoise() {
        const float white = noise.process();
        pink0 = 0.99765f * pink0 + white * 0.0990460f;
        pink1 = 0.96300f * pink1 + white * 0.2965164f;
        pink2 = 0.57000f * pink2 + white * 1.0526913f;
        return (pink0 + pink1 + pink2 + white * 0.1848f) * 0.16f;
    }

public:
    explicit KickVoice(float sampleRate = 48000.0f)
        : sampleRate(sampleRate)
        , pitchEnv(sampleRate)
        , ampEnv(sampleRate)
        , distortion()
        , dcBlocker(0.999f)
        , noise(987654321u)
        , clickFilter(sampleRate, BiquadFilter::Type::Highpass)
        , phase(0.0f)
        , pitchStart(100.0f)
        , pitchEnd(40.0f)
        , decayTime(0.3f)
        , driveAmount(1.0f)
        , punchAmount(0.15f)
        , transientAmount(0.0f)
        , transientEnv(0.0f)
        , transientDecay(std::exp(-1.0f / (sampleRate * 0.012f)))
        , pink0(0.0f)
        , pink1(0.0f)
        , pink2(0.0f)
        , level(1.0f)
        , velocity(1.0f)
    {
        // Configure click filter (8kHz HPF for punch)
        clickFilter.setParameters(8000.0f, 0.707f);

        // Configure default envelopes
        ampEnv.setAttackTime(0.001f);  // 1ms attack
        ampEnv.setDecayTime(0.3f);     // 300ms decay

        pitchEnv.setParameters(100.0f, 40.0f, 0.05f);  // Quick pitch decay
    }

    /**
     * Set pitch parameter (normalized 0-1)
     * Maps to starting pitch 30-250 Hz, anchored to keep the default near 99 Hz
     */
    void setPitch(float value) {
        pitchStart = normalizedKickPitchToHz(value);
        pitchEnv.setStartFreq(pitchStart);
        pitchEnv.setEndFreq(pitchEnd);
    }

    /**
     * Set decay parameter (normalized 0-1)
     * Maps to decay time 50-1500ms
     */
    void setDecay(float value) {
        decayTime = expoMap(value, 0.05f, 1.5f);
        ampEnv.setDecayTime(decayTime);

        // Pitch envelope decays faster (20% of amp decay)
        pitchEnv.setDecayTime(decayTime * 0.2f);
    }

    /**
     * Set drive parameter (normalized 0-1)
     * Maps to distortion drive 1.0-10.0
     */
    void setDrive(float value) {
        driveAmount = 1.0f + value * 9.0f;  // Linear 1-10
        distortion.setDrive(driveAmount);
    }

    /**
     * Set punch parameter (normalized 0-1)
     * Controls amount of high-frequency click
     */
    void setPunch(float value) {
        punchAmount = std::clamp(value, 0.0f, 1.0f);
    }

    /**
     * Set pink-noise transient amount (normalized 0-1)
     */
    void setTransient(float value) {
        transientAmount = std::clamp(value, 0.0f, 1.0f);
    }

    void setLevel(float value) {
        level = std::clamp(value, 0.0f, 1.5f);
    }

    /**
     * Trigger the kick drum
     * @param vel - Velocity 0-1
     */
    void trigger(float vel = 1.0f) {
        velocity = std::clamp(vel, 0.0f, 1.0f);
        phase = 0.0f;

        pitchEnv.trigger();
        ampEnv.trigger();
        transientEnv = 1.0f;
        pink0 = pink1 = pink2 = 0.0f;
        dcBlocker.reset();
    }

    /**
     * Process one sample
     */
    float process() {
        if (!ampEnv.isActive()) {
            return 0.0f;
        }

        // Get current pitch from envelope
        const float currentFreq = pitchEnv.process();

        // Generate sine wave with pitch modulation
        const float phaseIncrement = TWO_PI * currentFreq / sampleRate;
        phase += phaseIncrement;
        if (phase >= TWO_PI) {
            phase -= TWO_PI;
        }

        float sample = std::sin(phase);

        if (transientEnv > 0.0001f) {
            if (transientAmount > 0.001f) {
                const float transient = processPinkNoise() * transientEnv * transientAmount * velocity;
                sample += transient * 0.65f;
            }
            transientEnv *= transientDecay;
        }

        // Apply distortion
        sample = distortion.process(sample);

        // Add punch (high-frequency click burst)
        if (punchAmount > 0.01f && ampEnv.getValue() > 0.9f) {
            // Only add click during early attack
            const float clickSample = clickFilter.process(noise.process());
            sample += clickSample * punchAmount * 0.3f;
        }

        // Apply amplitude envelope
        const float env = ampEnv.process();
        sample *= env * velocity;

        // Remove DC offset
        sample = dcBlocker.process(sample);

        return sample * 0.8f * level;  // Output scaling
    }

    /**
     * Check if voice is active
     */
    bool isActive() const {
        return ampEnv.isActive();
    }

    /**
     * Force stop the voice
     */
    void reset() {
        ampEnv.reset();
        pitchEnv.reset();
        dcBlocker.reset();
        phase = 0.0f;
        transientEnv = 0.0f;
        pink0 = pink1 = pink2 = 0.0f;
    }
};

} // namespace downspout::drumkit

#endif // KICK_VOICE_HPP
