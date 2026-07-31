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
    assert(result.count == kLaneCount);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto& event = result.events[static_cast<std::size_t>(lane)];
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
    assert(process(state, parameters, transport, 12000, 48000.0).count == kLaneCount);
    transport.playing = false;
    assert(process(state, parameters, transport, 12000, 48000.0).count == 0);
    transport = playingAt(2.0);
    (void)process(state, parameters, transport, 12000, 48000.0);
    transport = playingAt(0.0);
    const MidiBlock rewind = process(state, parameters, transport, 12000, 48000.0);
    assert(rewind.count == kLaneCount);
    assert(rewind.events[0].frame == 0);
}

void testMultipleBoundariesTempoAndMeterChanges()
{
    const auto parameters = defaults();
    State state;
    Transport transport = playingAt();
    const MidiBlock crossed = process(state, parameters, transport, 24001, 48000.0);
    assert(crossed.count == kLaneCount * 3);
    assert(crossed.events[0].frame == 0);
    assert(crossed.events[kLaneCount].frame == 12000);
    assert(crossed.events[kLaneCount * 2].frame == 24000);

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
    assert(meterChange.count == kLaneCount);
    assert(meterChange.events[0].frame == 0);
}

void testDisabledRestoresUnity()
{
    auto parameters = defaults();
    parameters[kEnabled] = 0.0f;
    State state;
    const MidiBlock result = process(state, parameters, playingAt(), 12000, 48000.0);
    assert(result.count == kLaneCount);
    for (std::uint32_t index = 0; index < result.count; ++index)
        assert(result.events[index].data[2] == 127);
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
    return 0;
}
