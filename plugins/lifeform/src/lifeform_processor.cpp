#include "lifeform_processor.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::lifeform {
namespace {

inline constexpr std::uint32_t kLeanMaxNotesPerGeneration = 8;

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

constexpr std::array<std::uint8_t, 9> kProgrammerModeSysex = {{
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x0d, 0x0e, 0x01, 0xf7,
}};

constexpr std::array<std::array<int, 8>, static_cast<std::size_t>(ScaleId::count)> kScaleIntervals = {{
    {{0, 1, 2, 3, 4, 5, 6, 7}},    // chromatic
    {{0, 2, 4, 5, 7, 9, 11, 12}},  // major
    {{0, 2, 4, 5, 7, 9, 11, 12}},  // ionian
    {{0, 2, 3, 5, 7, 8, 10, 12}},  // minor
    {{0, 2, 3, 5, 7, 9, 10, 12}},  // dorian
    {{0, 2, 4, 5, 7, 9, 10, 12}},  // mixolydian
    {{0, 2, 4, 7, 9, 12, 14, 16}}, // pentatonic
    {{0, 3, 5, 6, 7, 10, 12, 15}}, // blues
    {{0, 1, 4, 5, 7, 9, 11, 12}},  // neapolitanMajor
    {{0, 1, 3, 5, 7, 8, 10, 12}},  // neapolitanMinor
    {{0, 1, 3, 4, 6, 8, 10, 12}},  // altered
    {{0, 1, 3, 4, 6, 7, 9, 10}},   // halfWholeDiminished
    {{0, 2, 3, 5, 6, 8, 9, 11}},   // wholeHalfDiminished
    {{0, 2, 4, 5, 7, 9, 10, 11}},  // bebopDominant
    {{0, 2, 4, 5, 7, 8, 9, 11}},   // bebopMajor
    {{0, 2, 3, 4, 5, 7, 9, 10}},   // bebopMinor
}};

} // namespace

void Processor::init(const double sampleRate)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    activate();
}

void Processor::activate()
{
    resetToDefaults();
}

void Processor::resetToDefaults()
{
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        parameters_[i] = parameterDefault(i);
    cells_.fill(false);
    born_.fill(false);
    lastCellLeds_.fill(255u);
    lastTopLeds_.fill(255u);
    lastSideLeds_.fill(255u);
    pendingNoteOffs_.fill({});
    generation_ = 0;
    lastHostBeat_ = static_cast<std::uint64_t>(-1);
    freeBeatSamples_ = 0.0;
    ledRefreshSamples_ = 0;
    ledInitialized_ = false;
    panicRequested_ = false;
    suppressLedFeedbackOnce_ = false;
    seedPattern();
    updateStatus();
}

float Processor::parameterDefault(const std::uint32_t index) const noexcept
{
    if (index < kCellCount)
        return 0.0f;

    switch (index)
    {
    case kParamRootNote: return 48.0f;
    case kParamScale: return 0.0f;
    case kParamGate: return 0.52f;
    case kParamVelocity: return 0.72f;
    case kParamMutation: return 0.0f;
    case kParamDensity: return 0.34f;
    case kParamClockMode: return 0.0f;
    case kParamOutputMode: return 0.0f;
    case kParamEmitMode: return 0.0f;
    case kParamBaseChannel: return 4.0f;
    case kParamLedFeedback: return 1.0f;
    case kParamRunning: return 1.0f;
    case kParamSeed: return 0.0f;
    case kParamPassInput: return 0.0f;
    default: return 0.0f;
    }
}

