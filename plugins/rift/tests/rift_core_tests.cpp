#include "rift_engine.hpp"
#include "rift_sample_loader.hpp"
#include "rift_serialization.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace downspout::rift;

namespace {

void testClampParameters() {
    Parameters parameters;
    parameters.grid = 42.0f;
    parameters.density = -5.0f;
    parameters.dub = 140.0f;
    parameters.damage = 120.0f;
    parameters.memoryBars = 99.0f;
    parameters.drift = -1.0f;
    parameters.pitch = 99.0f;
    parameters.mix = 140.0f;
    parameters.blend = 140.0f;
    parameters.hold = 7.0f;
    parameters.sourceMode = 7.0f;
    parameters.sampleBeats = 99.0f;
    parameters.chop = 140.0f;

    const Parameters clamped = clampParameters(parameters);
    assert(std::fabs(clamped.grid - 16.0f) < 1e-6f);
    assert(std::fabs(clamped.density) < 1e-6f);
    assert(std::fabs(clamped.dub - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.damage - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.memoryBars - 8.0f) < 1e-6f);
    assert(std::fabs(clamped.drift) < 1e-6f);
    assert(std::fabs(clamped.pitch - 12.0f) < 1e-6f);
    assert(std::fabs(clamped.mix - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.blend - 100.0f) < 1e-6f);
    assert(std::fabs(clamped.hold - 1.0f) < 1e-6f);
    assert(std::fabs(clamped.sourceMode - 2.0f) < 1e-6f);
    assert(std::fabs(clamped.sampleBeats - 32.0f) < 1e-6f);
    assert(std::fabs(clamped.chop - 100.0f) < 1e-6f);
}

void testPreviewActionHonorsZeroDensity() {
    Parameters parameters;
    parameters.density = 0.0f;
    for (std::uint64_t index = 0; index < 32; ++index) {
        assert(previewActionForBlock(parameters, index) == ActionType::Pass);
    }
}

void testPreviewActionHonorsDubProbability() {
    Parameters parameters;
    parameters.density = 0.0f;
    parameters.dub = 100.0f;
    for (std::uint64_t index = 0; index < 32; ++index) {
        assert(previewActionForBlock(parameters, index) == ActionType::Dub);
    }
}

void testStoppedTransportPassesThrough() {
    EngineState state;
    activate(state, 8.0, 2);

    std::array<float, 8> inL {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    std::array<float, 8> inR {0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f};
    std::array<float, 8> outL {};
    std::array<float, 8> outR {};

    AudioBlock audio;
    audio.inputs[0] = inL.data();
    audio.inputs[1] = inR.data();
    audio.outputs[0] = outL.data();
    audio.outputs[1] = outR.data();
    audio.channelCount = 2;

    const OutputStatus status = processBlock(state,
                                             Parameters {},
                                             Triggers {},
                                             TransportSnapshot {},
                                             static_cast<std::uint32_t>(inL.size()),
                                             8.0,
                                             audio);

    assert(status.action == ActionType::Pass);
    assert(status.sequenceCell == -1);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        assert(std::fabs(outL[i] - inL[i]) < 1e-6f);
        assert(std::fabs(outR[i] - inR[i]) < 1e-6f);
    }
}

void testDefaultParametersPassThroughWhilePlaying() {
    EngineState state;
    activate(state, 8.0, 1);

    std::array<float, 32> input {};
    std::array<float, 32> output {};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i) * 0.03f;
    }

    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = output.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    const OutputStatus status = processBlock(state,
                                             Parameters {},
                                             Triggers {},
                                             transport,
                                             static_cast<std::uint32_t>(input.size()),
                                             8.0,
                                             audio);

    assert(status.action == ActionType::Pass);
    assert(status.sequenceCell == 7);
    for (std::size_t i = 0; i < input.size(); ++i) {
        assert(std::fabs(output[i] - input[i]) < 1e-6f);
    }
}

