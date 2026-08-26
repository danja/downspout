#include "mixgen_core.hpp"

#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::mixgen;

namespace {

std::array<float, kParameterCount> defaults()
{
    std::array<float, kParameterCount> parameters {};
    for (std::size_t index = 0; index < parameters.size(); ++index)
        parameters[index] = kParameterSpecs[index].defaultValue;
    return parameters;
}

Transport playingAt(double barBeat = 0.0)
{
    Transport transport;
    transport.valid = true;
    transport.playing = true;
    transport.bpm = 120.0;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.barBeat = barBeat;
    return transport;
}

void testModesAreDeterministicAndBounded()
{
    auto parameters = defaults();
    for (int mode = 0; mode < 3; ++mode) {
        parameters[kMode] = static_cast<float>(mode);
        for (int lane = 0; lane < kLaneCount; ++lane) {
            for (int step = 0; step < 32; ++step) {
                const float first = gainForStep(parameters, lane, step);
                const float second = gainForStep(parameters, lane, step);
                assert(first == second);
                assert(first >= 0.0f && first <= 1.0f);
            }
        }
    }
}

void testDepthAndDensityHaveClearExtremes()
{
    auto parameters = defaults();
    parameters[kMode] = 2.0f;
    parameters[kDepth] = 1.0f;
    parameters[kDensity] = 0.0f;
    for (int lane = 0; lane < kLaneCount; ++lane)
        assert(gainForStep(parameters, lane, 0) == 0.0f);
    parameters[kDensity] = 1.0f;
    parameters[kVariation] = 0.0f;
    for (int lane = 0; lane < kLaneCount; ++lane)
        assert(gainForStep(parameters, lane, 0) == 1.0f);
}

void testEuclideanPulseCountAndLaneSpread()
{
    auto parameters = defaults();
    parameters[kMode] = 2.0f;
    parameters[kSteps] = 16.0f;
    parameters[kDensity] = 0.5f;
    parameters[kDepth] = 1.0f;
    parameters[kVariation] = 0.0f;
    parameters[kSpread] = 1.0f;
    int laneOneHits = 0;
    int laneEightHits = 0;
    bool patternsDiffer = false;
    for (int step = 0; step < 16; ++step) {
        const bool laneOne = gainForStep(parameters, 0, step) > 0.5f;
        const bool laneEight = gainForStep(parameters, 7, step) > 0.5f;
        laneOneHits += laneOne ? 1 : 0;
        laneEightHits += laneEight ? 1 : 0;
        patternsDiffer = patternsDiffer || laneOne != laneEight;
    }
    assert(laneOneHits == 8);
    assert(laneEightHits == 8);
    assert(patternsDiffer);
}

void testBlockEmitsTheTmixContract()
{
    const auto parameters = defaults();
    State state;
    const MidiBlock result = process(state, parameters, playingAt(), 12000, 48000.0);
    assert(result.count == kLaneCount + 1);
    assert(result.events[0].data[1] == kProducerLifecycleCc);
    assert(result.events[0].data[2] == 127);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto& event = result.events[static_cast<std::size_t>(lane + 1)];
        assert(event.frame == 0);
        assert(event.data[0] == 0xb0);
        assert(event.data[1] == kTargetCcBase + lane);
        assert(event.data[2] <= 127);
    }
}

void testStoppedTransportAndRewind()
{
    auto parameters = defaults();
    State state;
    Transport transport = playingAt();
    assert(process(state, parameters, transport, 12000, 48000.0).count == kLaneCount + 1);
    transport.playing = false;
    const MidiBlock stopped = process(state, parameters, transport, 12000, 48000.0);
    assert(stopped.count == kLaneCount + 1);
    assert(stopped.events[0].data[1] == kProducerLifecycleCc && stopped.events[0].data[2] == 0);
    transport = playingAt(2.0);
    (void)process(state, parameters, transport, 12000, 48000.0);
    transport = playingAt(0.0);
    const MidiBlock rewind = process(state, parameters, transport, 12000, 48000.0);
    assert(rewind.count == kLaneCount + 1);
    assert(rewind.events[0].frame == 0);
}

