#include "cadence_engine.hpp"

#include "cadence_harmony.hpp"
#include "cadence_transport.hpp"
#include "cadence_variation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace downspout::cadence {
namespace {

void appendMidi(BlockResult& result,
                const std::uint32_t frame,
                const std::uint8_t size,
                const std::uint8_t b0,
                const std::uint8_t b1 = 0,
                const std::uint8_t b2 = 0,
                const std::uint8_t b3 = 0)
{
    if (result.eventCount >= kMaxScheduledMidiEvents)
        return;

    ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(result.eventCount++)];
    event.frame = frame;
    event.size = size;
    event.data[0] = b0;
    event.data[1] = b1;
    event.data[2] = b2;
    event.data[3] = b3;
}

void appendMidi(BlockResult& result, const InputMidiEvent& event)
{
    if (result.eventCount >= kMaxScheduledMidiEvents)
        return;

    result.events[static_cast<std::size_t>(result.eventCount++)] = event;
}

int resolve_output_channel(const EngineState& state, const int configuredChannel)
{
    if (configuredChannel == 0)
        return clampi(state.lastInputChannel, 1, 16);
    return clampi(configuredChannel, 1, 16);
}

void emit_note_on(BlockResult& result, const std::uint32_t frame, const int note, const int velocity, const int outputChannel)
{
    const std::uint8_t channel = static_cast<std::uint8_t>(clampi(outputChannel, 1, 16) - 1);
    appendMidi(result,
               frame,
               3,
               static_cast<std::uint8_t>(0x90 | channel),
               static_cast<std::uint8_t>(clampi(note, 0, 127)),
               static_cast<std::uint8_t>(clampi(velocity, 1, 127)));
}

void emit_note_off(BlockResult& result, const std::uint32_t frame, const int note, const int outputChannel)
{
    const std::uint8_t channel = static_cast<std::uint8_t>(clampi(outputChannel, 1, 16) - 1);
    appendMidi(result,
               frame,
               3,
               static_cast<std::uint8_t>(0x80 | channel),
               static_cast<std::uint8_t>(clampi(note, 0, 127)),
               0);
}

void emit_cc(BlockResult& result, const std::uint32_t frame, const int controller, const int value, const int outputChannel)
{
    const std::uint8_t channel = static_cast<std::uint8_t>(clampi(outputChannel, 1, 16) - 1);
    appendMidi(result,
               frame,
               3,
               static_cast<std::uint8_t>(0xB0 | channel),
               static_cast<std::uint8_t>(clampi(controller, 0, 127)),
               static_cast<std::uint8_t>(clampi(value, 0, 127)));
}

void clear_pending_harmony_off(EngineState& state)
{
    state.harmonyOffPending = false;
    state.pendingHarmonyOffBeat = 0.0;
    cadence_clear_comp_release(&state.compState);
}

void schedule_harmony_off_at(EngineState& state, const double absOffBeat)
{
    clear_pending_harmony_off(state);
    if (state.activeHarmonyCount == 0 || absOffBeat <= 0.0)
        return;

    if (cadence_note_length_fraction(state.controls) >= 0.995)
        return;

    state.harmonyOffPending = true;
    state.pendingHarmonyOffBeat = absOffBeat;
    cadence_set_comp_release(&state.compState, absOffBeat);
}

void clear_held_notes(EngineState& state)
{
    state.heldNotes.fill(false);
    state.heldVelocity.fill(0);
}

bool note_in_set(const std::array<std::uint8_t, kMaxChordNotes>& notes,
                 const std::uint8_t count,
                 const std::uint8_t note)
{
    for (std::uint8_t i = 0; i < count; ++i)
    {
        if (notes[static_cast<std::size_t>(i)] == note)
            return true;
    }
    return false;
}