void testStatusReportsTransportSequenceCell() {
    EngineState state;
    activate(state, 8.0, 1);

    std::array<float, 8> input {};
    std::array<float, 8> output {};

    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = output.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 1.0;
    transport.barBeat = 2.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    Parameters parameters;
    parameters.grid = 4.0f;

    const OutputStatus status = processBlock(state,
                                             parameters,
                                             Triggers {},
                                             transport,
                                             static_cast<std::uint32_t>(input.size()),
                                             8.0,
                                             audio);

    assert(status.action == ActionType::Pass);
    assert(status.sequenceCell == 6);
}

void testSequenceCellsContinueAcrossOneBarLoop() {
    EngineState state;
    activate(state, 8.0, 1);

    Parameters parameters;
    parameters.grid = 8.0f;
    parameters.density = 0.0f;
    parameters.memoryBars = 2.0f;
    parameters.mix = 100.0f;

    SequencePattern sequence;
    sequence.cells[8].kind = SequenceCellKind::Dub;
    sequence.cells[15].kind = SequenceCellKind::Reverse;

    std::array<float, 72> input {};
    std::array<float, 72> output {};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i) * 0.01f;
    }

    AudioBlock audio;
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    OutputStatus status;
    for (int step = 0; step < 16; ++step) {
        audio.inputs[0] = input.data() + static_cast<std::size_t>(step) * 4u;
        audio.outputs[0] = output.data() + static_cast<std::size_t>(step) * 4u;
        transport.bar = 0.0;
        transport.barBeat = static_cast<double>(step % 8) * 0.5;
        status = processBlock(state, parameters, Triggers {}, sequence, transport, 4u, 8.0, audio);

        assert(status.sequenceCell == step);
    }

    assert(status.action == ActionType::Reverse);
    assert(state.activeSequenceSerial == 15);
}

void testScatterMutatesAndRecoverReturnsDry() {
    EngineState state;
    activate(state, 8.0, 1);

    Parameters parameters;
    parameters.grid = 1.0f;
    parameters.density = 0.0f;
    parameters.damage = 100.0f;
    parameters.memoryBars = 2.0f;
    parameters.drift = 80.0f;
    parameters.pitch = 7.0f;
    parameters.mix = 100.0f;

    std::array<float, 64> firstIn {};
    std::array<float, 32> secondIn {};
    std::array<float, 32> thirdIn {};
    for (std::size_t i = 0; i < firstIn.size(); ++i) {
        firstIn[i] = static_cast<float>(i) * 0.05f;
    }
    for (std::size_t i = 0; i < secondIn.size(); ++i) {
        secondIn[i] = 10.0f + static_cast<float>(i) * 0.07f;
        thirdIn[i] = 20.0f + static_cast<float>(i) * 0.09f;
    }

    std::array<float, 64> firstOut {};
    std::array<float, 32> secondOut {};
    std::array<float, 32> thirdOut {};

    AudioBlock audio;
    audio.inputs[0] = firstIn.data();
    audio.outputs[0] = firstOut.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    const OutputStatus firstStatus = processBlock(state,
                                                  parameters,
                                                  Triggers {},
                                                  transport,
                                                  32u,
                                                  8.0,
                                                  audio);
    assert(firstStatus.action == ActionType::Pass);

    audio.inputs[0] = firstIn.data() + 32;
    audio.outputs[0] = firstOut.data() + 32;
    transport.bar = 1.0;
    const OutputStatus warmupStatus = processBlock(state,
                                                   parameters,
                                                   Triggers {},
                                                   transport,
                                                   32u,
                                                   8.0,
                                                   audio);
    assert(warmupStatus.action == ActionType::Pass);

    audio.inputs[0] = secondIn.data();
    audio.outputs[0] = secondOut.data();
    transport.bar = 2.0;
    const OutputStatus mutated = processBlock(state,
                                              parameters,
                                              Triggers {.scatterSerial = 1},
                                              transport,
                                              static_cast<std::uint32_t>(secondIn.size()),
                                              8.0,
                                              audio);

    assert(mutated.action != ActionType::Pass);
    bool changed = false;
    for (std::size_t i = 0; i < secondIn.size(); ++i) {
        if (std::fabs(secondOut[i] - secondIn[i]) > 1e-4f) {
            changed = true;
            break;
        }
    }
    assert(changed);

    audio.inputs[0] = thirdIn.data();
    audio.outputs[0] = thirdOut.data();
    transport.bar = 3.0;
    const OutputStatus recovered = processBlock(state,
                                                parameters,
                                                Triggers {.scatterSerial = 1, .recoverSerial = 1},
                                                transport,
                                                static_cast<std::uint32_t>(thirdIn.size()),
                                                8.0,
                                                audio);

    assert(recovered.action == ActionType::Pass);
    bool recoverTransitionChanged = false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (std::fabs(thirdOut[i] - thirdIn[i]) > 1e-4f) {
            recoverTransitionChanged = true;
            break;
        }
    }
    assert(recoverTransitionChanged);
    for (std::size_t i = 8; i < thirdIn.size(); ++i) {
        assert(std::fabs(thirdOut[i] - thirdIn[i]) < 1e-6f);
    }
}

