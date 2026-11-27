#include "padseq_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace downspout::padseq {
namespace {

constexpr std::array<std::uint8_t, 9> kProgrammerModeSysex = {{
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x0d, 0x0e, 0x01, 0xf7,
}};

// PadSeq runs sixteenth notes: 16 steps are one 4/4 bar. Host steps are counted
// in the same grid as xoxolo's sixteenth-note resolution.
constexpr double kStepsPerBeat = 4.0;

template <typename T>
[[nodiscard]] T clampValue(const T value, const T minimum, const T maximum) noexcept
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] int findIndex(const std::uint8_t value, const std::uint8_t* data, const std::size_t count) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (data[i] == value)
            return static_cast<int>(i);
    }
    return -1;
}

[[nodiscard]] int positiveMod(const std::int64_t value, const int modulus) noexcept
{
    const int period = std::max(1, modulus);
    int result = static_cast<int>(value % period);
    if (result < 0)
        result += period;
    return result;
}

[[nodiscard]] double stepPosition(const double absoluteSteps) noexcept
{
    return std::floor(absoluteSteps + 1e-9);
}

[[nodiscard]] std::uint32_t frameForBoundary(const double absStepsStart,
                                             const double absStepsEnd,
                                             const std::uint32_t frameCount,
                                             const std::int64_t boundary) noexcept
{
    const double t = (static_cast<double>(boundary) - absStepsStart) /
                     std::max(1e-12, absStepsEnd - absStepsStart);
    return static_cast<std::uint32_t>(clampValue(
        static_cast<int>(std::floor(t * static_cast<double>(frameCount))),
        0,
        static_cast<int>(frameCount) - 1));
}

[[nodiscard]] float intParameter(const std::array<float, kParameterCount>& parameters,
                                 const ParamId id) noexcept
{
    return parameters[static_cast<std::size_t>(id)];
}

} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────────

void Processor::init(const double sampleRate)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    resetToDefaults();
}

void Processor::activate()
{
    // Host startup/resume clears timing only. The parameter array (and therefore
    // the pattern) survives, so a saved project reopens with the same cells.
    resetRuntime();
}

void Processor::setSampleRate(const double sampleRate)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    resetRuntime();
}

void Processor::resetToDefaults()
{
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        parameters_[i] = rangeFor(static_cast<ParamId>(i)).defaultValue;
    cells_.fill(false);
    const float seed = intParameter(parameters_, ParamId::seed);
    rngState_ = (static_cast<std::uint64_t>(static_cast<std::int64_t>(seed)) * 0x9e3779b97f4a7c15ull) ^
                0x8f73a2bdu;
    previousSeed_ = seed;
    resetRuntime();
}

void Processor::resetRuntime()
{
    pendingNoteOffs_.fill({});
    usingHost_ = false;
    wasHostPlaying_ = false;
    freeStarted_ = false;
    lastHostStep_ = -1;
    patternBaseStep_ = 0;
    currentPatternStep_ = -1;
    freeStep_ = 0;
    freeStepAccum_ = 0.0;
    currentHostStep_ = 0.0;
    ledRefreshSamples_ = 0;
    ledInitialized_ = false;
    ledResetHold_ = false;
    panicPending_ = false;
    clockResetPending_ = false;
    previousRandomise_ = 0.0f;
    previousClear_ = 0.0f;
    previousTriggers_.fill(0.0f);
    pendingTriggerMask_ = 0;
    lastCellLeds_.fill(255u);
    lastTopLeds_.fill(255u);
    lastSideLeds_.fill(255u);
    updateStatus();
}

void Processor::clampPatch()
{
    for (std::uint32_t i = 0; i < kInputParameterCount; ++i)
    {
        const auto& range = rangeFor(static_cast<ParamId>(i));
        float value = clampValue(parameters_[i], range.minimum, range.maximum);
        if (range.integer)
            value = std::round(value);
        if (range.boolean)
            value = value >= 0.5f ? 1.0f : 0.0f;
        if (range.trigger)
            value = 0.0f;
        parameters_[i] = value;
    }

    const auto p = [](const ParamId id) { return static_cast<std::size_t>(id); };
    const int steps = static_cast<int>(std::lround(parameters_[p(ParamId::steps)]));
    parameters_[p(ParamId::steps)] = static_cast<float>(clampValue(steps, kMinSteps, kMaxSteps));
    const int stepCount = static_cast<int>(parameters_[p(ParamId::steps)]);
    parameters_[p(ParamId::page)] = static_cast<float>(clampValue(
        static_cast<int>(std::lround(parameters_[p(ParamId::page)])),
        0,
        pageCount(stepCount) - 1));

    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        if (i >= static_cast<std::size_t>(stepCount) * kGridWidth)
        {
            cells_[i] = false;
            parameters_[p(ParamId::cellStart) + i] = 0.0f;
        }
        else
        {
            cells_[i] = parameters_[p(ParamId::cellStart) + i] >= 0.5f;
            parameters_[p(ParamId::cellStart) + i] = cells_[i] ? 1.0f : 0.0f;
        }
    }
}

