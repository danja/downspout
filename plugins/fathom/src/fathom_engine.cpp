#include "fathom_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::fathom {
namespace {

constexpr float kVoiceGain = 0.45f;
constexpr float kDcCoefficient = 0.995f;
constexpr float kTiny = 1.0e-20f;
constexpr float kLowpassK = 1.2f;  // damping of the low-pass (Q near 0.8)
constexpr float kNotchK = 0.5f;    // notch width (Q of 2)
constexpr double kBendRange = 2.0;

// Pan law for the four voices, so a chord opens up across the field.
constexpr std::array<float, kVoiceCount> kPans = {{-0.45f, 0.45f, -0.2f, 0.2f}};

[[nodiscard]] std::size_t slot(const ParamId id) noexcept
{
    return static_cast<std::size_t>(id);
}

[[nodiscard]] float envelopeStep(const float milliseconds, const double sampleRate) noexcept
{
    const double seconds = std::max(0.001, static_cast<double>(milliseconds) * 0.001);
    return static_cast<float>(1.0 / std::max(1.0, seconds * sampleRate));
}

}  // namespace

Engine::Engine()
{
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        parameters_[i] = kParameterSpecs[i].defaultValue;
    activate(48000.0);
}

void Engine::activate(const double sampleRate)
{
    sampleRate_ = std::max(sampleRate, 1.0);

    for (std::size_t i = 0; i < voices_.size(); ++i)
    {
        Voice& voice = voices_[i];
        voice.generator.setSampleRate(sampleRate_);
        voice.generator.restart();
        voice.envelope.reset();
        voice.lowpass.reset();
        for (Svf& n : voice.notches)
            n.reset();
        voice.note = -1;
        voice.gate = false;
        voice.keyDown = false;
        voice.pan = kPans[i];
        voice.age = 0;
    }
    clock_ = 0;
    havePitch_ = false;
    bend_ = 0.0;

    processor_.prepare(sampleRate_);

    mixSmooth_ = parameters_[slot(ParamId::mix)];
    levelSmooth_ = parameters_[slot(ParamId::level)];
    fieldSmooth_ = parameters_[slot(ParamId::field)];
    notchSmooth_ = parameters_[slot(ParamId::notch)];
    smoothCoefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate_)));
    dcInput_.fill(0.0f);
    dcOutput_.fill(0.0f);
}

void Engine::setParameter(const std::uint32_t index, const float value) noexcept
{
    if (index >= kInputParameterCount)
        return;  // the status parameter is read-only
    const float clamped = clampParameter(index, value);
    if (index == static_cast<std::uint32_t>(ParamId::hold))
    {
        setHold(clamped > 0.5f);
        return;
    }
    parameters_[index] = clamped;
}

float Engine::getParameter(const std::uint32_t index) const noexcept
{
    if (index < kInputParameterCount)
        return parameters_[index];
    if (index == static_cast<std::uint32_t>(ParamId::outVoices))
        return static_cast<float>(activeVoices());
    return 0.0f;
}

int Engine::activeVoices() const noexcept
{
    int count = 0;
    for (const Voice& voice : voices_)
        count += voice.envelope.active() ? 1 : 0;
    return count;
}

// ── MIDI ───────────────────────────────────────────────────────────────────

void Engine::handleMidi(const MidiMessage& message) noexcept
{
    if (message.size < 2)
        return;

    const std::uint8_t status = message.data[0];
    if (status >= 0xF0)
        return;

    const int channelSetting = static_cast<int>(parameters_[slot(ParamId::midiCh)] + 0.5f);
    if (channelSetting == 0)
        return;
    const int channel = static_cast<int>(status & 0x0F) + 1;
    if (channelSetting <= 16 && channel != channelSetting)
        return;

    const std::uint8_t data1 = message.data[1];
    const std::uint8_t data2 = message.size > 2 ? message.data[2] : std::uint8_t {0};

    switch (status & 0xF0)
    {
    case 0x90:
        if (data2 > 0)
            noteOn(data1, data2);
        else
            noteOff(data1);
        break;

    case 0x80:
        noteOff(data1);
        break;

    case 0xE0:
    {
        const int raw = (static_cast<int>(data2) << 7) | static_cast<int>(data1);
        bend_ = (static_cast<double>(raw) - 8192.0) / 8192.0 * kBendRange;
        break;
    }

    case 0xB0:
    {
        if (data1 == kCcAllNotesOff)
        {
            allNotesOff();
            break;
        }
        if (data1 == kCcAllSoundOff)
        {
            allSoundOff();
            break;
        }
        if (data1 == kCcHold)
        {
            setHold(data2 >= 64);
            break;
        }

        ParamId target = ParamId::warp;
        if (controllerTarget(data1, target))
            parameters_[slot(target)] = controllerToParameter(target, data2);
        break;
    }

    default:
        break;
    }
}

