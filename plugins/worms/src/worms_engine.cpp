#include "worms_engine.hpp"

#include "worms_pattern.hpp"
#include "worms_tonnetz.hpp"

#include "downspout/meter.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::worms {
namespace {

inline int clampi(int v, int lo, int hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void appendEvent(BlockResult& result,
                 MidiEventType type,
                 std::uint32_t frame,
                 int channel,
                 int note,
                 int velocity) noexcept
{
    if (result.eventCount >= kMaxScheduledEvents) return;
    auto& ev   = result.events[result.eventCount++];
    ev.type    = type;
    ev.frame   = frame;
    ev.channel = static_cast<std::uint8_t>(clampi(channel - 1, 0, 15));
    ev.note    = static_cast<std::uint8_t>(clampi(note, 0, 127));
    ev.velocity = static_cast<std::uint8_t>(clampi(velocity, 0, 127));
}

void emitNoteOff(BlockResult& result, const Controls& controls, std::uint32_t frame, int note)
{
    if (note < 0) return;
    appendEvent(result, MidiEventType::noteOff, frame, controls.midiCh, note, 0);
}

void emitNoteOn(BlockResult& result, const Controls& controls, std::uint32_t frame, int note, int velocity)
{
    appendEvent(result, MidiEventType::noteOn, frame, controls.midiCh, note, velocity);
}

void clearActiveNote(EngineState& state, BlockResult& result, std::uint32_t frame)
{
    if (state.activeNote >= 0) {
        emitNoteOff(result, state.controls, frame, state.activeNote);
        state.activeNote = -1;
    }
}

[[nodiscard]] std::uint32_t frameForBoundary(double absStepsStart,
                                              double absStepsEnd,
                                              std::uint32_t nframes,
                                              std::int64_t boundary) noexcept
{
    const double range = absStepsEnd - absStepsStart;
    if (range <= 0.0 || nframes == 0) return 0;
    const double frac = (static_cast<double>(boundary) - absStepsStart) / range;
    const long long raw = std::llround(frac * static_cast<double>(nframes));
    return static_cast<std::uint32_t>(
        std::clamp(raw, 0LL, static_cast<long long>(nframes - 1)));
}

[[nodiscard]] bool transportRestartDetected(bool wasPlaying,
                                             std::int64_t lastStep,
                                             std::int64_t currentStep) noexcept
{
    return !wasPlaying || (lastStep >= 0 && currentStep < lastStep);
}

::downspout::Meter resolvedMeter(const EngineState& state, const TransportSnapshot& transport)
{
    if (transport.valid && transport.beatsPerBar > 0.0)
        return ::downspout::sanitizeMeter(transport.meter);
    if (state.pattern.stepsPerBar > 0)
        return ::downspout::sanitizeMeter(state.pattern.meter);
    return ::downspout::Meter {};
}

// At a step boundary: emit note-off for previous note, note-on for new note
void processBoundary(EngineState& state,
                     BlockResult& result,
                     std::uint32_t nframes,
                     double absStepsStart,
                     double absStepsEnd,
                     std::int64_t boundary,
                     int completedLoops)
{
    const double localStep = localStepFromAbsolute(state.pattern, static_cast<double>(boundary));
    const std::uint32_t frame = frameForBoundary(absStepsStart, absStepsEnd, nframes, boundary);

    // Check loop wrap: increment completedLoops counter on wrap
    (void)completedLoops;

    // Emit note-off for previous active note (with safety gap)
    if (state.activeNote >= 0) {
        const std::uint32_t offFrame = frame > static_cast<std::uint32_t>(kSafetyGapSamples)
            ? frame - static_cast<std::uint32_t>(kSafetyGapSamples)
            : 0;
        emitNoteOff(result, state.controls, offFrame, state.activeNote);
        state.activeNote = -1;
    }

    // Find what note (if any) should play at this boundary
    const NoteEvent* event = findActiveEvent(state.pattern, localStep);
    if (event) {
        emitNoteOn(result, state.controls, frame, event->note, event->velocity);
        state.activeNote = event->note;
    }
}

bool controlsStructurallyChanged(const Controls& a, const Controls& b)
{
    return a.root     != b.root
        || a.reg      != b.reg
        || a.stepSize != b.stepSize
        || a.patLen   != b.patLen
        || a.quantize != b.quantize
        || a.scale    != b.scale
        || a.rule.turn != b.rule.turn;
}

}  // namespace

Controls clampControls(const Controls& c)
{
    Controls out = c;
    out.root      = clampi(c.root, 0, 11);
    out.reg       = clampi(c.reg, 0, 4);
    out.stepSize  = clampi(c.stepSize, 0, 3);
    out.patLen    = clampi(c.patLen, 0, 3);
    out.density   = std::clamp(c.density, 0.0f, 1.0f);
    out.velocity  = std::clamp(c.velocity, 0.0f, 1.0f);
    out.vary      = std::clamp(c.vary, 0.0f, 1.0f);
    out.seed      = std::clamp(c.seed, 0.0f, 1.0f);
    out.condCh    = clampi(c.condCh, 0, 16);
    out.scale     = clampi(c.scale, 0, static_cast<int>(ScaleId::count) - 1);
    out.midiCh    = clampi(c.midiCh, 1, 16);
    for (int i = 0; i < 6; ++i)
        out.rule.turn[i] = clampi(c.rule.turn[i], 0, 4);
    out.actionRandomize = c.actionRandomize;
    out.actionMutate    = c.actionMutate;
    return out;
}

void activate(EngineState& state, const Controls& controls)
{
    state.activeNote       = -1;
    state.lastTransportStep = -1;
    state.wasPlaying       = false;
    state.controls         = clampControls(controls);
    state.previousControls = state.controls;
    if (!state.patternValid) {
        generatePattern(state.pattern, state.controls, ::downspout::Meter {});
        state.patternValid = true;
    }
}

void deactivate(EngineState& state, BlockResult& result)
{
    clearActiveNote(state, result, 0);
    state.wasPlaying        = false;
    state.lastTransportStep = -1;
}

BlockResult processBlock(EngineState& state,
                         const Controls& controls,
                         const TransportSnapshot& transport,
                         std::uint32_t nframes,
                         double sampleRate)
{
    BlockResult result;
    if (nframes == 0) return result;

    const Controls fresh  = clampControls(controls);
    const ::downspout::Meter targetMeter = resolvedMeter(state, transport);

    // Handle action triggers
    const bool triggerRandomize = fresh.actionRandomize != state.previousControls.actionRandomize;
    const bool triggerMutate    = fresh.actionMutate    != state.previousControls.actionMutate;
    const bool structChanged    = controlsStructurallyChanged(fresh, state.controls);

    if (triggerRandomize) {
        Controls mutable_fresh = fresh;
        const std::uint64_t s = static_cast<std::uint64_t>(fresh.seed * 65535.0f);
        randomizeRules(mutable_fresh.rule, s, state.pattern.generationSerial);
        state.controls = mutable_fresh;
        generatePattern(state.pattern, state.controls, targetMeter);
        state.patternValid = true;
    } else if (triggerMutate) {
        Controls mutable_fresh = fresh;
        const std::uint64_t s = static_cast<std::uint64_t>(fresh.seed * 65535.0f);
        mutateRule(mutable_fresh.rule, s, state.pattern.generationSerial);
        state.controls = mutable_fresh;
        generatePattern(state.pattern, state.controls, targetMeter);
        state.patternValid = true;
    } else if (!state.patternValid || structChanged) {
        state.controls = fresh;
        generatePattern(state.pattern, state.controls, targetMeter);
        state.patternValid = true;
    } else {
        state.controls = fresh;
    }
    state.previousControls = fresh;

    const bool playing = transport.valid && transport.playing
                      && transport.bpm > 0.0 && transport.beatsPerBar > 0.0;
    if (!playing || !state.patternValid) {
        clearActiveNote(state, result, 0);
        state.wasPlaying        = false;
        state.lastTransportStep = -1;
        return result;
    }

    const int stepsPerBeat = state.pattern.stepsPerBeat;
    const double absBeatsStart  = transport.bar * transport.beatsPerBar + transport.barBeat;
    const double absBeatsStep   = (static_cast<double>(nframes) * transport.bpm)
                                / (60.0 * std::max(sampleRate, 1.0));
    const double absStepsStart  = absBeatsStart * static_cast<double>(stepsPerBeat);
    const double absStepsEnd    = (absBeatsStart + absBeatsStep) * static_cast<double>(stepsPerBeat);
    const std::int64_t startStepFloor = static_cast<std::int64_t>(std::floor(absStepsStart + 1e-9));

    const bool restart = transportRestartDetected(state.wasPlaying, state.lastTransportStep, startStepFloor);
    if (restart) {
        clearActiveNote(state, result, 0);
    }

    state.wasPlaying        = true;
    state.lastTransportStep = startStepFloor;

    // Iterate step boundaries within this block
    std::int64_t boundary    = static_cast<std::int64_t>(std::floor(absStepsStart)) + 1;
    const std::int64_t boundaryEnd = static_cast<std::int64_t>(std::floor(absStepsEnd + 1e-9));

    while (boundary <= boundaryEnd) {
        processBoundary(state, result, nframes, absStepsStart, absStepsEnd, boundary,
                        static_cast<int>(static_cast<std::int64_t>(boundary)
                            / std::max(1, state.pattern.patternSteps)));
        ++boundary;
    }

    return result;
}

}  // namespace downspout::worms