void Processor::setParameter(const std::uint32_t index, float value)
{
    if (index >= kParameterCount)
        return;

    if (index >= kParamStatusCellStart && index < kParamPassInput)
        return;

    if (index < kCellCount)
    {
        cells_[index] = value >= 0.5f;
        born_[index] = cells_[index];
        parameters_[index] = cells_[index] ? 1.0f : 0.0f;
        updateStatus();
        return;
    }

    switch (index)
    {
    case kParamRootNote:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 0, 127));
        break;
    case kParamScale:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 0, static_cast<int>(ScaleId::count) - 1));
        break;
    case kParamClockMode:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 0, static_cast<int>(ClockMode::count) - 1));
        break;
    case kParamOutputMode:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 0, static_cast<int>(OutputMode::count) - 1));
        break;
    case kParamEmitMode:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 0, static_cast<int>(EmitMode::count) - 1));
        break;
    case kParamBaseChannel:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 1, 16));
        break;
    case kParamLedFeedback:
    case kParamRunning:
    case kParamPassInput:
        value = value >= 0.5f ? 1.0f : 0.0f;
        break;
    case kParamRandomize:
        if (value > 0.5f)
            randomizeCells();
        value = 0.0f;
        break;
    case kParamClear:
        if (value > 0.5f)
            clearCells();
        value = 0.0f;
        break;
    case kParamSeed:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 0, 7));
        parameters_[index] = value;
        seedPattern();
        updateStatus();
        return;
    case kParamStep:
        if (value > 0.5f)
        {
            ProcessResult ignored {};
            runGeneration(ignored, 0);
        }
        value = 0.0f;
        break;
    case kParamPanic:
        if (value > 0.5f)
            requestPanic();
        value = 0.0f;
        break;
    case kParamStatusActive:
    case kParamStatusGeneration:
        return;
    default:
        value = clampValue(value, 0.0f, 1.0f);
        break;
    }

    parameters_[index] = value;
    updateStatus();
}

float Processor::getParameter(const std::uint32_t index) const noexcept
{
    if (index == kParamStatusActive)
        return status_.activeCells;
    if (index == kParamStatusGeneration)
        return status_.generation;
    if (index >= kParamStatusCellStart && index < kParamStatusCellStart + kCellCount)
        return cells_[index - kParamStatusCellStart] ? 1.0f : 0.0f;
    return index < kParameterCount ? parameters_[index] : 0.0f;
}

const Status& Processor::getStatus() const noexcept
{
    return status_;
}

ProcessResult Processor::processBlock(const std::uint32_t frameCount,
                                      const TransportSnapshot& transport,
                                      const MidiMessage* inputEvents,
                                      const std::uint32_t inputEventCount)
{
    ProcessResult result {};
    processPendingNoteOffs(result, frameCount);
    if (panicRequested_)
        runPanic(result);

    const int clockMode = static_cast<int>(std::lround(parameters_[kParamClockMode]));
    const bool running = parameters_[kParamRunning] >= 0.5f;
    const bool useHost = running && ((clockMode == static_cast<int>(ClockMode::host)) ||
                                     (clockMode == static_cast<int>(ClockMode::autoClock) && transport.valid));
    const bool useFree = running && ((clockMode == static_cast<int>(ClockMode::free)) ||
                                     (clockMode == static_cast<int>(ClockMode::autoClock) && !transport.valid && transport.playing));

    std::array<std::uint32_t, 32> generationFrames {};
    std::uint32_t generationCount = 0;
    if (useHost && transport.valid && transport.playing)
        scheduleClockedGenerations(generationFrames, generationCount, frameCount, transport);
    else if (useFree)
        scheduleFreeGenerations(generationFrames, generationCount, frameCount, transport.bpm > 1.0 ? transport.bpm : 120.0);

    bool forceLeds = false;
    std::uint32_t midiIndex = 0;
    std::uint32_t generationIndex = 0;
    while (midiIndex < inputEventCount || generationIndex < generationCount)
    {
        const std::uint32_t midiFrame = (inputEvents != nullptr && midiIndex < inputEventCount)
                                            ? std::min(inputEvents[midiIndex].frame, frameCount > 0 ? frameCount - 1u : 0u)
                                            : UINT32_MAX;
        const std::uint32_t generationFrame = generationIndex < generationCount
                                                  ? generationFrames[generationIndex]
                                                  : UINT32_MAX;

        if (generationFrame <= midiFrame)
        {
            runGeneration(result, generationFrame);
            ++generationIndex;
        }
        else
        {
            if (handleMidi(inputEvents[midiIndex], result))
                forceLeds = true;
            ++midiIndex;
        }
    }

    const bool periodicRefresh = ledRefreshSamples_ >= static_cast<std::uint32_t>(sampleRate_ * 0.75);
    if (suppressLedFeedbackOnce_)
    {
        suppressLedFeedbackOnce_ = false;
    }
    else
    {
        emitLedFeedback(result, forceLeds || periodicRefresh || !ledInitialized_);
    }
    ledRefreshSamples_ = periodicRefresh ? 0u : ledRefreshSamples_ + frameCount;

    updateStatus();
    result.status = status_;
    return result;
}

