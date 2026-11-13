#include "downspout/test_assert.h"
#include "bore_engine.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using namespace downspout::bore;

constexpr double kSampleRate = 48000.0;
constexpr std::uint32_t kBlock = 64;

std::uint32_t id(const ParamId parameter)
{
    return static_cast<std::uint32_t>(parameter);
}

MidiMessage noteOn(const std::uint8_t note, const std::uint8_t velocity = 100, const std::uint8_t channel = 0)
{
    return {0, 3, {static_cast<std::uint8_t>(0x90 | channel), note, velocity}};
}

MidiMessage noteOff(const std::uint8_t note, const std::uint8_t channel = 0)
{
    return {0, 3, {static_cast<std::uint8_t>(0x80 | channel), note, 0}};
}

MidiMessage control(const std::uint8_t controller, const std::uint8_t value, const std::uint8_t channel = 0)
{
    return {0, 3, {static_cast<std::uint8_t>(0xB0 | channel), controller, value}};
}

struct Audio {
    std::vector<float> left;
    std::vector<float> right;
};

Audio render(Engine& engine, const std::uint32_t blocks, const std::vector<MidiMessage>& events = {})
{
    Audio audio;
    std::array<float, kBlock> left {};
    std::array<float, kBlock> right {};
    for (std::uint32_t block = 0; block < blocks; ++block) {
        engine.process(left.data(), right.data(), kBlock,
                       block == 0 ? events.data() : nullptr,
                       block == 0 ? static_cast<std::uint32_t>(events.size()) : 0u);
        audio.left.insert(audio.left.end(), left.begin(), left.end());
        audio.right.insert(audio.right.end(), right.begin(), right.end());
    }
    return audio;
}

float peak(const std::vector<float>& audio, const std::size_t begin, const std::size_t end)
{
    float result = 0.0f;
    for (std::size_t i = begin; i < std::min(end, audio.size()); ++i)
        result = std::max(result, std::abs(audio[i]));
    return result;
}

double amplitudeAt(const std::vector<float>& audio, const std::size_t begin, const std::size_t count, const double hz)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double phase = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / kSampleRate;
        real += audio[begin + i] * std::cos(phase);
        imaginary -= audio[begin + i] * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(count);
}

double rms(const std::vector<float>& samples, const std::size_t begin, const std::size_t end)
{
    double sum = 0.0;
    for (std::size_t i = begin; i < std::min(end, samples.size()); ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / static_cast<double>(end - begin));
}

Audio renderWithParameter(const ParamId parameter, const float value)
{
    Engine engine;
    engine.setParameter(id(parameter), value);
    engine.activate(kSampleRate);
    return render(engine, 112, {noteOn(60)});
}

void testParameterContract()
{
    assert(kParameterSpecs.size() == kParameterCount);
    assert(kInputParameterCount + 1 == kParameterCount);
    Engine engine;
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        assert(engine.getParameter(static_cast<std::uint32_t>(i)) == kParameterSpecs[i].defaultValue);
    engine.setParameter(id(ParamId::pressure), 2.0f);
    assert(engine.getParameter(id(ParamId::pressure)) == 1.0f);
    engine.setParameter(id(ParamId::embouchure), std::nanf(""));
    assert(engine.getParameter(id(ParamId::embouchure)) == 1.0f);
    engine.setParameter(id(ParamId::midiCh), 4.6f);
    assert(engine.getParameter(id(ParamId::midiCh)) == 5.0f);
    engine.setParameter(id(ParamId::outVoices), 6.0f);
    assert(engine.getParameter(id(ParamId::outVoices)) == 0.0f);
}