void testBlockTransitionsArmCrossfade() {
    EngineState state;
    activate(state, 8.0, 1);

    Parameters parameters;
    parameters.grid = 1.0f;
    parameters.density = 0.0f;
    parameters.damage = 100.0f;
    parameters.memoryBars = 2.0f;
    parameters.drift = 80.0f;
    parameters.pitch = 7.0f;
    parameters.mix = 100.0f;

    std::array<float, 64> historyIn {};
    std::array<float, 4> mutatedIn {};
    std::array<float, 4> continuedIn {};
    std::array<float, 64> historyOut {};
    std::array<float, 4> mutatedOut {};
    std::array<float, 4> continuedOut {};

    for (std::size_t i = 0; i < historyIn.size(); ++i) {
        historyIn[i] = static_cast<float>(i) * 0.1f;
    }
    for (std::size_t i = 0; i < mutatedIn.size(); ++i) {
        mutatedIn[i] = 4.0f + static_cast<float>(i);
        continuedIn[i] = 8.0f + static_cast<float>(i);
    }

    AudioBlock audio;
    audio.inputs[0] = historyIn.data();
    audio.outputs[0] = historyOut.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    const OutputStatus initial = processBlock(state,
                                              parameters,
                                              Triggers {},
                                              transport,
                                              32u,
                                              8.0,
                                              audio);
    assert(initial.action == ActionType::Pass);

    audio.inputs[0] = historyIn.data() + 32;
    audio.outputs[0] = historyOut.data() + 32;
    transport.bar = 1.0;
    const OutputStatus warmup = processBlock(state,
                                             parameters,
                                             Triggers {},
                                             transport,
                                             32u,
                                             8.0,
                                             audio);
    assert(warmup.action == ActionType::Pass);

    audio.inputs[0] = mutatedIn.data();
    audio.outputs[0] = mutatedOut.data();
    transport.bar = 2.0;
    const OutputStatus mutated = processBlock(state,
                                              parameters,
                                              Triggers {.scatterSerial = 1},
                                              transport,
                                              static_cast<std::uint32_t>(mutatedIn.size()),
                                              8.0,
                                              audio);

    assert(mutated.action != ActionType::Pass);
    assert(state.transitionBlock.valid);
    assert(state.transitionFramesTotal == 8u);
    assert(state.transitionFramesRemaining == 4u);

    audio.inputs[0] = continuedIn.data();
    audio.outputs[0] = continuedOut.data();
    transport.bar = 2.0;
    transport.barBeat = 0.5;
    const OutputStatus continued = processBlock(state,
                                                parameters,
                                                Triggers {.scatterSerial = 1},
                                                transport,
                                                static_cast<std::uint32_t>(continuedIn.size()),
                                                8.0,
                                                audio);
    assert(continued.action != ActionType::Pass);

    assert(state.transitionFramesRemaining == 0u);
    assert(!state.transitionBlock.valid);
}

