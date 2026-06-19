#include "xoxolo_engine.hpp"

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

[[nodiscard]] int nominalStepsFor(int bars, ResolutionId resolution)
{
    return clampi(bars, kMinBars, kMaxBars) * 4 * stepsPerBeatForResolution(resolution);
}

[[nodiscard]] ResolutionId supportedResolutionForBars(const int bars, ResolutionId resolution)
{
    resolution = clampResolution(resolution);
    while (resolution != ResolutionId::quarter && nominalStepsFor(bars, resolution) > kMaxSteps) {
        resolution = static_cast<ResolutionId>(static_cast<int>(resolution) - 1);
    }
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
              const std::uint32_t nframes,
              const double sampleRate)
{
    if (localStep < 0 || localStep >= state.pattern.totalSteps)
        return;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        if (state.pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(localStep)] == 0)
            continue;
        emitNotePair(state,
                     result,
                     frame,
                     state.pattern.lanes[static_cast<std::size_t>(lane)].midiNote,
                     100,
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
                 controls.channel,
                 nframes,
                 sampleRate);
}

}  // namespace

Controls clampControls(const Controls& controls)
{
    Controls result = controls;
    result.bars = clampi(result.bars, kMinBars, kMaxBars);
    result.resolution = supportedResolutionForBars(result.bars, result.resolution);
    result.channel = clampi(result.channel, 1, 16);
    result.previewLane = clampi(result.previewLane, 0, kLaneCount - 1);
    return result;
}

PatternState makeDefaultPattern()
{
    PatternState pattern {};
    for (int lane = 0; lane < kLaneCount; ++lane)
        pattern.lanes[static_cast<std::size_t>(lane)].midiNote = kDefaultLanes[static_cast<std::size_t>(lane)].note;
    sanitizePattern(pattern);
    return pattern;
}

void sanitizePattern(PatternState& pattern)
{
    pattern.bars = clampi(pattern.bars, kMinBars, kMaxBars);
    pattern.resolution = supportedResolutionForBars(pattern.bars, pattern.resolution);
    pattern.channel = clampi(pattern.channel, 1, 16);
    pattern.stepsPerBeat = stepsPerBeatForResolution(pattern.resolution);
    pattern.meter = ::downspout::sanitizeMeter(pattern.meter);
    pattern.stepsPerBar = clampi(stepsPerBarForMeter(pattern.meter, pattern.resolution), 1, kMaxSteps);
    pattern.totalSteps = clampi(pattern.bars * pattern.stepsPerBar, 1, kMaxSteps);
    pattern.version = kPatternStateVersion;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        LaneState& laneState = pattern.lanes[static_cast<std::size_t>(lane)];
        laneState.midiNote = clampi(laneState.midiNote, 0, 127);
        for (int step = 0; step < kMaxSteps; ++step)
            laneState.steps[static_cast<std::size_t>(step)] = laneState.steps[static_cast<std::size_t>(step)] != 0 ? 1 : 0;
    }
}

void resizePattern(PatternState& pattern, const int bars, const ResolutionId resolution, const ::downspout::Meter& meter)
{
    pattern.bars = clampi(bars, kMinBars, kMaxBars);
    pattern.resolution = supportedResolutionForBars(pattern.bars, resolution);
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

void activate(EngineState& state, const Controls& controls)
{
    state.controls = clampControls(controls);
    state.pendingNoteOffs.fill({});
    state.wasPlaying = false;
    state.lastTransportStep = -1;
    state.currentStep = -1;
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
    const ::downspout::Meter targetMeter = transport.valid
        ? ::downspout::sanitizeMeter(transport.meter)
        : ::downspout::sanitizeMeter(state.pattern.meter);
    resizePattern(state.pattern, controls.bars, controls.resolution, targetMeter);

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
        clearPendingNoteOffs(state, result, 0);
        const double local = localStepFromAbsolute(state.pattern, absStepsStart);
        const double frac = local - std::floor(local);
        if (frac < 1e-6 || frac > 1.0 - 1e-6)
            emitStep(state, result, 0, static_cast<int>(std::floor(local + 1e-6)), nframes, sampleRate);
    }

    state.wasPlaying = true;
    state.lastTransportStep = startFloor;
    state.currentStep = static_cast<int>(std::floor(localStepFromAbsolute(state.pattern, absStepsStart)));
    result.currentStep = state.currentStep;

    std::int64_t boundary = static_cast<std::int64_t>(std::floor(absStepsStart)) + 1;
    const std::int64_t boundaryEnd = static_cast<std::int64_t>(std::floor(absStepsEnd + 1e-9));
    while (boundary <= boundaryEnd) {
        emitStep(state,
                 result,
                 frameForBoundary(absStepsStart, absStepsEnd, nframes, boundary),
                 localStepForBoundary(state.pattern, boundary),
                 nframes,
                 sampleRate);
        ++boundary;
    }

    return result;
}

}  // namespace downspout::xoxolo