void testPitchedVoiceAndVelocity()
{
    Engine engine;
    engine.activate(kSampleRate);
    const Audio audio = render(engine, 112, {noteOn(60)});
    assert(peak(audio.left, 0, audio.left.size()) > 0.01f);
    for (float sample : audio.left)
        assert(std::isfinite(sample));
    const double fundamental = amplitudeAt(audio.left, 2048, 4096, 261.625565);
    const double subharmonic = amplitudeAt(audio.left, 2048, 4096, 130.812782);
    assert(fundamental > subharmonic * 2.0);

    Engine quiet;
    quiet.activate(kSampleRate);
    const Audio soft = render(quiet, 112, {noteOn(60, 20)});
    assert(rms(soft.left, 2048, 6144) < rms(audio.left, 2048, 6144));
}

void testMIDIChannelAndSampleAccurateOnset()
{
    Engine filtered;
    filtered.activate(kSampleRate);
    filtered.setParameter(id(ParamId::midiCh), 2.0f);
    assert(peak(render(filtered, 8, {noteOn(60, 100, 0)}).left, 0, 512) == 0.0f);

    Engine offset;
    offset.activate(kSampleRate);
    MidiMessage event = noteOn(60);
    event.frame = 32;
    std::array<float, 512> left {};
    std::array<float, 512> right {};
    offset.process(left.data(), right.data(), static_cast<std::uint32_t>(left.size()), &event, 1);
    for (std::size_t i = 0; i < 32; ++i)
        assert(left[i] == 0.0f);
    assert(peak(std::vector<float>(left.begin() + 200, left.end()), 0, left.size() - 200) > 0.0f);

    Engine channels;
    channels.activate(kSampleRate);
    render(channels, 1, {noteOn(60, 100, 0), noteOn(60, 100, 1)});
    assert(channels.activeVoices() == 2);
    render(channels, 1, {noteOff(60, 0)});
    assert(channels.activeVoices() == 2);
    render(channels, 1, {control(120, 0, 0)});
    assert(channels.activeVoices() == 1);
}

void testReleaseAndAllSoundOff()
{
    Engine engine;
    engine.activate(kSampleRate);
    const Audio sustained = render(engine, 160, {noteOn(48)});
    const float beforeRelease = peak(sustained.left, sustained.left.size() - 1024, sustained.left.size());
    const Audio tail = render(engine, 20, {noteOff(48)});
    assert(peak(tail.left, 0, 1024) > 0.0f);
    assert(engine.activeVoices() == 1);
    const Audio decay = render(engine, 1500);
    assert(peak(decay.left, decay.left.size() - 1024, decay.left.size()) < beforeRelease * 0.5f);
    assert(engine.activeVoices() == 0);

    Engine cut;
    cut.activate(kSampleRate);
    MidiMessage off = control(120, 0);
    Audio muted = render(cut, 32, {noteOn(60), off});
    assert(peak(muted.left, 0, muted.left.size()) < 1.0e-6f);
}

void testControlsChangeTheSound()
{
    const Audio baseline = renderWithParameter(ParamId::pressure, 0.58f);
    for (const auto& test : std::array<std::pair<ParamId, float>, 4> {{
             {ParamId::pressure, 0.2f},
             {ParamId::embouchure, 1.17f},
             {ParamId::bell, 0.05f},
             {ParamId::breath, 1.0f},
         }}) {
        const Audio changed = renderWithParameter(test.first, test.second);
        const std::size_t begin = 2048;
        double difference = 0.0;
        for (std::size_t i = begin; i < baseline.left.size(); ++i)
            difference += std::abs(static_cast<double>(baseline.left[i] - changed.left[i]));
        assert(difference > 1.0);
    }

    const Audio quiet = renderWithParameter(ParamId::level, 0.0f);
    assert(peak(quiet.left, 0, quiet.left.size()) == 0.0f);

    const Audio insensitive = renderWithParameter(ParamId::velocity, 0.0f);
    const Audio sensitive = renderWithParameter(ParamId::velocity, 1.0f);
    assert(rms(insensitive.left, 2048, 6144) > rms(sensitive.left, 2048, 6144));

    const auto attackSound = [](const float milliseconds) {
        Engine engine;
        engine.activate(kSampleRate);
        engine.setParameter(id(ParamId::attack), milliseconds);
        return render(engine, 112, {noteOn(60)});
    };
    const Audio fast = attackSound(1.0f);
    const Audio slow = attackSound(500.0f);
    assert(rms(fast.left, 0, 2048) > rms(slow.left, 0, 2048));
}