std::size_t Engine::allocateVoice(const int note) noexcept
{
    // The same note again retriggers its own voice rather than doubling up.
    for (std::size_t i = 0; i < kVoiceCount; ++i)
    {
        if (voices_[i].envelope.active() && voices_[i].note == note)
            return i;
    }
    for (std::size_t i = 0; i < kVoiceCount; ++i)
    {
        if (!voices_[i].envelope.active())
            return i;
    }

    // Steal the quietest voice that is already dying, else the oldest.
    std::size_t quietest = kVoiceCount;
    for (std::size_t i = 0; i < kVoiceCount; ++i)
    {
        if (voices_[i].envelope.releasing() &&
            (quietest == kVoiceCount || voices_[i].envelope.level() < voices_[quietest].envelope.level()))
            quietest = i;
    }
    if (quietest != kVoiceCount)
        return quietest;

    std::size_t oldest = 0;
    for (std::size_t i = 1; i < kVoiceCount; ++i)
    {
        if (voices_[i].age < voices_[oldest].age)
            oldest = i;
    }
    return oldest;
}

void Engine::noteOn(const int note, const int velocity) noexcept
{
    Voice& voice = voices_[allocateVoice(note)];
    const bool fresh = !voice.envelope.active();

    GeneratorControls controls;
    controls.algorithm = static_cast<int>(parameters_[slot(ParamId::algorithm)]);
    controls.warp = parameters_[slot(ParamId::warp)];
    controls.span = parameters_[slot(ParamId::span)];
    controls.morph = parameters_[slot(ParamId::morph)];
    controls.seed = parameters_[slot(ParamId::seed)];
    controls.fuse = parameters_[slot(ParamId::fuse)];
    voice.generator.setControls(controls);
    if (fresh)
    {
        voice.generator.snap();
        voice.generator.restart();
        voice.lowpass.reset();
        for (Svf& n : voice.notches)
            n.reset();
    }

    // A glide starts from wherever the last note left off.
    const double target = static_cast<double>(note) + static_cast<double>(parameters_[slot(ParamId::basis)]);
    if (fresh)
        voice.pitch = havePitch_ ? lastPitch_ : target;
    lastPitch_ = target;
    havePitch_ = true;

    // Velocity shades the level gently; a drone should not jump around.
    voice.velocityGain = 0.6f + 0.4f * (static_cast<float>(velocity) / 127.0f);
    voice.note = note;
    voice.gate = true;
    voice.keyDown = true;
    voice.age = ++clock_;
    voice.envelope.gateOn();
}

void Engine::noteOff(const int note) noexcept
{
    const bool holding = parameters_[slot(ParamId::hold)] > 0.5f;
    for (Voice& voice : voices_)
    {
        if (voice.keyDown && voice.note == note)
        {
            voice.keyDown = false;
            if (!holding)
            {
                voice.gate = false;
                voice.envelope.gateOff();
            }
        }
    }
}

// Hold keeps voices sounding after their keys come up; letting it go releases
// the ones whose keys are no longer down.
void Engine::setHold(const bool on) noexcept
{
    parameters_[slot(ParamId::hold)] = on ? 1.0f : 0.0f;
    if (on)
        return;
    for (Voice& voice : voices_)
    {
        if (voice.gate && !voice.keyDown)
        {
            voice.gate = false;
            voice.envelope.gateOff();
        }
    }
}

void Engine::allNotesOff() noexcept
{
    for (Voice& voice : voices_)
    {
        voice.keyDown = false;
        if (voice.gate)
        {
            voice.gate = false;
            voice.envelope.gateOff();
        }
    }
}

void Engine::allSoundOff() noexcept
{
    for (Voice& voice : voices_)
    {
        voice.envelope.reset();
        voice.gate = false;
        voice.keyDown = false;
        voice.note = -1;
    }
    processor_.clear();
    dcInput_.fill(0.0f);
    dcOutput_.fill(0.0f);
}

// ── Rendering ──────────────────────────────────────────────────────────────

void Engine::process(float* const left,
                     float* const right,
                     const std::uint32_t frames,
                     const MidiMessage* const events,
                     const std::uint32_t eventCount) noexcept
{
    if (left == nullptr || right == nullptr || frames == 0u)
        return;

    std::uint32_t cursor = 0;
    for (std::uint32_t i = 0; events != nullptr && i < eventCount; ++i)
    {
        const std::uint32_t at = std::min(events[i].frame, frames);
        if (at > cursor)
        {
            renderSegment(left + cursor, right + cursor, at - cursor);
            cursor = at;
        }
        handleMidi(events[i]);
    }

    if (cursor < frames)
        renderSegment(left + cursor, right + cursor, frames - cursor);
}

