#include "canticle_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace downspout::canticle {
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

float expMap(const float value, const float minimum, const float maximum)
{
    return minimum * std::pow(maximum / minimum, clampUnit(value));
}

float sanitizeAudio(const float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    return std::tanh(std::clamp(value, -4.0f, 4.0f));
}

float sine(const float phase)
{
    return std::sin(kTwoPi * phase);
}

float triangle(const float phase)
{
    return 4.0f * std::fabs(phase - std::floor(phase + 0.5f)) - 1.0f;
}

float softSaw(const float phase)
{
    const float saw = 2.0f * (phase - std::floor(phase + 0.5f));
    return std::tanh(saw * 1.35f);
}

float wrapPhase(float phase)
{
    phase -= std::floor(phase);
    return phase;
}

struct Params {
    std::array<float, kParameterCount> values {};

    Params()
    {
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameterSpecs[i].defaultValue;
    }
};

int paramChoice(const Params& params, const ParamId id, const int maxValue)
{
    return std::clamp(static_cast<int>(std::lround(params.values[static_cast<std::size_t>(id)])), 0, maxValue);
}

class Envelope {
public:
    void setSampleRate(const float sampleRate)
    {
        sampleRate_ = std::max(1000.0f, sampleRate);
    }

    void set(const float attack, const float decay, const float sustain, const float release)
    {
        attackSeconds_ = expMap(attack, 0.001f, 1.200f);
        decaySeconds_ = expMap(decay, 0.020f, 2.400f);
        sustain_ = clampUnit(sustain);
        releaseSeconds_ = expMap(release, 0.020f, 3.200f);
    }

    void gateOn()
    {
        active_ = true;
        stage_ = Stage::attack;
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
            value_ += 1.0f / std::max(1.0f, attackSeconds_ * sampleRate_);
            if (value_ >= 1.0f)
            {
                value_ = 1.0f;
                stage_ = Stage::decay;
            }
            break;
        case Stage::decay:
            value_ += (sustain_ - value_) * (1.0f / std::max(1.0f, decaySeconds_ * sampleRate_));
            if (std::fabs(value_ - sustain_) < 0.0004f)
                value_ = sustain_;
            break;
        case Stage::release:
            value_ -= 1.0f / std::max(1.0f, releaseSeconds_ * sampleRate_);
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
    bool releasing() const { return stage_ == Stage::release; }

private:
    enum class Stage {
        idle,
        attack,
        decay,
        release,
    };

    float sampleRate_ = 44100.0f;
    float attackSeconds_ = 0.02f;
    float decaySeconds_ = 0.20f;
    float sustain_ = 0.7f;
    float releaseSeconds_ = 0.35f;
    float value_ = 0.0f;
    bool active_ = false;
    Stage stage_ = Stage::idle;
};

class OnePoleLowpass {
public:
    void reset()
    {
        z_ = 0.0f;
        coefficient_ = 0.0f;
        previousCutoff_ = -1.0f;
        previousSampleRate_ = -1.0f;
    }