void Processor::updateStatus()
{
    const auto p = [](const ParamId id) { return static_cast<std::size_t>(id); };
    parameters_[p(ParamId::statusStep)] = static_cast<float>(currentPatternStep_);
    parameters_[p(ParamId::statusPage)] = parameters_[p(ParamId::page)];
    parameters_[p(ParamId::statusScale)] = parameters_[p(ParamId::scale)];
    parameters_[p(ParamId::statusRunning)] =
        (usingHost_ && wasHostPlaying_) || (!usingHost_ && freeStarted_) ? 1.0f : 0.0f;
    parameters_[p(ParamId::statusNoteView)] = parameters_[p(ParamId::noteView)];

    // Status cells mirror the visible 8x8 page: rows are notes, columns are steps.
    const int stepCount = static_cast<int>(std::lround(parameters_[p(ParamId::steps)]));
    const int page = static_cast<int>(std::lround(parameters_[p(ParamId::page)]));
    for (std::size_t note = 0; note < kGridHeight; ++note)
    {
        for (std::size_t stepColumn = 0; stepColumn < kGridWidth; ++stepColumn)
        {
            const int step = page * kStepsPerPage + static_cast<int>(stepColumn);
            const bool on = step >= 0 && step < stepCount &&
                            cells_[cellIndex(static_cast<std::size_t>(step), note)];
            parameters_[p(ParamId::statusCellStart) + note * kGridWidth + stepColumn] = on ? 1.0f : 0.0f;
        }
    }
}

// ── Parameters ───────────────────────────────────────────────────────────────

void Processor::setParameter(const std::uint32_t index, const float value)
{
    if (index >= kInputParameterCount)
        return;

    const auto cellBase = static_cast<std::uint32_t>(ParamId::cellStart);
    if (index >= cellBase && index < cellBase + static_cast<std::uint32_t>(kCellCount))
    {
        setCell(index - cellBase, value >= 0.5f);
        return;
    }

    const auto id = static_cast<ParamId>(index);
    const auto& range = rangeFor(id);
    float applied = clampValue(value, range.minimum, range.maximum);
    if (range.integer)
        applied = std::round(applied);
    if (range.boolean)
        applied = applied >= 0.5f ? 1.0f : 0.0f;

    const auto triggerBase = static_cast<std::uint32_t>(ParamId::triggerStart);
    if (index >= triggerBase && index < triggerBase + static_cast<std::uint32_t>(kGridWidth))
    {
        const std::size_t note = index - triggerBase;
        const bool edge = applied >= 0.5f && previousTriggers_[note] < 0.5f;
        previousTriggers_[note] = applied;
        if (edge)
        {
            pendingTriggerMask_ |= static_cast<std::uint8_t>(1u << note);
            releaseLedReset();
        }
        parameters_[index] = 0.0f;
        return;
    }

    if (range.trigger)
    {
        float* previous = nullptr;
        if (id == ParamId::randomise)
            previous = &previousRandomise_;
        else if (id == ParamId::clear)
            previous = &previousClear_;

        if (previous != nullptr)
        {
            const bool edge = applied >= 0.5f && *previous < 0.5f;
            *previous = applied;
            if (edge)
            {
                if (id == ParamId::randomise)
                    randomisePattern();
                else
                    clearPattern(true);
            }
        }
        applied = 0.0f;
    }

    if (id == ParamId::seed && applied != parameters_[index])
    {
        const std::uint64_t seed = static_cast<std::uint64_t>(static_cast<std::int64_t>(applied));
        rngState_ = (seed * 0x9e3779b97f4a7c15ull) ^ 0x8f73a2bdu;
        previousSeed_ = applied;
    }
    if (id == ParamId::clockMode && applied != parameters_[index])
        clockResetPending_ = true;
    if (id == ParamId::tempo && applied != parameters_[index])
    {
        // A faster tempo must not turn old accumulated samples into a burst of
        // steps at frame 0 of the next block.
        const double stepSamples = std::max(1.0, sampleRate_ * 60.0 /
                                                  (std::max(1.0, static_cast<double>(applied)) * kStepsPerBeat));
        while (freeStepAccum_ > stepSamples)
            freeStepAccum_ -= stepSamples;
    }

    if (applied != parameters_[index] && id != ParamId::clear)
        releaseLedReset();
    if (id == ParamId::ledFeedback && applied != parameters_[index])
    {
        // Re-enabling must paint the full state even if the cached colours
        // happen to match what the Launchpad had before it was switched off.
        lastCellLeds_.fill(255u);
        lastTopLeds_.fill(255u);
        lastSideLeds_.fill(255u);
        ledInitialized_ = false;
    }

    parameters_[index] = applied;
    clampPatch();
    updateStatus();
}

