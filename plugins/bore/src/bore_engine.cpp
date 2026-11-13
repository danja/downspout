#include "bore_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace downspout::bore {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kVoiceGain = 0.22f;
constexpr float kQuiet = 1.0e-5f;
constexpr double kAttackTimeScale = 5.0;
constexpr double kReleaseTimeScale = 7.0;

[[nodiscard]] std::size_t index(const ParamId id) noexcept
{
    return static_cast<std::size_t>(id);
}

[[nodiscard]] float onePoleCoefficient(const float cutoff, const double sampleRate) noexcept
{
    return static_cast<float>(1.0 - std::exp(-2.0 * kPi * static_cast<double>(cutoff) / sampleRate));
}

[[nodiscard]] std::uint32_t nextRandom(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

[[nodiscard]] float noise(std::uint32_t& state) noexcept
{
    return static_cast<float>(nextRandom(state) >> 8) * (2.0f / 16777215.0f) - 1.0f;
}

}  // namespace

Engine::Engine()
{
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        parameters_[i] = kParameterSpecs[i].defaultValue;
    activate(sampleRate_);
}

void Engine::activate(const double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) ? std::max(sampleRate, 1.0) : 48000.0;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        voices_[i] = Voice {};
        voices_[i].random = static_cast<std::uint32_t>(0x9E3779B9u * (i + 1));
    }
    age_ = 0;
    pressure_ = parameters_[index(ParamId::pressure)];
    embouchure_ = parameters_[index(ParamId::embouchure)];
    bell_ = parameters_[index(ParamId::bell)];
    breath_ = parameters_[index(ParamId::breath)];
    level_ = parameters_[index(ParamId::level)];
    smoothCoefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * sampleRate_)));
}

void Engine::setParameter(const std::uint32_t parameter, const float value) noexcept
{
    if (parameter < kInputParameterCount)
        parameters_[parameter] = clampParameter(parameter, value);
}

float Engine::getParameter(const std::uint32_t parameter) const noexcept
{
    if (parameter < kInputParameterCount)
        return parameters_[parameter];
    return parameter == index(ParamId::outVoices) ? static_cast<float>(activeVoices()) : 0.0f;
}

int Engine::activeVoices() const noexcept
{
    int count = 0;
    for (const Voice& voice : voices_)
        count += voice.envelope > kQuiet ? 1 : 0;
    return count;
}

std::size_t Engine::allocateVoice(const int note, const std::uint8_t channel) noexcept
{
    for (std::size_t i = 0; i < voices_.size(); ++i)
        if (voices_[i].note == note && voices_[i].channel == channel)
            return i;
    for (std::size_t i = 0; i < voices_.size(); ++i)
        if (voices_[i].note < 0)
            return i;

    std::size_t oldest = 0;
    for (std::size_t i = 1; i < voices_.size(); ++i)
        if (voices_[i].age < voices_[oldest].age)
            oldest = i;
    return oldest;
}

void Engine::noteOn(const int note, const int velocity, const std::uint8_t channel) noexcept
{
    const std::size_t voiceIndex = allocateVoice(note, channel);
    Voice& voice = voices_[voiceIndex];
    voice.note = note;
    voice.channel = channel;
    voice.held = true;
    voice.envelope = 0.0f;
    const float sensitivity = parameters_[index(ParamId::velocity)];
    const float normalizedVelocity = static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f;
    voice.velocity = 1.0f - sensitivity + sensitivity * normalizedVelocity;
    voice.boreFilter = 0.0f;
    voice.breathFilter = 0.0f;
    voice.lipPhase = 0.0;
    voice.writeIndex = 0;
    voice.samplesWritten = 0;
    voice.age = ++age_;
    if (voice.random == 0)
        voice.random = static_cast<std::uint32_t>(0xA341316Cu + voiceIndex);
}

void Engine::noteOff(const int note, const std::uint8_t channel) noexcept
{
    for (Voice& voice : voices_)
        if (voice.held && voice.note == note && voice.channel == channel)
            voice.held = false;
}

void Engine::allNotesOff(const std::uint8_t channel) noexcept
{
    for (Voice& voice : voices_)
        if (voice.channel == channel)
            voice.held = false;
}

void Engine::allSoundOff(const std::uint8_t channel) noexcept
{
    for (Voice& voice : voices_) {
        if (voice.channel != channel)
            continue;
        voice.held = false;
        voice.envelope = 0.0f;
        voice.note = -1;
        voice.boreFilter = 0.0f;
        voice.breathFilter = 0.0f;
        voice.writeIndex = 0;
        voice.samplesWritten = 0;
    }
}

void Engine::handleMidi(const MidiMessage& message) noexcept
{
    if (message.size < 2 || message.data[0] >= 0xF0)
        return;

    const int channelSetting = static_cast<int>(parameters_[index(ParamId::midiCh)] + 0.5f);
    if (channelSetting == 0)
        return;
    const int channel = static_cast<int>(message.data[0] & 0x0F) + 1;
    if (channelSetting <= 16 && channel != channelSetting)
        return;

    const std::uint8_t command = message.data[0] & 0xF0;
    const std::uint8_t midiChannel = message.data[0] & 0x0F;
    const std::uint8_t data1 = message.data[1];
    const std::uint8_t data2 = message.size > 2 ? message.data[2] : std::uint8_t {0};
    if (command == 0x90) {
        if (data2 == 0)
            noteOff(data1, midiChannel);
        else
            noteOn(data1, data2, midiChannel);
    } else if (command == 0x80) {
        noteOff(data1, midiChannel);
    } else if (command == 0xB0) {
        if (data1 == 120)
            allSoundOff(midiChannel);
        else if (data1 == 123)
            allNotesOff(midiChannel);
        else {
            for (std::size_t i = 0; i < kControllerNumbers.size(); ++i) {
                if (data1 == kControllerNumbers[i]) {
                    const ParamId target = kControllerTargets[i];
                    const ParamSpec& spec = kParameterSpecs[index(target)];
                    const float normalized = static_cast<float>(std::min<std::uint8_t>(data2, 127)) / 127.0f;
                    setParameter(index(target), spec.minimum + normalized * (spec.maximum - spec.minimum));
                    break;
                }
            }
        }
    }
}