    float process(const float input, const float cutoffHz, const float sampleRate)
    {
        const float safeRate = std::max(1000.0f, sampleRate);
        const float cutoff = std::clamp(cutoffHz, 30.0f, safeRate * 0.42f);
        if (cutoff != previousCutoff_ || safeRate != previousSampleRate_)
        {
            coefficient_ = 1.0f - std::exp(-kTwoPi * cutoff / safeRate);
            previousCutoff_ = cutoff;
            previousSampleRate_ = safeRate;
        }
        z_ += coefficient_ * (input - z_);
        if (!std::isfinite(z_))
            z_ = 0.0f;
        return z_;
    }

private:
    float z_ = 0.0f;
    float coefficient_ = 0.0f;
    float previousCutoff_ = -1.0f;
    float previousSampleRate_ = -1.0f;
};

struct ModelProfile {
    float sineMix = 0.30f;
    float triMix = 0.55f;
    float sawMix = 0.18f;
    float octaveMix = 0.12f;
    float cutoffBase = 900.0f;
    float cutoffRange = 5200.0f;
    float bodyScale = 0.35f;
    float attackBias = 1.0f;
    float decayBias = 1.0f;
    float sustainBias = 1.0f;
    float releaseBias = 1.0f;
    float movementScale = 0.30f;
    float driveScale = 0.8f;
};

ModelProfile profileForModel(const int model)
{
    ModelProfile profile {};
    switch (std::clamp(model, 0, 4))
    {
    case 0: // Keys.
        profile.sineMix = 0.22f;
        profile.triMix = 0.70f;
        profile.sawMix = 0.10f;
        profile.octaveMix = 0.12f;
        profile.cutoffBase = 850.0f;
        profile.cutoffRange = 5600.0f;
        profile.bodyScale = 0.34f;
        profile.attackBias = 0.45f;
        profile.decayBias = 0.75f;
        profile.sustainBias = 0.78f;
        profile.releaseBias = 0.75f;
        profile.movementScale = 0.18f;
        profile.driveScale = 0.55f;
        break;
    case 1: // Reed.
        profile.sineMix = 0.12f;
        profile.triMix = 0.45f;
        profile.sawMix = 0.42f;
        profile.octaveMix = 0.10f;
        profile.cutoffBase = 700.0f;
        profile.cutoffRange = 4200.0f;
        profile.bodyScale = 0.48f;
        profile.attackBias = 0.70f;
        profile.decayBias = 1.10f;
        profile.sustainBias = 0.92f;
        profile.releaseBias = 0.82f;
        profile.movementScale = 0.35f;
        profile.driveScale = 0.70f;
        break;
    case 2: // Pad.
        profile.sineMix = 0.45f;
        profile.triMix = 0.44f;
        profile.sawMix = 0.12f;
        profile.octaveMix = 0.22f;
        profile.cutoffBase = 500.0f;
        profile.cutoffRange = 3000.0f;
        profile.bodyScale = 0.62f;
        profile.attackBias = 1.85f;
        profile.decayBias = 1.50f;
        profile.sustainBias = 1.00f;
        profile.releaseBias = 1.75f;
        profile.movementScale = 0.58f;
        profile.driveScale = 0.38f;
        break;
    case 3: // Pluck.
        profile.sineMix = 0.18f;
        profile.triMix = 0.72f;
        profile.sawMix = 0.24f;
        profile.octaveMix = 0.08f;
        profile.cutoffBase = 1200.0f;
        profile.cutoffRange = 7000.0f;
        profile.bodyScale = 0.28f;
        profile.attackBias = 0.18f;
        profile.decayBias = 0.26f;
        profile.sustainBias = 0.20f;
        profile.releaseBias = 0.32f;
        profile.movementScale = 0.10f;
        profile.driveScale = 0.85f;
        break;
    default: // Glass.
        profile.sineMix = 0.62f;
        profile.triMix = 0.22f;
        profile.sawMix = 0.06f;
        profile.octaveMix = 0.34f;
        profile.cutoffBase = 1800.0f;
        profile.cutoffRange = 8800.0f;
        profile.bodyScale = 0.30f;
        profile.attackBias = 0.34f;
        profile.decayBias = 1.35f;
        profile.sustainBias = 0.52f;
        profile.releaseBias = 1.18f;
        profile.movementScale = 0.24f;
        profile.driveScale = 0.30f;
        break;
    }
    return profile;
}

class Voice {
public:
    void setSampleRate(const float sampleRate)
    {
        sampleRate_ = std::max(1000.0f, sampleRate);
        env_.setSampleRate(sampleRate_);
    }

    void reset()
    {
        env_.reset();
        filter_.reset();
        note_ = -1;
        phaseA_ = 0.0f;
        phaseB_ = 0.0f;
        phaseC_ = 0.0f;
        lfoPhase_ = 0.0f;
        velocity_ = 0.0f;
        age_ = 0;
        profile_ = {};
        voiceFrequency_ = 440.0f;
        detuneRatio_ = 1.0f;
        movement_ = 0.0f;
        body_ = 0.0f;
        metal_ = 0.0f;
        drive_ = 0.0f;
        cutoff_ = 1000.0f;
        panBase_ = 0.0f;
        openRangeBoost_ = 0.0f;
    }

    void start(const int midiNote, const std::uint8_t velocity, const Params& params, const std::uint64_t serial)
    {
        note_ = midiNote;
        frequency_ = midiNoteToFrequency(midiNote);
        velocity_ = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
        serial_ = serial;
        age_ = 0;
        phaseA_ = wrapPhase(static_cast<float>((midiNote * 17 + velocity) % 127) / 127.0f);
        phaseB_ = wrapPhase(phaseA_ + 0.31f);
        phaseC_ = wrapPhase(phaseA_ + 0.61f);
        lfoPhase_ = wrapPhase(static_cast<float>((midiNote * 11) % 97) / 97.0f);
        parametersChanged(params);
        env_.gateOn();
        filter_.reset();
    }

    void noteOff()
    {
        env_.gateOff();
    }

