#include "ground_engine.hpp"

#include "ground_pattern.hpp"
#include "ground_variation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace downspout::ground {
namespace {

struct UpdateDecision {
    bool forceResync = false;
};

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue)
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] bool eventActiveAt(const NoteEvent& event, const double localStep)
{
    const double start = static_cast<double>(event.startStep);
    const double end = static_cast<double>(event.startStep + event.durationSteps);
    return localStep >= start && localStep < end;
}

[[nodiscard]] double localStepFromAbsolute(const FormState& form, const double absSteps)
{
    const double localStep = std::fmod(absSteps, static_cast<double>(form.patternSteps));
    return localStep < 0.0 ? localStep + static_cast<double>(form.patternSteps) : localStep;
}

[[nodiscard]] const NoteEvent* findActiveEvent(const FormState& form, const double localStep)
{
    for (int i = 0; i < form.eventCount; ++i) {
        if (eventActiveAt(form.events[static_cast<std::size_t>(i)], localStep)) {
            return &form.events[static_cast<std::size_t>(i)];
        }
    }
    return nullptr;
}

[[nodiscard]] const NoteEvent* findEventStartingAt(const FormState& form, const int localStep)
{
    for (int i = 0; i < form.eventCount; ++i) {
        if (form.events[static_cast<std::size_t>(i)].startStep == localStep) {
            return &form.events[static_cast<std::size_t>(i)];
        }
    }
    return nullptr;
}