float Processor::getParameter(const std::uint32_t index) const noexcept
{
    if (index >= kParameterCount)
        return 0.0f;

    const auto statusBase = static_cast<std::uint32_t>(ParamId::statusCellStart);
    if (index >= statusBase && index < statusBase + static_cast<std::uint32_t>(kGridCellCount))
    {
        const std::uint32_t visible = index - statusBase;
        const int steps = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::steps)]));
        const int page = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::page)]));
        const std::size_t note = visible / kGridWidth;
        const int step = page * kStepsPerPage + static_cast<int>(visible % kGridWidth);
        return step >= 0 && step < steps && cells_[cellIndex(static_cast<std::size_t>(step), note)]
            ? 1.0f : 0.0f;
    }
    return parameters_[index];
}

bool Processor::cellActive(const std::size_t step, const std::size_t note) const noexcept
{
    if (step >= kMaxSteps || note >= kGridHeight)
        return false;
    return cells_[cellIndex(step, note)];
}

std::uint8_t Processor::noteForRow(const std::size_t note) const noexcept
{
    if (note >= kGridHeight)
        return 0u;

    const auto p = [](const ParamId id) { return static_cast<std::size_t>(id); };
    if (static_cast<int>(std::lround(parameters_[p(ParamId::noteMap)])) ==
        static_cast<int>(NoteMap::gmDrums))
        return kDrumNotes[note];

    const int scale = clampValue(
        static_cast<int>(std::lround(parameters_[p(ParamId::scale)])),
        0,
        static_cast<int>(ScaleId::count) - 1);
    const int root = clampValue(static_cast<int>(std::lround(parameters_[p(ParamId::root)])), 0, 127);
    const int view = clampValue(static_cast<int>(std::lround(parameters_[p(ParamId::noteView)])), -32, 32);
    return static_cast<std::uint8_t>(foldMidi(root + scaleStepAt(static_cast<std::size_t>(scale),
                                                                  static_cast<int>(note) + view)));
}

int Processor::patternStepFor(const std::size_t page, const std::size_t stepColumn) const noexcept
{
    if (page >= static_cast<std::size_t>(kMaxPages) || stepColumn >= kGridWidth)
        return -1;
    const int step = static_cast<int>(page) * kStepsPerPage + static_cast<int>(stepColumn);
    const int steps = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::steps)]));
    return step < clampValue(steps, kMinSteps, kMaxSteps) ? step : -1;
}

void Processor::setCell(const std::size_t index, const bool active)
{
    if (index >= kCellCount || cells_[index] == active)
        return;
    cells_[index] = active;
    parameters_[static_cast<std::size_t>(ParamId::cellStart) + index] = active ? 1.0f : 0.0f;
    releaseLedReset();
    updateStatus();
}

std::uint32_t Processor::random()
{
    rngState_ ^= rngState_ << 13u;
    rngState_ ^= rngState_ >> 7u;
    rngState_ ^= rngState_ << 17u;
    return static_cast<std::uint32_t>(rngState_ >> 24u);
}

void Processor::randomisePattern()
{
    const bool hasPattern = std::any_of(cells_.begin(), cells_.end(), [](const bool on) { return on; });
    const int steps = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::steps)]));

    if (hasPattern)
    {
        // Keep the existing idea recognisable: thin some hits and add a few new
        // ones rather than throwing the pattern away.
        for (int step = 0; step < steps; ++step)
        {
            for (std::size_t note = 0; note < kGridHeight; ++note)
            {
                const std::size_t index = cellIndex(static_cast<std::size_t>(step), note);
                const std::uint32_t draw = random() % 100u;
                if (cells_[index])
                    cells_[index] = draw >= 40u;
                else
                    cells_[index] = draw < 6u;
            }
        }
    }
    else
    {
        for (std::size_t index = 0; index < kCellCount; ++index)
            cells_[index] = random() % 100u < 8u;
    }

    const std::size_t usable = static_cast<std::size_t>(steps) * kGridWidth;
    for (std::size_t index = usable; index < kCellCount; ++index)
        cells_[index] = false;

    if (std::none_of(cells_.begin(), cells_.end(), [](const bool on) { return on; }))
    {
        const std::size_t index = static_cast<std::size_t>(random() % static_cast<std::uint32_t>(usable));
        cells_[index] = true;
    }

    for (std::size_t i = 0; i < kCellCount; ++i)
        parameters_[static_cast<std::size_t>(ParamId::cellStart) + i] = cells_[i] ? 1.0f : 0.0f;
    releaseLedReset();
    updateStatus();
}

void Processor::clearPattern(const bool emitPanic)
{
    cells_.fill(false);
    for (std::size_t i = 0; i < kCellCount; ++i)
        parameters_[static_cast<std::size_t>(ParamId::cellStart) + i] = 0.0f;
    if (emitPanic)
        panicPending_ = true;
    updateStatus();
}

void Processor::releaseLedReset()
{
    ledResetHold_ = false;
}

// ── MIDI input ───────────────────────────────────────────────────────────────

