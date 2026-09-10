#include "moka_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace downspout::moka {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kOutputGain = 1.6f;
constexpr float kMinT60 = 0.03f;
constexpr float kSilenceEps = 0.00012f;

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

float releaseT60ForParam(const float value)
{
    // 30 ms choke up to a 1.5 s tail.
    return 0.03f * std::pow(50.0f, clampUnit(value));
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

// Deterministic per-voice PRNG so identical notes render identically.
class XorShift32 {
public:
    void seed(const std::uint32_t value) { state_ = value != 0 ? value : 0x9E3779B9u; }

    float nextBipolar()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_) / 2147483648.0f;
    }

private:
    std::uint32_t state_ = 0x9E3779B9u;
};

class OnePoleLowpass {
public:
    void reset() { z_ = 0.0f; }

    float process(const float input, const float cutoffHz, const float sampleRate)
    {
        const float safeRate = std::max(1000.0f, sampleRate);
        const float cutoff = std::clamp(cutoffHz, 40.0f, safeRate * 0.42f);
        const float coefficient = 1.0f - std::exp(-kTwoPi * cutoff / safeRate);
        z_ += coefficient * (input - z_);
        if (!std::isfinite(z_))
            z_ = 0.0f;
        return z_;
    }

private:
    float z_ = 0.0f;
};

class OnePoleHighpass {
public:
    void reset()
    {
        x_ = 0.0f;
        y_ = 0.0f;
    }

    float process(const float input, const float cutoffHz, const float sampleRate)
    {
        const float safeRate = std::max(1000.0f, sampleRate);
        const float cutoff = std::clamp(cutoffHz, 20.0f, safeRate * 0.42f);
        const float rc = 1.0f / (kTwoPi * cutoff);
        const float dt = 1.0f / safeRate;
        const float alpha = rc / (rc + dt);
        y_ = alpha * (y_ + input - x_);
        x_ = input;
        if (!std::isfinite(y_))
        {
            x_ = 0.0f;
            y_ = 0.0f;
        }
        return y_;
    }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
};

struct ModeState {
    float phase = 0.0f;
    float increment = 0.0f;
    float amplitude = 0.0f;
    float decayCoeff = 0.0f;
    float pan = 0.0f;
};

class Voice {
public:
    void setSampleRate(const float sampleRate)
    {
        sampleRate_ = std::max(1000.0f, sampleRate);
    }

    void reset()
    {
        note_ = -1;
        serial_ = 0;
        age_ = 0;
        velocity_ = 0.0f;
        baseFrequency_ = 440.0f;
        releasing_ = false;
        transientSamples_ = 0;
        transientPosition_ = 0;
        transientLevel_ = 0.0f;
        transientDecay_ = 1.0f;
        active_ = false;
        for (auto& mode : modes_)
            mode = ModeState {};
        toneFilterLeft_.reset();
        toneFilterRight_.reset();
        transientHp_.reset();
    }