    void parametersChanged(const Params& params)
    {
        configureEnvelope(params);
        const int model = paramChoice(params, ParamId::model, 4);
        profile_ = profileForModel(model);
        const int range = paramChoice(params, ParamId::range, 3);
        const int ensemble = paramChoice(params, ParamId::ensemble, 3);
        const float rangeRatio = range == 1 ? 0.5f : (range == 2 ? 2.0f : 1.0f);
        const float ensembleDetuneScale = ensemble == 0 ? 0.65f
            : (ensemble == 1 ? 1.00f : (ensemble == 2 ? 1.55f : 2.10f));
        const float ensembleMovementScale = ensemble == 0 ? 0.70f
            : (ensemble == 1 ? 1.00f : (ensemble == 2 ? 1.25f : 1.45f));
        const float ensembleWidthBoost = ensemble == 0 ? -0.18f
            : (ensemble == 1 ? 0.0f : (ensemble == 2 ? 0.16f : 0.28f));

        voiceFrequency_ = frequency_ * rangeRatio;
        const float detuneCents =
            (params.values[static_cast<std::size_t>(ParamId::detune)] * 18.0f + 1.0f)
            * ensembleDetuneScale;
        detuneRatio_ = std::pow(2.0f, detuneCents / 1200.0f);
        movement_ = params.values[static_cast<std::size_t>(ParamId::movement)]
            * profile_.movementScale * ensembleMovementScale;
        body_ = params.values[static_cast<std::size_t>(ParamId::body)];
        metal_ = params.values[static_cast<std::size_t>(ParamId::metal)];
        const float tone = params.values[static_cast<std::size_t>(ParamId::tone)];
        drive_ = params.values[static_cast<std::size_t>(ParamId::drive)]
            * profile_.driveScale + metal_ * 0.45f;
        cutoff_ = profile_.cutoffBase + profile_.cutoffRange * tone * tone
            + std::clamp(voiceFrequency_ * (0.8f + body_), 0.0f, 3200.0f)
            + metal_ * (2400.0f + tone * 3600.0f);
        const float width = std::clamp(
            params.values[static_cast<std::size_t>(ParamId::width)] + ensembleWidthBoost,
            0.0f, 1.0f);
        panBase_ = (static_cast<float>((note_ * 37) % 101) / 100.0f - 0.5f)
            * 1.35f * width;
        openRangeBoost_ = range == 3 ? 0.22f : 0.0f;
    }

    StereoFrame process()
    {
        if (!env_.active())
            return {};

        const float env = env_.process();
        if (!env_.active())
        {
            note_ = -1;
            return {};
        }

        const float lfo = sine(lfoPhase_);
        lfoPhase_ = wrapPhase(lfoPhase_ + (0.08f + movement_ * 4.2f) / sampleRate_);

        const float vibrato = 1.0f + lfo * movement_ * 0.0045f;
        const float incA = (voiceFrequency_ * vibrato) / sampleRate_;
        const float incB = (voiceFrequency_ * detuneRatio_ * (1.0f - movement_ * 0.001f)) / sampleRate_;
        const float incC = (voiceFrequency_ * 2.0f * (1.0f + movement_ * 0.0015f)) / sampleRate_;
        phaseA_ = wrapPhase(phaseA_ + incA);
        phaseB_ = wrapPhase(phaseB_ + incB);
        phaseC_ = wrapPhase(phaseC_ + incC);

        const float raw = sine(phaseA_) * profile_.sineMix +
                          triangle(phaseB_) * profile_.triMix +
                          softSaw(phaseA_ + 0.15f * lfo) * profile_.sawMix +
                          sine(phaseC_) * (profile_.octaveMix + openRangeBoost_);
        const float metallic = sine(phaseA_ * 2.997f + phaseB_ * 0.173f) * 0.42f +
                               triangle(phaseB_ * 4.011f + phaseC_ * 0.071f) * 0.26f +
                               softSaw(phaseC_ * 3.731f + phaseA_ * 0.113f) * 0.20f;
        const float bodyTone = raw +
                               body_ * profile_.bodyScale * (sine(phaseA_ * 0.5f) + 0.35f * sine(phaseC_ * 0.5f)) +
                               metal_ * (0.16f + body_ * 0.28f) * metallic;
        const float filtered = filter_.process(bodyTone, cutoff_, sampleRate_);
        const float shaped = sanitizeAudio(filtered * (1.0f + drive_ * 3.5f) + metallic * metal_ * 0.08f);
        const float amp = env * (0.18f + velocity_ * 0.82f);
        const float mono = sanitizeAudio(shaped * amp * 0.42f);

        const float pan = std::clamp(panBase_ + lfo * movement_ * 0.15f, -0.88f, 0.88f);
        const float leftGain = std::sqrt(0.5f * (1.0f - pan));
        const float rightGain = std::sqrt(0.5f * (1.0f + pan));
        ++age_;
        return {mono * leftGain, mono * rightGain};
    }