void transition_to_slot(EngineState& state,
                        BlockResult& result,
                        const std::uint32_t frame,
                        const ChordSlot* slot,
                        const bool retrigger)
{
    const std::uint8_t nextCount = slot != nullptr && slot->valid ? slot->note_count : 0;
    const int nextChannel = resolve_output_channel(state, state.controls.output_channel);

    if (retrigger && slot != nullptr && slot->valid)
    {
        for (std::uint8_t i = 0; i < state.activeHarmonyCount; ++i)
            emit_note_off(result, frame, state.activeHarmonyNotes[static_cast<std::size_t>(i)], state.activeHarmonyChannel);

        for (std::uint8_t i = 0; i < slot->note_count; ++i)
            emit_note_on(result, frame, slot->notes[static_cast<std::size_t>(i)], slot->velocity, nextChannel);

        state.activeHarmonyCount = slot->note_count;
        state.activeHarmonyChannel = nextChannel;
        for (std::uint8_t i = 0; i < slot->note_count; ++i)
            state.activeHarmonyNotes[static_cast<std::size_t>(i)] = slot->notes[static_cast<std::size_t>(i)];
        return;
    }

    for (std::uint8_t i = 0; i < state.activeHarmonyCount; ++i)
    {
        const std::uint8_t note = state.activeHarmonyNotes[static_cast<std::size_t>(i)];
        if (!(slot != nullptr && slot->valid && note_in_set(slot->notes, nextCount, note)))
            emit_note_off(result, frame, note, state.activeHarmonyChannel);
    }

    if (slot != nullptr && slot->valid)
    {
        for (std::uint8_t i = 0; i < slot->note_count; ++i)
        {
            const std::uint8_t note = slot->notes[static_cast<std::size_t>(i)];
            if (!note_in_set(state.activeHarmonyNotes, state.activeHarmonyCount, note))
                emit_note_on(result, frame, note, slot->velocity, nextChannel);
        }
        state.activeHarmonyChannel = nextChannel;
    }

    state.activeHarmonyCount = nextCount;
    for (std::uint8_t i = 0; i < nextCount; ++i)
        state.activeHarmonyNotes[static_cast<std::size_t>(i)] = slot->notes[static_cast<std::size_t>(i)];
}

void silence_harmony(EngineState& state, BlockResult& result, const std::uint32_t frame)
{
    clear_pending_harmony_off(state);
    transition_to_slot(state, result, frame, nullptr, false);
}

void panic_harmony(EngineState& state, BlockResult& result, const std::uint32_t frame)
{
    for (std::uint8_t i = 0; i < state.activeHarmonyCount; ++i)
        emit_note_off(result, frame, state.activeHarmonyNotes[static_cast<std::size_t>(i)], state.activeHarmonyChannel);

    const int currentChannel = clampi(state.activeHarmonyChannel, 1, 16);
    emit_cc(result, frame, 123, 0, currentChannel);
    emit_cc(result, frame, 120, 0, currentChannel);

    const int configuredChannel = resolve_output_channel(state, state.controls.output_channel);
    if (configuredChannel != currentChannel)
    {
        emit_cc(result, frame, 123, 0, configuredChannel);
        emit_cc(result, frame, 120, 0, configuredChannel);
    }

    state.activeHarmonyCount = 0;
    clear_pending_harmony_off(state);
}

void clear_learning_state(EngineState& state)
{
    cadence_clear_capture(state.capture.data(), kMaxSegments);
    cadence_clear_capture(state.learnedCapture.data(), kMaxSegments);
    state.learnedSegmentCount = 0;
    state.haveLearnedCapture = false;

    cadence_clear_progression(state.baseProgression.data(), kMaxSegments);
    state.baseSegmentCount = 0;
    cadence_clear_progression(state.playback.data(), kMaxSegments);
    state.playbackSegmentCount = 0;
    state.ready = false;

    clear_held_notes(state);
    clear_pending_harmony_off(state);
    state.activeHarmonyCount = 0;
    state.activeHarmonyChannel = 1;
    state.lastInputChannel = 1;
    cadence_reset_comp_playback(&state.compState);
    cadence_reset_variation_progress(&state.variation);
}

void adopt_base_progression(EngineState& state)
{
    cadence_clear_progression(state.playback.data(), kMaxSegments);
    if (state.baseSegmentCount > 0)
        cadence_copy_progression(state.playback.data(), state.baseProgression.data(), state.baseSegmentCount);
    state.playbackSegmentCount = state.baseSegmentCount;
}