void Processor::clearCells()
{
    cells_.fill(false);
    born_.fill(false);
    for (std::uint32_t i = 0; i < kCellCount; ++i)
        parameters_[i] = 0.0f;
    updateStatus();
}

void Processor::randomizeCells()
{
    const std::uint32_t threshold = static_cast<std::uint32_t>(clampValue(parameters_[kParamDensity], 0.02f, 0.95f) * 65535.0f);
    for (std::uint32_t i = 0; i < kCellCount; ++i)
    {
        cells_[i] = (random() & 0xffffu) < threshold;
        born_[i] = cells_[i];
        parameters_[i] = cells_[i] ? 1.0f : 0.0f;
    }
    updateStatus();
}

void Processor::seedPattern()
{
    clearCells();
    const int seed = static_cast<int>(std::lround(parameters_[kParamSeed]));
    switch (seed)
    {
    case 0:
        setCell(3, 2, true); setCell(3, 3, true); setCell(3, 4, true);
        break;
    case 1:
        setCell(1, 2, true); setCell(2, 3, true); setCell(3, 1, true); setCell(3, 2, true); setCell(3, 3, true);
        break;
    case 2:
        setCell(2, 2, true); setCell(2, 3, true); setCell(3, 2, true); setCell(3, 3, true);
        setCell(4, 4, true); setCell(4, 5, true); setCell(5, 4, true); setCell(5, 5, true);
        break;
    case 3:
        setCell(2, 3, true); setCell(2, 4, true); setCell(2, 5, true); setCell(3, 2, true); setCell(3, 3, true); setCell(3, 4, true);
        break;
    case 4:
        setCell(1, 1, true); setCell(1, 4, true); setCell(2, 5, true); setCell(3, 1, true); setCell(3, 5, true);
        setCell(4, 2, true); setCell(4, 3, true); setCell(4, 4, true); setCell(4, 5, true);
        break;
    case 5:
        setCell(2, 3, true); setCell(3, 2, true); setCell(3, 3, true); setCell(3, 4, true);
        setCell(4, 3, true); setCell(1, 3, true); setCell(5, 3, true);
        break;
    case 6:
        parameters_[kParamDensity] = 0.20f;
        randomizeCells();
        return;
    default:
        parameters_[kParamDensity] = 0.46f;
        randomizeCells();
        return;
    }
    updateStatus();
}

void Processor::setCell(const std::size_t row, const std::size_t col, const bool alive)
{
    if (row >= kGridHeight || col >= kGridWidth)
        return;
    const std::size_t index = cellIndex(row, col);
    cells_[index] = alive;
    born_[index] = alive;
    parameters_[index] = alive ? 1.0f : 0.0f;
}

bool Processor::handleMidi(const MidiMessage& event, ProcessResult& result)
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
            return handleGridPress(data1);
        return false;
    }

    const bool launchpadTop = findIndex(data1, kTopButtonCCs.data(), kTopButtonCCs.size()) >= 0;
    const bool launchpadSide = findIndex(data1, kSideButtonCCs.data(), kSideButtonCCs.size()) >= 0;
    if (status == 0xb0u && (launchpadTop || launchpadSide))
    {
        if (data2 > 0u)
            return handleTopButton(data1, result, event.frame) || handleSideButton(data1);
        return false;
    }

    if (parameters_[kParamPassInput] >= 0.5f && (status == 0x90u || status == 0x80u || status == 0xb0u))
        appendMidi(result, event.frame, event.data[0], data1, data2);
    return false;
}

bool Processor::handleGridPress(const std::uint8_t note)
{
    std::size_t row = 0;
    std::size_t col = 0;
    if (!noteToGrid(note, row, col))
        return false;

    const std::size_t index = cellIndex(row, col);
    cells_[index] = !cells_[index];
    born_[index] = cells_[index];
    parameters_[index] = cells_[index] ? 1.0f : 0.0f;
    updateStatus();
    return true;
}