void Engine::renderSegment(float* const left, float* const right, const std::uint32_t frames) noexcept
{
    const float attackStep = envelopeStep(parameters_[slot(ParamId::attack)], sampleRate_);
    const float releaseStep = envelopeStep(parameters_[slot(ParamId::release)], sampleRate_);

    // Per-segment controls: the generator smooths its own, and the filter moves
    // through a one-pole so a stepped CC does not zipper.
    GeneratorControls controls;
    controls.algorithm = static_cast<int>(parameters_[slot(ParamId::algorithm)]);
    controls.warp = parameters_[slot(ParamId::warp)];
    controls.span = parameters_[slot(ParamId::span)];
    controls.morph = parameters_[slot(ParamId::morph)];
    controls.seed = parameters_[slot(ParamId::seed)];
    controls.fuse = parameters_[slot(ParamId::fuse)];

    ProcessorControls fx;
    fx.kind = static_cast<int>(parameters_[slot(ParamId::processor)]);
    fx.time = parameters_[slot(ParamId::time)];
    fx.form = parameters_[slot(ParamId::form)];
    fx.feed = parameters_[slot(ParamId::feed)];
    fx.shift = parameters_[slot(ParamId::shift)];
    processor_.setControls(fx);

    const float segmentSmooth = 1.0f - std::exp(-static_cast<float>(frames) / (0.02f * static_cast<float>(sampleRate_)));
    fieldSmooth_ += (parameters_[slot(ParamId::field)] - fieldSmooth_) * segmentSmooth;
    notchSmooth_ += (parameters_[slot(ParamId::notch)] - notchSmooth_) * segmentSmooth;

    const double cutoff = fieldToHertz(fieldSmooth_);
    const double glideMs = static_cast<double>(parameters_[slot(ParamId::glide)]);
    const double glideCoefficient =
        glideMs < 0.5 ? 1.0 : 1.0 - std::exp(-1.0 / (glideMs * 0.001 * sampleRate_));
    const double basis = static_cast<double>(parameters_[slot(ParamId::basis)]);

    for (Voice& voice : voices_)
    {
        voice.generator.setControls(controls);
        voice.lowpass.setCutoff(cutoff, sampleRate_, kLowpassK);
        voice.notches[0].setCutoff(cutoff * 0.45, sampleRate_, kNotchK);
        voice.notches[1].setCutoff(cutoff * 0.45 * 1.5, sampleRate_, kNotchK);
    }

    const float notchDepth = notchSmooth_;
    const float mixTarget = parameters_[slot(ParamId::mix)];
    const float levelTarget = parameters_[slot(ParamId::level)];

    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        mixSmooth_ += (mixTarget - mixSmooth_) * smoothCoefficient_;
        levelSmooth_ += (levelTarget - levelSmooth_) * smoothCoefficient_;

        float dryLeft = 0.0f;
        float dryRight = 0.0f;
        float mono = 0.0f;

        for (Voice& voice : voices_)
        {
            if (!voice.envelope.active())
                continue;

            const double targetPitch = static_cast<double>(voice.note) + basis;
            voice.pitch += (targetPitch - voice.pitch) * glideCoefficient;
            const double hertz = noteToHertz(voice.pitch + bend_);

            float s = voice.generator.process(hertz);

            for (Svf& n : voice.notches)
            {
                n.process(s);
                s -= notchDepth * n.k() * n.bandpass();
            }
            voice.lowpass.process(s);
            s = voice.lowpass.lowpass();

            const float amplitude = voice.envelope.process(attackStep, releaseStep) * voice.velocityGain * kVoiceGain;
            s *= amplitude;
            mono += s;
            dryLeft += s * (1.0f - std::max(voice.pan, 0.0f));
            dryRight += s * (1.0f + std::min(voice.pan, 0.0f));
        }

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        processor_.process(mono, wetLeft, wetRight);

        const float dry = 1.0f - mixSmooth_;
        const float inL = (dryLeft * dry + wetLeft * mixSmooth_) * levelSmooth_;
        const float inR = (dryRight * dry + wetRight * mixSmooth_) * levelSmooth_;

        const std::array<float, 2> in = {{inL, inR}};
        std::array<float, 2> out {};
        for (std::size_t c = 0; c < 2; ++c)
        {
            float o = in[c] - dcInput_[c] + kDcCoefficient * dcOutput_[c];
            dcInput_[c] = in[c];
            if (std::fabs(o) < kTiny)
                o = 0.0f;
            dcOutput_[c] = o;
            out[c] = std::min(std::max(o, -1.0f), 1.0f);
        }
        left[frame] = out[0];
        right[frame] = out[1];
    }
}

}  // namespace downspout::fathom
