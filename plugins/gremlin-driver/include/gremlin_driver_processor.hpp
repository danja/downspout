#pragma once

#include "gremlin_driver_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::gremlin_driver {

struct TransportSnapshot {
    bool transportDetected = false;
    bool transportRunning = false;
    bool playing = true;
    float bpm = 120.0f;
};

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 4> data {};
};

struct Status {
    std::array<float, kLaneCount> laneValues {};
    std::array<float, kTriggerCount> triggerFlashes {};
    float transportIndicator = 0.0f;
    float effectiveBpm = 120.0f;
    float randomizeFlash = 0.0f;
};

inline constexpr std::size_t kMaxProcessEvents = 384;

struct ProcessResult {
    std::array<MidiMessage, kMaxProcessEvents> events {};
    std::uint32_t eventCount = 0;
    Status status {};
};

class Processor {
public:
    void init(double sampleRate);
    void activate();

    void setClockMode(int mode);
    void setBpm(float bpm);
    void setLaneTarget(std::size_t laneIndex, int target);
    void setLaneShape(std::size_t laneIndex, int shape);
    void setLaneRate(std::size_t laneIndex, float rate);
    void setLaneDepth(std::size_t laneIndex, float depth);
    void setLaneCenter(std::size_t laneIndex, float center);
    void setTriggerAction(std::size_t triggerIndex, int action);
    void setTriggerRate(std::size_t triggerIndex, float rate);
    void setTriggerChance(std::size_t triggerIndex, float chance);
    void triggerRandomize();

    [[nodiscard]] int getClockMode() const noexcept;
    [[nodiscard]] float getBpm() const noexcept;
    [[nodiscard]] const LaneConfig& getLane(std::size_t laneIndex) const noexcept;
    [[nodiscard]] const TriggerConfig& getTrigger(std::size_t triggerIndex) const noexcept;
    [[nodiscard]] const Status& getStatus() const noexcept;

    ProcessResult processBlock(std::uint32_t frameCount,
                               const TransportSnapshot& transport,
                               const MidiMessage* inputEvents,
                               std::uint32_t inputEventCount);

private:
    static constexpr std::size_t kRandomizeCcCount = kPrimaryKnobCCs.size() + kHiddenKnobCCs.size();

    struct RandomizeCcEvent {
        std::uint8_t cc = 0;
        std::uint8_t value = 0;
    };

    void resetToDefaults();
    void appendEvent(ProcessResult& result,
                     std::uint32_t frame,
                     std::uint8_t status,
                     std::uint8_t data1,
                     std::uint8_t data2);
    void appendPassThrough(ProcessResult& result,
                           const MidiMessage& message);
    void prepareRandomizeBurst();
    void emitPendingRandomizeBurst(ProcessResult& result, std::uint32_t frameCount);
    std::uint8_t randomPrimaryKnobValue(std::uint8_t cc);
    std::uint8_t randomHiddenKnobValue(std::uint8_t cc);
    int clampCc(float normalized) const;
    std::uint32_t nextRandom();
    std::uint8_t randomCc(std::uint8_t minValue = 0, std::uint8_t maxValue = 127);

    double sampleRate_ = 44100.0;
    flues::gremlindriver::GremlinDriverEngine engine_ {};
    std::array<LaneConfig, kLaneCount> lanes_ {};
    std::array<TriggerConfig, kTriggerCount> triggers_ {};
    std::array<int, kTargetCount> lastSentCc_ {};
    std::array<RandomizeCcEvent, kRandomizeCcCount> randomizeBurst_ {};
    std::uint32_t refreshSamples_ = 0;
    std::uint32_t randomState_ = 0x3c6ef372u;
    int randomizeBlocksRemaining_ = 0;
    int clockMode_ = static_cast<int>(ClockMode::Transport);
    float bpm_ = 120.0f;
    Status status_ {};
};

}  // namespace downspout::gremlin_driver