void testSamplePlaybackPassesDryInputAtZeroDensity() {
    EngineState state;
    activate(state, 2.0, 1);

    SampleSource source;
    source.channelCount = 1;
    source.sampleRate = 4.0;
    source.sourceBpm = 100.0;
    source.loopBeats = 4.0;
    source.interleaved = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};

    assert(isSampleSourceUsable(source));
    assert(std::fabs(sampleLoopBeats(source) - 4.0) < 1e-6);

    Parameters parameters;
    parameters.density = 0.0f;

    std::array<float, 4> dry {0.25f, 0.5f, 0.75f, 1.0f};
    std::array<float, 4> out {};
    AudioBlock audio;
    audio.inputs[0] = dry.data();
    audio.outputs[0] = out.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    SamplePlayback playback;
    playback.source = &source;
    playback.mode = InputSourceMode::Sample;

    const OutputStatus status = processBlock(state,
                                             parameters,
                                             Triggers {},
                                             transport,
                                             static_cast<std::uint32_t>(out.size()),
                                             2.0,
                                             audio,
                                             playback);

    assert(status.action == ActionType::Pass);
    for (std::size_t i = 0; i < dry.size(); ++i) {
        assert(std::fabs(out[i] - dry[i]) < 1e-6f);
    }
}

void testSamplePlaybackIsSilentWithoutDryInputAtZeroDensity() {
    EngineState state;
    activate(state, 2.0, 1);

    SampleSource source;
    source.channelCount = 1;
    source.sampleRate = 4.0;
    source.loopBeats = 4.0;
    source.interleaved = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};

    Parameters parameters;
    parameters.density = 0.0f;

    std::array<float, 4> out {};
    AudioBlock audio;
    audio.outputs[0] = out.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    SamplePlayback playback;
    playback.source = &source;
    playback.mode = InputSourceMode::Sample;

    const OutputStatus status = processBlock(state,
                                             parameters,
                                             Triggers {},
                                             transport,
                                             static_cast<std::uint32_t>(out.size()),
                                             2.0,
                                             audio,
                                             playback);

    assert(status.action == ActionType::Pass);
    for (const float value : out) {
        assert(std::fabs(value) < 1e-6f);
    }
}

void testSamplePlaybackFeedsExistingMutationBuffer() {
    EngineState state;
    activate(state, 8.0, 1);

    SampleSource source;
    source.channelCount = 1;
    source.sampleRate = 8.0;
    source.loopBeats = 16.0;
    source.interleaved.resize(128);
    for (std::size_t i = 0; i < source.interleaved.size(); ++i) {
        source.interleaved[i] = static_cast<float>(i) * 0.05f;
    }

    Parameters parameters;
    parameters.grid = 1.0f;
    parameters.density = 0.0f;
    parameters.damage = 100.0f;
    parameters.memoryBars = 2.0f;
    parameters.drift = 80.0f;
    parameters.pitch = 7.0f;
    parameters.mix = 100.0f;

    std::array<float, 32> firstOut {};
    std::array<float, 32> secondOut {};
    std::array<float, 32> mutatedOut {};

    AudioBlock audio;
    audio.outputs[0] = firstOut.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    SamplePlayback playback;
    playback.source = &source;
    playback.mode = InputSourceMode::Sample;

    const OutputStatus first = processBlock(state,
                                            parameters,
                                            Triggers {},
                                            transport,
                                            static_cast<std::uint32_t>(firstOut.size()),
                                            8.0,
                                            audio,
                                            playback);
    assert(first.action == ActionType::Pass);

    audio.outputs[0] = secondOut.data();
    transport.bar = 1.0;
    const OutputStatus second = processBlock(state,
                                             parameters,
                                             Triggers {},
                                             transport,
                                             static_cast<std::uint32_t>(secondOut.size()),
                                             8.0,
                                             audio,
                                             playback);
    assert(second.action == ActionType::Pass);

    audio.outputs[0] = mutatedOut.data();
    transport.bar = 2.0;
    const OutputStatus mutated = processBlock(state,
                                              parameters,
                                              Triggers {.scatterSerial = 1},
                                              transport,
                                              static_cast<std::uint32_t>(mutatedOut.size()),
                                              8.0,
                                              audio,
                                              playback);

    assert(mutated.action != ActionType::Pass);
    bool changed = false;
    for (std::size_t i = 0; i < mutatedOut.size(); ++i) {
        if (std::fabs(mutatedOut[i]) > 1e-4f) {
            changed = true;
            break;
        }
    }
    assert(changed);
}

