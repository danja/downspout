#include "trajectory_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::trajectory {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kVoiceGain = 0.35f;     // headroom for a handful of voices at full level
constexpr float kDcCoefficient = 0.995f;  // ~7 Hz at 44.1 kHz
constexpr float kTiny = 1.0e-20f;         // flush denormals out of the DC blocker

[[nodiscard]] double radians(const float degrees) noexcept
{
    return static_cast<double>(degrees) * (kPi / 180.0);
}

[[nodiscard]] std::size_t slot(const ParamId id) noexcept
{
    return static_cast<std::size_t>(id);
}

}  // namespace

Engine::Engine()
{
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        parameters_[i] = kParameterSpecs[i].defaultValue;

    activate(48000.0);
}

void Engine::activate(const double sampleRate) noexcept
{
    sampleRate_ = std::max(sampleRate, 1.0);

    std::uint32_t seed = 0x1234567u;
    for (Voice& voice : voices_)
    {
        voice.oscillator.setSampleRate(sampleRate_);
        voice.oscillator.seedNoise(seed);
        seed = seed * 1664525u + 1013904223u;
        voice.envelope.reset();
        voice.note = -1;
        voice.held = false;
        voice.age = 0;
    }
    clock_ = 0;

    mixXSmooth_ = parameters_[slot(ParamId::mixX)];
    mixYSmooth_ = parameters_[slot(ParamId::mixY)];
    levelSmooth_ = parameters_[slot(ParamId::level)];
    smoothCoefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate_)));
    dcInput_ = 0.0f;
    dcOutput_ = 0.0f;
}

void Engine::setParameter(const std::uint32_t index, const float value) noexcept
{
    if (index >= kInputParameterCount)
        return;  // the status parameter is read-only
    parameters_[index] = clampParameter(index, value);
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

        ParamId target = ParamId::sides;
        if (controllerTarget(data1, target))
            parameters_[static_cast<std::size_t>(target)] = controllerToParameter(target, data2);
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

    // Oscillator shape first, so the point is launched into the current polygon.
    voice.oscillator.setSides(static_cast<int>(parameters_[slot(ParamId::sides)]));
    voice.oscillator.setBounceJitter(radians(parameters_[slot(ParamId::bounceJitter)]));
    voice.oscillator.setFrequency(noteToHertz(note));
    voice.oscillator.start(radians(parameters_[slot(ParamId::startPosition)]),
                           radians(parameters_[slot(ParamId::launchAngle)]));

    const float sensitivity = parameters_[slot(ParamId::velocity)];
    voice.velocityGain = 1.0f - sensitivity + sensitivity * (static_cast<float>(velocity) / 127.0f);
    voice.note = note;
    voice.held = true;
    voice.age = ++clock_;
    voice.envelope.gateOn();
}

void Engine::noteOff(const int note) noexcept
{
    for (Voice& voice : voices_)
    {
        if (voice.held && voice.note == note)
        {
            voice.held = false;
            voice.envelope.gateOff();
        }
    }
}

void Engine::allNotesOff() noexcept
{
    for (Voice& voice : voices_)
    {
        if (voice.held)
        {
            voice.held = false;
            voice.envelope.gateOff();
        }
    }
}

void Engine::allSoundOff() noexcept
{
    for (Voice& voice : voices_)
    {
        voice.envelope.reset();
        voice.held = false;
        voice.note = -1;
    }
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
    AdsrSteps steps;
    steps.attack = envelopeStep(parameters_[slot(ParamId::attack)], sampleRate_);
    steps.decay = envelopeStep(parameters_[slot(ParamId::decay)], sampleRate_);
    steps.release = envelopeStep(parameters_[slot(ParamId::release)], sampleRate_);
    steps.sustain = parameters_[slot(ParamId::sustain)];

    const float mixXTarget = parameters_[slot(ParamId::mixX)];
    const float mixYTarget = parameters_[slot(ParamId::mixY)];
    const float levelTarget = parameters_[slot(ParamId::level)];

    // Live oscillator changes (a CC from Drift, say) reach sounding voices here;
    // a note picks the same values up in noteOn().
    const int sides = static_cast<int>(parameters_[slot(ParamId::sides)]);
    const double jitter = radians(parameters_[slot(ParamId::bounceJitter)]);
    const double launch = radians(parameters_[slot(ParamId::launchAngle)]);
    for (Voice& voice : voices_)
    {
        if (!voice.envelope.active())
            continue;
        voice.oscillator.setSides(sides);
        voice.oscillator.setBounceJitter(jitter);
        voice.oscillator.setLaunchAngle(launch);
    }

    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        mixXSmooth_ += (mixXTarget - mixXSmooth_) * smoothCoefficient_;
        mixYSmooth_ += (mixYTarget - mixYSmooth_) * smoothCoefficient_;
        levelSmooth_ += (levelTarget - levelSmooth_) * smoothCoefficient_;

        float sum = 0.0f;
        for (Voice& voice : voices_)
        {
            if (!voice.envelope.active())
                continue;
            const float envelope = voice.envelope.process(steps);
            sum += voice.oscillator.process(mixXSmooth_, mixYSmooth_) * envelope * voice.velocityGain;
        }

        const float input = sum * kVoiceGain * levelSmooth_;
        float output = input - dcInput_ + kDcCoefficient * dcOutput_;
        dcInput_ = input;
        if (std::fabs(output) < kTiny)
            output = 0.0f;
        dcOutput_ = output;

        const float clipped = std::min(std::max(output, -1.0f), 1.0f);
        left[frame] = clipped;
        right[frame] = clipped;
    }
}

}  // namespace downspout::trajectory
