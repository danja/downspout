#include "mixgen_core.hpp"
#include "t_mix_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>

int main()
{
    using namespace downspout;
    std::array<float, mixgen::kParameterCount> generatorParameters {};
    for (std::size_t index = 0; index < generatorParameters.size(); ++index)
        generatorParameters[index] = mixgen::kParameterSpecs[index].defaultValue;
    generatorParameters[mixgen::kVariation] = 0.0f;

    generative::Transport transport;
    transport.valid = true;
    transport.playing = true;
    transport.bpm = 120.0;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    mixgen::State generatorState;
    const generative::MidiBlock generated =
        mixgen::process(generatorState, generatorParameters, transport, 64, 48000.0);
    assert(generated.count == tmix::kInputChannelCount);

    std::array<tmix::MidiControlEvent, tmix::kInputChannelCount> controls {};
    for (std::uint32_t index = 0; index < controls.size(); ++index) {
        controls[index].frame = generated.events[index].frame;
        controls[index].size = generated.events[index].size;
        controls[index].data = {generated.events[index].data[0],
                                generated.events[index].data[1],
                                generated.events[index].data[2]};
    }

    tmix::EngineState mixerState;
    tmix::activate(mixerState);
    tmix::Parameters mixerParameters;
    mixerParameters.producerSlewMs = 0.0f;
    std::array<std::array<float, 1>, tmix::kInputChannelCount> inputBuffers {};
    tmix::AudioBlock audio;
    for (std::uint32_t channel = 0; channel < tmix::kInputChannelCount; ++channel) {
        inputBuffers[channel][0] = 1.0f;
        audio.inputs[channel] = inputBuffers[channel].data();
    }
    std::array<float, 1> left {};
    std::array<float, 1> right {};
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();
    const tmix::OutputStatus status = tmix::processBlock(
        mixerState, mixerParameters, 1, 48000.0, audio, controls.data(), controls.size());

    float gainSum = 0.0f;
    for (std::uint32_t channel = 0; channel < tmix::kInputChannelCount; ++channel) {
        const float expected = static_cast<float>(controls[channel].data[2]) / 127.0f;
        assert(std::fabs(status.producerGains[channel] - expected) < 1.0e-6f);
        gainSum += expected;
    }
    const float expectedSide = gainSum * std::sqrt(0.5f);
    assert(std::fabs(left[0] - expectedSide) < 1.0e-5f);
    assert(std::fabs(right[0] - expectedSide) < 1.0e-5f);
    return 0;
}