void testMultipleBoundariesTempoAndMeterChanges()
{
    const auto parameters = defaults();
    State state;
    Transport transport = playingAt();
    const MidiBlock crossed = process(state, parameters, transport, 24001, 48000.0);
    assert(crossed.count == kLaneCount * 3 + 1);
    assert(crossed.events[0].frame == 0);
    assert(crossed.events[kLaneCount + 1].frame == 12000);
    assert(crossed.events[kLaneCount * 2 + 1].frame == 24000);

    reset(state);
    (void)process(state, parameters, transport, 12000, 48000.0);
    transport.barBeat = 0.5;
    transport.bpm = 90.0;
    const MidiBlock tempoChange = process(state, parameters, transport, 64, 48000.0);
    assert(tempoChange.count == kLaneCount);
    assert(tempoChange.events[0].frame == 0);

    transport.bar = 1.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 3.0;
    const MidiBlock meterChange = process(state, parameters, transport, 64, 48000.0);
    assert(meterChange.count == kLaneCount + 1);
    assert(meterChange.events[0].frame == 0);
}

void testDisabledRestoresUnity()
{
    auto parameters = defaults();
    State state;
    (void)process(state, parameters, playingAt(), 12000, 48000.0);
    parameters[kEnabled] = 0.0f;
    const MidiBlock result = process(state, parameters, playingAt(.5), 64, 48000.0);
    assert(result.count == kLaneCount + 1);
    assert(result.events[0].data[1] == kProducerLifecycleCc && result.events[0].data[2] == 0);
    for (std::uint32_t index = 1; index < result.count; ++index)
        assert(result.events[index].data[2] == 127);
}

void testFullBusAndCustomFxRouting()
{
    auto parameters = defaults();
    parameters[kRoutingProfile] = 2.0f;
    parameters[kFxSourceBase] = 8.0f;
    parameters[kFxCcBase] = 74.0f;
    parameters[kFxMinimumBase] = 0.2f;
    parameters[kFxMaximumBase] = 0.6f;
    parameters[kFxInvertBase] = 0.0f;
    State state;
    const MidiBlock result = process(state, parameters, playingAt(), 12000, 48000.0);
    assert(result.count == 1 + kLaneCount + kFxLaneCount);
    assert(result.events[1 + kLaneCount].data[1] == 74);
    const float expected = fxValueForStep(parameters, 0, 0);
    assert(expected >= 0.2f && expected <= 0.6f);
    assert(std::fabs(state.fxValues[0] - expected) < 1.0e-6f);
}

void testChannelChangeReleasesOldOwner()
{
    auto parameters = defaults();
    State state;
    (void)process(state, parameters, playingAt(), 12000, 48000.0);
    parameters[kMidiChannel] = 5.0f;
    const MidiBlock result = process(state, parameters, playingAt(.5), 12000, 48000.0);
    assert(result.events[0].data[0] == 0xb0);
    assert(result.events[0].data[1] == kProducerLifecycleCc && result.events[0].data[2] == 0);
    assert(result.events[kLaneCount + 1].data[0] == 0xb4);
    assert(result.events[kLaneCount + 1].data[1] == kProducerLifecycleCc);
}

} // namespace

int main()
{
    testModesAreDeterministicAndBounded();
    testDepthAndDensityHaveClearExtremes();
    testEuclideanPulseCountAndLaneSpread();
    testBlockEmitsTheTmixContract();
    testStoppedTransportAndRewind();
    testMultipleBoundariesTempoAndMeterChanges();
    testDisabledRestoresUnity();
    testFullBusAndCustomFxRouting();
    testChannelChangeReleasesOldOwner();
    return 0;
}
