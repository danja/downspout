#include "floozy_engine.hpp"

#include "flues/disyn/modules/OscillatorModule.hpp"
#include "flues/pm/Random.hpp"
#include "flues/pm/modules/EnvelopeModule.hpp"
#include "flues/pm/modules/FilterModule.hpp"
#include "flues/pm/modules/ModulationModule.hpp"
#include "flues/pm/modules/ReverbModule.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace downspout::floozy {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kVoiceCount = FloozyEngine::kMaxVoices;

float midiNoteToFrequency(const int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

float clampUnit(const float value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

float sanitizeAudio(const float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    if (std::fabs(value) < 1.0e-18f)
        return 0.0f;
    return std::tanh(std::clamp(value, -4.0f, 4.0f));
}

float equalPowerLeft(const float pan)
{
    return std::cos(std::clamp(pan, 0.0f, 1.0f) * kPi * 0.5f);
}

float equalPowerRight(const float pan)
{
    return std::sin(std::clamp(pan, 0.0f, 1.0f) * kPi * 0.5f);
}

float outputGainFromParameter(const float value)
{
    return clampUnit(value) * 2.0f;
}

float expoMap(const float value, const float minimum, const float maximum)
{
    return minimum * std::pow(maximum / minimum, clampUnit(value));
}

int quantizedTuneSemitone(const float value)
{
    return static_cast<int>(std::lround((clampUnit(value) - 0.5f) * 48.0f));
}

struct FloozyParams {
    std::array<float, kParameterCount> values {};
    std::uint64_t version = 1;

    FloozyParams()
    {
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameterSpecs[i].defaultValue;
    }
};

class FloozySource {
public:
    explicit FloozySource(const float sampleRate)
        : oscillator_(sampleRate)
    {
    }

    void reset()
    {
        oscillator_.reset();
        dcBlockX1_ = 0.0f;
        dcBlockY1_ = 0.0f;
    }

    void sync(const FloozyParams& params)
    {
        algorithm_ = static_cast<flues::disyn::AlgorithmType>(
            static_cast<int>(std::round(std::clamp(params.values[0], 0.0f, 6.0f))));
        param1_ = clampUnit(params.values[1]);
        param2_ = clampUnit(params.values[2]);
        toneLevel_ = clampUnit(params.values[3]);
        noiseLevel_ = clampUnit(params.values[4]) * 0.25f;
        dcLevel_ = (clampUnit(params.values[5]) - 0.5f) * 0.20f;
    }

    float process(const float frequency)
    {
        const auto out = oscillator_.process(algorithm_, std::max(1.0f, frequency), param1_, param2_);
        const float osc = (out.primary * 0.75f + out.secondary * 0.25f) * toneLevel_;
        const float noise = rng_.uniformSignedFloat() * noiseLevel_;
        const float dcBlocked = dcBlock(osc + noise + dcLevel_);
        return sanitizeAudio(dcBlocked);
    }

private:
    float dcBlock(const float sample)
    {
        const float y = sample - dcBlockX1_ + 0.995f * dcBlockY1_;
        dcBlockX1_ = sample;
        dcBlockY1_ = y;
        return y;
    }

    flues::disyn::OscillatorModule oscillator_;
    flues::pm::Random rng_ {0x710020u};
    flues::disyn::AlgorithmType algorithm_ = flues::disyn::AlgorithmType::TANH_SQUARE;
    float param1_ = 0.55f;
    float param2_ = 0.50f;
    float toneLevel_ = 0.50f;
    float noiseLevel_ = 0.05f;
    float dcLevel_ = 0.0f;
    float dcBlockX1_ = 0.0f;
    float dcBlockY1_ = 0.0f;
};

class FloozyBody {
public:
    explicit FloozyBody(const float sampleRate)
        : sampleRate_(std::max(1000.0f, sampleRate))
        , maxDelay_(static_cast<std::size_t>(sampleRate_ / 12.0f))
        , delay1_(maxDelay_, 0.0f)
        , delay2_(maxDelay_, 0.0f)
    {
    }

    void reset()
    {
        std::fill(delay1_.begin(), delay1_.end(), 0.0f);
        std::fill(delay2_.begin(), delay2_.end(), 0.0f);
        write1_ = 0;
        write2_ = 0;
        lowpass1_ = 0.0f;
        lowpass2_ = 0.0f;
        previousOutput_ = 0.0f;
        dcBlockX_ = 0.0f;
        dcBlockY_ = 0.0f;
        transient_ = 0.0f;
        bowState_ = 0.0f;
        currentDelay1_ = 0.0f;
        currentDelay2_ = 0.0f;
        currentFeedback1_ = 0.0f;
        currentFeedback2_ = 0.0f;
        currentCross_ = 0.0f;
    }

    void noteOn(const int interfaceType, const float velocity)
    {
        interfaceType_ = std::clamp(interfaceType, 0, 11);
        transient_ = std::clamp(velocity, 0.0f, 1.0f);
        bowState_ = 0.0f;
    }

    void sync(const FloozyParams& params)
    {
        interfaceType_ = std::clamp(
            static_cast<int>(std::lround(params.values[static_cast<std::size_t>(ParamId::interfaceType)])), 0, 11);
        intensity_ = clampUnit(params.values[static_cast<std::size_t>(ParamId::interfaceIntensity)]);
        tuneSemitones_ = quantizedTuneSemitone(params.values[static_cast<std::size_t>(ParamId::tuning)]);
        ratioControl_ = clampUnit(params.values[static_cast<std::size_t>(ParamId::ratio)]);
        feedback1Control_ = clampUnit(params.values[static_cast<std::size_t>(ParamId::delay1Feedback)]);
        feedback2Control_ = clampUnit(params.values[static_cast<std::size_t>(ParamId::delay2Feedback)]);
        crossFeedbackControl_ = clampUnit(params.values[static_cast<std::size_t>(ParamId::filterFeedback)]);
    }

    struct Output {
        float mix = 0.0f;
        float tap1 = 0.0f;
        float tap2 = 0.0f;
    };

    Output process(const float source, const float env, const float baseFrequency, const float velocity)
    {
        const Profile profile = makeProfile();
        const float tuned = std::clamp(baseFrequency * std::pow(2.0f, static_cast<float>(tuneSemitones_) / 12.0f),
                                       20.0f,
                                       sampleRate_ * 0.40f);
        const float targetDelay1 = delaySamples(tuned * profile.ratio1);
        const float targetDelay2 = delaySamples(tuned * profile.ratio2);
        currentDelay1_ = smoothControl(currentDelay1_, targetDelay1, 0.0025f);
        currentDelay2_ = smoothControl(currentDelay2_, targetDelay2, 0.0025f);
        currentFeedback1_ = smoothControl(currentFeedback1_, profile.feedback1, 0.0015f);
        currentFeedback2_ = smoothControl(currentFeedback2_, profile.feedback2, 0.0015f);
        currentCross_ = smoothControl(currentCross_, profile.cross, 0.0015f);

        const float tap1 = readDelay(delay1_, write1_, currentDelay1_);
        const float tap2 = readDelay(delay2_, write2_, currentDelay2_);
        const float bodyFeedback = (tap1 + tap2) * 0.5f + previousOutput_ * currentCross_;
        const float excitation = makeExcitation(profile, source, env, bodyFeedback, velocity);
        const float releaseScale = 0.58f + clampUnit(env) * 0.42f;

        const float damp1 = damp(tap1, lowpass1_, profile.damping);
        const float damp2 = damp(tap2, lowpass2_, profile.damping * 1.08f);
        const float writeA = sanitizeAudio(excitation + damp1 * currentFeedback1_ * releaseScale +
                                           damp2 * currentCross_ * releaseScale);
        const float writeB = sanitizeAudio(excitation * profile.secondInput + damp2 * currentFeedback2_ * releaseScale +
                                           damp1 * currentCross_ * releaseScale);

        delay1_[write1_] = writeA;
        delay2_[write2_] = writeB;
        write1_ = (write1_ + 1) % delay1_.size();
        write2_ = (write2_ + 1) % delay2_.size();

        const float mix = sanitizeAudio(dcBlock(tap1 * profile.mix1 + tap2 * profile.mix2) * profile.outputGain);
        previousOutput_ = mix;
        return {mix, tap1, tap2};
    }

private:
    struct Profile {
        float ratio1 = 1.0f;
        float ratio2 = 1.5f;
        float feedback1 = 0.82f;
        float feedback2 = 0.72f;
        float cross = 0.05f;
        float damping = 0.18f;
        float mix1 = 0.7f;
        float mix2 = 0.3f;
        float secondInput = 0.7f;
        float outputGain = 1.0f;
        float transientDecay = 0.995f;
    };

    Profile makeProfile() const
    {
        Profile profile {};
        const float ratioSpread = (ratioControl_ - 0.5f) * 0.45f;

        switch (interfaceType_)
        {
        case 0: // Plucked string.
            profile.ratio2 = 2.0f + ratioSpread;
            profile.feedback1 = 0.88f;
            profile.feedback2 = 0.80f;
            profile.damping = 0.16f + (1.0f - intensity_) * 0.18f;
            profile.transientDecay = 0.9915f;
            profile.outputGain = 2.25f;
            break;
        case 1: // Struck bar/plate.
            profile.ratio2 = 1.51f + ratioSpread * 0.8f;
            profile.feedback1 = 0.76f;
            profile.feedback2 = 0.68f;
            profile.cross = 0.12f;
            profile.damping = 0.24f + (1.0f - intensity_) * 0.18f;
            profile.transientDecay = 0.990f;
            profile.outputGain = 10.50f;
            break;
        case 2: // Reed, odd-harmonic leaning.
            profile.ratio2 = 3.0f + ratioSpread * 0.20f;
            profile.feedback1 = 0.92f;
            profile.feedback2 = 0.72f;
            profile.cross = 0.05f;
            profile.damping = 0.08f + (1.0f - intensity_) * 0.14f;
            profile.mix1 = 0.92f;
            profile.mix2 = 0.08f;
            profile.outputGain = 3.60f;
            break;
        case 3: // Flute/jet.
            profile.ratio2 = 2.0f + ratioSpread * 0.15f;
            profile.feedback1 = 0.90f;
            profile.feedback2 = 0.76f;
            profile.cross = 0.03f;
            profile.damping = 0.05f + (1.0f - intensity_) * 0.18f;
            profile.mix1 = 1.0f;
            profile.mix2 = 0.0f;
            profile.outputGain = 3.40f;
            break;
        case 4: // Brass lip.
            profile.ratio2 = 2.0f + ratioSpread * 0.25f;
            profile.feedback1 = 0.94f;
            profile.feedback2 = 0.82f;
            profile.cross = 0.08f;
            profile.damping = 0.07f + (1.0f - intensity_) * 0.10f;
            profile.mix1 = 0.88f;
            profile.mix2 = 0.12f;
            profile.outputGain = 3.70f;
            break;
        case 5: // Bowed string.
            profile.ratio2 = 2.0f + ratioSpread * 0.15f;
            profile.feedback1 = 0.93f;
            profile.feedback2 = 0.86f;
            profile.cross = 0.07f;
            profile.damping = 0.10f + (1.0f - intensity_) * 0.18f;
            profile.mix1 = 0.90f;
            profile.mix2 = 0.10f;
            profile.outputGain = 2.80f;
            break;
        case 6: // Bell.
            profile.ratio2 = 2.72f + ratioSpread;
            profile.feedback1 = 0.86f;
            profile.feedback2 = 0.91f;
            profile.cross = 0.16f;
            profile.damping = 0.14f;
            profile.mix1 = 0.45f;
            profile.mix2 = 0.55f;
            profile.transientDecay = 0.997f;
            profile.outputGain = 7.50f;
            break;
        case 7: // Drum membrane.
            profile.ratio1 = 0.50f;
            profile.ratio2 = 0.74f + ratioSpread * 0.5f;
            profile.feedback1 = 0.77f;
            profile.feedback2 = 0.70f;
            profile.cross = 0.20f;
            profile.damping = 0.22f + (1.0f - intensity_) * 0.20f;
            profile.transientDecay = 0.986f;
            profile.outputGain = 10.00f;
            break;
        default: // Synthetic body variants keep more inharmonicity.
            profile.ratio2 = 1.25f + ratioControl_ * 2.75f;
            profile.feedback1 = 0.84f + intensity_ * 0.08f;
            profile.feedback2 = 0.78f + intensity_ * 0.11f;
            profile.cross = 0.10f + intensity_ * 0.12f;
            profile.damping = 0.09f + (1.0f - intensity_) * 0.22f;
            profile.outputGain = 2.50f;
            break;
        }

        const float feedback1Boost = std::pow(std::max(0.0f, (feedback1Control_ - 0.50f) / 0.50f), 1.35f);
        const float feedback2Boost = std::pow(std::max(0.0f, (feedback2Control_ - 0.10f) / 0.90f), 1.25f);
        const float resonance = std::max({feedback1Boost, feedback2Boost, crossFeedbackControl_ * 0.85f});

        profile.feedback1 *= 0.08f + std::sqrt(feedback1Control_) * 0.89f + feedback1Boost * 0.18f;
        profile.feedback2 *= 0.06f + std::sqrt(feedback2Control_) * 0.86f + feedback2Boost * 0.18f;
        profile.feedback1 = std::min(profile.feedback1, 1.14f);
        profile.feedback2 = std::min(profile.feedback2, 1.08f);
        profile.cross += crossFeedbackControl_ * 0.50f;
        profile.cross = std::min(profile.cross, 0.68f);
        profile.damping = std::max(0.015f, profile.damping * (1.0f - resonance * 0.72f));
        profile.outputGain *= 1.0f + resonance * 1.25f;
        profile.ratio1 = std::max(0.25f, profile.ratio1);
        profile.ratio2 = std::max(0.25f, profile.ratio2);
        return profile;
    }

    float makeExcitation(const Profile& profile,
                         const float source,
                         const float env,
                         const float bodyFeedback,
                         const float velocity)
    {
        const float noise = rng_.uniformSignedFloat();
        const float pressure = env * (0.25f + intensity_ * 0.80f) * (0.35f + velocity * 0.65f);
        const float gatedSource = source * env;
        const float gatedFeedback = bodyFeedback * env;
        const float transient = transient_;
        transient_ *= profile.transientDecay;

        switch (interfaceType_)
        {
        case 0:
            return sanitizeAudio(gatedSource * 0.30f + noise * transient * (0.75f + intensity_ * 0.55f));
        case 1:
            return sanitizeAudio((noise * 1.25f + gatedSource * 0.30f) * transient * (1.25f + intensity_ * 0.85f));
        case 2:
            return sanitizeAudio(std::tanh((pressure * 0.35f + gatedSource * 1.10f - gatedFeedback * 0.85f) *
                                           (2.0f + intensity_ * 5.5f)) * 0.40f);
        case 3:
            return sanitizeAudio((std::tanh((pressure * 0.03f + gatedSource * 1.45f +
                                            noise * (0.002f + intensity_ * 0.004f) - gatedFeedback * 0.45f) *
                                           (1.2f + intensity_ * 2.5f)) *
                                  0.42f) +
                                 noise * env * (0.0005f + intensity_ * 0.0015f));
        case 4:
            return sanitizeAudio(std::tanh((pressure * 0.45f + gatedSource * 1.00f - gatedFeedback * 0.55f) *
                                           (2.5f + intensity_ * 6.0f)) *
                                 (0.34f + intensity_ * 0.14f));
        case 5:
        {
            const float slip = pressure * 0.35f + gatedSource * 0.70f - bowState_ - gatedFeedback * 0.70f;
            const float friction = std::tanh(slip * (5.0f + intensity_ * 12.0f));
            bowState_ = bowState_ * (0.985f - intensity_ * 0.08f) + (pressure + friction * 0.08f) * 0.05f;
            return sanitizeAudio(friction * (0.25f + intensity_ * 0.20f) + noise * env * intensity_ * 0.004f);
        }
        case 6:
            return sanitizeAudio((gatedSource * 0.30f + noise * 1.05f) * transient * (0.95f + intensity_ * 0.75f));
        case 7:
            return sanitizeAudio((noise * 1.35f + gatedSource * 0.18f) * transient * (1.25f + intensity_ * 0.80f));
        default:
            return sanitizeAudio(gatedSource * (0.26f + intensity_ * 0.30f) +
                                 noise * transient * 0.45f -
                                 gatedFeedback * crossFeedbackControl_ * 0.18f);
        }
    }

    float delaySamples(const float frequency) const
    {
        return std::clamp(sampleRate_ / std::max(20.0f, frequency), 2.0f, static_cast<float>(maxDelay_ - 2));
    }

    float readDelay(const std::vector<float>& buffer, const std::size_t write, const float delay) const
    {
        const float raw = static_cast<float>(write) - delay;
        float wrapped = std::fmod(raw + static_cast<float>(buffer.size()), static_cast<float>(buffer.size()));
        if (wrapped < 0.0f)
            wrapped += static_cast<float>(buffer.size());
        const std::size_t index = static_cast<std::size_t>(std::floor(wrapped));
        const std::size_t next = (index + 1) % buffer.size();
        const float frac = wrapped - static_cast<float>(index);
        return buffer[index] * (1.0f - frac) + buffer[next] * frac;
    }

    static float damp(const float input, float& state, const float damping)
    {
        const float follow = std::clamp(1.0f - damping, 0.02f, 0.98f);
        state += (input - state) * follow;
        if (std::fabs(state) < 1.0e-18f)
            state = 0.0f;
        return state;
    }

    float dcBlock(const float input)
    {
        const float output = input - dcBlockX_ + 0.995f * dcBlockY_;
        dcBlockX_ = input;
        dcBlockY_ = std::fabs(output) < 1.0e-18f ? 0.0f : output;
        return dcBlockY_;
    }

    static float smoothControl(const float current, const float target, const float coefficient)
    {
        if (current <= 0.0f)
            return target;
        return current + (target - current) * coefficient;
    }

    float sampleRate_;
    std::size_t maxDelay_;
    std::vector<float> delay1_;
    std::vector<float> delay2_;
    std::size_t write1_ = 0;
    std::size_t write2_ = 0;
    float lowpass1_ = 0.0f;
    float lowpass2_ = 0.0f;
    float previousOutput_ = 0.0f;
    float dcBlockX_ = 0.0f;
    float dcBlockY_ = 0.0f;
    float transient_ = 0.0f;
    float bowState_ = 0.0f;
    float currentDelay1_ = 0.0f;
    float currentDelay2_ = 0.0f;
    float currentFeedback1_ = 0.0f;
    float currentFeedback2_ = 0.0f;
    float currentCross_ = 0.0f;
    int interfaceType_ = 2;
    int tuneSemitones_ = 0;
    float intensity_ = 0.5f;
    float ratioControl_ = 0.5f;
    float feedback1Control_ = 0.5f;
    float feedback2Control_ = 0.1f;
    float crossFeedbackControl_ = 0.0f;
    flues::pm::Random rng_ {0x7100b0u};
};

class FloozyVoice {
public:
    FloozyVoice(const float sampleRate, const std::size_t slot)
        : source_(sampleRate)
        , envelope_(sampleRate)
        , body_(sampleRate)
        , filter_(sampleRate)
        , modulation_(sampleRate)
        , pan_(kVoiceCount > 1 ? static_cast<float>(slot) / static_cast<float>(kVoiceCount - 1) : 0.5f)
    {
    }

    void noteOn(const int midiNote, const std::uint8_t velocity, const FloozyParams& params, const std::uint64_t age)
    {
        midiNote_ = midiNote;
        frequency_ = midiNoteToFrequency(midiNote);
        velocityGain_ = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
        age_ = age;
        active_ = true;
        releasing_ = false;
        resetModules();
        envelope_.setGate(true);
        body_.noteOn(static_cast<int>(std::lround(params.values[static_cast<std::size_t>(ParamId::interfaceType)])),
                     velocityGain_);
        sync(params);
    }

    void noteOff()
    {
        if (!active_)
            return;
        releasing_ = true;
        envelope_.setGate(false);
    }

    void forceStop()
    {
        active_ = false;
        releasing_ = false;
        midiNote_ = -1;
        resetModules();
        envelope_.setGate(false);
    }

    StereoFrame process(const FloozyParams& params)
    {
        if (!active_)
            return {};

        sync(params);

        const auto mod = modulation_.process();
        const float source = source_.process(frequency_ * mod.fm) * velocityGain_;
        const float env = envelope_.process();
        const bool envActive = envelope_.isPlaying();

        if (envActive)
            releaseDamp_ = 1.0f;
        else
            releaseDamp_ *= 0.9998f;

        const auto body = body_.process(source, env, frequency_, velocityGain_);
        const float filtered = filter_.process(sanitizeAudio(body.mix));
        const float mono = sanitizeAudio(filtered * mod.am * velocityGain_ *
                                         outputGainFromParameter(params.values[static_cast<std::size_t>(ParamId::masterGain)]));

        prevDelay1_ = sanitizeAudio(body.tap1);
        prevDelay2_ = sanitizeAudio(body.tap2);
        prevFilter_ = sanitizeAudio(filtered);
        lastLevel_ = std::fabs(mono);

        if (!envActive && releaseDamp_ < 1.0e-4f)
            forceStop();

        return {mono * equalPowerLeft(pan_), mono * equalPowerRight(pan_)};
    }

    bool active() const { return active_; }
    bool releasing() const { return releasing_; }
    int note() const { return midiNote_; }
    std::uint64_t age() const { return age_; }
    float level() const { return lastLevel_; }

private:
    void sync(const FloozyParams& params)
    {
        if (paramsVersion_ == params.version)
            return;
        paramsVersion_ = params.version;

        source_.sync(params);
        envelope_.setAttack(params.values[static_cast<std::size_t>(ParamId::attack)]);
        envelope_.setRelease(params.values[static_cast<std::size_t>(ParamId::release)]);
        body_.sync(params);
        filter_.setFrequency(params.values[static_cast<std::size_t>(ParamId::filterFrequency)]);
        filter_.setQ(params.values[static_cast<std::size_t>(ParamId::filterQ)]);
        filter_.setShape(params.values[static_cast<std::size_t>(ParamId::filterShape)]);
        modulation_.setFrequency(params.values[static_cast<std::size_t>(ParamId::lfoFrequency)]);
        modulation_.setTypeLevel(params.values[static_cast<std::size_t>(ParamId::modulationMix)]);
    }

    void resetModules()
    {
        source_.reset();
        envelope_.reset();
        body_.reset();
        filter_.reset();
        modulation_.reset();
        prevDelay1_ = 0.0f;
        prevDelay2_ = 0.0f;
        prevFilter_ = 0.0f;
        releaseDamp_ = 1.0f;
        lastLevel_ = 0.0f;
        paramsVersion_ = 0;
    }

    FloozySource source_;
    flues::pm::EnvelopeModule envelope_;
    FloozyBody body_;
    flues::pm::FilterModule filter_;
    flues::pm::ModulationModule modulation_;
    float pan_ = 0.5f;
    float frequency_ = 440.0f;
    float velocityGain_ = 1.0f;
    bool active_ = false;
    bool releasing_ = false;
    int midiNote_ = -1;
    std::uint64_t age_ = 0;
    std::uint64_t paramsVersion_ = 0;
    float prevDelay1_ = 0.0f;
    float prevDelay2_ = 0.0f;
    float prevFilter_ = 0.0f;
    float releaseDamp_ = 1.0f;
    float lastLevel_ = 0.0f;
};

} // namespace

class FloozyEngine::Impl {
public:
    explicit Impl(const float sampleRate)
        : sampleRate_(std::max(1000.0f, sampleRate))
        , reverbLeft_(sampleRate_)
        , reverbRight_(sampleRate_)
    {
        createVoices();
        syncReverb();
    }

    void setSampleRate(const float sampleRate)
    {
        sampleRate_ = std::max(1000.0f, sampleRate);
        reverbLeft_ = flues::pm::ReverbModule(sampleRate_);
        reverbRight_ = flues::pm::ReverbModule(sampleRate_);
        createVoices();
        syncReverb();
    }

    void reset()
    {
        for (auto& voice : voices_)
            voice->forceStop();
        reverbLeft_.reset();
        reverbRight_.reset();
        ageCounter_ = 0;
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

        if (std::fabs(params_.values[index] - clamped) <= 1.0e-7f)
            return;

        params_.values[index] = clamped;
        ++params_.version;

        if (index == static_cast<std::uint32_t>(ParamId::reverbSize) ||
            index == static_cast<std::uint32_t>(ParamId::reverbLevel))
            syncReverb();
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
            existing->noteOn(midiNote, velocity, params_, ++ageCounter_);
            return;
        }

        if (auto* idle = findIdleVoice())
        {
            idle->noteOn(midiNote, velocity, params_, ++ageCounter_);
            return;
        }

        if (auto* victim = selectVoiceToSteal())
            victim->noteOn(midiNote, velocity, params_, ++ageCounter_);
    }

    void noteOff(const int midiNote)
    {
        if (auto* voice = findVoiceByNote(midiNote))
            voice->noteOff();
    }

    void allNotesOff()
    {
        reset();
    }

    void handleMidi(const std::uint8_t* const data, const std::uint32_t size)
    {
        if (data == nullptr || size < 1)
            return;

        const std::uint8_t status = data[0] & 0xf0U;
        const std::uint8_t data1 = size > 1 ? data[1] : 0;
        const std::uint8_t data2 = size > 2 ? data[2] : 0;

        switch (status)
        {
        case 0x80:
            noteOff(data1);
            break;
        case 0x90:
            noteOn(data1, data2);
            break;
        case 0xb0:
            if (data1 == 120 || data1 == 123)
                allNotesOff();
            break;
        default:
            break;
        }
    }

    StereoFrame processStereo()
    {
        StereoFrame dry {};
        for (auto& voice : voices_)
        {
            const StereoFrame frame = voice->process(params_);
            dry.left += frame.left;
            dry.right += frame.right;
        }

        const float wetLeft = reverbLeft_.process(sanitizeAudio(dry.left + dry.right * 0.08f));
        const float wetRight = reverbRight_.process(sanitizeAudio(dry.right + dry.left * 0.05f));
        return {sanitizeAudio(wetLeft), sanitizeAudio(wetRight)};
    }

    void processBlock(float* const left,
                      float* const right,
                      const std::uint32_t frames,
                      const MidiMessage* const midi,
                      const std::uint32_t midiCount)
    {
        std::uint32_t eventIndex = 0;
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            while (eventIndex < midiCount && midi[eventIndex].frame <= frame)
            {
                handleMidi(midi[eventIndex].data.data(), midi[eventIndex].size);
                ++eventIndex;
            }

            const StereoFrame out = processStereo();
            if (left)
                left[frame] = out.left;
            if (right)
                right[frame] = out.right;
        }

        while (eventIndex < midiCount)
        {
            handleMidi(midi[eventIndex].data.data(), midi[eventIndex].size);
            ++eventIndex;
        }
    }

    std::size_t activeVoiceCount() const
    {
        std::size_t count = 0;
        for (const auto& voice : voices_)
            if (voice->active())
                ++count;
        return count;
    }

    bool hasActiveVoices() const
    {
        return activeVoiceCount() > 0;
    }

    float sampleRate() const { return sampleRate_; }