[[nodiscard]] bool anyEventEndsAt(const FormState& form, const int localStep)
{
    for (int i = 0; i < form.eventCount; ++i) {
        const NoteEvent& event = form.events[static_cast<std::size_t>(i)];
        const int endStep = (event.startStep + event.durationSteps) % form.patternSteps;
        if (event.durationSteps < form.patternSteps && endStep == localStep) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::uint32_t frameForBoundary(const double absStepsStart,
                                             const double absStepsEnd,
                                             const std::uint32_t nframes,
                                             const std::int64_t boundary)
{
    const double relSteps = static_cast<double>(boundary) - absStepsStart;
    const double t = relSteps / (absStepsEnd - absStepsStart + 1e-12);
    return static_cast<std::uint32_t>(
        clampi(static_cast<int>(std::floor(t * static_cast<double>(nframes))), 0, static_cast<int>(nframes) - 1));
}

[[nodiscard]] int localStepForBoundary(const FormState& form, const std::int64_t boundary)
{
    const double localStep = std::fmod(static_cast<double>(boundary), static_cast<double>(form.patternSteps));
    return static_cast<int>(localStep < 0.0 ? localStep + static_cast<double>(form.patternSteps) : localStep);
}

[[nodiscard]] bool transportRestartDetected(const bool wasPlaying,
                                            const std::int64_t lastTransportStep,
                                            const std::int64_t startStepFloor)
{
    return !wasPlaying || (lastTransportStep >= 0 && startStepFloor < lastTransportStep);
}

[[nodiscard]] int phraseIndexForLocalStep(const FormState& form, const double localStep)
{
    const int stepsPerPhrase = std::max(1, form.phraseBars * 16);
    const int phraseIndex = static_cast<int>(std::floor(localStep + 1e-9)) / stepsPerPhrase;
    return clampi(phraseIndex, 0, std::max(0, form.phraseCount - 1));
}

[[nodiscard]] PhraseRoleId roleForPhraseIndex(const FormState& form, const int phraseIndex)
{
    if (phraseIndex < 0 || phraseIndex >= form.phraseCount) {
        return PhraseRoleId::statement;
    }
    return form.phrases[static_cast<std::size_t>(phraseIndex)].role;
}

void appendMidi(BlockResult& result,
                const MidiEventType type,
                const std::uint32_t frame,
                const int channel,
                const int data1,
                const int data2)
{
    if (result.eventCount >= kMaxScheduledMidiEvents) {
        return;
    }

    ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(result.eventCount++)];
    event.type = type;
    event.frame = frame;
    event.channel = static_cast<std::uint8_t>(clampi(channel - 1, 0, 15));
    event.data1 = static_cast<std::uint8_t>(clampi(data1, 0, 127));
    event.data2 = static_cast<std::uint8_t>(clampi(data2, 0, 127));
}

void emitNoteOff(BlockResult& result, const EngineState& state, const std::uint32_t frame, const int note)
{
    if (note < 0) {
        return;
    }
    appendMidi(result, MidiEventType::noteOff, frame, state.controls.channel, note, 0);
}

void emitNoteOn(BlockResult& result,
                const EngineState& state,
                const std::uint32_t frame,
                const int note,
                const int velocity)
{
    appendMidi(result, MidiEventType::noteOn, frame, state.controls.channel, note, velocity);
}

void clearActiveNote(EngineState& state, BlockResult& result, const std::uint32_t frame)
{
    if (state.activeNote >= 0) {
        emitNoteOff(result, state, frame, state.activeNote);
        state.activeNote = -1;
    }
}

void updatePhraseStatus(EngineState& state, BlockResult& result, const double localStep)
{
    state.currentPhraseIndex = state.formValid ? phraseIndexForLocalStep(state.form, localStep) : 0;
    state.currentRole = state.formValid ? roleForPhraseIndex(state.form, state.currentPhraseIndex)
                                        : PhraseRoleId::statement;
    result.currentPhraseIndex = state.currentPhraseIndex;
    result.currentRole = state.currentRole;
}

void syncNoteStateToPosition(EngineState& state, BlockResult& result, const double localStep)
{
    updatePhraseStatus(state, result, localStep);
    const NoteEvent* event = state.formValid ? findActiveEvent(state.form, localStep) : nullptr;
    const int shouldNote = event != nullptr ? event->note : -1;

    if (state.activeNote >= 0 && state.activeNote != shouldNote) {
        emitNoteOff(result, state, 0, state.activeNote);
        state.activeNote = -1;
    }

    if (shouldNote >= 0 && state.activeNote < 0) {
        emitNoteOn(result, state, 0, shouldNote, event->velocity);
        state.activeNote = shouldNote;
    }
}

void resetTransportState(EngineState& state)
{
    state.wasPlaying = false;
    state.lastTransportStep = -1;
}

void handleStoppedTransport(EngineState& state, BlockResult& result)
{
    clearActiveNote(state, result, 0);
    resetTransportState(state);
    result.currentPhraseIndex = state.currentPhraseIndex;
    result.currentRole = state.currentRole;
}

void handleTransportRestart(EngineState& state, BlockResult& result, const double absStepsStart)
{
    clearActiveNote(state, result, 0);
    syncNoteStateToPosition(state, result, localStepFromAbsolute(state.form, absStepsStart));
}

UpdateDecision updateFormIfNeeded(EngineState& state,
                                  const Controls& freshControls,
                                  const int targetPhraseIndex)
{
    UpdateDecision decision;
    const bool structureChanged = !structureControlsMatch(freshControls, state.previousControls);
    const bool newFormTriggered = freshControls.actionNewForm != state.previousControls.actionNewForm;
    const bool newPhraseTriggered = freshControls.actionNewPhrase != state.previousControls.actionNewPhrase;
    const bool mutateTriggered = freshControls.actionMutateCell != state.previousControls.actionMutateCell;
    const bool varyJustEnabled = state.previousControls.vary <= 0.0001f && freshControls.vary > 0.0001f;

    state.controls = freshControls;

    if (!state.formValid || structureChanged || newFormTriggered) {
        regenerateForm(state.form, freshControls);
        state.formValid = true;
        resetVariationProgress(state.variation);
        decision.forceResync = true;
    } else if (newPhraseTriggered) {
        refreshPhrase(state.form, freshControls, targetPhraseIndex);
        resetVariationProgress(state.variation);
        decision.forceResync = true;
    } else if (mutateTriggered) {
        mutatePhraseCell(state.form, freshControls, targetPhraseIndex, 0.70f);
        resetVariationProgress(state.variation);
        decision.forceResync = true;
    } else if (varyJustEnabled) {
        resetVariationProgress(state.variation);
    }

    state.previousControls = freshControls;
    return decision;
}

void processBoundary(EngineState& state,
                     BlockResult& result,
                     const std::uint32_t nframes,
                     const double absStepsStart,
                     const double absStepsEnd,
                     const std::int64_t boundary,
                     const double beatsPerBar)
{
    const std::uint32_t frame = frameForBoundary(absStepsStart, absStepsEnd, nframes, boundary);
    const int localStep = localStepForBoundary(state.form, boundary);

    if (localStep == 0 &&
        applyLoopVariation(state.form, state.variation, state.controls, beatsPerBar, state.currentPhraseIndex)) {
        clearActiveNote(state, result, frame);
    }

    updatePhraseStatus(state, result, static_cast<double>(localStep));

    if (anyEventEndsAt(state.form, localStep)) {
        clearActiveNote(state, result, frame);
    }

    const NoteEvent* startEvent = findEventStartingAt(state.form, localStep);
    if (startEvent != nullptr) {
        const std::uint32_t onFrame = static_cast<std::uint32_t>(
            clampi(static_cast<int>(frame) + kSafetyGapSamples, 0, static_cast<int>(nframes) - 1));
        clearActiveNote(state, result, frame);
        emitNoteOn(result, state, onFrame, startEvent->note, startEvent->velocity);
        state.activeNote = startEvent->note;
    }
}

}  // namespace

