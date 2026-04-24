#include "gremlin_driver_processor.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::gremlin_driver {
namespace {

template <typename T>
constexpr std::size_t idx(const T value) noexcept
{
    return static_cast<std::size_t>(value);
}

float clampf(const float value, const float minValue, const float maxValue) noexcept
{
    return std::clamp(value, minValue, maxValue);
}

int clampi(const int value, const int minValue, const int maxValue) noexcept
{
    return std::clamp(value, minValue, maxValue);
}

}  // namespace

void Processor::init(const double sampleRate)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    engine_.init(sampleRate_);
    activate();
}

void Processor::activate()
{
    engine_.reset();
    refreshSamples_ = 0;
    randomState_ = 0x3c6ef372u;
    randomizePending_ = false;
    lastSentCc_.fill(-1);
    resetToDefaults();
}

void Processor::resetToDefaults()
{
    clockMode_ = static_cast<int>(kGlobalParamSpecs[0].defaultValue);
    bpm_ = kGlobalParamSpecs[1].defaultValue;

    for (std::size_t i = 0; i < kLaneCount; ++i)
    {
        lanes_[i].target = static_cast<int>(kLaneParamSpecs[i * 5 + 0].defaultValue);
        lanes_[i].shape = static_cast<int>(kLaneParamSpecs[i * 5 + 1].defaultValue);
        lanes_[i].rate = kLaneParamSpecs[i * 5 + 2].defaultValue;
        lanes_[i].depth = kLaneParamSpecs[i * 5 + 3].defaultValue;
        lanes_[i].center = kLaneParamSpecs[i * 5 + 4].defaultValue;
    }

    for (std::size_t i = 0; i < kTriggerCount; ++i)
    {
        triggers_[i].action = static_cast<int>(kTriggerParamSpecs[i * 3 + 0].defaultValue);
        triggers_[i].rate = kTriggerParamSpecs[i * 3 + 1].defaultValue;
        triggers_[i].chance = kTriggerParamSpecs[i * 3 + 2].defaultValue;
    }

    status_ = Status {};
}

void Processor::setClockMode(const int mode)
{
    clockMode_ = clampi(mode, 0, 1);
}

void Processor::setBpm(const float bpm)
{
    bpm_ = clampf(bpm, 40.0f, 220.0f);
}

void Processor::setLaneTarget(const std::size_t laneIndex, const int target)
{
    if (laneIndex >= lanes_.size())
        return;
    lanes_[laneIndex].target = clampi(target, 0, 9);
}

void Processor::setLaneShape(const std::size_t laneIndex, const int shape)
{
    if (laneIndex >= lanes_.size())
        return;
    lanes_[laneIndex].shape = clampi(shape, 0, 5);
}

void Processor::setLaneRate(const std::size_t laneIndex, const float rate)
{
    if (laneIndex >= lanes_.size())
        return;
    lanes_[laneIndex].rate = clampf(rate, 0.0f, 1.0f);
}

void Processor::setLaneDepth(const std::size_t laneIndex, const float depth)
{
    if (laneIndex >= lanes_.size())
        return;
    lanes_[laneIndex].depth = clampf(depth, 0.0f, 1.0f);
}

void Processor::setLaneCenter(const std::size_t laneIndex, const float center)
{
    if (laneIndex >= lanes_.size())
        return;
    lanes_[laneIndex].center = clampf(center, 0.0f, 1.0f);
}

void Processor::setTriggerAction(const std::size_t triggerIndex, const int action)
{
    if (triggerIndex >= triggers_.size())
        return;
    triggers_[triggerIndex].action = clampi(action, 0, 10);
}

void Processor::setTriggerRate(const std::size_t triggerIndex, const float rate)
{
    if (triggerIndex >= triggers_.size())
        return;
    triggers_[triggerIndex].rate = clampf(rate, 0.0f, 1.0f);
}

void Processor::setTriggerChance(const std::size_t triggerIndex, const float chance)
{
    if (triggerIndex >= triggers_.size())
        return;
    triggers_[triggerIndex].chance = clampf(chance, 0.0f, 1.0f);
}

void Processor::triggerRandomize()
{
    randomizePending_ = true;
}

int Processor::getClockMode() const noexcept
{
    return clockMode_;
}

float Processor::getBpm() const noexcept
{
    return bpm_;
}

const LaneConfig& Processor::getLane(const std::size_t laneIndex) const noexcept
{
    return lanes_[laneIndex];
}