bool Processor::handleMidi(const MidiMessage& event,
                           ProcessResult& result,
                           const std::int64_t offStep,
                           bool& forceLeds)
{
    if (event.size < 3)
        return false;

    const std::uint8_t status = event.data[0] & 0xf0u;
    const std::uint8_t data1 = event.data[1];
    const std::uint8_t data2 = event.data[2];

    std::size_t row = 0;
    std::size_t col = 0;
    if ((status == 0x90u || status == 0x80u) && noteToGrid(data1, row, col))
    {
        if (status == 0x90u && data2 > 0u)
            handleGridPress(data1);
        // Never echo grid messages: on the Launchpad they are LED commands.
        forceLeds = true;
        return true;
    }

    const int top = findIndex(data1, kTopButtonCCs.data(), kTopButtonCCs.size());
    const int side = findIndex(data1, kSideButtonCCs.data(), kSideButtonCCs.size());
    if (status == 0xb0u && (top >= 0 || side >= 0))
    {
        if (data2 > 0u)
        {
            if (top >= 0)
                handleTopButton(static_cast<std::uint32_t>(top));
            else
                handleSideButton(static_cast<std::size_t>(side), result, event.frame, offStep);
        }
        forceLeds = true;
        return true;
    }

    if (parameters_[static_cast<std::size_t>(ParamId::passInput)] >= 0.5f && event.size == 3)
        appendMidi(result, event.frame, event.data[0], data1, data2);
    return false;
}

bool Processor::handleGridPress(const std::uint8_t note)
{
    std::size_t row = 0;
    std::size_t col = 0;
    if (!noteToGrid(note, row, col))
        return false;

    const std::size_t page = static_cast<std::size_t>(std::lround(
        parameters_[static_cast<std::size_t>(ParamId::page)]));
    // Hardware row selects the note, hardware column selects the step on this page.
    const int step = patternStepFor(page, col);
    if (step < 0)
        return false;
    const std::size_t index = cellIndex(static_cast<std::size_t>(step), row);
    setCell(index, !cells_[index]);
    return true;
}

void Processor::handleTopButton(const std::uint32_t button)
{
    if (button >= static_cast<std::uint32_t>(TopButton::count) ||
        button == static_cast<std::uint32_t>(TopButton::logo))
        return;

    releaseLedReset();
    const auto p = [](const ParamId id) { return static_cast<std::uint32_t>(id); };
    const auto set = [this, p](const ParamId id, const float value) {
        setParameter(p(id), value);
    };
    const auto current = [this, p](const ParamId id) { return parameters_[p(id)]; };

    switch (static_cast<TopButton>(button))
    {
    case TopButton::viewDown:
        set(ParamId::noteView, current(ParamId::noteView) - 1.0f);
        break;
    case TopButton::viewUp:
        set(ParamId::noteView, current(ParamId::noteView) + 1.0f);
        break;
    case TopButton::pageBack:
        set(ParamId::page, current(ParamId::page) - 1.0f);
        break;
    case TopButton::pageForward:
        set(ParamId::page, current(ParamId::page) + 1.0f);
        break;
    case TopButton::scaleBack:
        set(ParamId::scale, current(ParamId::scale) - 1.0f);
        break;
    case TopButton::scaleForward:
        set(ParamId::scale, current(ParamId::scale) + 1.0f);
        break;
    case TopButton::randomise:
        setParameter(p(ParamId::randomise), 1.0f);
        setParameter(p(ParamId::randomise), 0.0f);
        break;
    case TopButton::clear:
        setParameter(p(ParamId::clear), 1.0f);
        setParameter(p(ParamId::clear), 0.0f);
        break;
    case TopButton::logo:
    case TopButton::count:
        break;
    }
}

void Processor::handleSideButton(const std::size_t note,
                                 ProcessResult& result,
                                 const std::uint32_t frame,
                                 const std::int64_t offStep)
{
    if (note >= kGridHeight)
        return;
    releaseLedReset();
    const auto p = [](const ParamId id) { return static_cast<std::size_t>(id); };
    const std::uint8_t channel = static_cast<std::uint8_t>(
        clampValue(static_cast<int>(std::lround(parameters_[p(ParamId::channel)])), 1, 16) - 1);
    const std::uint8_t velocity = static_cast<std::uint8_t>(
        clampValue(static_cast<int>(std::lround(parameters_[p(ParamId::velocity)])), 1, 127));
    const std::uint8_t playedNote = noteForRow(note);
    appendMidi(result, frame, static_cast<std::uint8_t>(0x90u | channel), playedNote, velocity);
    enqueueNoteOff(offStep, static_cast<std::uint8_t>(0x80u | channel), playedNote);
}

// ── Clocks and note scheduling ───────────────────────────────────────────────

