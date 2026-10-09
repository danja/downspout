#include "xoxolo_engine.hpp"
#include "xoxolo_automaton.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::xoxolo {
namespace {

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] int stepsPerBeatForResolution(const ResolutionId resolution)
{
    switch (resolution) {
    case ResolutionId::quarter: return 1;
    case ResolutionId::eighth: return 2;
    case ResolutionId::sixteenth: return 4;
    case ResolutionId::count: break;
    }
    return 4;
}

[[nodiscard]] ResolutionId clampResolution(const ResolutionId resolution)
{
    const int raw = static_cast<int>(resolution);
    if (raw < 0)
        return ResolutionId::quarter;
    if (raw >= static_cast<int>(ResolutionId::count))
        return ResolutionId::sixteenth;
    return resolution;
}

[[nodiscard]] int stepsPerBarForMeter(const ::downspout::Meter& meter, const ResolutionId resolution)
{
    const ::downspout::Meter sanitized = ::downspout::sanitizeMeter(meter);
    const int beatsPerBar = clampi(sanitized.numerator, 1, 16);
    return beatsPerBar * stepsPerBeatForResolution(resolution);
}

[[nodiscard]] double localStepFromAbsolute(const PatternState& pattern, const double absSteps)
{
    const double total = static_cast<double>(std::max(1, pattern.totalSteps));
    const double local = std::fmod(absSteps, total);
    return local < 0.0 ? local + total : local;
}

[[nodiscard]] int localStepForBoundary(const PatternState& pattern, const std::int64_t boundary)
{
    const int total = std::max(1, pattern.totalSteps);
    int local = static_cast<int>(boundary % total);
    if (local < 0)
        local += total;
    return local;
}

[[nodiscard]] std::uint32_t frameForBoundary(const double absStepsStart,
                                             const double absStepsEnd,
                                             const std::uint32_t nframes,
                                             const std::int64_t boundary)
{
    const double t = (static_cast<double>(boundary) - absStepsStart) /
                     std::max(1e-12, absStepsEnd - absStepsStart);
    return static_cast<std::uint32_t>(clampi(static_cast<int>(std::floor(t * static_cast<double>(nframes))),
                                             0,
                                             static_cast<int>(nframes) - 1));
}

void appendMidi(BlockResult& result,
                const MidiEventType type,
                const std::uint32_t frame,
                const int channel,
                const int note,
                const int velocity)
{
    if (result.eventCount >= kMaxScheduledMidiEvents)
        return;

    ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(result.eventCount++)];
    event.type = type;
    event.frame = frame;
    event.channel = static_cast<std::uint8_t>(clampi(channel - 1, 0, 15));
    event.data1 = static_cast<std::uint8_t>(clampi(note, 0, 127));
    event.data2 = static_cast<std::uint8_t>(clampi(velocity, 0, 127));
}

void enqueueNoteOff(EngineState& state, const int note, const int channel, const int remainingSamples)
{
    for (PendingNoteOff& pending : state.pendingNoteOffs) {
        if (pending.active)
            continue;
        pending.active = true;
        pending.note = note;
        pending.channel = channel;
        pending.remainingSamples = std::max(0, remainingSamples);
        return;
    }
}

// A repeatable pseudo-random number in [0, 1) for (step, lane): splitmix64 on the pair.
[[nodiscard]] float thinningUnit(const std::int64_t step, const int lane)
{
    std::uint64_t z = static_cast<std::uint64_t>(step) * 0x9e3779b97f4a7c15ull + static_cast<std::uint64_t>(lane) * 0xbf58476d1ce4e5b9ull + 0x94d049bb133111ebull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    return static_cast<float>(static_cast<double>(z >> 11) / 9007199254740992.0);
}

// Energy 1 is the programmed velocity of 100; lower values play softer, down to 40 %.
[[nodiscard]] int velocityForEnergy(const float energy)
{
    return clampi(static_cast<int>(std::lround(100.0 * (0.4 + 0.6 * static_cast<double>(energy)))), 1, 127);
}

void emitNotePair(EngineState& state,
                  BlockResult& result,
                  const std::uint32_t frame,
                  const int note,
                  const int velocity,
                  const int channel,
                  const std::uint32_t nframes,
                  const double sampleRate)
{
    appendMidi(result, MidiEventType::noteOn, frame, channel, note, velocity);
    const int gateSamples = clampi(static_cast<int>(std::lround(sampleRate * 0.03)), 8, 4096);
    const int offFrame = static_cast<int>(frame) + gateSamples;
    if (offFrame < static_cast<int>(nframes)) {
        appendMidi(result, MidiEventType::noteOff, static_cast<std::uint32_t>(offFrame), channel, note, 0);
    } else {
        enqueueNoteOff(state, note, channel, offFrame - static_cast<int>(nframes));
    }
}