void plan_current_segment_comp(EngineState& state,
                               const int segmentIndex,
                               const double segmentStartBeat,
                               const double segmentBeats)
{
    if (segmentIndex < 0 || segmentIndex >= state.playbackSegmentCount)
    {
        cadence_reset_comp_playback(&state.compState);
        return;
    }

    const SegmentCapture* learnedSegment =
        state.haveLearnedCapture && segmentIndex < state.learnedSegmentCount
            ? &state.learnedCapture[static_cast<std::size_t>(segmentIndex)]
            : nullptr;
    const ChordSlot* slot = &state.playback[static_cast<std::size_t>(segmentIndex)];
    const std::uint32_t seed = static_cast<std::uint32_t>(state.variation.completed_cycles & 0xffffffffu) ^
                               (static_cast<std::uint32_t>(state.variation.mutation_serial & 0x7fffffffu) * 2246822519u) ^
                               (static_cast<std::uint32_t>(segmentIndex) * 3266489917u) ^
                               (static_cast<std::uint32_t>(slot->root_pc) << 10) ^
                               (static_cast<std::uint32_t>(slot->quality) << 18) ^
                               static_cast<std::uint32_t>(std::lround(state.controls.comp * 1000.0f));

    cadence_plan_segment_comp(learnedSegment,
                              state.controls,
                              slot,
                              segmentIndex,
                              segmentStartBeat,
                              segmentBeats,
                              seed,
                              &state.compState);
}

bool update_base_from_capture(EngineState& state, const int segmentCount)
{
    std::array<ChordSlot, kMaxSegments> built {};
    const CadenceBuildOptions baseOptions {};
    if (!cadence_build_progression_from_capture(state.capture.data(),
                                                segmentCount,
                                                state.controls,
                                                nullptr,
                                                0,
                                                baseOptions,
                                                built.data()))
    {
        return false;
    }

    cadence_clear_progression(state.baseProgression.data(), kMaxSegments);
    cadence_copy_progression(state.baseProgression.data(), built.data(), segmentCount);
    state.baseSegmentCount = segmentCount;
    state.ready = true;

    cadence_clear_capture(state.learnedCapture.data(), kMaxSegments);
    cadence_copy_capture(state.learnedCapture.data(), state.capture.data(), segmentCount);
    state.learnedSegmentCount = segmentCount;
    state.haveLearnedCapture = true;
    return true;
}

void capture_interval(EngineState& state,
                      const double beatsPerBar,
                      const int segmentCount,
                      const double absStart,
                      const double absEnd)
{
    if (absEnd <= absStart + 1e-12 || segmentCount <= 0)
        return;

    const int segment = cadence_segment_index_for_time(state.controls,
                                                       beatsPerBar,
                                                       segmentCount,
                                                       absStart + kBeatEpsilon);
    for (int note = 0; note < 128; ++note)
    {
        if (!state.heldNotes[static_cast<std::size_t>(note)])
            continue;
        state.capture[static_cast<std::size_t>(segment)].duration[static_cast<std::size_t>(note % 12)] += absEnd - absStart;
    }
}

void capture_onset(EngineState& state,
                   const double beatsPerBar,
                   const int segmentCount,
                   const double absBeats,
                   const std::uint8_t note,
                   const std::uint8_t velocity)
{
    if (segmentCount <= 0)
        return;

    const int segment = cadence_segment_index_for_time(state.controls,
                                                       beatsPerBar,
                                                       segmentCount,
                                                       absBeats + kBeatEpsilon);
    const double segmentBeats = cadence_segment_beats_for_controls(state.controls, beatsPerBar);
    const double cyclePos = cadence_wrapped_cycle_position(absBeats, state.controls, beatsPerBar);
    const double segmentPos = std::fmod(cyclePos, segmentBeats);
    double bonus = 0.35 + (static_cast<double>(velocity) / 127.0) * 0.65;

    if (segmentPos < std::max(0.12, segmentBeats * 0.15))
        bonus *= 1.15;

    SegmentCapture& capture = state.capture[static_cast<std::size_t>(segment)];
    capture.onset[static_cast<std::size_t>(note % 12)] += bonus;
    cadence_capture_timing_onset(&capture, segmentPos, segmentBeats, bonus);
}