void testSamplePlaybackMatchesLiveInputForSequence() {
    EngineState liveState;
    EngineState sampleState;
    activate(liveState, 8.0, 1);
    activate(sampleState, 8.0, 1);

    SampleSource source;
    source.channelCount = 1;
    source.sampleRate = 8.0;
    source.loopBeats = 16.0;
    source.interleaved.resize(128);
    for (std::size_t i = 0; i < source.interleaved.size(); ++i) {
        source.interleaved[i] = static_cast<float>(i) * 0.01f;
    }

    Parameters parameters;
    parameters.grid = 4.0f;
    parameters.density = 0.0f;
    parameters.memoryBars = 2.0f;
    parameters.drift = 0.0f;
    parameters.mix = 100.0f;
    parameters.blend = 0.0f;

    SequencePattern sequence;
    sequence.cells[8].kind = SequenceCellKind::TwoBeat;

    SamplePlayback playback;
    playback.source = &source;
    playback.mode = InputSourceMode::Sample;
    playback.loopBeats = source.loopBeats;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    std::array<float, 80> liveIn {};
    std::array<float, 80> liveOut {};
    std::array<float, 80> sampleOut {};
    for (std::size_t i = 0; i < liveIn.size(); ++i) {
        liveIn[i] = source.interleaved[i];
    }

    AudioBlock liveAudio;
    liveAudio.inputs[0] = liveIn.data();
    liveAudio.outputs[0] = liveOut.data();
    liveAudio.channelCount = 1;

    AudioBlock sampleAudio;
    sampleAudio.inputs[0] = liveIn.data();
    sampleAudio.outputs[0] = sampleOut.data();
    sampleAudio.channelCount = 1;

    for (int block = 0; block < 10; ++block) {
        liveAudio.inputs[0] = liveIn.data() + static_cast<std::size_t>(block) * 8u;
        liveAudio.outputs[0] = liveOut.data() + static_cast<std::size_t>(block) * 8u;
        sampleAudio.inputs[0] = liveIn.data() + static_cast<std::size_t>(block) * 8u;
        sampleAudio.outputs[0] = sampleOut.data() + static_cast<std::size_t>(block) * 8u;
        transport.bar = static_cast<double>(block / 4);
        transport.barBeat = static_cast<double>(block % 4);

        const OutputStatus liveStatus = processBlock(liveState,
                                                     parameters,
                                                     Triggers {},
                                                     sequence,
                                                     transport,
                                                     8u,
                                                     8.0,
                                                     liveAudio);
        const OutputStatus sampleStatus = processBlock(sampleState,
                                                       parameters,
                                                       Triggers {},
                                                       sequence,
                                                       transport,
                                                       8u,
                                                       8.0,
                                                       sampleAudio,
                                                       playback);
        assert(liveStatus.action == sampleStatus.action);
    }

    for (std::size_t i = 0; i < liveOut.size(); ++i) {
        assert(std::fabs(liveOut[i] - sampleOut[i]) < 1e-6f);
    }
}