void processPendingNoteOffs(EngineState& state, BlockResult& result, const std::uint32_t nframes)
{
    for (PendingNoteOff& pending : state.pendingNoteOffs) {
        if (!pending.active)
            continue;
        if (pending.remainingSamples < static_cast<int>(nframes)) {
            appendMidi(result,
                       MidiEventType::noteOff,
                       static_cast<std::uint32_t>(clampi(pending.remainingSamples, 0, static_cast<int>(nframes) - 1)),
                       pending.channel,
                       pending.note,
                       0);
            pending.active = false;
        } else {
            pending.remainingSamples -= static_cast<int>(nframes);
        }
    }
}

void clearPendingNoteOffs(EngineState& state, BlockResult& result, const std::uint32_t frame)
{
    for (PendingNoteOff& pending : state.pendingNoteOffs) {
        if (!pending.active)
            continue;
        appendMidi(result, MidiEventType::noteOff, frame, pending.channel, pending.note, 0);
        pending.active = false;
    }
}

void emitStep(EngineState& state,
              BlockResult& result,
              const std::uint32_t frame,
              const int localStep,
              const std::int64_t pass,
              const std::int64_t absoluteStep,
              const std::uint32_t nframes,
              const double sampleRate)
{
    if (localStep < 0 || localStep >= state.pattern.totalSteps)
        return;

    const int laneCount = activeLaneCountForPreset(state.pattern.notePreset);
    for (int lane = 0; lane < laneCount; ++lane) {
        // A lane marked "evolve" plays the automaton's generation for this pass instead of its grid.
        if (!caStepActive(state.pattern.lanes[static_cast<std::size_t>(lane)], state.pattern.totalSteps,
                          state.controls.caRule, state.controls.caEvery, pass, localStep, state.controls.caShift))
            continue;
        // Density thins the hits, seeded by the absolute step and lane so a render is repeatable.
        if (state.controls.density < 1.0f && thinningUnit(absoluteStep, lane) > state.controls.density)
            continue;
        emitNotePair(state,
                     result,
                     frame,
                     state.pattern.lanes[static_cast<std::size_t>(lane)].midiNote,
                     velocityForEnergy(state.controls.energy),
                     state.controls.channel,
                     nframes,
                     sampleRate);
    }
}

void handlePreview(EngineState& state,
                   BlockResult& result,
                   const Controls& controls,
                   const std::uint32_t nframes,
                   const double sampleRate)
{
    if (controls.previewSerial == state.previousPreviewSerial)
        return;
    state.previousPreviewSerial = controls.previewSerial;

    const int lane = clampi(controls.previewLane, 0, kLaneCount - 1);
    emitNotePair(state,
                 result,
                 0,
                 state.pattern.lanes[static_cast<std::size_t>(lane)].midiNote,
                 112,
                 10,
                 nframes,
                 sampleRate);
}

}  // namespace

NotePresetId clampNotePreset(const NotePresetId preset)
{
    const int raw = static_cast<int>(preset);
    if (raw < 0 || raw >= static_cast<int>(NotePresetId::count))
        return NotePresetId::downspout;
    return preset;
}

const std::array<LaneSpec, kLaneCount>& laneSpecsForPreset(const NotePresetId preset)
{
    switch (clampNotePreset(preset)) {
    case NotePresetId::downspout: return kDownspoutLanes;
    case NotePresetId::avlDrumkits: return kAvlDrumkitsLanes;
    case NotePresetId::count: break;
    }
    return kDownspoutLanes;
}

int activeLaneCountForPreset(const NotePresetId preset)
{
    switch (clampNotePreset(preset)) {
    case NotePresetId::downspout: return kDownspoutLaneCount;
    case NotePresetId::avlDrumkits: return kAvlDrumkitsLaneCount;
    case NotePresetId::count: break;
    }
    return kDownspoutLaneCount;
}

const char* notePresetName(const NotePresetId preset)
{
    switch (clampNotePreset(preset)) {
    case NotePresetId::downspout: return "Downspout";
    case NotePresetId::avlDrumkits: return "AVL-Drumkits";
    case NotePresetId::count: break;
    }
    return "Downspout";
}

Controls clampControls(const Controls& controls)
{
    Controls result = controls;
    result.steps = clampi(result.steps, kMinSteps, kMaxSteps);
    result.resolution = clampResolution(result.resolution);
    result.channel = clampi(result.channel, 1, 16);
    result.notePreset = clampNotePreset(result.notePreset);
    result.previewLane = clampi(result.previewLane, 0, activeLaneCountForPreset(result.notePreset) - 1);
    result.caRule = clampi(result.caRule, 0, kCaRuleCount - 1);
    result.caEvery = clampi(result.caEvery, kMinCaEvery, kMaxCaEvery);
    result.conductorCh = clampi(result.conductorCh, 0, 16);
    result.density = std::isfinite(result.density) ? std::clamp(result.density, 0.0f, 1.0f) : 1.0f;
    result.energy = std::isfinite(result.energy) ? std::clamp(result.energy, 0.0f, 1.0f) : 1.0f;
    result.caShift = clampi(result.caShift, 0, kCaGenerations - 1);
    return result;
}