private:
    void createVoices()
    {
        for (std::size_t i = 0; i < voices_.size(); ++i)
            voices_[i] = std::make_unique<FloozyVoice>(sampleRate_, i);
    }

    void syncReverb()
    {
        const float size = params_.values[static_cast<std::size_t>(ParamId::reverbSize)];
        const float level = params_.values[static_cast<std::size_t>(ParamId::reverbLevel)];
        reverbLeft_.setSize(size);
        reverbRight_.setSize(std::clamp(size * 0.93f + 0.04f, 0.0f, 1.0f));
        reverbLeft_.setLevel(level);
        reverbRight_.setLevel(level);
    }

    FloozyVoice* findVoiceByNote(const int midiNote)
    {
        for (auto& voice : voices_)
            if (voice->active() && voice->note() == midiNote)
                return voice.get();
        return nullptr;
    }

    FloozyVoice* findIdleVoice()
    {
        for (auto& voice : voices_)
            if (!voice->active())
                return voice.get();
        return nullptr;
    }

    FloozyVoice* selectVoiceToSteal()
    {
        FloozyVoice* candidate = nullptr;
        std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
        for (auto& voice : voices_)
        {
            if (voice->releasing() && voice->age() < oldest)
            {
                oldest = voice->age();
                candidate = voice.get();
            }
        }

        if (candidate)
            return candidate;

        float lowestLevel = std::numeric_limits<float>::max();
        for (auto& voice : voices_)
        {
            if (voice->level() < lowestLevel)
            {
                lowestLevel = voice->level();
                candidate = voice.get();
            }
        }
        return candidate;
    }

    float sampleRate_;
    FloozyParams params_;
    std::array<std::unique_ptr<FloozyVoice>, kMaxVoices> voices_;
    flues::pm::ReverbModule reverbLeft_;
    flues::pm::ReverbModule reverbRight_;
    std::uint64_t ageCounter_ = 0;
};