void Engine::process(float* const left,
                     float* const right,
                     const std::uint32_t frames,
                     const MidiMessage* const events,
                     const std::uint32_t eventCount) noexcept
{
    if (left == nullptr || right == nullptr || frames == 0)
        return;

    std::uint32_t cursor = 0;
    for (std::uint32_t i = 0; events != nullptr && i < eventCount; ++i) {
        const std::uint32_t at = std::min(events[i].frame, frames);
        if (at > cursor) {
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
    const float attackSeconds = std::max(0.001f, parameters_[index(ParamId::attack)] * 0.001f);
    const float releaseSeconds = std::max(0.02f, parameters_[index(ParamId::release)] * 0.001f);
    const float attackCoefficient = static_cast<float>(1.0 - std::exp(-kAttackTimeScale / (attackSeconds * sampleRate_)));
    const float releaseCoefficient = static_cast<float>(1.0 - std::exp(-kReleaseTimeScale / (releaseSeconds * sampleRate_)));
    const double sr = sampleRate_;

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        pressure_ += smoothCoefficient_ * (parameters_[index(ParamId::pressure)] - pressure_);
        embouchure_ += smoothCoefficient_ * (parameters_[index(ParamId::embouchure)] - embouchure_);
        bell_ += smoothCoefficient_ * (parameters_[index(ParamId::bell)] - bell_);
        breath_ += smoothCoefficient_ * (parameters_[index(ParamId::breath)] - breath_);
        level_ += smoothCoefficient_ * (parameters_[index(ParamId::level)] - level_);
        const float cutoff = 900.0f + bell_ * bell_ * static_cast<float>(std::min(14500.0, sr * 0.42) - 900.0);
        const float boreCoefficient = onePoleCoefficient(cutoff, sr);
        const float breathCoefficient = onePoleCoefficient(static_cast<float>(std::min(7000.0, sr * 0.35)), sr);
        float sample = 0.0f;

        for (Voice& voice : voices_) {
            if (voice.note < 0)
                continue;
            if (voice.held)
                voice.envelope += (1.0f - voice.envelope) * attackCoefficient;
            else
                voice.envelope -= voice.envelope * releaseCoefficient;
            if (voice.envelope <= kQuiet && !voice.held) {
                voice.envelope = 0.0f;
                voice.note = -1;
                voice.boreFilter = 0.0f;
                continue;
            }

            const double maxFrequency = std::max(20.0, std::min(12000.0, sr * 0.4));
            const double frequency = std::clamp(noteToHertz(voice.note), 20.0, maxFrequency);
            const double delay = std::clamp(sr / frequency, 2.0, static_cast<double>(kMaxDelaySamples - 2));
            float delayed = 0.0f;
            if (voice.samplesWritten >= static_cast<std::uint32_t>(std::ceil(delay))) {
                double read = static_cast<double>(voice.writeIndex) - delay;
                if (read < 0.0)
                    read += static_cast<double>(kMaxDelaySamples);
                const std::size_t first = static_cast<std::size_t>(read);
                const std::size_t second = (first + 1) % kMaxDelaySamples;
                const float fraction = static_cast<float>(read - static_cast<double>(first));
                delayed = voice.bore[first] + fraction * (voice.bore[second] - voice.bore[first]);
            }
            voice.boreFilter += boreCoefficient * (delayed - voice.boreFilter);

            const double lipFrequency = std::min(frequency * static_cast<double>(embouchure_), sr * 0.45);
            const float lip = static_cast<float>(std::sin(voice.lipPhase));
            voice.lipPhase += 2.0 * kPi * lipFrequency / sr;
            if (voice.lipPhase >= 2.0 * kPi)
                voice.lipPhase -= 2.0 * kPi;
            const float breathNoise = noise(voice.random);
            voice.breathFilter += breathCoefficient * (breathNoise - voice.breathFilter);
            const float drive = 1.0f + pressure_ * 5.0f;
            const float reed = std::tanh(drive * lip + breath_ * voice.breathFilter * 0.35f);
            const float feedback = 0.82f + pressure_ * 0.15f;
            const float excitation = pressure_ * (0.035f + 0.01f * breath_) * reed;
            voice.bore[voice.writeIndex] = excitation + feedback * voice.boreFilter;
            voice.writeIndex = (voice.writeIndex + 1) % static_cast<std::uint32_t>(kMaxDelaySamples);
            voice.samplesWritten = std::min(voice.samplesWritten + 1, static_cast<std::uint32_t>(kMaxDelaySamples));
            sample += voice.boreFilter * voice.envelope * voice.velocity * kVoiceGain;
        }
        sample *= level_;
        left[frame] = sample;
        right[frame] = sample;
    }
}

}  // namespace downspout::bore
