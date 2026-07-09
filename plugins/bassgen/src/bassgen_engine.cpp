#include "bassgen_engine.hpp"

#include "bassgen_pattern.hpp"
#include "bassgen_transport.hpp"
#include "bassgen_variation.hpp"

#include <cmath>
#include <cstdint>

namespace downspout::bassgen {
namespace {

constexpr int kMusicalGuideChannel = 1;

[[nodiscard]] int clampi(int value, int minValue, int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue) {
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

void emitNoteOff(BlockResult& result, const EngineState& state, std::uint32_t frame, int note) {
    if (note < 0) {
        return;
    }
    appendMidi(result, MidiEventType::noteOff, frame, state.controls.channel, note, 0);
}

void emitNoteOn(BlockResult& result, const EngineState& state, std::uint32_t frame, int note, int velocity) {
    appendMidi(result, MidiEventType::noteOn, frame, state.controls.channel, note, velocity);
}

void clearActiveNote(EngineState& state, BlockResult& result, std::uint32_t frame) {
    if (state.activeNote >= 0) {
        emitNoteOff(result, state, frame, state.activeNote);
        state.activeNote = -1;
    }
    state.injectedNoteEndBoundary = -1;
}

void syncNoteStateToPosition(EngineState& state, BlockResult& result, double localStepPos) {
    const NoteEvent* event = state.patternValid ? findActiveEvent(state.pattern, localStepPos) : nullptr;
    const int shouldNote = event ? event->note : -1;
    if (state.activeNote >= 0 && state.activeNote != shouldNote) {
        emitNoteOff(result, state, 0, state.activeNote);
        state.activeNote = -1;
    }
    if (shouldNote >= 0 && state.activeNote < 0) {
        emitNoteOn(result, state, 0, shouldNote, event->velocity);
        state.activeNote = shouldNote;
    }
}

void resetTransportState(EngineState& state) {
    state.wasPlaying = false;
    state.lastTransportStep = -1;
    state.inputTriggerPending = false;
    state.inputTriggerVelocity = 0;
    state.injectedNoteEndBoundary = -1;
    state.inputTriggerMusicalGuide = false;
}

void handleStoppedTransport(EngineState& state, BlockResult& result) {
    clearActiveNote(state, result, 0);
    resetTransportState(state);
}

void handleTransportRestart(EngineState& state, BlockResult& result, double absStepsStart) {
    clearActiveNote(state, result, 0);
    const double localStep = localStepFromAbsolute(state.pattern, absStepsStart);
    syncNoteStateToPosition(state, result, localStep);
}

[[nodiscard]] ::downspout::Meter resolvedMeterFor(const EngineState& state, const TransportSnapshot& transport) {
    if (transport.valid && transport.beatsPerBar > 0.0) {
        return ::downspout::sanitizeMeter(transport.meter);
    }
    if (state.pattern.stepsPerBar > 0) {
        return ::downspout::sanitizeMeter(state.pattern.meter);
    }
    return ::downspout::Meter {};
}

struct PatternUpdateResult {
    bool regenerated = false;
};

PatternUpdateResult updatePatternIfNeeded(EngineState& state,
                                          const Controls& fresh,
                                          const ::downspout::Meter& targetMeter) {
    PatternUpdateResult result;
    const bool paramsChanged = structuralControlsChanged(fresh, state.previousControls);
    const bool triggerNew = fresh.actionNew != state.previousControls.actionNew;
    const bool triggerNotes = fresh.actionNotes != state.previousControls.actionNotes;
    const bool triggerRhythm = fresh.actionRhythm != state.previousControls.actionRhythm;
    const bool varyJustEnabled = state.previousControls.vary <= 0.0001f && fresh.vary > 0.0001f;
    const bool meterChanged = state.patternValid && !::downspout::metersEqual(state.pattern.meter, targetMeter);

    state.controls = fresh;

    if (!state.patternValid || paramsChanged || triggerNew || meterChanged) {
        regeneratePattern(state.pattern, state.controls, targetMeter, true, true);
        state.patternValid = true;
        resetVariationProgress(state.variation);
        result.regenerated = true;
    } else if (triggerRhythm) {
        regeneratePattern(state.pattern, state.controls, targetMeter, true, false);
        resetVariationProgress(state.variation);
        result.regenerated = true;
    } else if (triggerNotes) {
        regeneratePattern(state.pattern, state.controls, targetMeter, false, true);
        resetVariationProgress(state.variation);
        result.regenerated = true;
    } else if (varyJustEnabled) {
        resetVariationProgress(state.variation);
    }

    state.previousControls = fresh;
    return result;
}

[[nodiscard]] bool isNoteOn(const InputMidiEvent& event) {
    return event.size >= 3 && (event.data[0] & 0xf0u) == 0x90u && event.data[2] > 0;
}

[[nodiscard]] int midiChannel(const InputMidiEvent& event) {
    return static_cast<int>(event.data[0] & 0x0fu) + 1;
}

[[nodiscard]] int midiNote(const InputMidiEvent& event) {
    return static_cast<int>(event.data[1]);
}

[[nodiscard]] bool inputMatchesListenTarget(const Controls& controls, const InputMidiEvent& event) {
    if (!isNoteOn(event)) {
        return false;
    }
    const int channel = midiChannel(event);
    const int note = midiNote(event);
    switch (controls.inputMatchMode) {
    case InputMatchModeId::exact:
        return channel == controls.listenChannel && note == controls.listenNote;
    case InputMatchModeId::channel:
        return channel == controls.listenChannel;
    case InputMatchModeId::any:
        return true;
    case InputMatchModeId::count:
        break;
    }
    return false;
}

[[nodiscard]] int nearestPitchClassNote(const int pitchClass, const int referenceNote) {
    int best = clampi((referenceNote / 12) * 12 + pitchClass, 0, 127);
    int bestDistance = std::abs(best - referenceNote);
    for (int octave = -2; octave <= 2; ++octave) {
        const int candidate = clampi(best + octave * 12, 0, 127);
        const int distance = std::abs(candidate - referenceNote);
        if (distance < bestDistance || (distance == bestDistance && candidate < best)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

[[nodiscard]] int musicalGuideTargetNote(const EngineState& state, const int generatedNote, const int localStep) {
    const int guideNote = clampi(state.inputTriggerNote, 0, 127);
    const int guidePitchClass = guideNote % 12;
    const int direct = nearestPitchClassNote(guidePitchClass, generatedNote);

    const bool strongStep = state.pattern.stepsPerBeat > 0 && (localStep % state.pattern.stepsPerBeat) == 0;
    if (strongStep) {
        return direct;
    }

    const int fifth = nearestPitchClassNote((guidePitchClass + 7) % 12, generatedNote);
    const int octaveBelow = clampi(direct > generatedNote + 5 ? direct - 12 : direct, 0, 127);
    const std::uint32_t choice =
        (state.controls.seed ^
         (static_cast<std::uint32_t>(localStep + 257) * 3266489917u) ^
         (static_cast<std::uint32_t>(guideNote + 129) * 668265263u)) %
        4u;
    if (choice == 0u) {
        return fifth;
    }
    if (choice == 1u) {
        return octaveBelow;
    }
    return direct;
}

void consumeInputMidi(EngineState& state, const InputMidiEvent& event) {
    if (isNoteOn(event) && midiChannel(event) == kMusicalGuideChannel) {
        state.inputTriggerPending = true;
        state.inputTriggerVelocity = clampi(static_cast<int>(event.data[2]), 1, 127);
        state.inputTriggerNote = clampi(midiNote(event), 0, 127);
        state.inputTriggerMusicalGuide = true;
        return;
    }

    if (!inputMatchesListenTarget(state.controls, event)) {
        return;
    }
    state.inputTriggerPending = true;
    state.inputTriggerVelocity = clampi(static_cast<int>(event.data[2]), 1, 127);
    state.inputTriggerNote = clampi(midiNote(event), 0, 127);
    state.inputTriggerMusicalGuide = false;
}

void consumeInputUntil(EngineState& state,
                       const InputMidiEvent* midiEvents,
                       std::uint32_t midiEventCount,
                       std::uint32_t& inputIndex,
                       std::uint32_t frame) {
    if (midiEvents == nullptr) {
        return;
    }
    while (inputIndex < midiEventCount && midiEvents[inputIndex].frame <= frame) {
        consumeInputMidi(state, midiEvents[inputIndex]);
        ++inputIndex;
    }
}

[[nodiscard]] std::uint32_t responseHash(const EngineState& state, const int localStep) {
    std::uint32_t value = state.controls.seed;
    value ^= static_cast<std::uint32_t>(state.pattern.generationSerial + 31) * 2246822519u;
    value ^= static_cast<std::uint32_t>(localStep + 257) * 3266489917u;
    value ^= static_cast<std::uint32_t>(state.controls.listenNote + 129) * 668265263u;
    value ^= value >> 15;
    value *= 2246822519u;
    value ^= value >> 13;
    return value;
}

[[nodiscard]] bool chancePasses(const EngineState& state, const int localStep, const float probability) {
    if (probability >= 0.999f) {
        return true;
    }
    if (probability <= 0.001f) {
        return false;
    }
    return (responseHash(state, localStep) % 10000u) < static_cast<std::uint32_t>(probability * 10000.0f);
}

[[nodiscard]] NoteEvent noteForFollowStep(const EngineState& state, const int localStep) {
    const NoteEvent* closest = nullptr;
    int bestDistance = kMaxPatternSteps + 1;
    for (int index = 0; index < state.pattern.eventCount; ++index) {
        const NoteEvent& event = state.pattern.events[index];
        int distance = std::abs(event.startStep - localStep);
        if (state.pattern.patternSteps > 0) {
            distance = std::min(distance, state.pattern.patternSteps - distance);
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            closest = &event;
        }
    }

    NoteEvent generated;
    generated.startStep = localStep;
    generated.durationSteps = std::max(1, static_cast<int>(std::lround(
        static_cast<float>(std::max(1, state.pattern.stepsPerBeat)) * std::max(0.1f, state.controls.hold))));
    if (closest) {
        generated.note = closest->note;
        generated.velocity = closest->velocity;
    } else {
        generated.note = clampi(state.controls.rootNote + registerOffset(state.controls.reg), 0, 127);
        generated.velocity = 96;
    }
    if (state.inputTriggerNote >= 0 && state.controls.inputMatchMode != InputMatchModeId::exact) {
        const int guideTarget = state.inputTriggerMusicalGuide
            ? musicalGuideTargetNote(state, generated.note, localStep)
            : (std::abs(generated.note - clampi(state.inputTriggerNote + 12, 0, 127)) <=
                       std::abs(generated.note - clampi(state.inputTriggerNote + 7, 0, 127))
                   ? clampi(state.inputTriggerNote + 12, 0, 127)
                   : clampi(state.inputTriggerNote + 7, 0, 127));
        const float sensitivity = clampf(state.controls.inputSensitivity, 0.0f, 1.0f);
        generated.note = clampi(static_cast<int>(std::lround(static_cast<float>(generated.note) * (1.0f - sensitivity) +
                                                             static_cast<float>(guideTarget) * sensitivity)),
                                0,
                                127);
    } else if (state.inputTriggerMusicalGuide && state.inputTriggerNote >= 0) {
        const int guideTarget = musicalGuideTargetNote(state, generated.note, localStep);
        const float sensitivity = clampf(state.controls.inputSensitivity, 0.0f, 1.0f);
        generated.note = clampi(static_cast<int>(std::lround(static_cast<float>(generated.note) * (1.0f - sensitivity) +
                                                             static_cast<float>(guideTarget) * sensitivity)),
                                0,
                                127);
    }
    if (state.inputTriggerVelocity > 0) {
        generated.velocity = clampi((generated.velocity + state.inputTriggerVelocity) / 2, 1, 127);
    }
    return generated;
}

void processBoundary(EngineState& state,
                     BlockResult& result,
                     std::uint32_t nframes,
                     double absStepsStart,
                     double absStepsEnd,
                     std::int64_t boundary,
                     const ::downspout::Meter& meter,
                     double beatsPerBar) {
    const std::uint32_t frame = frameForBoundary(absStepsStart, absStepsEnd, nframes, boundary);
    const int localStep = localStepForBoundary(state.pattern, boundary);
    const bool matchedInput = state.inputTriggerPending;
    state.inputTriggerPending = false;

    if (state.injectedNoteEndBoundary >= 0 && boundary >= state.injectedNoteEndBoundary) {
        clearActiveNote(state, result, frame);
    }

    if (localStep == 0 && applyLoopVariation(state.pattern, state.variation, state.controls, meter, beatsPerBar)) {
        clearActiveNote(state, result, frame);
    }

    if (anyEventEndsAt(state.pattern, localStep)) {
        clearActiveNote(state, result, frame);
    }

    const NoteEvent* startEvent = findEventStartingAt(state.pattern, localStep);
    const float responseAmount = std::fabs(state.controls.followDodge) * clampf(state.controls.inputSensitivity, 0.0f, 1.0f);
    if (startEvent) {
        const std::uint32_t onFrame = static_cast<std::uint32_t>(
            clampi(static_cast<int>(frame) + kSafetyGapSamples, 0, static_cast<int>(nframes) - 1));
        const bool dodge = matchedInput && state.controls.followDodge < -0.001f &&
            chancePasses(state, localStep, responseAmount);
        clearActiveNote(state, result, frame);
        if (dodge) {
            return;
        }
        const int velocity = matchedInput && state.controls.followDodge > 0.001f
            ? clampi(startEvent->velocity + static_cast<int>(std::lround(responseAmount * 16.0f)), 1, 127)
            : startEvent->velocity;
        emitNoteOn(result, state, onFrame, startEvent->note, velocity);
        state.activeNote = startEvent->note;
    } else if (matchedInput && state.controls.followDodge > 0.001f &&
               chancePasses(state, localStep, responseAmount)) {
        const NoteEvent followEvent = noteForFollowStep(state, localStep);
        const std::uint32_t onFrame = static_cast<std::uint32_t>(
            clampi(static_cast<int>(frame) + kSafetyGapSamples, 0, static_cast<int>(nframes) - 1));
        clearActiveNote(state, result, frame);
        emitNoteOn(result, state, onFrame, followEvent.note, followEvent.velocity);
        state.activeNote = followEvent.note;
        state.injectedNoteEndBoundary = boundary + std::max<std::int64_t>(1, followEvent.durationSteps);
    }
}

}  // namespace

void activate(EngineState& state, const Controls& controls) {
    state.activeNote = -1;
    resetTransportState(state);
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    if (!state.patternValid) {
        regeneratePattern(state.pattern, state.controls, ::downspout::Meter {}, true, true);
        state.patternValid = true;
        resetVariationProgress(state.variation);
    }
}

void deactivate(EngineState& state) {
    state.activeNote = -1;
    resetTransportState(state);
}

BlockResult processBlock(EngineState& state,
                         const Controls& controls,
                         const TransportSnapshot& transport,
                         std::uint32_t nframes,
                         double sampleRate) {
    return processBlock(state, controls, transport, nframes, sampleRate, nullptr, 0);
}

BlockResult processBlock(EngineState& state,
                         const Controls& controls,
                         const TransportSnapshot& transport,
                         std::uint32_t nframes,
                         double sampleRate,
                         const InputMidiEvent* midiEvents,
                         std::uint32_t midiEventCount) {
    BlockResult result;
    if (nframes == 0) {
        return result;
    }

    const Controls freshControls = clampControls(controls);
    const ::downspout::Meter targetMeter = resolvedMeterFor(state, transport);
    const PatternUpdateResult patternUpdate = updatePatternIfNeeded(state, freshControls, targetMeter);
    if (std::fabs(state.controls.followDodge) <= 0.001f) {
        state.inputTriggerPending = false;
        state.inputTriggerVelocity = 0;
        state.inputTriggerNote = -1;
        state.inputTriggerMusicalGuide = false;
        midiEvents = nullptr;
        midiEventCount = 0;
    }

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
    const std::int64_t startStepFloor = static_cast<std::int64_t>(std::floor(absStepsStart + 1e-9));
    std::uint32_t inputIndex = 0;

    const bool restart = transportRestartDetected(state.wasPlaying, state.lastTransportStep, startStepFloor);
    if (restart) {
        consumeInputUntil(state, midiEvents, midiEventCount, inputIndex, 0);
        handleTransportRestart(state, result, absStepsStart);
    } else if (patternUpdate.regenerated) {
        clearActiveNote(state, result, 0);
        const double localStep = localStepFromAbsolute(state.pattern, absStepsStart);
        syncNoteStateToPosition(state, result, localStep);
    }

    state.wasPlaying = true;
    state.lastTransportStep = startStepFloor;

    std::int64_t boundary = static_cast<std::int64_t>(std::floor(absStepsStart)) + 1;
    const std::int64_t boundaryEnd = static_cast<std::int64_t>(std::floor(absStepsEnd + 1e-9));

    while (boundary <= boundaryEnd) {
        const std::uint32_t frame = frameForBoundary(absStepsStart, absStepsEnd, nframes, boundary);
        consumeInputUntil(state, midiEvents, midiEventCount, inputIndex, frame);
        processBoundary(state, result, nframes, absStepsStart, absStepsEnd, boundary, targetMeter, transport.beatsPerBar);
        boundary += 1;
    }
    consumeInputUntil(state, midiEvents, midiEventCount, inputIndex, nframes - 1);

    return result;
}

}  // namespace downspout::bassgen