    bool active() const { return env_.active(); }
    bool releasing() const { return env_.releasing(); }
    int note() const { return note_; }
    std::uint64_t serial() const { return serial_; }
    std::uint64_t age() const { return age_; }

private:
    void configureEnvelope(const Params& params)
    {
        const int model = static_cast<int>(std::lround(params.values[static_cast<std::size_t>(ParamId::model)]));
        const ModelProfile profile = profileForModel(model);
        float attackBias = profile.attackBias;
        float decayBias = profile.decayBias;
        float sustainBias = profile.sustainBias;
        float releaseBias = profile.releaseBias;

        switch (paramChoice(params, ParamId::articulation, 3))
        {
        case 1: // Short.
            attackBias *= 0.45f;
            decayBias *= 0.38f;
            sustainBias *= 0.45f;
            releaseBias *= 0.42f;
            break;
        case 2: // Sustain.
            attackBias *= 0.70f;
            decayBias *= 1.20f;
            sustainBias *= 1.18f;
            releaseBias *= 1.25f;
            break;
        case 3: // Bloom.
            attackBias *= 1.80f;
            decayBias *= 1.35f;
            sustainBias *= 0.95f;
            releaseBias *= 1.45f;
            break;
        default:
            break;
        }

        env_.set(std::clamp(params.values[static_cast<std::size_t>(ParamId::attack)] * attackBias, 0.0f, 1.0f),
                 std::clamp(params.values[static_cast<std::size_t>(ParamId::decay)] * decayBias, 0.0f, 1.0f),
                 std::clamp(params.values[static_cast<std::size_t>(ParamId::sustain)] * sustainBias, 0.0f, 1.0f),
                 std::clamp(params.values[static_cast<std::size_t>(ParamId::release)] * releaseBias, 0.0f, 1.0f));
    }

    float sampleRate_ = 44100.0f;
    int note_ = -1;
    float frequency_ = 440.0f;
    float phaseA_ = 0.0f;
    float phaseB_ = 0.0f;
    float phaseC_ = 0.0f;
    float lfoPhase_ = 0.0f;
    float velocity_ = 0.0f;
    ModelProfile profile_ {};
    float voiceFrequency_ = 440.0f;
    float detuneRatio_ = 1.0f;
    float movement_ = 0.0f;
    float body_ = 0.0f;
    float metal_ = 0.0f;
    float drive_ = 0.0f;
    float cutoff_ = 1000.0f;
    float panBase_ = 0.0f;
    float openRangeBoost_ = 0.0f;
    std::uint64_t serial_ = 0;
    std::uint64_t age_ = 0;
    Envelope env_;
    OnePoleLowpass filter_;
};

} // namespace

class CanticleEngine::Impl {
public:
    explicit Impl(const float sampleRate)
    {
        setSampleRate(sampleRate);
    }

    void setSampleRate(const float sampleRate)
    {
        sampleRate_ = std::max(1000.0f, sampleRate);
        for (auto& voice : voices_)
            voice.setSampleRate(sampleRate_);
        reset();
    }

    void reset()
    {
        for (auto& voice : voices_)
            voice.reset();
        serialCounter_ = 0;
        dcX_ = 0.0f;
        dcY_ = 0.0f;
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
        const auto& spec = kParameterSpecs[index];
        float clamped = std::clamp(std::isfinite(value) ? value : spec.defaultValue, spec.minimum, spec.maximum);
        if (spec.integer)
            clamped = std::round(clamped);
        if (params_.values[index] == clamped)
            return;
        params_.values[index] = clamped;
        for (auto& voice : voices_)
            if (voice.active())
                voice.parametersChanged(params_);
    }

    void noteOn(const int midiNote, const std::uint8_t velocity)
    {
        if (midiNote < 0 || midiNote > 127)
            return;
        if (velocity == 0)
        {
            noteOff(midiNote);
            return;
        }

        if (auto* existing = findVoiceByNote(midiNote))
        {
            existing->start(midiNote, velocity, params_, ++serialCounter_);
            return;
        }

        Voice* voice = findFreeVoice();
        if (voice == nullptr)
            voice = chooseVoiceToSteal();
        if (voice != nullptr)
            voice->start(midiNote, velocity, params_, ++serialCounter_);
    }

    void noteOff(const int midiNote)
    {
        for (auto& voice : voices_)
            if (voice.active() && voice.note() == midiNote)
                voice.noteOff();
    }