PatternState makeDefaultPattern()
{
    PatternState pattern {};
    pattern.notePreset = NotePresetId::downspout;
    applyNotePreset(pattern, pattern.notePreset);
    sanitizePattern(pattern);
    return pattern;
}

void sanitizePattern(PatternState& pattern)
{
    pattern.bars = clampi(pattern.bars, kMinBars, kMaxBars);
    pattern.resolution = clampResolution(pattern.resolution);
    pattern.channel = clampi(pattern.channel, 1, 16);
    pattern.notePreset = clampNotePreset(pattern.notePreset);
    for (LaneState& lane : pattern.lanes)
        lane.evolve = lane.evolve != 0 ? 1 : 0;
    pattern.stepsPerBeat = stepsPerBeatForResolution(pattern.resolution);
    pattern.meter = ::downspout::sanitizeMeter(pattern.meter);
    pattern.stepsPerBar = clampi(stepsPerBarForMeter(pattern.meter, pattern.resolution), 1, kMaxSteps);
    pattern.totalSteps = clampi(pattern.totalSteps, kMinSteps, kMaxSteps);
    pattern.bars = clampi((pattern.totalSteps + pattern.stepsPerBar - 1) / std::max(1, pattern.stepsPerBar),
                          kMinBars,
                          kMaxBars);
    pattern.version = kPatternStateVersion;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        LaneState& laneState = pattern.lanes[static_cast<std::size_t>(lane)];
        laneState.midiNote = clampi(laneState.midiNote, 0, 127);
        for (int step = 0; step < kMaxSteps; ++step)
            laneState.steps[static_cast<std::size_t>(step)] = laneState.steps[static_cast<std::size_t>(step)] != 0 ? 1 : 0;
    }
}

void applyNotePreset(PatternState& pattern, const NotePresetId preset)
{
    pattern.notePreset = clampNotePreset(preset);
    const auto& lanes = laneSpecsForPreset(pattern.notePreset);
    for (int lane = 0; lane < kLaneCount; ++lane)
        pattern.lanes[static_cast<std::size_t>(lane)].midiNote = lanes[static_cast<std::size_t>(lane)].note;
    sanitizePattern(pattern);
}

void resizePattern(PatternState& pattern, const int steps, const ResolutionId resolution, const ::downspout::Meter& meter)
{
    pattern.totalSteps = clampi(steps, kMinSteps, kMaxSteps);
    pattern.resolution = clampResolution(resolution);
    pattern.meter = ::downspout::sanitizeMeter(meter);
    sanitizePattern(pattern);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = std::max(0, pattern.totalSteps); step < kMaxSteps; ++step)
            pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)] = 0;
    }
}

void setCell(PatternState& pattern, const int lane, const int step, const bool active)
{
    sanitizePattern(pattern);
    if (lane < 0 || lane >= kLaneCount || step < 0 || step >= pattern.totalSteps)
        return;
    pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)] = active ? 1 : 0;
}

bool cellActive(const PatternState& pattern, const int lane, const int step)
{
    if (lane < 0 || lane >= kLaneCount || step < 0 || step >= pattern.totalSteps)
        return false;
    return pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)] != 0;
}

void clearPattern(PatternState& pattern)
{
    for (LaneState& lane : pattern.lanes)
        lane.steps.fill(0);
}

void handleMidi(EngineState& state, Controls& controls, const MidiInputEvent* events, const std::uint32_t count)
{
    if (events == nullptr || controls.conductorCh <= 0)
        return;
    for (std::uint32_t i = 0; i < count; ++i) {
        const MidiInputEvent& e = events[i];
        if (e.size < 3 || (e.data[0] & 0xf0) != 0xb0 || (e.data[0] & 0x0f) + 1 != controls.conductorCh)
            continue;
        const int value = e.data[2] & 0x7f;
        const float unit = static_cast<float>(value) / 127.0f;
        switch (e.data[1]) {
        case 21: controls.density = unit; break;
        case 22: controls.energy = unit; break;
        case 23: controls.caShift = clampi(static_cast<int>(std::lround(unit * (kCaGenerations - 1))), 0, kCaGenerations - 1); break;
        case 24:
            if (value == 127)
                state.restartPending = true;
            break;
        default: break;
        }
    }
}

void activate(EngineState& state, const Controls& controls)
{
    state.controls = clampControls(controls);
    state.pendingNoteOffs.fill({});
    state.wasPlaying = false;
    state.lastTransportStep = -1;
    state.currentStep = -1;
    state.restartPending = false;
    state.restartStep = 0;
    state.lastBar = -1;
    state.previousClearSerial = state.controls.clearSerial;
    state.previousPreviewSerial = state.controls.previewSerial;
    sanitizePattern(state.pattern);
}