ProcessResult Processor::processBlock(const std::uint32_t frameCount,
                                      const TransportSnapshot& transport,
                                      const MidiMessage* inputEvents,
                                      const std::uint32_t inputEventCount)
{
    ProcessResult result {};
    if (frameCount == 0 || sampleRate_ <= 0.0)
        return result;

    clampPatch();

    const auto p = [](const ParamId id) { return static_cast<std::size_t>(id); };
    const int mode = clampValue(static_cast<int>(std::lround(parameters_[p(ParamId::clockMode)])),
                                0,
                                static_cast<int>(ClockMode::count) - 1);
    const bool useHost = mode == static_cast<int>(ClockMode::host) ||
                         (mode == static_cast<int>(ClockMode::autoClock) && transport.valid);

    if (clockResetPending_)
    {
        flushAllNoteOffs(result, 0);
        usingHost_ = useHost;
        wasHostPlaying_ = false;
        freeStarted_ = false;
        lastHostStep_ = -1;
        currentPatternStep_ = -1;
        freeStep_ = 0;
        freeStepAccum_ = 0.0;
        clockResetPending_ = false;
    }
    usingHost_ = useHost;

    const double stepSamples = std::max(1.0,
                                        sampleRate_ * 60.0 /
                                            (std::max(1.0, static_cast<double>(
                                                parameters_[p(ParamId::tempo)])) * kStepsPerBeat));
    const int steps = static_cast<int>(std::lround(parameters_[p(ParamId::steps)]));
    double absStepsStart = 0.0;
    double absStepsEnd = 0.0;
    bool hostPlaying = false;

    if (useHost)
    {
        const double beatsPerBar = std::max(1.0, transport.beatsPerBar);
        const double absBeatsStart = transport.bar * beatsPerBar + transport.barBeat;
        const double blockBeats = static_cast<double>(frameCount) * transport.bpm / (60.0 * sampleRate_);
        absStepsStart = absBeatsStart * kStepsPerBeat;
        absStepsEnd = (absBeatsStart + blockBeats) * kStepsPerBeat;
        hostPlaying = transport.playing && transport.bpm > 0.0 && transport.beatsPerBar > 0.0;
        prepareHostBlock(result, absStepsStart, steps, hostPlaying);
        if (!hostPlaying)
        {
            // A stopped host has no pattern steps, but manual triggers still need
            // a grid-timed release. Use the free accumulator for off events only.
            absStepsStart = static_cast<double>(freeStep_) + freeStepAccum_ / stepSamples;
            absStepsEnd = absStepsStart + static_cast<double>(frameCount) / stepSamples;
        }
    }
    else
    {
        // A free clock's first block flushes stale note-offs before any trigger
        // is consumed, so a side-button note is not silenced by its own start.
        prepareFreeBlock(result, stepSamples);
        absStepsStart = static_cast<double>(freeStep_) + freeStepAccum_ / stepSamples;
        absStepsEnd = absStepsStart + static_cast<double>(frameCount) / stepSamples;
    }

    const double absStepsSpan = absStepsEnd - absStepsStart;
    const auto offStepFor = [&](const std::uint32_t frame) {
        return offStepForFrame(absStepsStart, absStepsSpan, frameCount, frame);
    };

    flushPendingTriggers(result, 0, offStepFor(0));

    bool forceLeds = false;
    for (std::uint32_t i = 0; i < inputEventCount; ++i)
    {
        if (inputEvents == nullptr)
            break;
        if (handleMidi(inputEvents[i], result, offStepFor(inputEvents[i].frame), forceLeds))
            forceLeds = true;
    }

    // Clearing can be requested by a parameter write or by the top CC98 button.
    // Either way the panic output belongs in the same block.
    if (panicPending_)
    {
        panicPending_ = false;
        emitPanic(result);
    }

    // Boundaries are scheduled after input so a side button pressed before a
    // boundary gets its note-off at that boundary, not one block later.
    if (useHost)
    {
        if (hostPlaying)
            scheduleHostBoundaries(result, frameCount, absStepsStart, absStepsEnd);
        else
            scheduleIdleBoundaries(result, frameCount, stepSamples);
    }
    else
    {
        scheduleFreeBoundaries(result, frameCount, stepSamples);
    }

    const bool periodicRefresh = ledRefreshSamples_ >= static_cast<std::uint32_t>(sampleRate_ * 0.75);
    if (!ledResetHold_)
        emitLedFeedback(result, forceLeds || periodicRefresh || !ledInitialized_);
    ledRefreshSamples_ = periodicRefresh ? 0u : ledRefreshSamples_ + frameCount;

    updateStatus();
    sortEvents(result);
    return result;
}

void Processor::prepareHostBlock(ProcessResult& result,
                                 const double absStepsStart,
                                 const int steps,
                                 const bool playing)
{
    if (!playing)
    {
        if (wasHostPlaying_)
            flushAllNoteOffs(result, 0);
        wasHostPlaying_ = false;
        lastHostStep_ = -1;
        currentPatternStep_ = -1;
        return;
    }

    const std::int64_t startFloor = static_cast<std::int64_t>(stepPosition(absStepsStart));
    const bool restarted = !wasHostPlaying_ ||
                           (lastHostStep_ >= 0 &&
                            (startFloor < lastHostStep_ || startFloor > lastHostStep_ + 1));

    if (!wasHostPlaying_)
        releaseLedReset();

    if (restarted)
    {
        // New start, transport jump or rewind: the pattern is counted again from
        // the host position, and nothing from the old pass is left ringing.
        flushAllNoteOffs(result, 0);
        patternBaseStep_ = startFloor;
        currentPatternStep_ = positiveMod(startFloor - patternBaseStep_, steps);

        // A transport start exactly on a grid boundary plays that step now;
        // a start mid-step waits for the next boundary.
        const double frac = absStepsStart - std::floor(absStepsStart);
        if (frac < 1e-6 || frac > 1.0 - 1e-6)
            emitStep(result, 0, startFloor, patternBaseStep_);
    }

    wasHostPlaying_ = true;
    lastHostStep_ = startFloor;
    currentHostStep_ = absStepsStart;
    currentPatternStep_ = positiveMod(
        static_cast<std::int64_t>(std::floor(absStepsStart - static_cast<double>(patternBaseStep_))), steps);
}