bool Processor::handleTopButton(const std::uint8_t cc, ProcessResult& result, const std::uint32_t frame)
{
    const int index = findIndex(cc, kTopButtonCCs.data(), kTopButtonCCs.size());
    if (index < 0)
        return false;

    switch (index)
    {
    case 0: setParameter(kParamRunning, parameters_[kParamRunning] >= 0.5f ? 0.0f : 1.0f); break;
    case 1: runGeneration(result, frame); break;
    case 2: randomizeCells(); break;
    case 3: clearCells(); break;
    case 4: setParameter(kParamSeed, std::fmod(parameters_[kParamSeed] + 1.0f, 8.0f)); break;
    case 5: setParameter(kParamMutation, parameters_[kParamMutation] - 0.04f); break;
    case 6: setParameter(kParamMutation, parameters_[kParamMutation] + 0.04f); break;
    case 7: setParameter(kParamDensity, parameters_[kParamDensity] + 0.06f); break;
    case 8: requestPanic(); runPanic(result); break;
    default: break;
    }
    return true;
}

bool Processor::handleSideButton(const std::uint8_t cc)
{
    const int index = findIndex(cc, kSideButtonCCs.data(), kSideButtonCCs.size());
    if (index < 0)
        return false;

    setParameter(kParamSeed, static_cast<float>(index));
    return true;
}

void Processor::processPendingNoteOffs(ProcessResult& result, const std::uint32_t frameCount)
{
    for (PendingNoteOff& pending : pendingNoteOffs_)
    {
        if (!pending.active)
            continue;

        if (pending.framesLeft <= frameCount)
        {
            appendMidi(result, pending.framesLeft, pending.status, pending.note, 0u);
            pending.active = false;
        }
        else
        {
            pending.framesLeft -= frameCount;
        }
    }
}

void Processor::scheduleClockedGenerations(std::array<std::uint32_t, 32>& frames,
                                           std::uint32_t& count,
                                           const std::uint32_t frameCount,
                                           const TransportSnapshot& transport)
{
    const double safeBpm = std::max(1.0, transport.bpm);
    const double beatsPerSecond = safeBpm / 60.0;
    const double startBeat = transport.bar * std::max(1.0, transport.beatsPerBar) + transport.barBeat;
    const double endBeat = startBeat + static_cast<double>(frameCount) * beatsPerSecond / sampleRate_;

    std::uint64_t firstBeat = static_cast<std::uint64_t>(std::floor(startBeat));
    if (static_cast<double>(firstBeat) < startBeat - 1.0e-9)
        ++firstBeat;

    const std::uint64_t lastBeat = static_cast<std::uint64_t>(std::floor(endBeat + 1.0e-9));
    for (std::uint64_t beat = firstBeat; beat <= lastBeat; ++beat)
    {
        if (beat == lastHostBeat_)
            continue;
        const double frame = (static_cast<double>(beat) - startBeat) / beatsPerSecond * sampleRate_;
        if (frame >= -0.5 && frame < static_cast<double>(frameCount))
        {
            if (count < frames.size())
                frames[count++] = static_cast<std::uint32_t>(std::max(0.0, std::round(frame)));
            lastHostBeat_ = beat;
        }
    }
}

void Processor::scheduleFreeGenerations(std::array<std::uint32_t, 32>& frames,
                                        std::uint32_t& count,
                                        const std::uint32_t frameCount,
                                        const double bpm)
{
    const double samplesPerBeat = sampleRate_ * 60.0 / std::max(1.0, bpm);
    double local = freeBeatSamples_;
    while (local < static_cast<double>(frameCount))
    {
        if (count < frames.size())
            frames[count++] = static_cast<std::uint32_t>(std::round(local));
        local += samplesPerBeat;
    }
    freeBeatSamples_ = local - static_cast<double>(frameCount);
}