    void allNotesOff()
    {
        for (auto& voice : voices_)
            voice.noteOff();
    }

    void handleMidi(const std::uint8_t* data, const std::uint32_t size)
    {
        if (data == nullptr || size == 0)
            return;

        const std::uint8_t status = data[0] & 0xF0u;
        if ((status == 0x80u || status == 0x90u) && size >= 3)
        {
            const int note = data[1] & 0x7F;
            const std::uint8_t velocity = data[2] & 0x7F;
            if (status == 0x90u && velocity > 0)
                noteOn(note, velocity);
            else
                noteOff(note);
        }
        else if (status == 0xB0u && size >= 3)
        {
            const std::uint8_t cc = data[1] & 0x7F;
            if (cc == 120 || cc == 123)
                allNotesOff();
        }
    }

    StereoFrame processStereo()
    {
        float left = 0.0f;
        float right = 0.0f;
        for (auto& voice : voices_)
        {
            const StereoFrame frame = voice.process();
            left += frame.left;
            right += frame.right;
        }

        const float output = params_.values[static_cast<std::size_t>(ParamId::output)] * 1.65f;
        left *= output;
        right *= output;

        const float mono = (left + right) * 0.5f;
        const float dc = mono - dcX_ + 0.995f * dcY_;
        dcX_ = mono;
        dcY_ = dc;
        const float dcCorrection = mono - dc;
        left -= dcCorrection;
        right -= dcCorrection;

        return {sanitizeAudio(left), sanitizeAudio(right)};
    }

    std::size_t activeVoiceCount() const
    {
        std::size_t count = 0;
        for (const auto& voice : voices_)
            if (voice.active())
                ++count;
        return count;
    }

private:
    Voice* findVoiceByNote(const int midiNote)
    {
        for (auto& voice : voices_)
            if (voice.active() && voice.note() == midiNote)
                return &voice;
        return nullptr;
    }

    Voice* findFreeVoice()
    {
        for (auto& voice : voices_)
            if (!voice.active())
                return &voice;
        return nullptr;
    }

    Voice* chooseVoiceToSteal()
    {
        Voice* candidate = nullptr;
        std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
        for (auto& voice : voices_)
        {
            if (voice.releasing() && voice.serial() < oldest)
            {
                oldest = voice.serial();
                candidate = &voice;
            }
        }
        if (candidate != nullptr)
            return candidate;

        oldest = std::numeric_limits<std::uint64_t>::max();
        for (auto& voice : voices_)
        {
            if (voice.serial() < oldest)
            {
                oldest = voice.serial();
                candidate = &voice;
            }
        }
        return candidate;
    }

    Params params_;
    std::array<Voice, CanticleEngine::kMaxVoices> voices_ {};
    float sampleRate_ = 44100.0f;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
    std::uint64_t serialCounter_ = 0;
};

CanticleEngine::CanticleEngine(const float sampleRate)
    : impl_(std::make_unique<Impl>(sampleRate))
    , sampleRate_(std::max(1000.0f, sampleRate))
{
}

CanticleEngine::~CanticleEngine() = default;

void CanticleEngine::setSampleRate(const float sampleRate)
{
    sampleRate_ = std::max(1000.0f, sampleRate);
    impl_->setSampleRate(sampleRate_);
}

void CanticleEngine::reset()
{
    impl_->reset();
}

float CanticleEngine::getParameter(const std::uint32_t index) const
{
    return impl_->getParameter(index);
}

void CanticleEngine::setParameter(const std::uint32_t index, const float value)
{
    impl_->setParameter(index, value);
}

float CanticleEngine::getParameter(const ParamId id) const
{
    return getParameter(static_cast<std::uint32_t>(id));
}

void CanticleEngine::setParameter(const ParamId id, const float value)
{
    setParameter(static_cast<std::uint32_t>(id), value);
}

void CanticleEngine::noteOn(const int midiNote, const std::uint8_t velocity)
{
    impl_->noteOn(midiNote, velocity);
}

void CanticleEngine::noteOff(const int midiNote)
{
    impl_->noteOff(midiNote);
}

void CanticleEngine::allNotesOff()
{
    impl_->allNotesOff();
}

void CanticleEngine::handleMidi(const std::uint8_t* data, const std::uint32_t size)
{
    impl_->handleMidi(data, size);
}

StereoFrame CanticleEngine::processStereo()
{
    return impl_->processStereo();
}

std::size_t CanticleEngine::activeVoiceCount() const
{
    return impl_->activeVoiceCount();
}

} // namespace downspout::canticle