void Processor::scheduleHostBoundaries(ProcessResult& result,
                                       const std::uint32_t frameCount,
                                       const double absStepsStart,
                                       const double absStepsEnd)
{
    const std::int64_t startFloor = static_cast<std::int64_t>(stepPosition(absStepsStart));
    const std::int64_t endFloor = static_cast<std::int64_t>(stepPosition(absStepsEnd));

    for (std::int64_t boundary = startFloor + 1; boundary <= endFloor; ++boundary)
    {
        const std::uint32_t frame = frameForBoundary(absStepsStart, absStepsEnd, frameCount, boundary);
        flushNoteOffsAt(result, boundary, frame);
        emitStep(result, frame, boundary, patternBaseStep_);
    }
}

void Processor::prepareFreeBlock(ProcessResult& result, const double stepSamples)
{
    static_cast<void>(stepSamples);
    if (!freeStarted_)
    {
        releaseLedReset();
        flushAllNoteOffs(result, 0);
        freeStarted_ = true;
        freeStep_ = 0;
        freeStepAccum_ = 0.0;
        currentPatternStep_ = 0;
        emitStep(result, 0, 0, 0);
    }
}

void Processor::scheduleFreeBoundaries(ProcessResult& result,
                                       const std::uint32_t frameCount,
                                       const double stepSamples)
{
    const int steps = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::steps)]));
    double untilNext = stepSamples - freeStepAccum_;
    while (untilNext < static_cast<double>(frameCount))
    {
        const std::uint32_t frame = static_cast<std::uint32_t>(clampValue(
            static_cast<int>(std::floor(untilNext)), 0, static_cast<int>(frameCount) - 1));
        ++freeStep_;
        flushNoteOffsAt(result, freeStep_, frame);
        emitStep(result, frame, static_cast<std::int64_t>(freeStep_), 0);
        untilNext += stepSamples;
    }

    // Keep an exact boundary as an accumulator equal to stepSamples rather than
    // wrapping it to zero: the next block must emit that step at frame 0.
    freeStepAccum_ += static_cast<double>(frameCount);
    while (freeStepAccum_ > stepSamples)
        freeStepAccum_ -= stepSamples;
    currentPatternStep_ = positiveMod(static_cast<std::int64_t>(freeStep_), steps);
}

void Processor::scheduleIdleBoundaries(ProcessResult& result,
                                       const std::uint32_t frameCount,
                                       const double stepSamples)
{
    // Same accumulator as the free clock, but no pattern step is emitted. This
    // only gives stopped-host manual notes their scheduled release frame.
    double untilNext = stepSamples - freeStepAccum_;
    while (untilNext < static_cast<double>(frameCount))
    {
        const std::uint32_t frame = static_cast<std::uint32_t>(clampValue(
            static_cast<int>(std::floor(untilNext)), 0, static_cast<int>(frameCount) - 1));
        ++freeStep_;
        flushNoteOffsAt(result, freeStep_, frame);
        untilNext += stepSamples;
    }

    freeStepAccum_ += static_cast<double>(frameCount);
    while (freeStepAccum_ > stepSamples)
        freeStepAccum_ -= stepSamples;
}

std::int64_t Processor::offStepForFrame(const double absStepsStart,
                                        const double absStepsSpan,
                                        const std::uint32_t frameCount,
                                        const std::uint32_t frame) const noexcept
{
    if (frameCount == 0)
        return stepPosition(absStepsStart) + 1;
    const double fraction = clampValue(static_cast<double>(frame) / static_cast<double>(frameCount),
                                       0.0,
                                       1.0);
    return stepPosition(absStepsStart + std::max(0.0, absStepsSpan) * fraction) + 1;
}

void Processor::flushPendingTriggers(ProcessResult& result,
                                     const std::uint32_t frame,
                                     const std::int64_t offStep)
{
    const std::uint8_t mask = pendingTriggerMask_;
    pendingTriggerMask_ = 0;
    for (std::size_t note = 0; note < kGridHeight; ++note)
    {
        if ((mask & (1u << note)) != 0u)
            handleSideButton(note, result, frame, offStep);
    }
}