void Processor::runGeneration(ProcessResult& result, const std::uint32_t frame)
{
    emitCurrentCells(result, frame);

    std::array<bool, kCellCount> next {};
    std::array<bool, kCellCount> nextBorn {};
    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            const std::size_t index = cellIndex(row, col);
            const int neighbors = liveNeighborCount(row, col);
            const bool alive = cells_[index];
            bool nextAlive = alive ? (neighbors == 2 || neighbors == 3) : neighbors == 3;
            const std::uint32_t mutationThreshold = static_cast<std::uint32_t>(parameters_[kParamMutation] * 1900.0f);
            if (mutationThreshold > 0u && (random() & 0xffffu) < mutationThreshold)
                nextAlive = !nextAlive;
            next[index] = nextAlive;
            nextBorn[index] = nextAlive && !alive;
        }
    }

    cells_ = next;
    born_ = nextBorn;
    for (std::uint32_t i = 0; i < kCellCount; ++i)
        parameters_[i] = cells_[i] ? 1.0f : 0.0f;
    ++generation_;
    updateStatus();
}

void Processor::emitCurrentCells(ProcessResult& result, const std::uint32_t frame)
{
    const int emitMode = static_cast<int>(std::lround(parameters_[kParamEmitMode]));
    if (emitMode == static_cast<int>(EmitMode::full))
    {
        for (std::size_t row = 0; row < kGridHeight; ++row)
        {
            for (std::size_t col = 0; col < kGridWidth; ++col)
            {
                const std::size_t index = cellIndex(row, col);
                if (cells_[index])
                    emitCell(result, frame, row, col, born_[index]);
            }
        }
        return;
    }

    std::uint32_t emitted = 0;
    for (std::size_t row = 0; row < kGridHeight && emitted < kLeanMaxNotesPerGeneration; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth && emitted < kLeanMaxNotesPerGeneration; ++col)
        {
            const std::size_t index = cellIndex(row, col);
            if (cells_[index] && born_[index])
            {
                emitCell(result, frame, row, col, true);
                ++emitted;
            }
        }
    }

    const std::size_t startCol = static_cast<std::size_t>(generation_ % kGridWidth);
    for (std::size_t colOffset = 0; colOffset < kGridWidth && emitted < kLeanMaxNotesPerGeneration; ++colOffset)
    {
        const std::size_t col = (startCol + colOffset) % kGridWidth;
        for (std::size_t row = 0; row < kGridHeight && emitted < kLeanMaxNotesPerGeneration; ++row)
        {
            const std::size_t index = cellIndex(row, col);
            if (cells_[index] && !born_[index])
            {
                emitCell(result, frame, row, col, false);
                ++emitted;
            }
        }
    }
}

void Processor::emitCell(ProcessResult& result,
                         const std::uint32_t frame,
                         const std::size_t row,
                         const std::size_t col,
                         const bool born)
{
    const std::uint32_t gateFrames = static_cast<std::uint32_t>((0.08 + parameters_[kParamGate] * 0.72) *
                                                                sampleRate_ * 60.0 / 120.0);
    emitNote(result, frame, outputChannel(row), noteForCell(row, col), velocityForCell(row, col, born), gateFrames);
}

void Processor::emitNote(ProcessResult& result,
                         const std::uint32_t frame,
                         const std::uint8_t channel,
                         const std::uint8_t note,
                         const std::uint8_t velocity,
                         const std::uint32_t gateFrames)
{
    const std::uint8_t status = static_cast<std::uint8_t>(0x90u | (channel & 0x0fu));
    appendMidi(result, frame, status, note, velocity);
    appendNoteOff(status, note, gateFrames);
}

void Processor::appendNoteOff(const std::uint8_t status, const std::uint8_t note, const std::uint32_t framesLeft)
{
    for (PendingNoteOff& pending : pendingNoteOffs_)
    {
        if (!pending.active)
        {
            pending.active = true;
            pending.status = status;
            pending.note = note;
            pending.framesLeft = framesLeft;
            return;
        }
    }
}

void Processor::requestPanic()
{
    panicRequested_ = true;
}