void testChopShortensMutationSlice() {
    EngineState state;
    activate(state, 16.0, 1);

    Parameters parameters;
    parameters.grid = 4.0f;
    parameters.density = 0.0f;
    parameters.damage = 100.0f;
    parameters.memoryBars = 2.0f;
    parameters.drift = 20.0f;
    parameters.mix = 100.0f;
    parameters.chop = 100.0f;

    std::array<float, 96> input {};
    std::array<float, 96> output {};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i) * 0.01f;
    }

    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = output.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    (void)processBlock(state, parameters, Triggers {}, transport, 32u, 16.0, audio);
    audio.inputs[0] = input.data() + 32;
    audio.outputs[0] = output.data() + 32;
    transport.bar = 0.0;
    transport.barBeat = 2.0;
    (void)processBlock(state, parameters, Triggers {}, transport, 32u, 16.0, audio);

    audio.inputs[0] = input.data() + 64;
    audio.outputs[0] = output.data() + 64;
    transport.bar = 1.0;
    transport.barBeat = 0.0;
    const OutputStatus chopped = processBlock(state,
                                              parameters,
                                              Triggers {.scatterSerial = 1},
                                              transport,
                                              16u,
                                              16.0,
                                              audio);

    assert(chopped.action != ActionType::Pass);
    assert(state.activeBlock.sourceLengthFrames <= 2u);
}

void testSequenceCellForcesTwoBeatRepeat() {
    EngineState state;
    activate(state, 8.0, 1);

    Parameters parameters;
    parameters.grid = 4.0f;
    parameters.density = 0.0f;
    parameters.memoryBars = 2.0f;
    parameters.mix = 100.0f;

    SequencePattern sequence;
    sequence.cells[8].kind = SequenceCellKind::TwoBeat;

    std::array<float, 80> input {};
    std::array<float, 80> output {};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i) * 0.02f;
    }

    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = output.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    for (int block = 0; block < 8; ++block) {
        audio.inputs[0] = input.data() + static_cast<std::size_t>(block) * 8u;
        audio.outputs[0] = output.data() + static_cast<std::size_t>(block) * 8u;
        transport.bar = static_cast<double>(block / 4);
        transport.barBeat = static_cast<double>(block % 4);
        (void)processBlock(state, parameters, Triggers {}, sequence, transport, 8u, 8.0, audio);
    }

    audio.inputs[0] = input.data() + 64;
    audio.outputs[0] = output.data() + 64;
    transport.bar = 2.0;
    transport.barBeat = 0.0;
    const OutputStatus status = processBlock(state, parameters, Triggers {}, sequence, transport, 8u, 8.0, audio);

    assert(status.action == ActionType::Repeat);
    assert(status.sequenceCell == 8);
    assert(state.activeBlock.sourceLengthFrames == 16u);
    assert(state.sequenceBlocksRemaining == 1u);

    audio.inputs[0] = input.data() + 72;
    audio.outputs[0] = output.data() + 72;
    transport.bar = 2.0;
    transport.barBeat = 1.0;
    const OutputStatus held = processBlock(state, parameters, Triggers {}, sequence, transport, 8u, 8.0, audio);
    assert(held.action == ActionType::Repeat);
    assert(held.sequenceCell == 9);
    assert(state.sequenceBlocksRemaining == 0u);
}