void Processor::emitStep(ProcessResult& result,
                         const std::uint32_t frame,
                         const std::int64_t absoluteStep,
                         const std::int64_t patternBaseStep)
{
    const int steps = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::steps)]));
    const int step = positiveMod(absoluteStep - patternBaseStep, steps);
    const auto p = [](const ParamId id) { return static_cast<std::size_t>(id); };
    const std::uint8_t channel = static_cast<std::uint8_t>(
        clampValue(static_cast<int>(std::lround(parameters_[p(ParamId::channel)])), 1, 16) - 1);
    const std::uint8_t velocity = static_cast<std::uint8_t>(
        clampValue(static_cast<int>(std::lround(parameters_[p(ParamId::velocity)])), 1, 127));

    for (std::size_t note = 0; note < kGridHeight; ++note)
    {
        if (!cellActive(static_cast<std::size_t>(step), note))
            continue;
        const std::uint8_t playedNote = noteForRow(note);
        appendMidi(result, frame, static_cast<std::uint8_t>(0x90u | channel), playedNote, velocity);
        enqueueNoteOff(absoluteStep + 1, static_cast<std::uint8_t>(0x80u | channel), playedNote);
    }
}

void Processor::enqueueNoteOff(const std::int64_t offStep,
                               const std::uint8_t status,
                               const std::uint8_t note)
{
    for (PendingNoteOff& pending : pendingNoteOffs_)
    {
        if (pending.active)
            continue;
        pending.active = true;
        pending.offStep = offStep;
        pending.status = status;
        pending.note = note;
        return;
    }
}

void Processor::flushNoteOffsAt(ProcessResult& result, const std::int64_t upToStep, const std::uint32_t frame)
{
    for (PendingNoteOff& pending : pendingNoteOffs_)
    {
        if (!pending.active || pending.offStep > upToStep)
            continue;
        appendMidi(result, frame, pending.status, pending.note, 0u);
        pending.active = false;
    }
}

void Processor::flushAllNoteOffs(ProcessResult& result, const std::uint32_t frame)
{
    for (PendingNoteOff& pending : pendingNoteOffs_)
    {
        if (!pending.active)
            continue;
        appendMidi(result, frame, pending.status, pending.note, 0u);
        pending.active = false;
    }
}

// ── Launchpad feedback ───────────────────────────────────────────────────────

std::uint8_t Processor::statusColor(const std::size_t step, const std::size_t note) const noexcept
{
    const bool on = cellActive(step, note);
    const int current = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::statusStep)]));
    if (on && static_cast<int>(step) == current)
        return kLedWhite;
    if (on)
        return kLedGreen;
    return static_cast<int>(step) == current ? kLedDim : kLedOff;
}

std::uint8_t Processor::topColor(const std::uint32_t button) const noexcept
{
    switch (static_cast<TopButton>(button))
    {
    case TopButton::viewDown:
    case TopButton::viewUp:
        return kLedBlue;
    case TopButton::pageBack:
    case TopButton::pageForward:
        return kLedPurple;
    case TopButton::scaleBack:
    case TopButton::scaleForward:
        return kLedAmber;
    case TopButton::randomise:
        return kLedCyan;
    case TopButton::clear:
        return kLedRed;
    case TopButton::logo:
    case TopButton::count:
        break;
    }
    return kLedOff;
}

std::uint8_t Processor::sideColor(const std::size_t note) const noexcept
{
    const std::size_t page = static_cast<std::size_t>(std::lround(
        parameters_[static_cast<std::size_t>(ParamId::page)]));
    const int steps = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::steps)]));
    for (std::size_t stepColumn = 0; stepColumn < kGridWidth; ++stepColumn)
    {
        const int step = static_cast<int>(page) * kStepsPerPage + static_cast<int>(stepColumn);
        if (step < steps && cellActive(static_cast<std::size_t>(step), note))
            return kLedPink;
    }
    return kLedDim;
}

void Processor::emitLedFeedback(ProcessResult& result, const bool force)
{
    // Enter programmer mode on the first block, and again after activation or a
    // sample-rate change, even when LED feedback is off. A controller left in
    // session mode sends pad presses as CCs rather than the 11..88 grid notes
    // the core consumes, so it appears connected but does nothing.
    if (!ledInitialized_)
    {
        appendSysex(result, kProgrammerModeSysex.data(), kProgrammerModeSysex.size());
        ledInitialized_ = true;
    }

    if (parameters_[static_cast<std::size_t>(ParamId::ledFeedback)] < 0.5f)
        return;

    // Launchpad colours must never be channel-1 notes or CCs: on a shared MIDI
    // output an instrument would hear the scan as held notes. Build the complete
    // LED state and send it as one Novation lighting SysEx instead.
    std::array<std::uint8_t, kGridCellCount> grid {};
    std::array<std::uint8_t, kTopButtonCCs.size()> top {};
    std::array<std::uint8_t, kGridWidth> side {};

    const std::size_t page = static_cast<std::size_t>(std::lround(
        parameters_[static_cast<std::size_t>(ParamId::page)]));
    for (std::size_t note = 0; note < kGridHeight; ++note)
    {
        for (std::size_t stepColumn = 0; stepColumn < kGridWidth; ++stepColumn)
        {
            const int step = patternStepFor(page, stepColumn);
            grid[note * kGridWidth + stepColumn] = step >= 0
                ? statusColor(static_cast<std::size_t>(step), note)
                : kLedOff;
        }
    }
    for (std::size_t i = 0; i < top.size(); ++i)
        top[i] = topColor(static_cast<std::uint32_t>(i));
    for (std::size_t note = 0; note < side.size(); ++note)
        side[note] = sideColor(note);

    bool changed = force;
    for (std::size_t i = 0; i < grid.size() && !changed; ++i)
        changed = grid[i] != lastCellLeds_[i];
    for (std::size_t i = 0; i < top.size() && !changed; ++i)
        changed = top[i] != lastTopLeds_[i];
    for (std::size_t i = 0; i < side.size() && !changed; ++i)
        changed = side[i] != lastSideLeds_[i];
    if (!changed)
        return;

    appendLedBulkSysex(result, grid, side, top);

    lastCellLeds_ = grid;
    lastTopLeds_ = top;
    lastSideLeds_ = side;
    ledInitialized_ = true;
}