void Processor::runPanic(ProcessResult& result)
{
    for (PendingNoteOff& pending : pendingNoteOffs_)
    {
        if (pending.active)
        {
            appendMidi(result, 0, pending.status, pending.note, 0u);
            pending.active = false;
        }
    }

    appendSysex(result, kProgrammerModeSysex.data(), static_cast<std::uint16_t>(kProgrammerModeSysex.size()));
    appendClearAllSysex(result);
    appendClearAllMidi(result);

    clearCells();
    parameters_[kParamRunning] = 0.0f;
    generation_ = 0;
    lastHostBeat_ = static_cast<std::uint64_t>(-1);
    freeBeatSamples_ = 0.0;
    ledRefreshSamples_ = 0;
    lastCellLeds_.fill(255u);
    lastTopLeds_.fill(255u);
    lastSideLeds_.fill(255u);
    ledInitialized_ = true;
    suppressLedFeedbackOnce_ = true;
    panicRequested_ = false;
    updateStatus();
}

void Processor::emitLedFeedback(ProcessResult& result, const bool force)
{
    if (parameters_[kParamLedFeedback] < 0.5f)
        return;

    if (!ledInitialized_)
        appendSysex(result, kProgrammerModeSysex.data(), static_cast<std::uint8_t>(kProgrammerModeSysex.size()));

    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            const std::size_t index = cellIndex(row, col);
            const std::uint8_t color = ledColorForCell(row, col, cells_[index]);
            if (force || color != lastCellLeds_[index])
            {
                appendMidi(result, 0, 0x90u, gridToNote(row, col), color);
                lastCellLeds_[index] = color;
            }
        }
    }

    const std::array<std::uint8_t, kTopButtonCCs.size()> topColors = {{
        kLedGreen, kLedWhite, kLedPurple, kLedRed, kLedBlue, kLedOrange, kLedOrange, kLedCyan, kLedRed,
    }};
    for (std::size_t i = 0; i < topColors.size(); ++i)
    {
        const std::uint8_t color = i == 0 && parameters_[kParamRunning] < 0.5f ? kLedYellow : topColors[i];
        if (force || color != lastTopLeds_[i])
        {
            appendMidi(result, 0, 0xb0u, kTopButtonCCs[i], color);
            lastTopLeds_[i] = color;
        }
    }

    const int seed = static_cast<int>(std::lround(parameters_[kParamSeed]));
    for (std::size_t i = 0; i < kSideButtonCCs.size(); ++i)
    {
        const std::uint8_t color = static_cast<int>(i) == seed ? kLedPink : kLedDim;
        if (force || color != lastSideLeds_[i])
        {
            appendMidi(result, 0, 0xb0u, kSideButtonCCs[i], color);
            lastSideLeds_[i] = color;
        }
    }

    ledInitialized_ = true;
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

void Processor::appendSysex(ProcessResult& result, const std::uint8_t* data, const std::uint16_t size)
{
    if (result.eventCount >= result.events.size() || data == nullptr || size > result.events[0].data.size())
        return;

    MidiMessage& event = result.events[result.eventCount++];
    event.frame = 0;
    event.size = size;
    for (std::uint16_t i = 0; i < size; ++i)
        event.data[i] = data[i];
}

void Processor::appendClearAllSysex(ProcessResult& result)
{
    std::array<std::uint8_t, 256> data {};
    std::uint16_t size = 0;
    auto append = [&](const std::uint8_t byte) {
        if (size < data.size())
            data[size++] = byte;
    };
    auto appendLight = [&](const std::uint8_t id) {
        append(0x00u);
        append(id);
        append(kLedOff);
    };

    append(0xf0u);
    append(0x00u);
    append(0x20u);
    append(0x29u);
    append(0x02u);
    append(0x0du);
    append(0x03u);

    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
            appendLight(gridToNote(row, col));
    }

    for (const std::uint8_t cc : kSideButtonCCs)
        appendLight(cc);
    for (const std::uint8_t cc : kTopButtonCCs)
        appendLight(cc);

    append(0xf7u);
    appendSysex(result, data.data(), size);
}

void Processor::appendClearAllMidi(ProcessResult& result)
{
    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
            appendMidi(result, 0, 0x90u, gridToNote(row, col), kLedOff);
    }

    for (const std::uint8_t cc : kSideButtonCCs)
        appendMidi(result, 0, 0xb0u, cc, kLedOff);
    for (const std::uint8_t cc : kTopButtonCCs)
        appendMidi(result, 0, 0xb0u, cc, kLedOff);
}