void handle_boundary(EngineState& state,
                     BlockResult& result,
                     const std::uint32_t frame,
                     const double absBoundaryBeat,
                     const double beatsPerBar,
                     const int segmentCount)
{
    const double segmentBeats = cadence_segment_beats_for_controls(state.controls, beatsPerBar);
    const std::int64_t boundaryIndex = static_cast<std::int64_t>(std::llround(absBoundaryBeat / segmentBeats));
    const bool cycleBoundary = segmentCount > 0 ? ((boundaryIndex % segmentCount) == 0) : false;

    bool baseUpdated = false;
    if (cycleBoundary)
    {
        baseUpdated = update_base_from_capture(state, segmentCount);
        cadence_clear_capture(state.capture.data(), kMaxSegments);

        if (state.ready && state.baseSegmentCount == segmentCount)
        {
            std::array<ChordSlot, kMaxSegments> varied {};
            const bool mutated = cadence_apply_cycle_variation(state.haveLearnedCapture ? state.learnedCapture.data() : nullptr,
                                                               state.learnedSegmentCount,
                                                               state.controls,
                                                               &state.variation,
                                                               state.baseProgression.data(),
                                                               state.baseSegmentCount,
                                                               state.playback.data(),
                                                               state.playbackSegmentCount,
                                                               varied.data());
            if (mutated)
            {
                cadence_clear_progression(state.playback.data(), kMaxSegments);
                cadence_copy_progression(state.playback.data(), varied.data(), state.baseSegmentCount);
                state.playbackSegmentCount = state.baseSegmentCount;
            }
            else if (baseUpdated || state.playbackSegmentCount != state.baseSegmentCount)
            {
                adopt_base_progression(state);
            }
        }
    }

    if (state.ready && state.playbackSegmentCount == segmentCount)
    {
        const int segment = cadence_segment_index_for_time(state.controls,
                                                           beatsPerBar,
                                                           segmentCount,
                                                           absBoundaryBeat + kBeatEpsilon);
        silence_harmony(state, result, frame);
        plan_current_segment_comp(state, segment, absBoundaryBeat, segmentBeats);
    }
    else
    {
        cadence_reset_comp_playback(&state.compState);
        silence_harmony(state, result, frame);
    }
}

bool is_note_message(const InputMidiEvent& event)
{
    if (event.size < 2)
        return false;
    const std::uint8_t type = event.data[0] & 0xf0;
    return (type == 0x80 || type == 0x90) && event.size >= 3;
}

void forward_input_event(const EngineState& state, BlockResult& result, const InputMidiEvent& event)
{
    if (state.controls.pass_input)
    {
        appendMidi(result, event);
    }
    else if (!is_note_message(event))
    {
        appendMidi(result, event);
    }
}

void sync_harmony_to_position(EngineState& state,
                              BlockResult& result,
                              const std::uint32_t frame,
                              const double absBeats,
                              const double beatsPerBar,
                              const int segmentCount)
{
    if (state.ready && state.playbackSegmentCount == segmentCount)
    {
        const int segment = cadence_segment_index_for_time(state.controls,
                                                           beatsPerBar,
                                                           segmentCount,
                                                           absBeats + kBeatEpsilon);
        const double segmentBeats = cadence_segment_beats_for_controls(state.controls, beatsPerBar);
        const double segmentStart = std::floor((absBeats + kBeatEpsilon) / segmentBeats) * segmentBeats;
        bool shouldSound = false;
        double offBeat = 0.0;

        plan_current_segment_comp(state, segment, segmentStart, segmentBeats);
        cadence_sync_comp_to_position(&state.compState, absBeats, &shouldSound, &offBeat);

        if (shouldSound)
        {
            transition_to_slot(state, result, frame, &state.playback[static_cast<std::size_t>(segment)], false);
            schedule_harmony_off_at(state, offBeat);
        }
        else
        {
            silence_harmony(state, result, frame);
        }
    }
    else
    {
        cadence_reset_comp_playback(&state.compState);
        silence_harmony(state, result, frame);
    }
}

