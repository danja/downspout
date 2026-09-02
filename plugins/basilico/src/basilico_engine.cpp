#include "basilico_engine.hpp"

#include "basilico_flanger.hpp"
#include "basilico_modulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace downspout::basilico {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

float clampUnit(const float value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

float midiNoteToFrequency(const int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

float sanitizeAudio(const float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    return std::tanh(std::clamp(value, -4.0f, 4.0f));
}

float expMap(const float value, const float minimum, const float maximum)
{
    return minimum * std::pow(maximum / minimum, clampUnit(value));
}

float outputGain(const float value)
{
    return clampUnit(value) * 2.0f;
}

struct Params {
    std::array<float, kParameterCount> values {};

    Params()
    {
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameterSpecs[i].defaultValue;
    }
};

class Envelope {
public:
    void setSampleRate(const float sampleRate)
    {
        sampleRate_ = std::max(1000.0f, sampleRate);
    }

    void set(float attack, float decay, float sustain, float release)
    {
        attackSeconds_ = expMap(attack, 0.0005f, 0.120f);
        decaySeconds_ = expMap(decay, 0.010f, 1.200f);
        sustain_ = clampUnit(sustain);
        releaseSeconds_ = expMap(release, 0.006f, 1.500f);
    }

    void gateOn()
    {
        stage_ = Stage::attack;
        active_ = true;
    }

    void gateOff()
    {
        if (active_)
            stage_ = Stage::release;
    }

    void reset()
    {
        value_ = 0.0f;
        active_ = false;
        stage_ = Stage::idle;
    }

    float process()
    {
        switch (stage_)
        {
        case Stage::idle:
            value_ = 0.0f;
            break;
        case Stage::attack:
            value_ += 1.0f / (attackSeconds_ * sampleRate_);
            if (value_ >= 1.0f)
            {
                value_ = 1.0f;
                stage_ = Stage::decay;
            }
            break;
        case Stage::decay:
            value_ += (sustain_ - value_) * (1.0f / (decaySeconds_ * sampleRate_));
            if (std::fabs(value_ - sustain_) < 0.0005f)
                value_ = sustain_;
            break;
        case Stage::release:
            value_ -= 1.0f / (releaseSeconds_ * sampleRate_);
            if (value_ <= 0.0f)
            {
                value_ = 0.0f;
                active_ = false;
                stage_ = Stage::idle;
            }
            break;
        }

        return clampUnit(value_);
    }

    bool active() const { return active_; }

private:
    enum class Stage {
        idle,
        attack,
        decay,
        release,
    };

    float sampleRate_ = 44100.0f;
    float attackSeconds_ = 0.001f;
    float decaySeconds_ = 0.2f;
    float sustain_ = 0.6f;
    float releaseSeconds_ = 0.08f;
    float value_ = 0.0f;
    bool active_ = false;
    Stage stage_ = Stage::idle;
};

class StateVariableFilter {
public:
    void reset()
    {
        low_ = 0.0f;
        band_ = 0.0f;
    }

    float process(const float input, const float cutoffHz, const float resonance, const float sampleRate)
    {
        const float cutoff = std::clamp(cutoffHz, 20.0f, sampleRate * 0.42f);
        const float f = 2.0f * std::sin(kPi * cutoff / sampleRate);
        const float q = 0.55f + clampUnit(resonance) * 10.0f;
        const float high = input - low_ - band_ / q;
        band_ += f * high;
        low_ += f * band_;

        if (!std::isfinite(low_))
            low_ = 0.0f;
        if (!std::isfinite(band_))
            band_ = 0.0f;

        return low_;
    }

private:
    float low_ = 0.0f;
    float band_ = 0.0f;
};

class BiquadBandpass {
public:
    void reset()
    {
        x1_ = 0.0f;
        x2_ = 0.0f;
        y1_ = 0.0f;
        y2_ = 0.0f;
    }

    float process(const float input, const float frequency, const float q, const float sampleRate)
    {
        const float safeSampleRate = std::max(1000.0f, sampleRate);
        const float safeFrequency = std::clamp(frequency, 20.0f, safeSampleRate * 0.42f);
        const float safeQ = std::clamp(q, 0.35f, 12.0f);
        const float omega = kTwoPi * safeFrequency / safeSampleRate;
        const float alpha = std::sin(omega) / (2.0f * safeQ);
        const float a0 = 1.0f + alpha;
        const float b0 = alpha / a0;
        const float b2 = -alpha / a0;
        const float a1 = (-2.0f * std::cos(omega)) / a0;
        const float a2 = (1.0f - alpha) / a0;

        const float output = b0 * input + b2 * x2_ - a1 * y1_ - a2 * y2_;
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = sanitizeAudio(output);

        return y1_;
    }

private:
    float x1_ = 0.0f;
    float x2_ = 0.0f;
    float y1_ = 0.0f;
    float y2_ = 0.0f;
};

struct ModelProfile {
    float oscMix = 0.75f;
    float subMix = 0.5f;
    float bodyAmount = 0.5f;
    float biteAmount = 0.3f;
    float cutoffBias = 0.45f;
    float resonanceBias = 0.2f;
    float envScale = 0.5f;
    float decayScale = 1.0f;
    float sustainScale = 1.0f;
    float driveScale = 1.0f;
    int forcedWave = -1;
    int forcedDrive = -1;
};

ModelProfile profileForModel(const int model)
{
    ModelProfile profile {};
    switch (std::clamp(model, 0, 6))
    {
    case 0: // Upright.
        profile.oscMix = 0.45f;
        profile.subMix = 0.35f;
        profile.bodyAmount = 0.85f;
        profile.biteAmount = 0.42f;
        profile.cutoffBias = 0.30f;
        profile.resonanceBias = 0.12f;
        profile.envScale = 0.25f;
        profile.decayScale = 0.75f;
        profile.sustainScale = 0.65f;
        profile.driveScale = 0.35f;
        profile.forcedWave = 1;
        profile.forcedDrive = 0;
        break;
    case 1: // Electric.
        profile.oscMix = 0.62f;
        profile.subMix = 0.55f;
        profile.bodyAmount = 0.55f;
        profile.biteAmount = 0.38f;
        profile.cutoffBias = 0.42f;
        profile.resonanceBias = 0.18f;
        profile.envScale = 0.35f;
        profile.decayScale = 0.95f;
        profile.sustainScale = 0.85f;
        profile.driveScale = 0.60f;
        break;
    case 2: // Dub.
        profile.oscMix = 0.40f;
        profile.subMix = 0.85f;
        profile.bodyAmount = 0.75f;
        profile.biteAmount = 0.10f;
        profile.cutoffBias = 0.22f;
        profile.resonanceBias = 0.10f;
        profile.envScale = 0.18f;
        profile.decayScale = 0.45f;
        profile.sustainScale = 0.55f;
        profile.driveScale = 0.45f;
        profile.forcedWave = 0;
        profile.forcedDrive = 1;
        break;
    case 3: // Acid.
        profile.oscMix = 0.92f;
        profile.subMix = 0.28f;
        profile.bodyAmount = 0.22f;
        profile.biteAmount = 0.62f;
        profile.cutoffBias = 0.52f;
        profile.resonanceBias = 0.58f;
        profile.envScale = 0.90f;
        profile.decayScale = 0.60f;
        profile.sustainScale = 0.55f;
        profile.driveScale = 1.25f;
        profile.forcedDrive = 2;
        break;
    case 4: // Industrial.
        profile.oscMix = 0.95f;
        profile.subMix = 0.42f;
        profile.bodyAmount = 0.28f;
        profile.biteAmount = 0.90f;
        profile.cutoffBias = 0.66f;
        profile.resonanceBias = 0.32f;
        profile.envScale = 0.45f;
        profile.decayScale = 0.55f;
        profile.sustainScale = 0.70f;
        profile.driveScale = 1.75f;
        profile.forcedDrive = 3;
        break;
    case 5: // Reese — dark detuned saw, dominant sub, minimal filter movement.
        profile.oscMix = 0.78f;
        profile.subMix = 0.92f;
        profile.bodyAmount = 0.35f;
        profile.biteAmount = 0.06f;
        profile.cutoffBias = 0.16f;
        profile.resonanceBias = 0.12f;
        profile.envScale = 0.08f;
        profile.decayScale = 0.85f;
        profile.sustainScale = 0.92f;
        profile.driveScale = 0.72f;
        profile.forcedWave = 2;
        break;
    default: // Hoover — nasal, sweeping, high resonance filter dive.
        profile.oscMix = 0.95f;
        profile.subMix = 0.12f;
        profile.bodyAmount = 0.10f;
        profile.biteAmount = 0.82f;
        profile.cutoffBias = 0.68f;
        profile.resonanceBias = 0.75f;
        profile.envScale = 1.0f;
        profile.decayScale = 0.28f;
        profile.sustainScale = 0.28f;
        profile.driveScale = 1.55f;
        profile.forcedWave = 2;
        break;
    }
    return profile;
}

} // namespace

class BasilicoEngine::Impl {
public:
    explicit Impl(const float sampleRate)
    {
        setSampleRate(sampleRate);
    }

    void setSampleRate(const float sampleRate)
    {
        sampleRate_ = std::max(1000.0f, sampleRate);
        ampEnv_.setSampleRate(sampleRate_);
        filterEnv_.setSampleRate(sampleRate_);
        flanger_.setSampleRate(sampleRate_);
        reset();
    }

    void reset()
    {
        ampEnv_.reset();
        filterEnv_.reset();
        filter_.reset();
        bodyLow_.reset();
        bodyHigh_.reset();
        wobble_.reset();
        flanger_.reset();
        heldNotes_.clear();
        currentNote_ = -1;
        targetFrequency_ = 55.0f;
        currentFrequency_ = 55.0f;
        phase_ = 0.0f;
        subPhase_ = 0.0f;
        detunePhase_ = 0.0f;
        punch_ = 0.0f;
        active_ = false;
        velocity_ = 0.0f;
        dcX_ = 0.0f;
        dcY_ = 0.0f;
    }

    void setTransport(const TransportSnapshot& transport)
    {
        wobble_.setTransport(transport);
    }

    float getParameter(const std::uint32_t index) const
    {
        if (index >= kParameterCount)
            return 0.0f;
        return params_.values[index];
    }

    void setParameter(const std::uint32_t index, const float value)
    {
        if (index >= kParameterCount)
            return;

        const bool wasBypassed = bypassed();
        const auto& spec = kParameterSpecs[index];
        float clamped = std::clamp(std::isfinite(value) ? value : spec.defaultValue, spec.minimum, spec.maximum);
        if (spec.integer)
            clamped = std::round(clamped);
        params_.values[index] = clamped;
        if (!wasBypassed && bypassed())
            reset();
    }

    void noteOn(const int midiNote, const std::uint8_t velocity)
    {
        if (bypassed())
            return;
        if (midiNote < 0 || midiNote > 127)
            return;
        if (velocity == 0)
        {
            noteOff(midiNote);
            return;
        }

        const bool hadHeldNotes = !heldNotes_.empty();
        removeHeld(midiNote);
        heldNotes_.push_back(midiNote);

        const bool canGlide = glideEnabled() && currentNote_ >= 0;
        const bool legatoGlide = canGlide && hadHeldNotes;
        currentNote_ = midiNote;
        targetFrequency_ = midiNoteToFrequency(midiNote);
        if (!canGlide)
        {
            currentFrequency_ = targetFrequency_;
            phase_ = 0.0f;
            subPhase_ = 0.0f;
            detunePhase_ = 0.0f;
            filter_.reset();
            bodyLow_.reset();
            bodyHigh_.reset();
        }

        if (!legatoGlide)
        {
            ampEnv_.gateOn();
            filterEnv_.gateOn();
            punch_ = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
        }
        active_ = true;
        velocity_ = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
    }

    void noteOff(const int midiNote)
    {
        removeHeld(midiNote);
        if (!heldNotes_.empty())
        {
            currentNote_ = heldNotes_.back();
            targetFrequency_ = midiNoteToFrequency(currentNote_);
            return;
        }

        ampEnv_.gateOff();
        filterEnv_.gateOff();
    }

    void allNotesOff()
    {
        heldNotes_.clear();
        ampEnv_.gateOff();
        filterEnv_.gateOff();
    }

    void handleMidi(const std::uint8_t* const data, const std::uint32_t size)
    {
        if (data == nullptr || size < 1)
            return;

        const std::uint8_t status = data[0] & 0xf0U;
        const std::uint8_t note = size > 1 ? data[1] : 0;
        const std::uint8_t value = size > 2 ? data[2] : 0;

        if (bypassed())
        {
            if (status == 0xb0 && (note == 120 || note == 123))
                allNotesOff();
            return;
        }

        switch (status)
        {
        case 0x80:
            noteOff(note);
            break;
        case 0x90:
            noteOn(note, value);
            break;
        case 0xb0:
            if (note == 120 || note == 123)
                allNotesOff();
            break;
        default:
            break;
        }
    }

    StereoFrame processStereo()
    {
        if (bypassed())
            return {};

        syncEnvelopes();

        const float amp = ampEnv_.process();
        const float filterEnv = filterEnv_.process();
        if (!ampEnv_.active() && heldNotes_.empty())
            active_ = false;

        updateGlide();

        const int model = static_cast<int>(params_.values[static_cast<std::size_t>(ParamId::model)]);
        const auto profile = profileForModel(model);
        const int wave = profile.forcedWave >= 0
                             ? profile.forcedWave
                             : static_cast<int>(params_.values[static_cast<std::size_t>(ParamId::waveform)]);
        const int driveType = profile.forcedDrive >= 0
                                  ? profile.forcedDrive
                                  : static_cast<int>(params_.values[static_cast<std::size_t>(ParamId::driveType)]);

        const float main = oscillator(wave, phase_);
        const float sub = oscillator(model == 2 ? 0 : 3, subPhase_);
        const float body = bodyTone(main, sub, profile);
        const float transient = transientTone(profile);

        const WobbleFrame wobble = wobble_.process(wobbleConfig(), sampleRate_);

        const float detuneOffsetCents = params_.values[static_cast<std::size_t>(ParamId::detuneOffset)];
        const float detuneLevel = params_.values[static_cast<std::size_t>(ParamId::detuneLevel)];
        const float detuneVoice = detuneLevel > 0.001f
                                      ? oscillator(wave, detunePhase_) * detuneLevel * profile.oscMix
                                      : 0.0f;

        const float harmonicMult = params_.values[static_cast<std::size_t>(ParamId::harmonic)];
        const float harmonicLevel = params_.values[static_cast<std::size_t>(ParamId::harmonicLevel)];
        const float harmonicSig = std::sin(phase_ * kTwoPi * harmonicMult) * harmonicLevel;

        advancePhase(phase_, currentFrequency_);
        advancePhase(subPhase_, currentFrequency_ * 0.5f);
        const float detuneRatio = detuneOffsetCents > 0.001f
                                      ? std::pow(2.0f, detuneOffsetCents / 1200.0f)
                                      : 1.0f;
        advancePhase(detunePhase_, currentFrequency_ * detuneRatio);

        const float subLevel = params_.values[static_cast<std::size_t>(ParamId::subLevel)];
        const float wobbleSubMix = params_.values[static_cast<std::size_t>(ParamId::wobbleSubMix)];
        const float envSubMix = params_.values[static_cast<std::size_t>(ParamId::envSubMix)];
        const float subWobbleMod = wobbleSubMix > 0.0001f ? (1.0f + wobble.bipolar * wobbleSubMix) : 1.0f;
        const float effectiveSubLevel = clampUnit(subLevel * profile.subMix * subWobbleMod * (1.0f + envSubMix * amp));

        const float bodyAmount = params_.values[static_cast<std::size_t>(ParamId::body)] * profile.bodyAmount;
        const float dry = main * profile.oscMix + sub * effectiveSubLevel + harmonicSig + detuneVoice;
        const float source = dry * (1.0f - bodyAmount * 0.45f) +
                             body * bodyAmount * 1.65f +
                             transient;

        const float cutoff = cutoffHz(profile, filterEnv, wobble);
        const float filtered = filter_.process(sanitizeAudio(source), cutoff, resonance(profile), sampleRate_);
        const float driven = applyDrive(filtered, driveType, profile);
        const float ampMod = amplitudeWobble(wobble);
        const float out = sanitizeAudio(dcBlock(sanitizeAudio(driven * amp * ampMod * (0.35f + velocity_ * 0.65f) *
                                                        outputGain(params_.values[static_cast<std::size_t>(ParamId::output)]))));
        const float phaseDepth = params_.values[static_cast<std::size_t>(ParamId::phaseWobble)];
        const auto stereo = flanger_.process(out, wobble.bipolar, phaseDepth);

        punch_ *= 0.995f - params_.values[static_cast<std::size_t>(ParamId::mute)] * 0.018f;

        return {stereo.left, stereo.right};
    }

    bool active() const { return active_; }
    int currentNote() const { return currentNote_; }
    float currentFrequency() const { return currentFrequency_; }
    float sampleRate() const { return sampleRate_; }

private:
    bool bypassed() const
    {
        return params_.values[static_cast<std::size_t>(ParamId::bypass)] >= 0.5f;
    }

    bool glideEnabled() const
    {
        return params_.values[static_cast<std::size_t>(ParamId::glide)] >= 0.015f;
    }

    void syncEnvelopes()
    {
        const int model = static_cast<int>(params_.values[static_cast<std::size_t>(ParamId::model)]);
        const auto profile = profileForModel(model);
        const float mute = params_.values[static_cast<std::size_t>(ParamId::mute)];
        const float squelch = params_.values[static_cast<std::size_t>(ParamId::squelch)];
        const float decay = std::clamp(params_.values[static_cast<std::size_t>(ParamId::decay)] * profile.decayScale * (1.15f - mute * 0.65f),
                                       0.0f,
                                       1.0f);
        const float sustain = std::clamp(params_.values[static_cast<std::size_t>(ParamId::sustain)] * profile.sustainScale * (1.0f - mute * 0.55f),
                                         0.0f,
                                         1.0f);
        ampEnv_.set(params_.values[static_cast<std::size_t>(ParamId::attack)],
                    decay,
                    sustain,
                    params_.values[static_cast<std::size_t>(ParamId::release)]);
        const float filterDecay = std::clamp(decay * (0.85f - squelch * 0.32f), 0.015f, 1.0f);
        const float filterSustain = std::clamp(sustain * (0.45f - squelch * 0.22f), 0.0f, 1.0f);
        filterEnv_.set(0.0f, filterDecay, filterSustain, params_.values[static_cast<std::size_t>(ParamId::release)]);
    }

    void updateGlide()
    {
        const float glide = params_.values[static_cast<std::size_t>(ParamId::glide)];
        const float shaped = glide * glide;
        const float glideSeconds = 0.002f + shaped * 1.250f;
        const float coefficient = glide <= 0.001f ? 1.0f : 1.0f - std::exp(-1.0f / (glideSeconds * sampleRate_));
        currentFrequency_ += (targetFrequency_ - currentFrequency_) * coefficient;
    }

    void advancePhase(float& phase, const float frequency)
    {
        phase += frequency / sampleRate_;
        if (phase >= 1.0f)
            phase -= std::floor(phase);
    }

    float oscillator(const int wave, const float phase) const
    {
        switch (std::clamp(wave, 0, 4))
        {
        case 0:
            return std::sin(phase * kTwoPi);
        case 1:
            return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
        case 2:
            return 2.0f * phase - 1.0f;
        case 3:
            return phase < 0.5f ? 1.0f : -1.0f;
        default:
            return phase < (0.20f + params_.values[static_cast<std::size_t>(ParamId::bite)] * 0.55f) ? 1.0f : -1.0f;
        }
    }

    float bodyTone(const float main, const float sub, const ModelProfile& profile)
    {
        const float body = params_.values[static_cast<std::size_t>(ParamId::body)];
        const float second = std::sin(phase_ * kTwoPi * 2.0f);
        const float third = std::sin(phase_ * kTwoPi * 3.01f);
        const float stringTone = sanitizeAudio(main * 0.55f + sub * 0.28f + second * 0.16f + third * 0.09f);

        const float lowFrequency = std::clamp(72.0f + currentFrequency_ * (0.28f + body * 0.50f),
                                              58.0f,
                                              260.0f);
        const float highFrequency = std::clamp(185.0f + currentFrequency_ * (1.30f + body * 1.85f),
                                               145.0f,
                                               1200.0f);
        const float q = 1.1f + body * 7.5f + profile.bodyAmount * 1.8f;
        const float low = bodyLow_.process(stringTone, lowFrequency, q, sampleRate_);
        const float high = bodyHigh_.process(stringTone, highFrequency, q * 0.72f, sampleRate_);
        const float hollow = low * (1.4f + body * 1.8f) - high * (0.25f + body * 0.95f);
        const float direct = stringTone * (0.22f + (1.0f - body) * 0.42f);

        return sanitizeAudio(direct + hollow);
    }

    float transientTone(const ModelProfile& profile) const
    {
        const float punchScale = params_.values[static_cast<std::size_t>(ParamId::punch)];
        const float bite = params_.values[static_cast<std::size_t>(ParamId::bite)] * profile.biteAmount;
        const float click = (phase_ < 0.5f ? 1.0f : -1.0f) * punch_ * punchScale * bite * 0.22f;
        const float finger = std::sin(phase_ * kTwoPi * 7.0f) * punch_ * punchScale * bite * 0.08f;
        return sanitizeAudio(click + finger);
    }

    WobbleConfig wobbleConfig() const
    {
        return {
            params_.values[static_cast<std::size_t>(ParamId::wobbleSync)] >= 0.5f,
            static_cast<int>(params_.values[static_cast<std::size_t>(ParamId::wobbleDivision)]),
            static_cast<int>(params_.values[static_cast<std::size_t>(ParamId::wobbleShape)]),
            params_.values[static_cast<std::size_t>(ParamId::lfoFrequency)],
            params_.values[static_cast<std::size_t>(ParamId::wobbleStart)],
        };
    }

    float amplitudeWobble(const WobbleFrame& wobble) const
    {
        const float depth = params_.values[static_cast<std::size_t>(ParamId::ampWobble)];
        if (depth <= 0.0001f)
            return 1.0f;

        const float gate = 1.0f - wobble.unipolar;
        const float floor = 0.04f + (1.0f - depth) * 0.86f;
        return std::clamp((1.0f - depth) + depth * (floor + (1.0f - floor) * gate), 0.0f, 1.0f);
    }

    float cutoffHz(const ModelProfile& profile, const float env, const WobbleFrame& wobble) const
    {
        const float cutoffParam = params_.values[static_cast<std::size_t>(ParamId::cutoff)];
        const float squelch = params_.values[static_cast<std::size_t>(ParamId::squelch)];
        const float envAmount = params_.values[static_cast<std::size_t>(ParamId::filterEnv)] * (profile.envScale + squelch * 0.36f);
        const float keyTrack = params_.values[static_cast<std::size_t>(ParamId::keyTrack)];
        const float accent = params_.values[static_cast<std::size_t>(ParamId::accent)] * std::clamp(punch_, 0.0f, 1.0f);
        const float lfoDepth = params_.values[static_cast<std::size_t>(ParamId::lfoDepth)];
        const float normalized = std::clamp(profile.cutoffBias + (cutoffParam - 0.5f) * 0.85f +
                                                env * envAmount * (0.65f + squelch * 0.28f) +
                                                accent * (0.22f + squelch * 0.28f) +
                                                squelch * (env * 0.24f - 0.06f) +
                                                wobble.bipolar * lfoDepth * 0.22f,
                                            0.0f,
                                            1.0f);
        float base = expMap(normalized, 55.0f, 9000.0f);
        base *= std::pow(2.0f, lfoDepth * (wobble.unipolar * 3.50f - 0.35f));
        const float tracked = currentFrequency_ * (0.5f + keyTrack * 4.0f);
        return std::clamp(std::max(base, tracked * keyTrack), 20.0f, sampleRate_ * 0.42f);
    }

    float resonance(const ModelProfile& profile) const
    {
        const float squelch = params_.values[static_cast<std::size_t>(ParamId::squelch)];
        return std::clamp(profile.resonanceBias +
                              params_.values[static_cast<std::size_t>(ParamId::resonance)] * 0.80f +
                              squelch * 0.34f,
                          0.0f,
                          0.98f);
    }

    float applyDrive(float input, const int driveType, const ModelProfile& profile) const
    {
        const float squelch = params_.values[static_cast<std::size_t>(ParamId::squelch)];
        const float drive = params_.values[static_cast<std::size_t>(ParamId::drive)] * profile.driveScale + squelch * 0.18f;
        input = sanitizeAudio(input + std::sin(phase_ * kTwoPi) * squelch * drive * 0.12f);
        switch (std::clamp(driveType, 0, 3))
        {
        case 0: // Clean — gentle soft saturation, unchanged.
            return std::tanh(input * (1.0f + drive * 1.5f));
        case 1: // Amp — hard saturation with x|x| shaping for odd harmonics.
        {
            const float gained = input * (1.0f + drive * 18.0f);
            const float shaped = gained + gained * std::fabs(gained) * drive * 0.35f;
            return std::tanh(shaped) * 0.82f;
        }
        case 2: // Acid — resonant sine injection with 3rd harmonic feedback.
        {
            const float mod = std::sin(phase_ * kTwoPi) * drive * 0.50f;
            const float third = std::sin(phase_ * kTwoPi * 3.0f) * drive * 0.28f;
            const float gained = (input + mod + third) * (1.0f + drive * 14.0f);
            return std::tanh(gained);
        }
        default: // Fold — aggressive wave folding, dominates at high drive.
        {
            const float foldGain = 2.5f + drive * 22.0f;
            const float folded = std::sin(input * foldGain);
            const float foldMix = 0.40f + drive * 0.55f;
            return sanitizeAudio((folded * foldMix + input * (1.0f - foldMix)) * (1.8f + drive * 0.8f));
        }
        }
    }

    float dcBlock(const float input)
    {
        const float output = input - dcX_ + 0.995f * dcY_;
        dcX_ = input;
        dcY_ = output;
        return output;
    }

    void removeHeld(const int midiNote)
    {
        heldNotes_.erase(std::remove(heldNotes_.begin(), heldNotes_.end(), midiNote), heldNotes_.end());
    }

    float sampleRate_ = 44100.0f;
    Params params_;
    Envelope ampEnv_;
    Envelope filterEnv_;
    StateVariableFilter filter_;
    BiquadBandpass bodyLow_;
    BiquadBandpass bodyHigh_;
    WobbleModulator wobble_;
    BasilicoFlanger flanger_;
    std::vector<int> heldNotes_;
    int currentNote_ = -1;
    float targetFrequency_ = 55.0f;
    float currentFrequency_ = 55.0f;
    float phase_ = 0.0f;
    float subPhase_ = 0.0f;
    float detunePhase_ = 0.0f;
    float punch_ = 0.0f;
    float velocity_ = 0.0f;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
    bool active_ = false;
};

BasilicoEngine::BasilicoEngine(const float sampleRate)
    : impl_(std::make_unique<Impl>(sampleRate))
    , sampleRate_(impl_->sampleRate())
{
}

BasilicoEngine::~BasilicoEngine() = default;

void BasilicoEngine::setSampleRate(const float sampleRate)
{
    impl_->setSampleRate(sampleRate);
    sampleRate_ = impl_->sampleRate();
}

void BasilicoEngine::reset()
{
    impl_->reset();
}

void BasilicoEngine::setTransport(const TransportSnapshot& transport)
{
    impl_->setTransport(transport);
}

float BasilicoEngine::getParameter(const std::uint32_t index) const
{
    return impl_->getParameter(index);
}

void BasilicoEngine::setParameter(const std::uint32_t index, const float value)
{
    impl_->setParameter(index, value);
}

float BasilicoEngine::getParameter(const ParamId id) const
{
    return getParameter(static_cast<std::uint32_t>(id));
}

void BasilicoEngine::setParameter(const ParamId id, const float value)
{
    setParameter(static_cast<std::uint32_t>(id), value);
}

void BasilicoEngine::noteOn(const int midiNote, const std::uint8_t velocity)
{
    impl_->noteOn(midiNote, velocity);
}

void BasilicoEngine::noteOff(const int midiNote)
{
    impl_->noteOff(midiNote);
}

void BasilicoEngine::allNotesOff()
{
    impl_->allNotesOff();
}

void BasilicoEngine::handleMidi(const std::uint8_t* const data, const std::uint32_t size)
{
    impl_->handleMidi(data, size);
}

StereoFrame BasilicoEngine::processStereo()
{
    return impl_->processStereo();
}

bool BasilicoEngine::active() const
{
    return impl_->active();
}

int BasilicoEngine::currentNote() const
{
    return impl_->currentNote();
}

float BasilicoEngine::currentFrequency() const
{
    return impl_->currentFrequency();
}

} // namespace downspout::basilico