void testActivationKeepsThePatch()
{
    Engine engine;
    engine.setParameter(id(ParamId::pressure), 0.31f);
    engine.setParameter(id(ParamId::bell), 0.22f);
    engine.activate(96000.0);
    assert(engine.getParameter(id(ParamId::pressure)) == 0.31f);
    assert(engine.getParameter(id(ParamId::bell)) == 0.22f);
    assert(engine.activeVoices() == 0);
}

void testControllersAndBlockSegmentation()
{
    Engine fromControl;
    fromControl.activate(kSampleRate);
    render(fromControl, 1, {control(1, 127), control(2, 0), control(3, 64), control(4, 127), control(7, 32)});
    assert(fromControl.getParameter(id(ParamId::pressure)) == 1.0f);
    assert(fromControl.getParameter(id(ParamId::embouchure)) == 0.8f);
    assert(std::abs(fromControl.getParameter(id(ParamId::bell)) - 64.0f / 127.0f) < 1.0e-6f);
    assert(fromControl.getParameter(id(ParamId::breath)) == 1.0f);
    assert(std::abs(fromControl.getParameter(id(ParamId::level)) - 32.0f / 127.0f) < 1.0e-6f);

    Engine whole;
    Engine split;
    whole.activate(kSampleRate);
    split.activate(kSampleRate);
    MidiMessage at = noteOn(67);
    at.frame = 173;
    std::array<float, 512> wholeLeft {};
    std::array<float, 512> wholeRight {};
    whole.process(wholeLeft.data(), wholeRight.data(), 512, &at, 1);
    std::array<float, 173> firstLeft {};
    std::array<float, 173> firstRight {};
    split.process(firstLeft.data(), firstRight.data(), 173, nullptr, 0);
    std::array<float, 339> secondLeft {};
    std::array<float, 339> secondRight {};
    at.frame = 0;
    split.process(secondLeft.data(), secondRight.data(), 339, &at, 1);
    for (std::size_t i = 0; i < firstLeft.size(); ++i)
        assert(wholeLeft[i] == firstLeft[i]);
    for (std::size_t i = 0; i < secondLeft.size(); ++i)
        assert(wholeLeft[i + firstLeft.size()] == secondLeft[i]);
}

void testPolyphonicPeakAndStability()
{
    Engine engine;
    engine.setParameter(id(ParamId::pressure), 1.0f);
    engine.setParameter(id(ParamId::bell), 1.0f);
    engine.setParameter(id(ParamId::breath), 1.0f);
    engine.setParameter(id(ParamId::velocity), 0.0f);
    engine.setParameter(id(ParamId::level), 1.0f);
    engine.activate(192000.0);
    std::vector<MidiMessage> notes;
    for (std::uint8_t note = 36; note < 108; note = static_cast<std::uint8_t>(note + 10))
        notes.push_back(noteOn(note, 127));
    const Audio audio = render(engine, 128, notes);
    for (std::size_t i = 0; i < audio.left.size(); ++i) {
        assert(std::isfinite(audio.left[i]));
        assert(std::isfinite(audio.right[i]));
        assert(std::abs(audio.left[i]) <= 1.0f);
    }
}

}  // namespace

int main()
{
    testParameterContract();
    testPitchedVoiceAndVelocity();
    testMIDIChannelAndSampleAccurateOnset();
    testReleaseAndAllSoundOff();
    testControlsChangeTheSound();
    testActivationKeepsThePatch();
    testControllersAndBlockSegmentation();
    testPolyphonicPeakAndStability();
    return 0;
}