const TriggerConfig& Processor::getTrigger(const std::size_t triggerIndex) const noexcept
{
    return triggers_[triggerIndex];
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

    flues::gremlindriver::ClockState clock {};
    clock.transportDetected = transport.transportDetected;
    clock.transportRunning = transport.transportRunning;
    clock.bpm = clampf(transport.bpm, 40.0f, 220.0f);

    if (clockMode_ == static_cast<int>(ClockMode::Free))
    {
        clock.bpm = bpm_;
        clock.running = true;
    }
    else
    {
        clock.running = transport.transportDetected ? transport.transportRunning : true;
    }

    const auto engineResult = engine_.process(frameCount, clock, lanes_, triggers_);
    result.status.laneValues = engineResult.laneValues;
    result.status.triggerFlashes = engineResult.triggerFlashes;
    result.status.transportIndicator = engineResult.transportIndicator;
    result.status.effectiveBpm = engineResult.effectiveBpm;
    status_ = result.status;

    if (randomizePending_)
    {
        emitRandomizedPatch(result);
        randomizePending_ = false;
    }

    refreshSamples_ += frameCount;
    const bool forceRefresh = refreshSamples_ >= static_cast<std::uint32_t>(sampleRate_ * 0.25);
    if (forceRefresh)
        refreshSamples_ = 0;

    for (int target = 1; target <= 9; ++target)
    {
        if (!engineResult.activeTargets[idx(target)])
            continue;

        const int ccValue = clampCc(engineResult.targetValues[idx(target)]);
        if (!forceRefresh && lastSentCc_[idx(target)] == ccValue)
            continue;

        const std::uint8_t cc = target == 9 ? kMasterFaderCC : kMacroFaderCCs[idx(target - 1)];
        appendEvent(result, 0, 0xB0u, cc, static_cast<std::uint8_t>(ccValue));
        lastSentCc_[idx(target)] = ccValue;
    }

    for (std::size_t i = 0; i < kTriggerCount; ++i)
    {
        if (!engineResult.triggerFired[i])
            continue;

        const int action = clampi(triggers_[i].action, 0, 10);
        const std::uint8_t note = kActionNotes[idx(action)];
        if (note == 0u)
            continue;

        appendEvent(result,
                    std::min(engineResult.triggerFrames[i], frameCount > 0 ? frameCount - 1 : 0u),
                    0x90u,
                    note,
                    127u);
    }

    for (std::uint32_t i = 0; i < inputEventCount; ++i)
        appendPassThrough(result, inputEvents[i]);

    return result;
}

void Processor::appendEvent(ProcessResult& result,
                            const std::uint32_t frame,
                            const std::uint8_t status,
                            const std::uint8_t data1,
                            const std::uint8_t data2)
{
    if (result.eventCount >= result.events.size())
        return;

    MidiMessage& message = result.events[result.eventCount++];
    message.frame = frame;
    message.size = 3;
    message.data[0] = status;
    message.data[1] = data1;
    message.data[2] = data2;
    message.data[3] = 0;
}

void Processor::appendPassThrough(ProcessResult& result, const MidiMessage& message)
{
    if (result.eventCount >= result.events.size())
        return;
    result.events[result.eventCount++] = message;
}

void Processor::emitRandomizedPatch(ProcessResult& result)
{
    for (const std::uint8_t cc : kPrimaryKnobCCs)
        appendEvent(result, 0, 0xB0u, cc, randomPrimaryKnobValue(cc));

    for (const std::uint8_t cc : kHiddenKnobCCs)
        appendEvent(result, 0, 0xB0u, cc, randomHiddenKnobValue(cc));
}

std::uint8_t Processor::randomPrimaryKnobValue(const std::uint8_t cc)
{
    switch (cc)
    {
    case 16: return randomCc(20, 82);
    case 20: return randomCc(18, 78);
    case 24: return randomCc(0, 40);
    case 28: return randomCc(8, 58);
    case 46: return randomCc(0, 52);
    case 50: return randomCc(10, 55);
    case 54: return randomCc(0, 20);
    case 58: return randomCc(6, 40);
    case 17: return randomCc(10, 70);
    case 21: return randomCc(18, 78);
    case 25: return randomCc(5, 60);
    case 29: return randomCc(8, 62);
    case 47: return randomCc(60, 118);
    case 51: return randomCc(42, 98);
    case 55: return randomCc(16, 82);
    case 59: return randomCc(30, 76);
    default: return randomCc();
    }
}

std::uint8_t Processor::randomHiddenKnobValue(const std::uint8_t cc)
{
    switch (cc)
    {
    case 18: return randomCc(58, 104);
    case 22: return randomCc(70, 127);
    case 26: return randomCc(10, 68);
    case 30: return randomCc(20, 72);
    case 48: return randomCc(12, 68);
    case 52: return randomCc(10, 54);
    case 56: return randomCc(18, 76);
    case 60: return randomCc(28, 86);
    default: return randomCc();
    }
}

int Processor::clampCc(const float normalized) const
{
    return static_cast<int>(std::lround(clampf(normalized, 0.0f, 1.0f) * 127.0f));
}

std::uint32_t Processor::nextRandom()
{
    randomState_ = randomState_ * 1664525u + 1013904223u;
    return randomState_;
}

std::uint8_t Processor::randomCc(std::uint8_t minValue, std::uint8_t maxValue)
{
    if (maxValue < minValue)
        std::swap(maxValue, minValue);

    const std::uint32_t span = static_cast<std::uint32_t>(maxValue - minValue) + 1u;
    return static_cast<std::uint8_t>(minValue + (nextRandom() % span));
}

}  // namespace downspout::gremlin_driver