void process_timeline_until(EngineState& state,
                            BlockResult& result,
                            const std::uint32_t nframes,
                            const double absBeatsStart,
                            const double absBeatsEnd,
                            double targetBeat,
                            const double beatsPerBar,
                            const int segmentCount,
                            double& cursorBeats,
                            std::int64_t& boundaryIndex,
                            double& nextBoundary)
{
    if (targetBeat < cursorBeats)
        targetBeat = cursorBeats;

    const double segmentBeats = cadence_segment_beats_for_controls(state.controls, beatsPerBar);

    while (true)
    {
        const bool boundaryDue = nextBoundary <= targetBeat + kBeatEpsilon;
        const double nextHitBeat = cadence_next_comp_hit_beat(&state.compState);
        const bool hitDue = nextHitBeat <= targetBeat + kBeatEpsilon;
        const bool offDue = state.harmonyOffPending && state.pendingHarmonyOffBeat <= targetBeat + kBeatEpsilon;
        if (!boundaryDue && !hitDue && !offDue)
            break;

        double markerBeat = targetBeat;
        bool processBoundary = false;
        bool processHit = false;

        if (boundaryDue &&
            (!hitDue || nextBoundary <= nextHitBeat + kBeatEpsilon) &&
            (!offDue || nextBoundary <= state.pendingHarmonyOffBeat + kBeatEpsilon))
        {
            markerBeat = nextBoundary;
            processBoundary = true;
        }
        else if (hitDue && (!offDue || nextHitBeat <= state.pendingHarmonyOffBeat + kBeatEpsilon))
        {
            markerBeat = nextHitBeat;
            processHit = true;
        }
        else
        {
            markerBeat = state.pendingHarmonyOffBeat;
        }

        if (markerBeat > cursorBeats + 1e-12)
            capture_interval(state, beatsPerBar, segmentCount, cursorBeats, markerBeat);

        const std::uint32_t frame = cadence_frame_for_beat(absBeatsStart, absBeatsEnd, nframes, markerBeat);
        if (processBoundary)
        {
            handle_boundary(state, result, frame, markerBeat, beatsPerBar, segmentCount);
            boundaryIndex += 1;
            nextBoundary = static_cast<double>(boundaryIndex) * segmentBeats;
        }
        else if (processHit)
        {
            ScheduledCompHit hit {};
            if (cadence_take_due_comp_hit(&state.compState, markerBeat, &hit) &&
                state.compState.segment_index >= 0 &&
                state.compState.segment_index < state.playbackSegmentCount)
            {
                ChordSlot slot = state.playback[static_cast<std::size_t>(state.compState.segment_index)];
                slot.velocity = hit.velocity;
                transition_to_slot(state, result, frame, &slot, true);
                schedule_harmony_off_at(state, hit.off_beat);
            }
        }
        else
        {
            silence_harmony(state, result, frame);
        }

        cursorBeats = markerBeat;
    }

    if (targetBeat > cursorBeats + 1e-12)
    {
        capture_interval(state, beatsPerBar, segmentCount, cursorBeats, targetBeat);
        cursorBeats = targetBeat;
    }
}

}  // namespace

void activate(EngineState& state)
{
    cadence_clear_capture(state.capture.data(), kMaxSegments);
    clear_held_notes(state);
    state.activeHarmonyCount = 0;
    state.activeHarmonyChannel = 1;
    state.lastInputChannel = 1;
    clear_pending_harmony_off(state);
    cadence_reset_comp_playback(&state.compState);
    state.wasPlaying = false;
    state.lastAbsBeatsStart = 0.0;
    state.controlsInitialized = false;
}

void deactivate(EngineState& state)
{
    clear_held_notes(state);
    state.activeHarmonyCount = 0;
    state.activeHarmonyChannel = 1;
    clear_pending_harmony_off(state);
    cadence_reset_comp_playback(&state.compState);
    state.wasPlaying = false;
}