void testSequenceCellForcesDubEcho() {
    EngineState state;
    activate(state, 8.0, 1);

    Parameters parameters;
    parameters.grid = 4.0f;
    parameters.density = 0.0f;
    parameters.memoryBars = 2.0f;
    parameters.mix = 100.0f;

    SequencePattern sequence;
    sequence.cells[8].kind = SequenceCellKind::Dub;

    std::array<float, 72> input {};
    std::array<float, 72> output {};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i) * 0.03f;
    }

    AudioBlock audio;
    audio.inputs[0] = input.data();
    audio.outputs[0] = output.data();
    audio.channelCount = 1;

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 60.0;

    for (int block = 0; block < 8; ++block) {
        audio.inputs[0] = input.data() + static_cast<std::size_t>(block) * 8u;
        audio.outputs[0] = output.data() + static_cast<std::size_t>(block) * 8u;
        transport.bar = static_cast<double>(block / 4);
        transport.barBeat = static_cast<double>(block % 4);
        (void)processBlock(state, parameters, Triggers {}, sequence, transport, 8u, 8.0, audio);
    }

    audio.inputs[0] = input.data() + 64;
    audio.outputs[0] = output.data() + 64;
    transport.bar = 2.0;
    transport.barBeat = 0.0;
    const OutputStatus status = processBlock(state, parameters, Triggers {}, sequence, transport, 8u, 8.0, audio);

    assert(status.action == ActionType::Dub);
    assert(status.sequenceCell == 8);
    assert(state.activeBlock.sourceLengthFrames == 8u);
}

void testSerializationRoundTrip() {
    Parameters parameters;
    parameters.grid = 5.0f;
    parameters.density = 62.0f;
    parameters.dub = 27.0f;
    parameters.damage = 71.0f;
    parameters.memoryBars = 4.0f;
    parameters.drift = 55.0f;
    parameters.pitch = -7.0f;
    parameters.mix = 84.0f;
    parameters.blend = 31.0f;
    parameters.hold = 1.0f;
    parameters.sourceMode = 2.0f;
    parameters.sampleBeats = 8.0f;
    parameters.chop = 63.0f;

    const auto roundTrip = deserializeParameters(serializeParameters(parameters));
    assert(roundTrip.has_value());
    assert(std::fabs(roundTrip->grid - 5.0f) < 1e-6f);
    assert(std::fabs(roundTrip->density - 62.0f) < 1e-6f);
    assert(std::fabs(roundTrip->dub - 27.0f) < 1e-6f);
    assert(std::fabs(roundTrip->damage - 71.0f) < 1e-6f);
    assert(std::fabs(roundTrip->memoryBars - 4.0f) < 1e-6f);
    assert(std::fabs(roundTrip->drift - 55.0f) < 1e-6f);
    assert(std::fabs(roundTrip->pitch + 7.0f) < 1e-6f);
    assert(std::fabs(roundTrip->mix - 84.0f) < 1e-6f);
    assert(std::fabs(roundTrip->blend - 31.0f) < 1e-6f);
    assert(std::fabs(roundTrip->hold - 1.0f) < 1e-6f);
    assert(std::fabs(roundTrip->sourceMode - 2.0f) < 1e-6f);
    assert(std::fabs(roundTrip->sampleBeats - 8.0f) < 1e-6f);
    assert(std::fabs(roundTrip->chop - 63.0f) < 1e-6f);
}

void testSequenceSerializationRoundTrip() {
    SequencePattern pattern;
    pattern.cells[0].kind = SequenceCellKind::Ratchet;
    pattern.cells[3].kind = SequenceCellKind::TwoBeat;
    pattern.cells[7].kind = SequenceCellKind::Reverse;
    pattern.cells[10].kind = SequenceCellKind::Dub;

    const std::string text = serializeSequencePattern(pattern);
    const auto parsed = deserializeSequencePattern(text);
    assert(parsed.has_value());
    assert(parsed->cells[0].kind == SequenceCellKind::Ratchet);
    assert(parsed->cells[3].kind == SequenceCellKind::TwoBeat);
    assert(parsed->cells[7].kind == SequenceCellKind::Reverse);
    assert(parsed->cells[10].kind == SequenceCellKind::Dub);
    assert(parsed->cells[1].kind == SequenceCellKind::Empty);
    assert(sequencePatternHasCells(*parsed));
}