void deactivate(EngineState& state)
{
    state.pendingNoteOffs.fill({});
    state.wasPlaying = false;
    state.lastTransportStep = -1;
    state.currentStep = -1;
}

BlockResult processBlock(EngineState& state,
                         const Controls& rawControls,
                         const TransportSnapshot& transport,
                         const std::uint32_t nframes,
                         const double sampleRate)
{
    BlockResult result {};
    if (nframes == 0 || sampleRate <= 0.0)
        return result;

    Controls controls = clampControls(rawControls);
    state.controls = controls;
    state.pattern.channel = controls.channel;
    state.pattern.notePreset = controls.notePreset;
    const ::downspout::Meter targetMeter = transport.valid
        ? ::downspout::sanitizeMeter(transport.meter)
        : ::downspout::sanitizeMeter(state.pattern.meter);
    resizePattern(state.pattern, controls.steps, controls.resolution, targetMeter);

    processPendingNoteOffs(state, result, nframes);

    if (controls.clearSerial != state.previousClearSerial) {
        state.previousClearSerial = controls.clearSerial;
        clearPattern(state.pattern);
    }

    const bool playing = transport.valid && transport.playing && transport.bpm > 0.0 && transport.beatsPerBar > 0.0;
    if (!playing && state.wasPlaying)
        clearPendingNoteOffs(state, result, 0);

    handlePreview(state, result, controls, nframes, sampleRate);

    if (!playing) {
        state.wasPlaying = false;
        state.lastTransportStep = -1;
        state.currentStep = -1;
        state.restartPending = false;
        state.restartStep = 0;
        state.lastBar = -1;
        result.currentStep = -1;
        return result;
    }

    const double absBeatsStart = transport.bar * transport.beatsPerBar + transport.barBeat;
    const double blockBeats = (static_cast<double>(nframes) * transport.bpm) / (60.0 * sampleRate);
    const double absStepsStart = absBeatsStart * static_cast<double>(state.pattern.stepsPerBeat);
    const double absStepsEnd = (absBeatsStart + blockBeats) * static_cast<double>(state.pattern.stepsPerBeat);
    const std::int64_t startFloor = static_cast<std::int64_t>(std::floor(absStepsStart + 1e-9));
    const bool restarted = !state.wasPlaying || (state.lastTransportStep >= 0 && startFloor < state.lastTransportStep);

    if (restarted) {
        // A new start or a loop jump: the pattern is counted from the host position again.
        state.restartPending = false;
        state.restartStep = 0;
        state.lastBar = -1;
        clearPendingNoteOffs(state, result, 0);
        const double local = localStepFromAbsolute(state.pattern, absStepsStart);
        const double frac = local - std::floor(local);
        if (frac < 1e-6 || frac > 1.0 - 1e-6)
            emitStep(state, result, 0, static_cast<int>(std::floor(local + 1e-6)),
                     caPassForStep(static_cast<std::int64_t>(std::floor(absStepsStart + 1e-6)), state.pattern.totalSteps),
                     static_cast<std::int64_t>(std::floor(absStepsStart + 1e-6)), nframes, sampleRate);
    }

    state.wasPlaying = true;
    state.lastTransportStep = startFloor;
    state.currentStep = static_cast<int>(std::floor(
        localStepFromAbsolute(state.pattern, absStepsStart - static_cast<double>(state.restartStep))));
    result.currentStep = state.currentStep;

    std::int64_t boundary = static_cast<std::int64_t>(std::floor(absStepsStart)) + 1;
    const std::int64_t boundaryEnd = static_cast<std::int64_t>(std::floor(absStepsEnd + 1e-9));
    while (boundary <= boundaryEnd) {
        // A Conductor reset waits for a bar line, then the pattern and the evolve generation count from there.
        const std::int64_t stepsPerBar = std::max(1, state.pattern.stepsPerBar);
        std::int64_t bar = boundary / stepsPerBar;
        if (boundary % stepsPerBar != 0 && boundary < 0)
            --bar;
        if (bar != state.lastBar) {
            state.lastBar = bar;
            if (state.restartPending) {
                state.restartStep = boundary;
                state.restartPending = false;
            }
        }
        const std::int64_t relative = boundary - state.restartStep;
        emitStep(state,
                 result,
                 frameForBoundary(absStepsStart, absStepsEnd, nframes, boundary),
                 localStepForBoundary(state.pattern, relative),
                 caPassForStep(relative, state.pattern.totalSteps),
                 boundary,
                 nframes,
                 sampleRate);
        ++boundary;
    }

    return result;
}

}  // namespace downspout::xoxolo