    void start(const int midiNote, const std::uint8_t velocity, const Params& params,
               const std::uint64_t serial)
    {
        note_ = midiNote;
        baseFrequency_ = midiNoteToFrequency(midiNote);
        velocity_ = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
        serial_ = serial;
        age_ = 0;
        releasing_ = false;

        const int model = paramChoice(params, ParamId::instrument, 5);
        const ModelSpec& spec = kModelSpecs[static_cast<std::size_t>(model)];
        const float decay = params.values[static_cast<std::size_t>(ParamId::decay)];
        const float mallet = params.values[static_cast<std::size_t>(ParamId::mallet)];
        const float spread = params.values[static_cast<std::size_t>(ParamId::spread)];
        const float position = params.values[static_cast<std::size_t>(ParamId::position)];

        // Overall resonance time: 0.15x–3x around the model base.
        const float decayScale = 0.15f * std::pow(20.0f, clampUnit(decay));
        // Spread stretches partials from near-harmonic (0.6x) to bell-like (1.5x).
        const float stretch = 0.60f + 0.90f * clampUnit(spread);
        // Hard beater excites upper partials; soft beater stays near the hum.
        const float spectralSlope = (1.80f - 1.74f * clampUnit(mallet)) * 0.50f;
        // Strike position: edge (0) is bright and clangorous, centre (1) is round.
        const float edgeMix = 1.0f - clampUnit(position);
        const float width = clampUnit(params.values[static_cast<std::size_t>(ParamId::width)]);

        XorShift32 rng {};
        rng.seed(static_cast<std::uint32_t>(midiNote * 131 + velocity * 17 + serial * 1013904223u));

        for (std::size_t i = 0; i < kModeCount; ++i)
        {
            const ModeEntry& entry = spec.modes[i];
            ModeState& mode = modes_[i];
            if (entry.ratio <= 0.0f || entry.level <= 0.0f)
            {
                mode = ModeState {};
                continue;
            }

            const float ratio = 1.0f + (entry.ratio - 1.0f) * stretch;
            const float frequency = std::min(baseFrequency_ * ratio, sampleRate_ * 0.42f);
            mode.increment = frequency / sampleRate_;
            mode.phase = rng.nextBipolar() * 0.5f;

            float weight = entry.level * std::exp(-(ratio - 1.0f) * spectralSlope);
            // Centre strike damps partials proportionally to their distance
            // from the fundamental; edge strike lets them ring and adds bite.
            weight /= 1.0f + (1.0f - edgeMix) * (ratio - 1.0f) * 1.10f;
            weight *= 1.0f + edgeMix * std::min(ratio / 8.0f, 1.0f) * 0.90f;
            if (i == 0)
                weight *= 1.0f + (1.0f - edgeMix) * 0.25f;

            mode.amplitude = weight * (0.25f + 0.75f * velocity_);
            const float t60 = std::max(kMinT60, spec.baseT60 * decayScale * entry.decayMul);
            mode.decayCoeff = std::exp(-6.9077553f / (t60 * sampleRate_));
            // Alternate partials across the stereo field; higher partials wider.
            // Width collapses everything toward centre when turned down.
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            mode.pan = std::clamp((side * 0.55f * std::min(ratio / 4.0f + 0.35f, 1.0f)
                                       + (static_cast<float>((midiNote * 37 + i * 11) % 101) / 100.0f - 0.5f) * 0.10f)
                                      * (0.05f + 0.95f * width),
                                  -0.85f, 0.85f);
        }

        // Mallet transient: hard beater = short loud click, soft = longer quiet thud.
        const float transientSeconds = 0.014f - 0.0115f * clampUnit(mallet);
        transientSamples_ = std::max(8, static_cast<int>(transientSeconds * sampleRate_));
        transientPosition_ = 0;
        transientLevel_ = spec.transient * (0.35f + 0.65f * velocity_) * (0.5f + 0.5f * edgeMix)
            * (0.45f + 0.75f * clampUnit(mallet));
        transientDecay_ = std::exp(-6.0f / static_cast<float>(transientSamples_));
        transientHp_.reset();
        toneFilterLeft_.reset();
        toneFilterRight_.reset();
        releaseCoeff_ = modes_[0].decayCoeff;
        active_ = true;
    }

    void release(const float releaseT60)
    {
        releasing_ = true;
        releaseCoeff_ = std::exp(-6.9077553f / (std::max(kMinT60, releaseT60) * sampleRate_));
    }

    StereoFrame process(const Params& params)
    {
        if (!active_)
            return {};

        const float tone = params.values[static_cast<std::size_t>(ParamId::tone)];
        const float cutoff = 900.0f + tone * tone * 11000.0f;

        float left = 0.0f;
        float right = 0.0f;
        float peak = 0.0f;

        for (auto& mode : modes_)
        {
            if (mode.amplitude < kSilenceEps && releasing_)
                continue;
            if (mode.increment <= 0.0f)
                continue;
            mode.phase += mode.increment;
            if (mode.phase >= 1.0f)
                mode.phase -= 1.0f;
            const float coeff = releasing_ ? releaseCoeff_ : mode.decayCoeff;
            mode.amplitude *= coeff;
            const float sample = std::sin(kTwoPi * mode.phase) * mode.amplitude;
            peak = std::max(peak, std::fabs(mode.amplitude));
            const float leftGain = std::sqrt(0.5f * (1.0f - mode.pan));
            const float rightGain = std::sqrt(0.5f * (1.0f + mode.pan));
            left += sample * leftGain;
            right += sample * rightGain;
        }

        if (transientPosition_ < transientSamples_)
        {
            transientLevel_ *= transientDecay_;
            const float noise = transientHp_.process(nextNoise(), 1800.0f, sampleRate_);
            const float click = noise * transientLevel_;
            left += click * 0.7f;
            right += click * 0.7f;
            peak = std::max(peak, std::fabs(click));
            ++transientPosition_;
        }

        const float filteredLeft = toneFilterLeft_.process(left, cutoff, sampleRate_);
        // Share one filter state per channel to keep the tone control cheap and
        // deterministic: reuse the same object twice is wrong, so keep two.
        const float filteredRight = toneFilterRight_.process(right, cutoff, sampleRate_);
        ++age_;

        if (releasing_ && peak < kSilenceEps && transientPosition_ >= transientSamples_)
        {
            active_ = false;
            note_ = -1;
            return {};
        }

        return {filteredLeft, filteredRight};
    }

    bool active() const { return active_; }
    bool releasing() const { return releasing_; }
    int note() const { return note_; }
    std::uint64_t serial() const { return serial_; }

private:
    // Bound method-local PRNG state kept on the voice for determinism.
    float nextNoise()
    {
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        return static_cast<float>(rngState_) / 2147483648.0f;
    }