void activate(EngineState& state, const Controls& controls)
{
    state.activeNote = -1;
    resetTransportState(state);
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    if (!state.formValid) {
        regenerateForm(state.form, state.controls);
        state.formValid = true;
        resetVariationProgress(state.variation);
    }
    state.currentPhraseIndex = 0;
    state.currentRole = roleForPhraseIndex(state.form, 0);
}

void deactivate(EngineState& state)
{
    state.activeNote = -1;
    resetTransportState(state);
}

BlockResult processBlock(EngineState& state,
                         const Controls& controls,
                         const TransportSnapshot& transport,
                         const std::uint32_t nframes,
                         const double sampleRate)
{
    BlockResult result;
    result.currentPhraseIndex = state.currentPhraseIndex;
    result.currentRole = state.currentRole;

    if (nframes == 0 || sampleRate <= 0.0) {
        return result;
    }

    const Controls freshControls = clampControls(controls);

    int targetPhraseIndex = state.currentPhraseIndex;
    const bool transportUsable = transport.valid && transport.bpm > 0.0 && transport.beatsPerBar > 0.0;
    if (state.formValid && transportUsable) {
        const double absBeatsStart = transport.bar * transport.beatsPerBar + transport.barBeat;
        const double absStepsStart = absBeatsStart * static_cast<double>(state.form.stepsPerBeat);
        targetPhraseIndex = phraseIndexForLocalStep(state.form, localStepFromAbsolute(state.form, absStepsStart));
    }

    const UpdateDecision updateDecision = updateFormIfNeeded(state, freshControls, targetPhraseIndex);
    result.currentPhraseIndex = state.currentPhraseIndex;
    result.currentRole = state.currentRole;

    const bool playing = transport.valid && transport.playing && transport.bpm > 0.0 && transport.beatsPerBar > 0.0;
    if (!playing || !state.formValid) {
        handleStoppedTransport(state, result);
        return result;
    }

    const double absBeatsStart = transport.bar * transport.beatsPerBar + transport.barBeat;
    const double absBeatsStep = (static_cast<double>(nframes) * transport.bpm) / (60.0 * sampleRate);
    const double absStepsStart = absBeatsStart * static_cast<double>(state.form.stepsPerBeat);
    const double absStepsEnd = (absBeatsStart + absBeatsStep) * static_cast<double>(state.form.stepsPerBeat);
    const double localStepStart = localStepFromAbsolute(state.form, absStepsStart);
    const std::int64_t startStepFloor = static_cast<std::int64_t>(std::floor(absStepsStart + 1e-9));

    if (transportRestartDetected(state.wasPlaying, state.lastTransportStep, startStepFloor)) {
        handleTransportRestart(state, result, absStepsStart);
    } else {
        updatePhraseStatus(state, result, localStepStart);
        if (updateDecision.forceResync) {
            syncNoteStateToPosition(state, result, localStepStart);
        }
    }

    state.wasPlaying = true;
    state.lastTransportStep = startStepFloor;

    std::int64_t boundary = static_cast<std::int64_t>(std::floor(absStepsStart)) + 1;
    const std::int64_t boundaryEnd = static_cast<std::int64_t>(std::floor(absStepsEnd + 1e-9));

    while (boundary <= boundaryEnd) {
        processBoundary(state, result, nframes, absStepsStart, absStepsEnd, boundary, transport.beatsPerBar);
        boundary += 1;
    }

    return result;
}

}  // namespace downspout::ground
