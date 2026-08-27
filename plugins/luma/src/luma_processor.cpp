#include "luma_processor.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::luma {
namespace {

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

[[nodiscard]] std::uint32_t hashCell(const std::uint64_t step,
                                     const std::size_t row,
                                     const std::size_t col,
                                     const std::uint32_t seed) noexcept
{
    std::uint32_t value = static_cast<std::uint32_t>(step * 0x9e3779b1u) ^ seed;
    value ^= static_cast<std::uint32_t>((row + 1u) * 0x85ebca6bu);
    value ^= static_cast<std::uint32_t>((col + 3u) * 0xc2b2ae35u);
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

constexpr std::array<std::uint8_t, 9> kProgrammerModeSysex = {{
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x0d, 0x0e, 0x01, 0xf7,
}};

constexpr std::array<std::array<int, 8>, static_cast<std::size_t>(ScaleId::count)> kScaleIntervals = {{
    {{0, 2, 4, 5, 7, 9, 11, 12}},  // major
    {{0, 2, 4, 5, 7, 9, 11, 12}},  // ionian
    {{0, 2, 3, 5, 7, 8, 10, 12}},  // minor
    {{0, 2, 3, 5, 7, 9, 10, 12}},  // dorian
    {{0, 2, 4, 5, 7, 9, 10, 12}},  // mixolydian
    {{0, 2, 4, 7, 9, 12, 14, 16}}, // pentatonic
    {{0, 3, 5, 6, 7, 10, 12, 15}}, // blues
    {{0, 1, 4, 5, 7, 8, 10, 12}},  // phrygianDominant
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
    lastCellLeds_.fill(255u);
    lastTopLeds_.fill(255u);
    lastSideLeds_.fill(255u);
    pendingNoteOffs_.fill({});
    ledRefreshSamples_ = 0;
    freeStep_ = 0;
    freeStepSamples_ = 0.0;
    lastHostStep_ = static_cast<std::uint64_t>(-1);
    ledInitialized_ = false;
    updateStatus();
}

float Processor::parameterDefault(const std::uint32_t index) const noexcept
{
    if (index < kCellCount)
        return 0.0f;
    switch (index)
    {
    case kParamRootNote: return 48.0f;
    case kParamScale: return 1.0f;
    case kParamDensity: return 0.42f;
    case kParamEnergy: return 0.48f;
    case kParamGate: return 0.45f;
    case kParamSwing: return 0.0f;
    case kParamClockMode: return 0.0f;
    case kParamOutputMode: return 0.0f;
    case kParamBaseChannel: return 1.0f;
    case kParamLedFeedback: return 1.0f;
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
    case kParamBaseChannel:
        value = static_cast<float>(clampValue(static_cast<int>(std::lround(value)), 1, 16));
        break;
    case kParamLedFeedback:
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
    case kParamStatusActive:
    case kParamStatusStep:
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
    if (index == kParamStatusStep)
        return status_.currentStep;
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

    bool forceLeds = false;
    for (std::uint32_t i = 0; i < inputEventCount; ++i)
    {
        if (handleMidi(inputEvents[i], result))
            forceLeds = true;
    }

    const int clockMode = static_cast<int>(std::lround(parameters_[kParamClockMode]));
    const bool useHost = (clockMode == static_cast<int>(ClockMode::host)) ||
                         (clockMode == static_cast<int>(ClockMode::autoClock) && transport.valid);
    const bool useFree = (clockMode == static_cast<int>(ClockMode::free)) ||
                         (clockMode == static_cast<int>(ClockMode::autoClock) && !transport.valid);

    if (useHost && transport.valid && transport.playing)
        scheduleClockedSteps(result, frameCount, transport);
    else if (useFree)
        scheduleFreeSteps(result, frameCount, transport.bpm > 1.0 ? transport.bpm : 120.0);

    const bool periodicRefresh = ledRefreshSamples_ >= static_cast<std::uint32_t>(sampleRate_ * 0.75);
    emitLedFeedback(result, forceLeds || periodicRefresh || !ledInitialized_);
    ledRefreshSamples_ = periodicRefresh ? 0u : ledRefreshSamples_ + frameCount;

    updateStatus();
    result.status = status_;
    return result;
}

void Processor::clearCells()
{
    cells_.fill(false);
    for (std::uint32_t i = 0; i < kCellCount; ++i)
        parameters_[i] = 0.0f;
    updateStatus();
}

void Processor::randomizeCells()
{
    cells_.fill(false);
    for (std::uint32_t i = 0; i < kCellCount; ++i)
    {
        const std::size_t row = i / kGridWidth;
        const float rowBias = row < 2 ? 0.10f : row < 4 ? 0.13f : row < 6 ? 0.16f : 0.11f;
        cells_[i] = (random() & 0xffffu) < static_cast<std::uint32_t>(rowBias * 65535.0f);
        parameters_[i] = cells_[i] ? 1.0f : 0.0f;
    }
    updateStatus();
}

bool Processor::handleMidi(const MidiMessage& event, ProcessResult& result)
{
    if (event.size < 3)
        return false;

    const std::uint8_t status = event.data[0] & 0xf0u;
    const std::uint8_t data1 = event.data[1];
    const std::uint8_t data2 = event.data[2];

    if (status == 0x90u && data2 > 0u)
    {
        if (handleGridPress(data1))
            return true;
    }
    if (status == 0xb0u && data2 > 0u)
    {
        if (handleTopButton(data1) || handleSideButton(data1))
            return true;
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
    parameters_[index] = cells_[index] ? 1.0f : 0.0f;
    updateStatus();
    return true;
}

bool Processor::handleTopButton(const std::uint8_t cc)
{
    const int index = findIndex(cc, kTopButtonCCs.data(), kTopButtonCCs.size());
    if (index < 0)
        return false;

    switch (index)
    {
    case 0: setParameter(kParamRootNote, parameters_[kParamRootNote] - 1.0f); break;
    case 1: setParameter(kParamRootNote, parameters_[kParamRootNote] + 1.0f); break;
    case 2: setParameter(kParamScale, parameters_[kParamScale] - 1.0f); break;
    case 3: setParameter(kParamScale, parameters_[kParamScale] + 1.0f); break;
    case 4: setParameter(kParamDensity, parameters_[kParamDensity] - 0.08f); break;
    case 5: setParameter(kParamDensity, parameters_[kParamDensity] + 0.08f); break;
    case 6: clearCells(); break;
    case 7: randomizeCells(); break;
    case 8:
        clearCells();
        pendingNoteOffs_.fill({});
        break;
    default: break;
    }
    return true;
}

bool Processor::handleSideButton(const std::uint8_t cc)
{
    const int index = findIndex(cc, kSideButtonCCs.data(), kSideButtonCCs.size());
    if (index < 0)
        return false;

    setParameter(kParamEnergy, static_cast<float>(index + 1) / 8.0f);
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

void Processor::scheduleClockedSteps(ProcessResult& result,
                                     const std::uint32_t frameCount,
                                     const TransportSnapshot& transport)
{
    const double safeBpm = std::max(1.0, transport.bpm);
    const double beatsPerSecond = safeBpm / 60.0;
    const double startBeat = transport.bar * std::max(1.0, transport.beatsPerBar) + transport.barBeat;
    const double stepsPerBeat = 4.0;
    const double startStep = startBeat * stepsPerBeat;
    const double endStep = startStep + static_cast<double>(frameCount) * beatsPerSecond * stepsPerBeat / sampleRate_;

    std::uint64_t firstStep = static_cast<std::uint64_t>(std::floor(startStep));
    if (static_cast<double>(firstStep) < startStep - 1.0e-9)
        ++firstStep;

    const std::uint64_t lastStep = static_cast<std::uint64_t>(std::floor(endStep + 1.0e-9));
    for (std::uint64_t step = firstStep; step <= lastStep; ++step)
    {
        if (step == lastHostStep_)
            continue;

        const double stepBeat = static_cast<double>(step) / stepsPerBeat;
        const double frame = (stepBeat - startBeat) / beatsPerSecond * sampleRate_;
        const double swingDelay = (step % 2u) == 1u
            ? parameters_[kParamSwing] * (sampleRate_ / beatsPerSecond / stepsPerBeat) * 0.45
            : 0.0;
        const double swungFrame = frame + swingDelay;
        if (swungFrame >= -0.5 && swungFrame < static_cast<double>(frameCount))
        {
            emitStep(result, static_cast<std::uint32_t>(std::max(0.0, std::round(swungFrame))), step);
            lastHostStep_ = step;
        }
    }
}

void Processor::scheduleFreeSteps(ProcessResult& result, const std::uint32_t frameCount, const double bpm)
{
    const double samplesPerStep = sampleRate_ * 60.0 / std::max(1.0, bpm) / 4.0;
    double local = freeStepSamples_;
    while (local < static_cast<double>(frameCount))
    {
        const double swingDelay = (freeStep_ % 2u) == 1u ? parameters_[kParamSwing] * samplesPerStep * 0.45 : 0.0;
        const double frame = local + swingDelay;
        if (frame < static_cast<double>(frameCount))
            emitStep(result, static_cast<std::uint32_t>(std::round(frame)), freeStep_);
        ++freeStep_;
        local += samplesPerStep;
    }
    freeStepSamples_ = local - static_cast<double>(frameCount);
}

void Processor::emitStep(ProcessResult& result, const std::uint32_t frame, const std::uint64_t step)
{
    status_.currentStep = static_cast<float>(step % 16u);
    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            if (cells_[cellIndex(row, col)])
                emitCell(result, frame, step, row, col);
        }
    }
}

void Processor::emitCell(ProcessResult& result,
                         const std::uint32_t frame,
                         const std::uint64_t step,
                         const std::size_t row,
                         const std::size_t col)
{
    if (!shouldFire(step, row, col))
        return;

    const float chance = chanceForCell(step, row, col);
    const float draw = static_cast<float>(hashCell(step, row, col, randomState_) & 0xffffu) / 65535.0f;
    if (draw > chance)
        return;

    const AgentRole role = roleForRow(row);
    const std::uint8_t channel = outputChannel(role);
    const std::uint32_t gateFrames = static_cast<std::uint32_t>((0.08 + parameters_[kParamGate] * 0.42) *
                                                                sampleRate_ * 60.0 / 120.0);
    if (role == AgentRole::chord)
    {
        emitNote(result, frame, channel, noteForCell(role, row, col, 0), velocityForCell(role, row, col), gateFrames);
        emitNote(result, frame, channel, noteForCell(role, row, col, 2), velocityForCell(role, row, col) - 8u, gateFrames);
        emitNote(result, frame, channel, noteForCell(role, row, col, 4), velocityForCell(role, row, col) - 14u, gateFrames);
        return;
    }

    emitNote(result, frame, channel, noteForCell(role, row, col, 0), velocityForCell(role, row, col), gateFrames);
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

    const std::array<std::uint8_t, 9> topColors = {{
        kLedBlue,
        kLedBlue,
        kLedPurple,
        kLedPurple,
        kLedGreen,
        kLedGreen,
        kLedRed,
        kLedAmber,
        kLedWhite,
    }};
    for (std::size_t i = 0; i < topColors.size(); ++i)
    {
        if (force || topColors[i] != lastTopLeds_[i])
        {
            appendMidi(result, 0, 0xb0u, kTopButtonCCs[i], topColors[i]);
            lastTopLeds_[i] = topColors[i];
        }
    }

    const int energyIndex = clampValue(static_cast<int>(std::ceil(parameters_[kParamEnergy] * 8.0f)) - 1, 0, 7);
    for (std::size_t i = 0; i < kSideButtonCCs.size(); ++i)
    {
        const std::uint8_t color = static_cast<int>(i) <= energyIndex ? kLedYellow : kLedOff;
        if (force || color != lastSideLeds_[i])
        {
            appendMidi(result, 0, 0xb0u, kSideButtonCCs[i], color);
            lastSideLeds_[i] = color;
        }
    }

    ledInitialized_ = true;
    status_.ledActivity = 1.0f;
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

void Processor::appendSysex(ProcessResult& result, const std::uint8_t* data, const std::uint8_t size)
{
    if (result.eventCount >= result.events.size() || data == nullptr || size > result.events[0].data.size())
        return;

    MidiMessage& event = result.events[result.eventCount++];
    event.frame = 0;
    event.size = size;
    for (std::uint8_t i = 0; i < size; ++i)
        event.data[i] = data[i];
}

Processor::AgentRole Processor::roleForRow(const std::size_t row) const noexcept
{
    if (row < 2)
        return AgentRole::bass;
    if (row < 4)
        return AgentRole::chord;
    if (row < 6)
        return AgentRole::melody;
    return AgentRole::drum;
}

bool Processor::shouldFire(const std::uint64_t step, const std::size_t row, const std::size_t col) const noexcept
{
    const AgentRole role = roleForRow(row);
    int period = 4;
    switch (role)
    {
    case AgentRole::bass: period = row == 0 ? 8 : 4; break;
    case AgentRole::chord: period = row == 2 ? 8 : 4; break;
    case AgentRole::melody: period = row == 4 ? 2 : 1; break;
    case AgentRole::drum: period = col < 2 ? 4 : (col < 5 ? 2 : 1); break;
    }

    return (step % static_cast<std::uint64_t>(period)) == static_cast<std::uint64_t>(col % static_cast<std::size_t>(period));
}

std::uint8_t Processor::outputChannel(const AgentRole role) const noexcept
{
    const int base = clampValue(static_cast<int>(std::lround(parameters_[kParamBaseChannel])) - 1, 0, 15);
    if (static_cast<int>(std::lround(parameters_[kParamOutputMode])) == static_cast<int>(OutputMode::single))
        return static_cast<std::uint8_t>(base);

    switch (role)
    {
    case AgentRole::bass: return static_cast<std::uint8_t>(clampValue(base + 1, 0, 15));
    case AgentRole::drum: return 9u;
    default: return static_cast<std::uint8_t>(base);
    }
}

std::uint8_t Processor::noteForCell(const AgentRole role,
                                    const std::size_t row,
                                    const std::size_t col,
                                    const int chordOffset) const noexcept
{
    if (role == AgentRole::drum)
        return kDrumNotes[col % kDrumNotes.size()];

    int degree = static_cast<int>(col) + chordOffset;
    int octave = 0;
    switch (role)
    {
    case AgentRole::bass:
        degree = static_cast<int>(col % 5);
        octave = row == 0 ? -2 : -1;
        break;
    case AgentRole::chord:
        octave = row == 2 ? 0 : 1;
        break;
    case AgentRole::melody:
        degree = static_cast<int>(col) + (row == 5 ? 3 : 0);
        octave = row == 5 ? 2 : 1;
        break;
    case AgentRole::drum:
        break;
    }

    return static_cast<std::uint8_t>(clampValue(scaleDegreeToMidi(degree, octave), 0, 127));
}

std::uint8_t Processor::velocityForCell(const AgentRole role, const std::size_t row, const std::size_t col) const noexcept
{
    const float energy = parameters_[kParamEnergy];
    int base = 58 + static_cast<int>(energy * 52.0f) + static_cast<int>(row * 3u) + static_cast<int>(col);
    if (role == AgentRole::bass)
        base += 8;
    if (role == AgentRole::drum)
        base += 12;
    return static_cast<std::uint8_t>(clampValue(base, 1, 127));
}

std::uint8_t Processor::ledColorForCell(const std::size_t row, const std::size_t, const bool active) const noexcept
{
    if (!active)
        return row < 2 ? 1u : 0u;

    switch (roleForRow(row))
    {
    case AgentRole::bass: return kLedBlue;
    case AgentRole::chord: return kLedAmber;
    case AgentRole::melody: return kLedGreen;
    case AgentRole::drum: return kLedRed;
    }
    return kLedWhite;
}

float Processor::chanceForCell(const std::uint64_t, const std::size_t row, const std::size_t col) const noexcept
{
    const AgentRole role = roleForRow(row);
    float chance = 0.25f + parameters_[kParamDensity] * 0.55f + parameters_[kParamEnergy] * 0.18f;
    if (role == AgentRole::bass)
        chance += row == 0 ? 0.24f : 0.10f;
    if (role == AgentRole::chord)
        chance += 0.08f;
    if (role == AgentRole::drum && col >= 5)
        chance -= 0.16f;
    return clampValue(chance, 0.02f, 1.0f);
}

int Processor::scaleDegreeToMidi(const int degree, const int octaveShift) const noexcept
{
    const int scaleIndex = clampValue(static_cast<int>(std::lround(parameters_[kParamScale])),
                                     0,
                                     static_cast<int>(ScaleId::count) - 1);
    const auto& scale = kScaleIntervals[static_cast<std::size_t>(scaleIndex)];
    const int octave = degree >= 0 ? degree / 7 : (degree - 6) / 7;
    const int wrapped = degree - octave * 7;
    const int root = static_cast<int>(std::lround(parameters_[kParamRootNote]));
    return root + octaveShift * 12 + octave * 12 + scale[static_cast<std::size_t>(wrapped)];
}

std::uint32_t Processor::random()
{
    randomState_ ^= randomState_ << 13u;
    randomState_ ^= randomState_ >> 17u;
    randomState_ ^= randomState_ << 5u;
    return randomState_;
}

void Processor::updateStatus()
{
    std::size_t active = 0;
    for (const bool cell : cells_)
    {
        if (cell)
            ++active;
    }
    status_.activeCells = static_cast<float>(active);
    status_.ledActivity = std::max(0.0f, status_.ledActivity - 0.04f);
}

} // namespace downspout::luma