    float sampleRate_ = 44100.0f;
    int note_ = -1;
    float baseFrequency_ = 440.0f;
    float velocity_ = 0.0f;
    std::array<ModeState, kModeCount> modes_ {};
    bool releasing_ = false;
    bool active_ = false;
    float releaseCoeff_ = 1.0f;
    int transientSamples_ = 0;
    int transientPosition_ = 0;
    float transientLevel_ = 0.0f;
    float transientDecay_ = 1.0f;
    std::uint32_t rngState_ = 0x9E3779B9u;
    OnePoleLowpass toneFilterLeft_ {};
    OnePoleLowpass toneFilterRight_ {};
    OnePoleHighpass transientHp_ {};
    std::uint64_t serial_ = 0;
    std::uint64_t age_ = 0;
};

} // namespace

class MokaEngine::Impl {
public:
    explicit Impl(const float sampleRate) { setSampleRate(sampleRate); }

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
        params_.values[index] = clamped;
    }

    void applyPreset(const std::size_t presetIndex)
    {
        if (presetIndex >= kPresets.size())
            return;
        for (std::size_t i = 0; i < kParameterCount; ++i)
            setParameter(static_cast<std::uint32_t>(i), kPresets[presetIndex].values[i]);
    }

    std::size_t voiceCap() const
    {
        return static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::lround(params_.values[static_cast<std::size_t>(ParamId::voices)])),
                       1, static_cast<int>(kMaxVoices)));
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

        const std::size_t cap = voiceCap();
        Voice* voice = findFreeVoice();
        if (voice != nullptr && activeVoiceCount() < cap)
        {
            voice->start(midiNote, velocity, params_, ++serialCounter_);
            return;
        }

        voice = chooseVoiceToSteal();
        if (voice != nullptr)
            voice->start(midiNote, velocity, params_, ++serialCounter_);
    }

    void noteOff(const int midiNote)
    {
        const float t60 = releaseT60ForParam(params_.values[static_cast<std::size_t>(ParamId::release)]);
        for (auto& voice : voices_)
            if (voice.active() && voice.note() == midiNote)
                voice.release(t60);
    }

    void allNotesOff()
    {
        const float t60 = releaseT60ForParam(params_.values[static_cast<std::size_t>(ParamId::release)]);
        for (auto& voice : voices_)
            if (voice.active())
                voice.release(t60);
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
            if (!voice.active())
                continue;
            const StereoFrame frame = voice.process(params_);
            left += frame.left;
            right += frame.right;
        }

        const float output = params_.values[static_cast<std::size_t>(ParamId::level)] * kOutputGain;
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
            if (voice.active() && voice.releasing() && voice.serial() < oldest)
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
            if (voice.active() && voice.serial() < oldest)
            {
                oldest = voice.serial();
                candidate = &voice;
            }
        }
        return candidate;
    }

    Params params_;
    std::array<Voice, MokaEngine::kMaxVoices> voices_ {};
    float sampleRate_ = 44100.0f;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
    std::uint64_t serialCounter_ = 0;
};

MokaEngine::MokaEngine(const float sampleRate)
    : impl_(std::make_unique<Impl>(sampleRate))
    , sampleRate_(std::max(1000.0f, sampleRate))
{
}

MokaEngine::~MokaEngine() = default;

void MokaEngine::setSampleRate(const float sampleRate)
{
    sampleRate_ = std::max(1000.0f, sampleRate);
    impl_->setSampleRate(sampleRate_);
}

void MokaEngine::reset()
{
    impl_->reset();
}

float MokaEngine::getParameter(const std::uint32_t index) const
{
    return impl_->getParameter(index);
}

void MokaEngine::setParameter(const std::uint32_t index, const float value)
{
    impl_->setParameter(index, value);
}

float MokaEngine::getParameter(const ParamId id) const
{
    return getParameter(static_cast<std::uint32_t>(id));
}

void MokaEngine::setParameter(const ParamId id, const float value)
{
    setParameter(static_cast<std::uint32_t>(id), value);
}

void MokaEngine::applyPreset(const std::size_t presetIndex)
{
    impl_->applyPreset(presetIndex);
}

void MokaEngine::noteOn(const int midiNote, const std::uint8_t velocity)
{
    impl_->noteOn(midiNote, velocity);
}

void MokaEngine::noteOff(const int midiNote)
{
    impl_->noteOff(midiNote);
}

void MokaEngine::allNotesOff()
{
    impl_->allNotesOff();
}

void MokaEngine::handleMidi(const std::uint8_t* data, const std::uint32_t size)
{
    impl_->handleMidi(data, size);
}

StereoFrame MokaEngine::processStereo()
{
    return impl_->processStereo();
}

std::size_t MokaEngine::activeVoiceCount() const
{
    return impl_->activeVoiceCount();
}

std::size_t MokaEngine::voiceCap() const
{
    return impl_->voiceCap();
}

} // namespace downspout::moka
