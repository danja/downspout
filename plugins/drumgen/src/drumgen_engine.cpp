#include "drumgen_engine.hpp"

#include "drumgen_pattern.hpp"
#include "drumgen_transport.hpp"
#include "drumgen_variation.hpp"

#include <cmath>
#include <cstdint>

namespace downspout::drumgen {
namespace {

constexpr int kLaneCrash = static_cast<int>(LaneId::crash);
constexpr int kLaneOpenHat = static_cast<int>(LaneId::openHat);

[[nodiscard]] int clampi(int value, int minValue, int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

void appendMidi(BlockResult& result,
                MidiEventType type,
                std::uint32_t frame,
                int channel,
                int data1,
                int data2) {
    if (result.eventCount >= kMaxScheduledMidiEvents) {
        return;
    }

    ScheduledMidiEvent& event = result.events[result.eventCount++];
    event.type = type;
    event.frame = frame;
    event.channel = static_cast<std::uint8_t>(clampi(channel - 1, 0, 15));
    event.data1 = static_cast<std::uint8_t>(clampi(data1, 0, 127));
    event.data2 = static_cast<std::uint8_t>(clampi(data2, 0, 127));
}

void emitNoteOn(BlockResult& result, const EngineState& state, std::uint32_t frame, int note, int velocity) {
    appendMidi(result, MidiEventType::noteOn, frame, state.controls.channel, note, velocity);
}

void emitNoteOff(BlockResult& result, const EngineState& state, std::uint32_t frame, int note, int channel) {
    appendMidi(result, MidiEventType::noteOff, frame, channel, note, 0);
}

void resetTransportState(EngineState& state) {
    state.wasPlaying = false;
    state.lastTransportStep = -1;
}

void enqueueNoteOff(EngineState& state, int note, int channel, int remainingSamples) {
    const int safeRemaining = remainingSamples < 0 ? 0 : remainingSamples;
    for (PendingNoteOff& pending : state.pendingNoteOffs) {
        if (pending.active) {
            continue;
        }
        pending.active = true;
        pending.note = note;
        pending.channel = channel;
        pending.remainingSamples = safeRemaining;
        return;
    }
}

void processPendingNoteOffs(EngineState& state, BlockResult& result, std::uint32_t nframes) {
    for (PendingNoteOff& pending : state.pendingNoteOffs) {
        if (!pending.active) {
            continue;
        }
        if (pending.remainingSamples < static_cast<int>(nframes)) {
            emitNoteOff(result,
                        state,
                        static_cast<std::uint32_t>(
                            clampi(pending.remainingSamples, 0, static_cast<int>(nframes) - 1)),
                        pending.note,
                        pending.channel);
            pending.active = false;
        } else {
            pending.remainingSamples -= static_cast<int>(nframes);
        }
    }
}

void clearPendingNoteOffs(EngineState& state, BlockResult& result, std::uint32_t frame) {
    for (PendingNoteOff& pending : state.pendingNoteOffs) {
        if (!pending.active) {
            continue;
        }
        emitNoteOff(result, state, frame, pending.note, pending.channel);
        pending.active = false;
    }
}

void updatePatternIfNeeded(EngineState& state, const Controls& current) {
    const bool controlsChanged = structuralControlsChanged(current, state.previousControls);
    const bool newChanged = current.actionNew != state.previousControls.actionNew;
    const bool mutateChanged = current.actionMutate != state.previousControls.actionMutate;
    const bool fillChanged = current.actionFill != state.previousControls.actionFill;
    const bool varyChanged = std::fabs(current.vary - state.previousControls.vary) >= 0.0001f;

    if (!state.patternValid || controlsChanged || newChanged || mutateChanged || fillChanged) {
        const bool fillOnlyRefresh = fillChanged && !controlsChanged && !newChanged && !mutateChanged;
        regeneratePattern(state.pattern, current, fillOnlyRefresh);
        state.patternValid = true;
        resetVariationProgress(state.variation);
    } else if (varyChanged) {
        resetVariationProgress(state.variation);
    }

    state.controls = current;
    state.previousControls = current;
}

void handleStoppedTransport(EngineState& state, BlockResult& result) {
    clearPendingNoteOffs(state, result, 0);
    resetTransportState(state);
}

void emitStepHits(EngineState& state,
                  BlockResult& result,
                  std::uint32_t frame,
                  int localStep,
                  double samplesPerStep,
                  std::uint32_t nframes,
                  double sampleRate) {
    if (!state.patternValid || localStep < 0 || localStep >= state.pattern.totalSteps) {
        return;
    }

    for (int lane = 0; lane < kLaneCount; ++lane) {
        const DrumStepCell& cell = state.pattern.lanes[lane].steps[localStep];
        if (cell.velocity == 0) {
            continue;
        }

        const int note = state.pattern.lanes[lane].midiNote;
        const int onFrame = clampi(static_cast<int>(frame) + (lane == kLaneOpenHat ? kSafetyGapSamples : 0),
                                   0,
                                   static_cast<int>(nframes) - 1);
        emitNoteOn(result, state, static_cast<std::uint32_t>(onFrame), note, cell.velocity);

        const int gateSamples = clampi(static_cast<int>(std::lround(samplesPerStep * (lane == kLaneCrash ? 0.60 : 0.35))),
                                       24,
                                       static_cast<int>(sampleRate * 0.05));
        const int offAt = onFrame + gateSamples;
        if (offAt < static_cast<int>(nframes)) {
            emitNoteOff(result, state, static_cast<std::uint32_t>(offAt), note, state.controls.channel);
        } else if (offAt >= 0) {
            enqueueNoteOff(state, note, state.controls.channel, offAt - static_cast<int>(nframes));
        }
    }
}

void handleTransportRestart(EngineState& state,
                            BlockResult& result,
                            double absStepsStart,
                            double samplesPerStep,
                            std::uint32_t nframes,
                            double sampleRate) {
    clearPendingNoteOffs(state, result, 0);

    const double wrapped = localStepFromAbsolute(state.pattern, absStepsStart);
    const double frac = wrapped - std::floor(wrapped);
    if (frac < 1e-6 || frac > 1.0 - 1e-6) {
        emitStepHits(state,
                     result,
                     0,
                     static_cast<int>(std::floor(wrapped + 1e-6)) % state.pattern.totalSteps,
                     samplesPerStep,
                     nframes,
                     sampleRate);
    }
}

void processBoundary(EngineState& state,
                     BlockResult& result,
                     std::uint32_t nframes,
                     double absStepsStart,
                     double absStepsEnd,
                     std::int64_t boundary,
                     double samplesPerStep,
                     double sampleRate) {
    const std::uint32_t frame = frameForBoundary(absStepsStart, absStepsEnd, nframes, boundary);
    const int localStep = localStepForBoundary(state.pattern, boundary);

    if (localStep == 0 && applyLoopVariation(state.pattern, state.variation, state.controls)) {
        clearPendingNoteOffs(state, result, frame);
    }

    emitStepHits(state, result, frame, localStep, samplesPerStep, nframes, sampleRate);
}

}  // namespace

void activate(EngineState& state, const Controls& controls) {
    state.pendingNoteOffs.fill({});
    resetTransportState(state);
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    if (!state.patternValid) {
        regeneratePattern(state.pattern, state.controls, false);
        state.patternValid = true;
        resetVariationProgress(state.variation);
    }
}

void deactivate(EngineState& state) {
    resetTransportState(state);
    state.pendingNoteOffs.fill({});
}

BlockResult processBlock(EngineState& state,
                         const Controls& controls,
                         const TransportSnapshot& transport,
                         std::uint32_t nframes,
                         double sampleRate) {
    BlockResult result;
    if (nframes == 0 || sampleRate <= 0.0) {
        return result;
    }

    updatePatternIfNeeded(state, clampControls(controls));
    processPendingNoteOffs(state, result, nframes);

    const bool playing = transport.valid && transport.playing && transport.bpm > 0.0 && transport.beatsPerBar > 0.0;
    if (!playing || !state.patternValid) {
        handleStoppedTransport(state, result);
        return result;
    }

    const int stepsPerBeat = state.pattern.stepsPerBeat;
    const double absBeatsStart = transport.bar * transport.beatsPerBar + transport.barBeat;
    const double absBeatsStep = (static_cast<double>(nframes) * transport.bpm) / (60.0 * sampleRate);
    const double absStepsStart = absBeatsStart * static_cast<double>(stepsPerBeat);
    const double absStepsEnd = (absBeatsStart + absBeatsStep) * static_cast<double>(stepsPerBeat);
    const double samplesPerStep = sampleRate * 60.0 / (transport.bpm * static_cast<double>(stepsPerBeat));
    const std::int64_t startStepFloor = static_cast<std::int64_t>(std::floor(absStepsStart + 1e-9));

    if (transportRestartDetected(state.wasPlaying, state.lastTransportStep, startStepFloor)) {
        handleTransportRestart(state, result, absStepsStart, samplesPerStep, nframes, sampleRate);
    }

    state.wasPlaying = true;
    state.lastTransportStep = startStepFloor;

    std::int64_t boundary = static_cast<std::int64_t>(std::floor(absStepsStart)) + 1;
    const std::int64_t boundaryEnd = static_cast<std::int64_t>(std::floor(absStepsEnd + 1e-9));

    while (boundary <= boundaryEnd) {
        processBoundary(state, result, nframes, absStepsStart, absStepsEnd, boundary, samplesPerStep, sampleRate);
        boundary += 1;
    }

    return result;
}

}  // namespace downspout::drumgen