BlockResult processBlock(EngineState& state,
                         const Controls& rawControls,
                         const TransportSnapshot& transport,
                         const std::uint32_t nframes,
                         const double sampleRate,
                         const InputMidiEvent* const midiEvents,
                         const std::uint32_t midiEventCount)
{
    BlockResult result {};
    state.controls = clampControls(rawControls);

    if (!state.controlsInitialized)
    {
        state.previousControls = state.controls;
        state.controlsInitialized = true;
    }

    const bool learnTriggered = state.controls.action_learn != state.previousControls.action_learn;
    const bool paramsChanged = !harmonyControlsMatch(state.controls, state.previousControls);
    const bool varyChanged = std::fabs(state.controls.vary - state.previousControls.vary) >= 0.0001f;
    const bool compChanged = std::fabs(state.controls.comp - state.previousControls.comp) >= 0.0001f;

    if (learnTriggered || paramsChanged)
    {
        silence_harmony(state, result, 0);
        clear_learning_state(state);
    }
    else if (varyChanged)
    {
        cadence_reset_variation_progress(&state.variation);
    }
    else if (compChanged)
    {
        silence_harmony(state, result, 0);
        cadence_reset_comp_playback(&state.compState);
    }
    state.previousControls = state.controls;

    if (!transport.valid || !transport.playing)
    {
        if (state.wasPlaying || state.activeHarmonyCount > 0 || state.harmonyOffPending)
            panic_harmony(state, result, 0);
        else
            silence_harmony(state, result, 0);

        clear_held_notes(state);
        state.wasPlaying = false;

        for (std::uint32_t i = 0; i < midiEventCount; ++i)
            forward_input_event(state, result, midiEvents[i]);

        result.ready = state.ready;
        return result;
    }

    const int segmentCount = cadence_segment_count_for_controls(state.controls, transport.beatsPerBar);
    const bool mismatch =
        (state.playbackSegmentCount > 0 && state.playbackSegmentCount != segmentCount) ||
        (state.baseSegmentCount > 0 && state.baseSegmentCount != segmentCount);
    if (mismatch)
    {
        silence_harmony(state, result, 0);
        cadence_clear_capture(state.capture.data(), kMaxSegments);
        cadence_clear_capture(state.learnedCapture.data(), kMaxSegments);
        cadence_clear_progression(state.baseProgression.data(), kMaxSegments);
        cadence_clear_progression(state.playback.data(), kMaxSegments);
        state.baseSegmentCount = 0;
        state.playbackSegmentCount = 0;
        state.ready = false;
        state.haveLearnedCapture = false;
        state.learnedSegmentCount = 0;
        cadence_reset_comp_playback(&state.compState);
        cadence_reset_variation_progress(&state.variation);
    }

    const double absBeatsStart = transport.bar * transport.beatsPerBar + transport.barBeat;
    const double absBeatsStep = (static_cast<double>(nframes) * transport.bpm) / (60.0 * sampleRate);
    const double absBeatsEnd = absBeatsStart + absBeatsStep;
    const double segmentBeats = cadence_segment_beats_for_controls(state.controls, transport.beatsPerBar);
    const bool restart = !state.wasPlaying || (absBeatsStart + kBeatEpsilon < state.lastAbsBeatsStart);
    const double boundaryPhase = std::fmod(absBeatsStart, segmentBeats);
    const bool onBoundaryAtStart = std::fabs(boundaryPhase) < kBeatEpsilon ||
                                   std::fabs(boundaryPhase - segmentBeats) < kBeatEpsilon;

    if (restart && !onBoundaryAtStart)
        sync_harmony_to_position(state, result, 0, absBeatsStart, transport.beatsPerBar, segmentCount);

    std::int64_t boundaryIndex = static_cast<std::int64_t>(std::ceil((absBeatsStart - kBeatEpsilon) / segmentBeats));
    double nextBoundary = static_cast<double>(boundaryIndex) * segmentBeats;
    double cursorBeats = absBeatsStart;

    for (std::uint32_t i = 0; i < midiEventCount; ++i)
    {
        const InputMidiEvent& event = midiEvents[i];
        if (event.size < 2)
            continue;

        if ((event.data[0] & 0xf0) != 0xf0)
            state.lastInputChannel = static_cast<int>((event.data[0] & 0x0f) + 1);

        const double eventBeats = absBeatsStart +
                                  (static_cast<double>(event.frame) / static_cast<double>(std::max(1u, nframes))) * absBeatsStep;
        process_timeline_until(state,
                               result,
                               nframes,
                               absBeatsStart,
                               absBeatsEnd,
                               eventBeats,
                               transport.beatsPerBar,
                               segmentCount,
                               cursorBeats,
                               boundaryIndex,
                               nextBoundary);

        forward_input_event(state, result, event);

        const std::uint8_t type = event.data[0] & 0xf0;
        if ((type == 0x90 || type == 0x80) && event.size >= 3)
        {
            const std::uint8_t note = event.data[1] & 0x7f;
            const std::uint8_t velocity = event.data[2] & 0x7f;
            const bool isNoteOn = type == 0x90 && velocity > 0;
            if (isNoteOn)
            {
                state.heldNotes[static_cast<std::size_t>(note)] = true;
                state.heldVelocity[static_cast<std::size_t>(note)] = velocity;
                capture_onset(state, transport.beatsPerBar, segmentCount, eventBeats, note, velocity);
            }
            else
            {
                state.heldNotes[static_cast<std::size_t>(note)] = false;
                state.heldVelocity[static_cast<std::size_t>(note)] = 0;
            }
        }
    }

    process_timeline_until(state,
                           result,
                           nframes,
                           absBeatsStart,
                           absBeatsEnd,
                           absBeatsEnd,
                           transport.beatsPerBar,
                           segmentCount,
                           cursorBeats,
                           boundaryIndex,
                           nextBoundary);

    state.wasPlaying = true;
    state.lastAbsBeatsStart = absBeatsStart;
    result.ready = state.ready;
    return result;
}

}  // namespace downspout::cadence