void testEarlierStateFormatDefaultsBlend() {
    const auto parsed = deserializeParameters(
        "version=1\n"
        "grid=4\n"
        "density=50\n"
        "damage=33\n"
        "memoryBars=2\n"
        "drift=11\n"
        "pitch=0\n"
        "mix=75\n"
        "hold=0\n");

    assert(parsed.has_value());
    assert(std::fabs(parsed->dub) < 1e-6f);
    assert(std::fabs(parsed->blend - 20.0f) < 1e-6f);
    assert(std::fabs(parsed->sourceMode) < 1e-6f);
    assert(std::fabs(parsed->sampleBeats - 4.0f) < 1e-6f);
    assert(std::fabs(parsed->chop) < 1e-6f);
}

void writeU16(std::ofstream& file, const std::uint16_t value) {
    file.put(static_cast<char>(value & 0xffu));
    file.put(static_cast<char>((value >> 8) & 0xffu));
}

void writeU32(std::ofstream& file, const std::uint32_t value) {
    file.put(static_cast<char>(value & 0xffu));
    file.put(static_cast<char>((value >> 8) & 0xffu));
    file.put(static_cast<char>((value >> 16) & 0xffu));
    file.put(static_cast<char>((value >> 24) & 0xffu));
}

void testLoadWavSampleSource() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "downspout_rift_loader_test.wav";
    {
        std::ofstream file(path, std::ios::binary);
        assert(file);

        const std::uint16_t channels = 1;
        const std::uint32_t sampleRate = 4;
        const std::uint16_t bitsPerSample = 16;
        const std::uint16_t blockAlign = channels * bitsPerSample / 8u;
        const std::uint32_t dataBytes = 4u * blockAlign;

        file.write("RIFF", 4);
        writeU32(file, 36u + dataBytes);
        file.write("WAVE", 4);
        file.write("fmt ", 4);
        writeU32(file, 16);
        writeU16(file, 1);
        writeU16(file, channels);
        writeU32(file, sampleRate);
        writeU32(file, sampleRate * blockAlign);
        writeU16(file, blockAlign);
        writeU16(file, bitsPerSample);
        file.write("data", 4);
        writeU32(file, dataBytes);
        writeU16(file, 0);
        writeU16(file, 16384);
        writeU16(file, 32767);
        writeU16(file, static_cast<std::uint16_t>(-16384));
    }

    const SampleLoadResult loaded = loadWavSampleSource(path.string().c_str(), 4.0);
    assert(loaded.error.empty());
    assert(isSampleSourceUsable(loaded.source));
    assert(loaded.source.channelCount == 1u);
    assert(std::fabs(loaded.source.sampleRate - 4.0) < 1e-6);
    assert(std::fabs(loaded.source.loopBeats - 4.0) < 1e-6);
    assert(loaded.source.interleaved.size() == 4u);
    assert(std::fabs(loaded.source.interleaved[0]) < 1e-6f);
    assert(std::fabs(loaded.source.interleaved[1] - 0.5f) < 1e-4f);
    assert(loaded.source.interleaved[2] > 0.99f);
    assert(std::fabs(loaded.source.interleaved[3] + 0.5f) < 1e-4f);
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    testClampParameters();
    testPreviewActionHonorsZeroDensity();
    testPreviewActionHonorsDubProbability();
    testStoppedTransportPassesThrough();
    testDefaultParametersPassThroughWhilePlaying();
    testStatusReportsTransportSequenceCell();
    testSequenceCellsContinueAcrossOneBarLoop();
    testScatterMutatesAndRecoverReturnsDry();
    testBlockTransitionsArmCrossfade();
    testSamplePlaybackPassesDryInputAtZeroDensity();
    testSamplePlaybackIsSilentWithoutDryInputAtZeroDensity();
    testSamplePlaybackFeedsExistingMutationBuffer();
    testSamplePlaybackMatchesLiveInputForSequence();
    testChopShortensMutationSlice();
    testSequenceCellForcesTwoBeatRepeat();
    testSequenceCellForcesDubEcho();
    testSerializationRoundTrip();
    testSequenceSerializationRoundTrip();
    testEarlierStateFormatDefaultsBlend();
    testLoadWavSampleSource();
    return 0;
}