// ── MIDI output ──────────────────────────────────────────────────────────────

void Processor::emitPanic(ProcessResult& result)
{
    const std::uint8_t channel = static_cast<std::uint8_t>(
        clampValue(static_cast<int>(std::lround(
                       parameters_[static_cast<std::size_t>(ParamId::channel)])), 1, 16) - 1);
    for (int note = 0; note < 128; ++note)
        appendMidi(result, 0, static_cast<std::uint8_t>(0x80u | channel), static_cast<std::uint8_t>(note), 0u);

    appendSysex(result, kProgrammerModeSysex.data(), kProgrammerModeSysex.size());
    appendClearAllSysex(result);

    pendingNoteOffs_.fill({});
    lastCellLeds_.fill(255u);
    lastTopLeds_.fill(255u);
    lastSideLeds_.fill(255u);
    ledInitialized_ = true;
    ledResetHold_ = true;
}

void Processor::appendMidi(ProcessResult& result,
                           const std::uint32_t frame,
                           const std::uint8_t status,
                           const std::uint8_t data1,
                           const std::uint8_t data2)
{
    if (result.eventCount >= result.events.size())
        return;
    MidiMessage& event = result.events[result.eventCount++];
    event.frame = frame;
    event.size = 3;
    event.data[0] = status;
    event.data[1] = data1;
    event.data[2] = data2;
}

void Processor::appendSysex(ProcessResult& result, const std::uint8_t* data, const std::size_t size)
{
    if (data == nullptr || size > result.events[0].data.size() || result.eventCount >= result.events.size())
        return;
    MidiMessage& event = result.events[result.eventCount++];
    event.frame = 0;
    event.size = static_cast<std::uint8_t>(size);
    std::memcpy(event.data.data(), data, size);
}

void Processor::appendClearAllSysex(ProcessResult& result)
{
    std::array<std::uint8_t, kGridCellCount> grid {};
    std::array<std::uint8_t, kTopButtonCCs.size()> top {};
    std::array<std::uint8_t, kGridWidth> side {};
    grid.fill(kLedOff);
    top.fill(kLedOff);
    side.fill(kLedOff);
    appendLedBulkSysex(result, grid, side, top);
}

void Processor::appendLedBulkSysex(
    ProcessResult& result,
    const std::array<std::uint8_t, kGridCellCount>& grid,
    const std::array<std::uint8_t, kGridWidth>& side,
    const std::array<std::uint8_t, kTopButtonCCs.size()>& top)
{
    std::array<std::uint8_t, 256> data {};
    std::size_t size = 0;
    const auto append = [&data, &size](const std::uint8_t byte) {
        if (size < data.size())
            data[size++] = byte;
    };
    const auto appendLight = [&append](const std::uint8_t id, const std::uint8_t color) {
        append(0x00u); // static colour
        append(id);
        append(color);
    };

    // Novation Mini MK3 programmer-mode lighting message.
    for (const std::uint8_t byte : {0xf0u, 0x00u, 0x20u, 0x29u, 0x02u, 0x0du, 0x03u})
        append(byte);
    for (std::size_t note = 0; note < kGridHeight; ++note)
        for (std::size_t stepColumn = 0; stepColumn < kGridWidth; ++stepColumn)
            appendLight(gridToNote(note, stepColumn), grid[note * kGridWidth + stepColumn]);
    for (std::size_t i = 0; i < side.size(); ++i)
        appendLight(kSideButtonCCs[i], side[i]);
    for (std::size_t i = 0; i < top.size(); ++i)
        appendLight(kTopButtonCCs[i], top[i]);
    append(0xf7u);
    appendSysex(result, data.data(), size);
}

void Processor::sortEvents(ProcessResult& result)
{
    std::stable_sort(result.events.begin(), result.events.begin() + result.eventCount,
                     [](const MidiMessage& a, const MidiMessage& b) { return a.frame < b.frame; });
}

} // namespace downspout::padseq