int Processor::liveNeighborCount(const std::size_t row, const std::size_t col) const noexcept
{
    int count = 0;
    for (int dr = -1; dr <= 1; ++dr)
    {
        for (int dc = -1; dc <= 1; ++dc)
        {
            if (dr == 0 && dc == 0)
                continue;
            const auto rr = static_cast<std::size_t>((static_cast<int>(row) + dr + static_cast<int>(kGridHeight)) %
                                                     static_cast<int>(kGridHeight));
            const auto cc = static_cast<std::size_t>((static_cast<int>(col) + dc + static_cast<int>(kGridWidth)) %
                                                     static_cast<int>(kGridWidth));
            if (cells_[cellIndex(rr, cc)])
                ++count;
        }
    }
    return count;
}

std::uint8_t Processor::outputChannel(const std::size_t row) const noexcept
{
    const int base = clampValue(static_cast<int>(std::lround(parameters_[kParamBaseChannel])) - 1, 0, 15);
    const int mode = static_cast<int>(std::lround(parameters_[kParamOutputMode]));
    if (mode == static_cast<int>(OutputMode::single) || mode == static_cast<int>(OutputMode::drums))
        return static_cast<std::uint8_t>(base);
    const int lane = row < 2 ? 0 : row < 5 ? 1 : 2;
    return static_cast<std::uint8_t>(clampValue(base + lane, 0, 15));
}

std::uint8_t Processor::noteForCell(const std::size_t row, const std::size_t col) const noexcept
{
    const int mode = static_cast<int>(std::lround(parameters_[kParamOutputMode]));
    if (mode == static_cast<int>(OutputMode::drums))
        return kDrumNotes[(row + col) % kDrumNotes.size()];

    const int degree = static_cast<int>(col) + static_cast<int>(row % 2u) * 2;
    const int octave = static_cast<int>(row / 2u) - 1;
    return static_cast<std::uint8_t>(clampValue(scaleDegreeToMidi(degree, octave), 0, 127));
}

std::uint8_t Processor::velocityForCell(const std::size_t row, const std::size_t col, const bool born) const noexcept
{
    const float base = 40.0f + parameters_[kParamVelocity] * 56.0f;
    const float rowLift = static_cast<float>(row) * 3.0f;
    const float colAccent = (col == (generation_ % 8u)) ? 12.0f : 0.0f;
    return static_cast<std::uint8_t>(clampValue(static_cast<int>(std::lround(base + rowLift + colAccent + (born ? 18.0f : 0.0f))), 1, 127));
}

std::uint8_t Processor::ledColorForCell(const std::size_t row, const std::size_t col, const bool alive) const noexcept
{
    if (!alive)
        return kLedOff;
    if (born_[cellIndex(row, col)])
        return kLedWhite;
    if (row < 2)
        return kLedBlue;
    if (row < 5)
        return kLedGreen;
    if (row < 7)
        return kLedOrange;
    return kLedPurple;
}

std::uint32_t Processor::random()
{
    randomState_ ^= randomState_ << 13u;
    randomState_ ^= randomState_ >> 17u;
    randomState_ ^= randomState_ << 5u;
    return randomState_;
}

int Processor::scaleDegreeToMidi(const int degree, const int octaveShift) const noexcept
{
    const auto scale = static_cast<std::size_t>(clampValue(static_cast<int>(std::lround(parameters_[kParamScale])), 0, static_cast<int>(ScaleId::count) - 1));
    const int wrapped = ((degree % 8) + 8) % 8;
    const int octaves = degree / 8 + octaveShift;
    return static_cast<int>(std::lround(parameters_[kParamRootNote])) + kScaleIntervals[scale][static_cast<std::size_t>(wrapped)] + octaves * 12;
}

void Processor::updateStatus()
{
    int active = 0;
    int births = 0;
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        if (cells_[i])
            ++active;
        if (born_[i])
            ++births;
    }
    status_.activeCells = static_cast<float>(active);
    status_.generation = static_cast<float>(generation_ % 1024u);
    status_.births = static_cast<float>(births);
}

} // namespace downspout::lifeform