FloozyEngine::FloozyEngine(const float sampleRate)
    : impl_(std::make_unique<Impl>(sampleRate))
    , sampleRate_(impl_->sampleRate())
{
}

FloozyEngine::~FloozyEngine() = default;

void FloozyEngine::setSampleRate(const float sampleRate)
{
    impl_->setSampleRate(sampleRate);
    sampleRate_ = impl_->sampleRate();
}

void FloozyEngine::reset()
{
    impl_->reset();
}

float FloozyEngine::getParameter(const std::uint32_t index) const
{
    return impl_->getParameter(index);
}

void FloozyEngine::setParameter(const std::uint32_t index, const float value)
{
    impl_->setParameter(index, value);
}

float FloozyEngine::getParameter(const ParamId id) const
{
    return getParameter(static_cast<std::uint32_t>(id));
}

void FloozyEngine::setParameter(const ParamId id, const float value)
{
    setParameter(static_cast<std::uint32_t>(id), value);
}

void FloozyEngine::noteOn(const int midiNote, const std::uint8_t velocity)
{
    impl_->noteOn(midiNote, velocity);
}

void FloozyEngine::noteOff(const int midiNote)
{
    impl_->noteOff(midiNote);
}

void FloozyEngine::allNotesOff()
{
    impl_->allNotesOff();
}

void FloozyEngine::handleMidi(const std::uint8_t* const data, const std::uint32_t size)
{
    impl_->handleMidi(data, size);
}

void FloozyEngine::handleMidi(const MidiMessage& message)
{
    impl_->handleMidi(message.data.data(), message.size);
}

StereoFrame FloozyEngine::processStereo()
{
    return impl_->processStereo();
}

void FloozyEngine::processBlock(float* const left,
                                float* const right,
                                const std::uint32_t frames,
                                const MidiMessage* const midi,
                                const std::uint32_t midiCount)
{
    impl_->processBlock(left, right, frames, midi, midiCount);
}

std::size_t FloozyEngine::activeVoiceCount() const
{
    return impl_->activeVoiceCount();
}

bool FloozyEngine::hasActiveVoices() const
{
    return impl_->hasActiveVoices();
}

} // namespace downspout::floozy
