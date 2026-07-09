#ifndef FLUES_GREMLIN_ENGINE_HPP
#define FLUES_GREMLIN_ENGINE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace flues::gremlin {

struct StereoFrame {
    float left;
    float right;
};

static constexpr float kPi = 3.14159265358979323846f;
static constexpr int kDelayBufferSize = 131072;

class OnePoleLowPass {
public:
    void setCutoffHz(float sampleRate, float cutoffHz) {
        const float clamped = std::clamp(cutoffHz, 20.0f, sampleRate * 0.45f);
        const float pole = std::exp(-2.0f * kPi * clamped / sampleRate);
        a0_ = 1.0f - pole;
        b1_ = pole;
    }

    float process(float x) {
        z1_ = a0_ * x + b1_ * z1_;
        return z1_;
    }

    void reset(float value = 0.0f) { z1_ = value; }

private:
    float a0_ = 1.0f;
    float b1_ = 0.0f;
    float z1_ = 0.0f;
};

class DCBlocker {
public:
    float process(float x) {
        const float y = x - x1_ + 0.9985f * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

    void reset() {
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

private:
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

class GremlinEngine {
public:
    enum Mode : int {
        SHARD = 0,
        SERVO = 1,
        SPRAY = 2,
        COLLAPSE = 3,
        RING = 4,
        VAPOR = 5
    };

    explicit GremlinEngine(float sampleRate)
        : sampleRate_(sampleRate)
    {
        updateAttack();
        updateRelease();
        updateTone();
        updateDamping();
    }

    void setMode(float value) {
        mode_ = std::clamp(static_cast<int>(std::lround(value)), 0, 5);
    }

    void setDamage(float value) { damage_ = std::clamp(value, 0.0f, 1.0f); }
    void setChaos(float value) { chaos_ = std::clamp(value, 0.0f, 1.0f); }
    void setNoise(float value) { noise_ = std::clamp(value, 0.0f, 1.0f); }
    void setDrift(float value) { drift_ = std::clamp(value, 0.0f, 1.0f); }
    void setCrunch(float value) { crunch_ = std::clamp(value, 0.0f, 1.0f); }
    void setFold(float value) { fold_ = std::clamp(value, 0.0f, 1.0f); }
    void setDelayTime(float value) { delayTime_ = std::clamp(value, 0.0f, 1.0f); }
    void setFeedback(float value) { feedback_ = std::clamp(value, 0.0f, 1.0f); }
    void setWarp(float value) { warp_ = std::clamp(value, 0.0f, 1.0f); }
    void setStutter(float value) { stutter_ = std::clamp(value, 0.0f, 1.0f); }
    void setSourceGain(float value) { sourceGain_ = std::clamp(value, 0.0f, 1.0f); }
    void setBurst(float value) { burst_ = std::clamp(value, 0.0f, 1.0f); }
    void setPitchSpread(float value) { pitchSpread_ = std::clamp(value, 0.0f, 1.0f); }
    void setDelayMix(float value) { delayMix_ = std::clamp(value, 0.0f, 1.0f); }
    void setCrossFeedback(float value) { crossFeedback_ = std::clamp(value, 0.0f, 1.0f); }
    void setGlitchLength(float value) { glitchLength_ = std::clamp(value, 0.0f, 1.0f); }
    void setChaosRate(float value) { chaosRate_ = std::clamp(value, 0.0f, 1.0f); }
    void setDuck(float value) { duck_ = std::clamp(value, 0.0f, 1.0f); }
    void setFreezeDelay(bool enabled) { freezeDelay_ = enabled; }

    void triggerBurst(float amount = 1.0f) {
        const float clamped = std::clamp(amount, 0.0f, 1.0f);
        transient_ = std::max(transient_, 0.8f + clamped * 1.8f);
        glitchExcite_ = std::max(glitchExcite_, 0.35f + clamped * 0.75f);
    }

    void reseedChaos(uint32_t salt = 0) {
        rngState_ = rngState_ * 1664525u + 1013904223u + (salt ? salt : 0x9e3779b9u);
        const float a = std::fabs(whiteNoise());
        const float b = std::fabs(whiteNoise());
        const float c = whiteNoise();
        logistic_ = 0.1f + a * 0.8f;
        tent_ = 0.1f + b * 0.8f;
        henonX_ = c * 0.8f;
        henonY_ = whiteNoise() * 0.2f;
        fastChaos_ = 0.0f;
        slowChaos_ = 0.0f;
    }

    void setTone(float value) {
        tone_ = std::clamp(value, 0.0f, 1.0f);
        updateTone();
    }

    void setDamping(float value) {
        damping_ = std::clamp(value, 0.0f, 1.0f);
        updateDamping();
    }

    void setSpace(float value) { space_ = std::clamp(value, 0.0f, 1.0f); }

    void setAttack(float value) {
        attack_ = std::clamp(value, 0.0f, 1.0f);
        updateAttack();
    }

    void setRelease(float value) {
        release_ = std::clamp(value, 0.0f, 1.0f);
        updateRelease();
    }

    void setOutput(float value) { output_ = std::clamp(value, 0.0f, 1.0f); }

    void noteOn(uint8_t midiNote, float velocity) {
        currentMidiNote_ = midiNote;
        gate_ = true;
        velocity_ = std::clamp(velocity, 0.0f, 1.0f);
        baseFrequency_ = midiToHz(midiNote);
        phaseA_ = 0.0f;
        phaseB_ = 0.125f;
        phaseC_ = 0.375f;
        phaseD_ = 0.0f;
        modeStateA_ *= 0.35f;
        modeStateB_ *= 0.35f;
        transient_ = 1.25f + velocity_ * 0.9f;
        glitchExcite_ = 1.0f;
    }

    void noteOff(uint8_t midiNote) {
        if (currentMidiNote_ == midiNote) {
            gate_ = false;
        }
    }

    void allNotesOff() {
        gate_ = false;
    }

    StereoFrame process() {
        const float target = gate_ ? 1.0f : 0.0f;
        env_ += (target - env_) * (gate_ ? attackStep_ : releaseStep_);
        transient_ *= std::max(0.90f, 0.9962f - damage_ * 0.022f - burst_ * 0.006f);
        glitchExcite_ *= 0.99945f - stutter_ * 0.00015f;

        if (env_ < 1.0e-5f && !gate_ && std::fabs(lastLeft_) < 1.0e-5f && std::fabs(lastRight_) < 1.0e-5f) {
            return {0.0f, 0.0f};
        }

        updateChaosState();
        updateSampleHold();

        const float extremityTarget = std::clamp((damage_ * 0.95f + fold_ * 0.75f + feedback_ * 0.55f
                                                + stutter_ * 0.55f + crunch_ * 0.40f - 1.15f) / 2.05f,
                                               0.0f,
                                               1.0f);
        catastrophe_ += (extremityTarget - catastrophe_) * (0.0012f + chaosRate_ * 0.0045f);

        const float freqA = std::clamp(baseFrequency_ * pitchModSemitone(0.62f + catastrophe_ * 0.55f),
                                       18.0f,
                                       sampleRate_ * 0.45f);
        const float intervalB = musicalIntervalSemitones(1);
        const float intervalC = musicalIntervalSemitones(2);
        const float detuneB = (slowChaos_ * drift_ * 1.8f + fastChaos_ * chaos_ * 1.2f) * (0.28f + catastrophe_ * 1.20f);
        const float detuneC = (tentChaos_ * drift_ * 1.4f - henonChaos_ * chaos_ * 1.0f) * (0.24f + catastrophe_ * 1.15f);
        const float freqB = std::clamp(baseFrequency_ * pitchRatio(intervalB + detuneB), 18.0f, sampleRate_ * 0.45f);
        const float freqC = std::clamp(baseFrequency_ * pitchRatio(intervalC + detuneC), 18.0f, sampleRate_ * 0.45f);
        const float freqD = std::clamp(baseFrequency_ * pitchRatio(musicalIntervalSemitones(3) - 12.0f + slowChaos_ * 2.0f),
                                       18.0f,
                                       sampleRate_ * 0.45f);

        phaseA_ = wrapPhase(phaseA_ + freqA / sampleRate_);
        phaseB_ = wrapPhase(phaseB_ + freqB / sampleRate_);
        phaseC_ = wrapPhase(phaseC_ + freqC / sampleRate_);
        phaseD_ = wrapPhase(phaseD_ + freqD / sampleRate_);

        const float sawA = 2.0f * phaseA_ - 1.0f;
        const float sawB = 2.0f * phaseB_ - 1.0f;
        const float triC = 1.0f - 4.0f * std::fabs(phaseC_ - 0.5f);
        const float triD = 1.0f - 4.0f * std::fabs(phaseD_ - 0.5f);
        const float squareA = phaseA_ < (0.5f + 0.12f * fastChaos_) ? 1.0f : -1.0f;
        const float squareB = phaseB_ < (0.48f - 0.10f * tentChaos_) ? 1.0f : -1.0f;
        const float sineA = std::sin(2.0f * kPi * (phaseA_ + 0.07f * fastChaos_ * chaos_));
        const float sineB = std::sin(2.0f * kPi * phaseB_);
        const float sineC = std::sin(2.0f * kPi * phaseC_);
        const float sineD = std::sin(2.0f * kPi * phaseD_);
        const float comparator = sawA > sawB ? 1.0f : -1.0f;
        const float chaosAudio = std::tanh((henonChaos_ * 0.9f + fastChaos_ * 0.7f + tentChaos_ * 0.35f) * (1.4f + 2.0f * chaos_));
        const float consonance = 1.0f - catastrophe_ * 0.45f;
        const float subBody = (0.68f * sineD + 0.32f * triD) * (0.12f + 0.32f * (1.0f - tone_) + 0.22f * catastrophe_);

        float source = 0.0f;
        const float burstFactor = 0.35f + burst_ * 1.65f;
        const float attackTone = transient_ * burstFactor
            * ((0.46f + 0.12f * (1.0f - damage_)) * sineA + 0.22f * sineB + 0.16f * sineC + 0.10f * triC);
        const float metallicTone = transient_ * (0.22f + 0.28f * pitchSpread_) * (squareA * sineC);
        const float gritGate = std::clamp(0.18f + transient_ * (0.62f + burst_ * 0.20f), 0.0f, 1.0f);
        switch (mode_) {
            case SHARD:
                source = 0.30f * sawA + 0.18f * sineB * consonance + 0.22f * comparator + 0.14f * (squareA * squareB)
                       + 0.40f * attackTone + 0.10f * metallicTone + 0.08f * chaosAudio;
                break;
            case SERVO:
                source = 0.70f * std::sin(2.0f * kPi * (phaseA_ + chaosAudio * (0.12f + 0.22f * chaos_)))
                       + 0.22f * (sineB * (0.62f + 0.24f * consonance) + sawB * triC * 0.38f)
                       + 0.32f * attackTone
                       + 0.05f * heldNoise_ * gritGate;
                break;
            case SPRAY:
                source = 0.18f * heldNoise_ * gritGate + 0.20f * (squareA * sawB) + 0.16f * comparator
                       + 0.24f * attackTone + 0.16f * metallicTone
                       + 0.16f * transient_ * burstFactor * (0.5f + 0.5f * chaosAbs_);
                break;
            case COLLAPSE:
                source = 0.24f * chaosAudio + 0.16f * sawA + 0.24f * sineA + 0.18f * subBody + 0.12f * comparator
                       + 0.08f * heldNoise_ * gritGate + 0.30f * attackTone + 0.10f * metallicTone;
                break;
            case RING: {
                const float ring = (sineA * sineB) * (0.72f + 0.42f * damage_)
                                 + (squareA * sineC) * (0.14f + 0.40f * fold_);
                const float brightRing = (sineC * squareB + sineB * triC) * (0.26f + 0.28f * pitchSpread_);
                const float bellSideband = std::sin(2.0f * kPi * (phaseC_ * 3.0f + phaseB_ * 0.5f))
                                          * (0.18f + pitchSpread_ * 0.28f);
                const float upperBell = std::sin(2.0f * kPi * (phaseC_ * 5.0f + phaseB_ * 2.0f))
                                      * (0.10f + pitchSpread_ * 0.20f);
                const float fm = std::sin(2.0f * kPi * (phaseA_ + sineC * (0.08f + chaos_ * 0.32f)));
                source = 0.38f * ring + 0.12f * fm + 0.18f * brightRing + 0.34f * bellSideband
                       + 0.24f * upperBell + 0.04f * comparator
                       + 0.08f * attackTone + 0.34f * metallicTone + 0.12f * chaosAudio;
                break;
            }
            case VAPOR:
            default: {
                const float cloud = std::tanh((heldNoise_ * 0.34f + chaosAudio * 0.46f + sineB * 0.20f)
                                            * (1.0f + chaos_ * 1.8f));
                const float airy = std::sin(2.0f * kPi * (phaseC_ + slowChaos_ * 0.08f))
                                 * (0.32f + 0.42f * space_);
                source = 0.26f * cloud + 0.28f * airy + 0.18f * sineA + 0.14f * sineB * consonance + 0.14f * triC
                       + 0.14f * attackTone + 0.05f * metallicTone;
                break;
            }
        }
        if (catastrophe_ > 0.001f) {
            const float shred = foldback(source * (2.4f + catastrophe_ * 8.5f) + chaosAudio * (0.45f + chaos_ * 0.70f),
                                         0.20f + (1.0f - fold_) * 0.30f);
            const float alarm = std::sin(2.0f * kPi * (phaseB_ + phaseC_ * (0.22f + fold_ * 0.55f)))
                              * (squareA * 0.55f + comparator * 0.45f);
            source += catastrophe_ * (0.30f * shred + 0.18f * alarm + 0.14f * subBody);
        }

        float modeDamage = damage_;
        float modeCrunch = crunch_;
        float modeFold = fold_;
        float noiseScale = 1.0f;
        conditionModeSource(source, noiseScale, modeDamage, modeCrunch, modeFold);

        const float noiseGate = std::clamp(0.16f + transient_ * 0.82f + env_ * 0.18f, 0.0f, 1.0f);
        source += whiteNoise() * noise_ * noiseScale
                * (0.02f + 0.10f * chaosAbs_ + 0.14f * transient_ * burstFactor) * noiseGate;
        source *= (0.24f + velocity_ * 0.9f) * env_ * (0.36f + sourceGain_ * 1.35f);

        const float crushed = processCrunch(source, modeCrunch, modeDamage);
        const float driven = crushed * (1.0f + modeDamage * 3.5f + modeFold * 4.5f);
        const float folded = foldback(driven, std::max(0.14f, 1.0f - modeFold * 0.8f));
        const float shaped = std::tanh(folded * (1.0f + modeDamage * 1.7f));
        const float toned = toneFilter_.process(shaped);

        const StereoFrame delayed = processDelay(toned);
        const float outGain = 0.03f + output_ * 0.72f;
        const float left = dcLeft_.process(delayed.left * outGain);
        const float right = dcRight_.process(delayed.right * outGain);

        lastLeft_ = left;
        lastRight_ = right;
        return {left, right};
    }

private:
    static float wrapPhase(float phase) {
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
        }
        return phase;
    }

    static float mixf(float a, float b, float t) {
        return a + (b - a) * t;
    }

    static float clampDelay(float value) {
        return std::clamp(value, 1.0f, static_cast<float>(kDelayBufferSize - 4));
    }

    static float clampUnit(float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    static float pitchRatio(float semitoneOffset) {
        return std::pow(2.0f, semitoneOffset / 12.0f);
    }

    static float midiToHz(uint8_t midiNote) {
        return 440.0f * std::pow(2.0f, (static_cast<int>(midiNote) - 69) / 12.0f);
    }

    float pitchModSemitone(float scale) const {
        const float semitone = drift_ * slowChaos_ * 7.0f + chaos_ * fastChaos_ * 11.0f * scale;
        return pitchRatio(semitone);
    }

    float musicalIntervalSemitones(int voice) const {
        const std::array<float, 4> shard {{0.0f, 7.0f, 12.0f, 19.0f}};
        const std::array<float, 4> servo {{0.0f, 5.0f, 12.0f, 17.0f}};
        const std::array<float, 4> spray {{0.0f, 10.0f, 14.0f, 22.0f}};
        const std::array<float, 4> collapse {{0.0f, -12.0f, 7.0f, 12.0f}};
        const std::array<float, 4> ring {{0.0f, 6.0f, 10.0f, 18.0f}};
        const std::array<float, 4> vapor {{0.0f, 12.0f, 19.0f, 24.0f}};

        const std::array<float, 4>* intervals = &shard;
        switch (mode_) {
            case SHARD: intervals = &shard; break;
            case SERVO: intervals = &servo; break;
            case SPRAY: intervals = &spray; break;
            case COLLAPSE: intervals = &collapse; break;
            case RING: intervals = &ring; break;
            case VAPOR:
            default: intervals = &vapor; break;
        }

        const int wrappedVoice = std::clamp(voice, 0, 3);
        const float base = (*intervals)[static_cast<std::size_t>(wrappedVoice)];
        const float spreadLift = pitchSpread_ * static_cast<float>(wrappedVoice) * 5.0f;
        const float extremeLift = catastrophe_ * static_cast<float>(wrappedVoice == 0 ? 0 : wrappedVoice + 1) * 3.5f;
        return base + spreadLift + extremeLift;
    }

    float whiteNoise() {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return static_cast<float>((rngState_ >> 8) & 0x00FFFFFFu) * (1.0f / 8388607.5f) - 1.0f;
    }

    void updateChaosState() {
        const float logisticR = 3.55f + chaos_ * 0.43f + damage_ * 0.015f;
        const float chaosBlend = 0.12f + chaosRate_ * 0.88f;
        const float nextLogistic = std::clamp(logisticR * logistic_ * (1.0f - logistic_), 0.0001f, 0.9999f);
        logistic_ = std::clamp(mixf(logistic_, nextLogistic, chaosBlend), 0.0001f, 0.9999f);

        const float tentMu = 1.60f + chaos_ * 0.38f;
        const float nextTent = tent_ < 0.5f ? tent_ * tentMu : (1.0f - tent_) * tentMu;
        tent_ = mixf(tent_, nextTent, chaosBlend);
        tent_ = tent_ - std::floor(tent_);

        const float a = 1.16f + chaos_ * 0.22f + damage_ * 0.08f;
        const float b = 0.18f + 0.10f * space_;
        const float nextHenonX = 1.0f - a * henonX_ * henonX_ + henonY_;
        const float nextHenonY = b * henonX_;
        henonX_ = std::clamp(mixf(henonX_, nextHenonX, chaosBlend), -1.5f, 1.5f);
        henonY_ = std::clamp(mixf(henonY_, nextHenonY, chaosBlend), -0.5f, 0.5f);

        fastChaos_ = std::clamp(0.55f * (logistic_ * 2.0f - 1.0f)
                              + 0.30f * (tent_ * 2.0f - 1.0f)
                              + 0.22f * (henonX_ * 0.7f), -1.0f, 1.0f);
        tentChaos_ = std::clamp(tent_ * 2.0f - 1.0f, -1.0f, 1.0f);
        henonChaos_ = std::clamp(henonX_ * 0.9f + henonY_ * 0.7f, -1.0f, 1.0f);
        chaosAbs_ = std::clamp(std::fabs(fastChaos_) * 0.65f + std::fabs(henonChaos_) * 0.35f, 0.0f, 1.0f);
        slowChaos_ += (fastChaos_ - slowChaos_) * ((0.0007f + drift_ * 0.0020f) * (0.35f + chaosRate_ * 2.1f));
    }

    void updateSampleHold() {
        if (--noiseHoldCounter_ <= 0) {
            const float interval = 1.0f + (1.0f - crunch_) * 18.0f + chaos_ * 36.0f;
            noiseHoldCounter_ = static_cast<int>(interval);
            heldNoise_ = whiteNoise();
        }
    }

    void conditionModeSource(float& source,
                             float& noiseScale,
                             float& modeDamage,
                             float& modeCrunch,
                             float& modeFold) {
        switch (mode_) {
            case SHARD: {
                modeStateA_ += (source - modeStateA_) * 0.030f;
                const float edge = source - modeStateA_;
                source = source * 0.08f + edge * (1.70f + damage_ * 0.82f)
                       + transient_ * 0.030f * (fastChaos_ > 0.0f ? 1.0f : -1.0f);
                modeDamage = clampUnit(modeDamage * 1.05f + 0.06f);
                modeCrunch = clampUnit(modeCrunch * 0.95f + 0.12f);
                modeFold = clampUnit(modeFold * 0.85f + 0.08f);
                noiseScale = 0.38f;
                break;
            }
            case SERVO:
                modeStateA_ += (source - modeStateA_) * (0.020f + (1.0f - damage_) * 0.050f);
                modeStateB_ += (modeStateA_ - modeStateB_) * 0.006f;
                source = modeStateA_ * 0.88f + (modeStateA_ - modeStateB_) * (0.42f + chaos_ * 0.28f);
                modeDamage = clampUnit(modeDamage * 0.55f);
                modeCrunch = clampUnit(modeCrunch * 0.42f);
                modeFold = clampUnit(modeFold * 0.46f);
                noiseScale = 0.28f;
                break;
            case SPRAY:
                modeStateA_ = heldNoise_;
                source = source * 0.38f + heldNoise_ * (0.34f + noise_ * 0.62f + transient_ * 0.075f)
                       + whiteNoise() * (0.055f + chaos_ * 0.12f);
                modeDamage = clampUnit(modeDamage * 0.80f + 0.08f);
                modeCrunch = clampUnit(modeCrunch * 1.55f + 0.22f);
                modeFold = clampUnit(modeFold * 0.66f);
                noiseScale = 3.10f;
                break;
            case COLLAPSE:
                modeStateA_ += (source - modeStateA_) * 0.0045f;
                source = source * 0.42f + modeStateA_ * (0.88f + feedback_ * 0.48f)
                       + std::sin(2.0f * kPi * (phaseA_ * 0.5f + slowChaos_ * 0.035f)) * (0.16f + damage_ * 0.16f)
                       + std::sin(2.0f * kPi * (phaseB_ * 0.25f - henonChaos_ * 0.025f)) * (0.08f + fold_ * 0.10f);
                modeDamage = clampUnit(modeDamage * 1.35f + 0.16f);
                modeCrunch = clampUnit(modeCrunch * 0.52f);
                modeFold = clampUnit(modeFold * 1.34f + 0.18f);
                noiseScale = 0.54f;
                break;
            case RING:
                modeStateA_ += (source - modeStateA_) * 0.050f;
                source = source * 1.45f + (source * modeStateA_) * (0.54f + fold_ * 0.58f);
                modeDamage = clampUnit(modeDamage * 1.04f + 0.14f);
                modeCrunch = clampUnit(modeCrunch * 0.74f + 0.08f);
                modeFold = clampUnit(modeFold * 1.46f + 0.16f);
                noiseScale = 0.95f;
                break;
            case VAPOR:
            default:
                modeStateA_ += (source - modeStateA_) * (0.004f + tone_ * 0.012f);
                modeStateB_ += (modeStateA_ - modeStateB_) * 0.0018f;
                source = modeStateA_ * 0.82f + modeStateB_ * 0.46f + heldNoise_ * (0.025f + space_ * 0.045f);
                modeDamage = clampUnit(modeDamage * 0.40f);
                modeCrunch = clampUnit(modeCrunch * 0.22f);
                modeFold = clampUnit(modeFold * 0.30f);
                noiseScale = 0.52f;
                break;
        }
    }

    float processCrunch(float input, float crunchAmount, float damageAmount) {
        if (--decimationCounter_ <= 0) {
            decimationCounter_ = 1 + static_cast<int>(crunchAmount * 48.0f + damageAmount * 28.0f);
            crushedSample_ = input;
        }

        const int bits = std::clamp(15 - static_cast<int>(crunchAmount * 10.0f + damageAmount * 3.0f), 3, 16);
        const float scale = static_cast<float>((1u << bits) - 1u);
        return std::round(crushedSample_ * scale) / scale;
    }

    static float foldback(float input, float threshold) {
        const float t = std::max(0.001f, threshold);
        float x = input;
        if (x > t || x < -t) {
            x = std::fabs(std::fmod(x - t, t * 4.0f));
            x = std::fabs(x - t * 2.0f) - t;
        }
        return x;
    }

    float readDelay(const std::array<float, kDelayBufferSize>& buffer, float delaySamples) const {
        const float readPos = static_cast<float>(writePos_) - delaySamples;
        const int i0 = wrapIndex(static_cast<int>(std::floor(readPos)));
        const int i1 = wrapIndex(i0 + 1);
        const float frac = readPos - std::floor(readPos);
        return buffer[static_cast<size_t>(i0)] + (buffer[static_cast<size_t>(i1)] - buffer[static_cast<size_t>(i0)]) * frac;
    }

    int wrapIndex(int index) const {
        int wrapped = index % kDelayBufferSize;
        if (wrapped < 0) {
            wrapped += kDelayBufferSize;
        }
        return wrapped;
    }

    void maybeTriggerGlitch(float baseDelaySamples) {
        if (glitchHold_ > 0) {
            --glitchHold_;
            return;
        }

        if (stutter_ <= 0.0001f || env_ <= 0.03f) {
            glitchBlend_ = 0.0f;
            return;
        }

        float modeChance = 1.0f;
        float modeLength = 1.0f;
        float modeBlend = 0.0f;
        switch (mode_) {
            case SHARD:
                modeChance = 0.62f;
                modeLength = 0.45f;
                modeBlend = -0.10f;
                break;
            case SERVO:
                modeChance = 0.34f;
                modeLength = 0.72f;
                modeBlend = -0.24f;
                break;
            case SPRAY:
                modeChance = 2.35f;
                modeLength = 0.34f;
                modeBlend = 0.12f;
                break;
            case COLLAPSE:
                modeChance = 0.82f;
                modeLength = 1.65f;
                modeBlend = 0.05f;
                break;
            case RING:
                modeChance = 0.58f;
                modeLength = 0.86f;
                modeBlend = -0.05f;
                break;
            case VAPOR:
            default:
                modeChance = 0.26f;
                modeLength = 2.10f;
                modeBlend = -0.18f;
                break;
        }

        const float noteBias = std::clamp(0.08f + transient_ * 0.55f + glitchExcite_ * 0.95f, 0.0f, 1.4f);
        const float chance = (0.000004f + stutter_ * (0.00008f + catastrophe_ * 0.00014f))
            * (0.45f + chaos_ * 0.55f + damage_ * 0.25f + glitchLength_ * 0.25f + catastrophe_ * 0.45f)
            * noteBias * modeChance;
        if ((whiteNoise() * 0.5f + 0.5f) < chance) {
            const float shortScale = 0.10f + glitchLength_ * 0.72f + catastrophe_ * 0.16f;
            const float maxShortDelay = std::max(24.0f, std::min(baseDelaySamples * shortScale * (0.55f + 0.45f * chaosAbs_), sampleRate_ * 0.18f));
            glitchDelaySamples_ = 12.0f + (whiteNoise() * 0.5f + 0.5f) * (maxShortDelay - 12.0f);
            glitchBlend_ = std::clamp(0.24f + 0.52f * stutter_ + 0.12f * glitchExcite_ + 0.22f * catastrophe_ + modeBlend, 0.0f, 0.98f);
            glitchHold_ = 24 + static_cast<int>((0.002f + 0.026f * stutter_ + 0.034f * glitchLength_ + catastrophe_ * 0.014f)
                                                * sampleRate_ * (0.7f + 0.3f * chaosAbs_) * modeLength);
            glitchExcite_ *= 0.35f;
        } else {
            glitchBlend_ = 0.0f;
        }
    }

    StereoFrame processDelay(float source) {
        float baseDelaySamples = 12.0f + std::pow(delayTime_, 1.8f) * (sampleRate_ * 0.65f);
        float wetScale = 1.0f;
        float feedbackBias = 0.0f;
        float warpScale = 1.0f;
        float spaceScale = 1.0f;

        switch (mode_) {
            case SHARD:
                baseDelaySamples = 8.0f + std::pow(delayTime_, 1.3f) * (sampleRate_ * 0.055f);
                wetScale = 0.58f;
                feedbackBias = -0.10f;
                warpScale = 0.62f;
                spaceScale = 0.55f;
                break;
            case SERVO:
                baseDelaySamples = 36.0f + std::pow(delayTime_, 2.1f) * (sampleRate_ * 0.18f);
                wetScale = 0.64f;
                feedbackBias = -0.04f;
                warpScale = 0.48f;
                spaceScale = 0.42f;
                break;
            case SPRAY:
                baseDelaySamples = 6.0f + std::pow(delayTime_, 1.15f) * (sampleRate_ * 0.085f);
                wetScale = 0.76f;
                feedbackBias = -0.08f;
                warpScale = 1.30f;
                spaceScale = 0.75f;
                break;
            case COLLAPSE:
                baseDelaySamples = 24.0f + std::pow(delayTime_, 1.7f) * (sampleRate_ * 0.55f);
                wetScale = 1.16f;
                feedbackBias = 0.07f;
                warpScale = 1.15f;
                spaceScale = 1.28f;
                break;
            case RING: {
                const float combHz = std::clamp(baseFrequency_ * (1.0f + pitchSpread_ * 4.0f), 35.0f, sampleRate_ * 0.20f);
                const float combPeriod = sampleRate_ / combHz;
                baseDelaySamples = combPeriod * (2.0f + std::round(delayTime_ * 13.0f));
                wetScale = 0.72f;
                feedbackBias = -0.02f;
                warpScale = 0.36f;
                spaceScale = 0.70f;
                break;
            }
            case VAPOR:
            default:
                baseDelaySamples = 140.0f + std::pow(delayTime_, 1.45f) * (sampleRate_ * 0.78f);
                wetScale = 1.22f;
                feedbackBias = 0.03f;
                warpScale = 0.78f;
                spaceScale = 1.45f;
                break;
        }
        baseDelaySamples = clampDelay(baseDelaySamples);
        maybeTriggerGlitch(baseDelaySamples);

        const float warpSamples = warp_ * warpScale * (18.0f + baseDelaySamples * 0.22f) * (0.75f * fastChaos_ + 0.35f * henonChaos_);
        const float spaceSamples = space_ * spaceScale * (10.0f + baseDelaySamples * 0.14f);

        float leftDelay = clampDelay(baseDelaySamples + warpSamples + spaceSamples);
        float rightDelay = clampDelay(baseDelaySamples - warpSamples - spaceSamples);

        if (glitchBlend_ > 0.0f) {
            leftDelay = clampDelay(mixf(leftDelay, glitchDelaySamples_ * (1.0f + 0.08f * fastChaos_), glitchBlend_));
            rightDelay = clampDelay(mixf(rightDelay, glitchDelaySamples_ * (1.0f - 0.08f * tentChaos_), glitchBlend_));
        }

        const float delayedLeft = readDelay(delayLeft_, leftDelay);
        const float delayedRight = readDelay(delayRight_, rightDelay);

        const float filteredLeft = feedbackFilterLeft_.process(delayedLeft);
        const float filteredRight = feedbackFilterRight_.process(delayedRight);

        const float feedbackGain = std::clamp(0.06f + std::pow(feedback_, 1.2f) * 0.91f + damage_ * 0.03f
                                                + catastrophe_ * 0.055f + feedbackBias,
                                              0.0f,
                                              0.992f);
        const float crossAmount = crossFeedback_ * space_ * (0.05f + 0.22f * chaos_ + 0.18f * catastrophe_);
        const float collapsePolarity = (mode_ == COLLAPSE && fastChaos_ < 0.0f) ? -0.82f : 1.0f;
        float inputSource = source;
        if (duck_ > 0.0001f) {
            const float feedbackEnergy = std::clamp((std::fabs(filteredLeft) + std::fabs(filteredRight)) * 0.75f, 0.0f, 1.0f);
            inputSource *= (1.0f - duck_ * feedbackEnergy);
        }

        const float writeLeft = freezeDelay_
            ? std::tanh((filteredLeft + filteredRight * crossAmount) * 0.998f)
            : std::tanh(inputSource + (filteredLeft + filteredRight * crossAmount) * feedbackGain);
        const float writeRight = freezeDelay_
            ? std::tanh((filteredRight * collapsePolarity + filteredLeft * crossAmount) * 0.998f)
            : std::tanh(inputSource + (filteredRight * collapsePolarity + filteredLeft * crossAmount) * feedbackGain);

        delayLeft_[static_cast<size_t>(writePos_)] = writeLeft;
        delayRight_[static_cast<size_t>(writePos_)] = writeRight;
        writePos_ = (writePos_ + 1) % kDelayBufferSize;

        const float wet = std::clamp((0.01f + delayMix_ * (0.40f + feedback_ * 0.28f + warp_ * 0.12f + stutter_ * 0.06f)
                                        + catastrophe_ * 0.10f) * wetScale,
                                     0.0f,
                                     1.0f);
        const float dry = 1.0f - wet * 0.88f;
        float left = source * dry + delayedLeft * wet;
        float right = source * dry + delayedRight * wet;

        const float mid = 0.5f * (left + right);
        const float side = (left - right) * 0.5f * (0.55f + space_ * 1.2f);
        left = std::tanh((mid + side) * (1.0f + damage_ * 0.5f));
        right = std::tanh((mid - side) * (1.0f + damage_ * 0.5f));
        return {left, right};
    }

    void updateAttack() {
        const float attackSeconds = 0.001f + std::pow(attack_, 2.0f) * 0.35f;
        attackStep_ = 1.0f - std::exp(-1.0f / (attackSeconds * sampleRate_));
    }

    void updateRelease() {
        const float releaseSeconds = 0.01f + std::pow(release_, 2.0f) * 2.7f;
        releaseStep_ = 1.0f - std::exp(-1.0f / (releaseSeconds * sampleRate_));
    }

    void updateTone() {
        const float cutoff = 180.0f + std::pow(tone_, 1.7f) * 16000.0f;
        toneFilter_.setCutoffHz(sampleRate_, cutoff);
    }

    void updateDamping() {
        const float cutoff = 300.0f + std::pow(1.0f - damping_, 1.6f) * 14000.0f;
        feedbackFilterLeft_.setCutoffHz(sampleRate_, cutoff);
        feedbackFilterRight_.setCutoffHz(sampleRate_, cutoff);
    }

    float sampleRate_ = 48000.0f;
    int mode_ = SHARD;

    float damage_ = 0.55f;
    float chaos_ = 0.60f;
    float noise_ = 0.30f;
    float drift_ = 0.35f;
    float crunch_ = 0.45f;
    float fold_ = 0.35f;
    float delayTime_ = 0.32f;
    float feedback_ = 0.55f;
    float warp_ = 0.45f;
    float stutter_ = 0.35f;
    float sourceGain_ = 0.58f;
    float burst_ = 0.40f;
    float pitchSpread_ = 0.50f;
    float delayMix_ = 0.55f;
    float crossFeedback_ = 0.50f;
    float glitchLength_ = 0.50f;
    float chaosRate_ = 0.55f;
    float duck_ = 0.10f;
    float tone_ = 0.65f;
    float damping_ = 0.45f;
    float space_ = 0.55f;
    float attack_ = 0.05f;
    float release_ = 0.25f;
    float output_ = 0.45f;

    bool gate_ = false;
    uint8_t currentMidiNote_ = 0;
    float velocity_ = 0.0f;
    float baseFrequency_ = 110.0f;
    float env_ = 0.0f;
    float transient_ = 0.0f;
    float attackStep_ = 0.01f;
    float releaseStep_ = 0.001f;

    float phaseA_ = 0.0f;
    float phaseB_ = 0.0f;
    float phaseC_ = 0.0f;
    float phaseD_ = 0.0f;
    float catastrophe_ = 0.0f;

    uint32_t rngState_ = 0x73419a5du;
    float logistic_ = 0.63f;
    float tent_ = 0.37f;
    float henonX_ = 0.11f;
    float henonY_ = 0.09f;
    float fastChaos_ = 0.0f;
    float slowChaos_ = 0.0f;
    float tentChaos_ = 0.0f;
    float henonChaos_ = 0.0f;
    float chaosAbs_ = 0.0f;

    int noiseHoldCounter_ = 1;
    float heldNoise_ = 0.0f;
    int decimationCounter_ = 1;
    float crushedSample_ = 0.0f;
    float modeStateA_ = 0.0f;
    float modeStateB_ = 0.0f;

    std::array<float, kDelayBufferSize> delayLeft_ {};
    std::array<float, kDelayBufferSize> delayRight_ {};
    int writePos_ = 0;
    int glitchHold_ = 0;
    float glitchDelaySamples_ = 48.0f;
    float glitchBlend_ = 0.0f;
    float glitchExcite_ = 0.0f;
    bool freezeDelay_ = false;

    OnePoleLowPass toneFilter_;
    OnePoleLowPass feedbackFilterLeft_;
    OnePoleLowPass feedbackFilterRight_;
    DCBlocker dcLeft_;
    DCBlocker dcRight_;

    float lastLeft_ = 0.0f;
    float lastRight_ = 0.0f;
};

} // namespace flues::gremlin

#endif
