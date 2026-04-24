#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace flues::gremlindriver {

static constexpr size_t kLaneCount = 4;
static constexpr size_t kTriggerCount = 2;
static constexpr size_t kTargetCount = 10;

enum class ClockMode : int {
    Free = 0,
    Transport = 1
};

enum class LaneTarget : int {
    Off = 0,
    Macro1 = 1,
    Macro2 = 2,
    Macro3 = 3,
    Macro4 = 4,
    Macro5 = 5,
    Macro6 = 6,
    Macro7 = 7,
    Macro8 = 8,
    Master = 9
};

enum class LaneShape : int {
    Sine = 0,
    Triangle = 1,
    Ramp = 2,
    SampleHold = 3,
    RandomWalk = 4,
    Logistic = 5
};

enum class TriggerAction : int {
    Off = 0,
    Reseed = 1,
    Burst = 2,
    RandomSource = 3,
    RandomDelay = 4,
    RandomAll = 5,
    SceneDown = 6,
    SceneUp = 7,
    Panic = 8,
    ModeDown = 9,
    ModeUp = 10
};

struct ClockState {
    float bpm = 120.0f;
    bool running = true;
    bool transportDetected = false;
    bool transportRunning = false;
};

struct LaneConfig {
    int target = static_cast<int>(LaneTarget::Off);
    int shape = static_cast<int>(LaneShape::Sine);
    float rate = 0.5f;
    float depth = 0.5f;
    float center = 0.5f;
};

struct TriggerConfig {
    int action = static_cast<int>(TriggerAction::Off);
    float rate = 0.4f;
    float chance = 0.5f;
};

struct ProcessResult {
    std::array<bool, kTargetCount> activeTargets{};
    std::array<float, kTargetCount> targetValues{};
    std::array<float, kLaneCount> laneValues{};
    std::array<float, kTriggerCount> triggerFlashes{};
    std::array<bool, kTriggerCount> triggerFired{};
    std::array<uint32_t, kTriggerCount> triggerFrames{};
    float transportIndicator = 0.0f;
    float effectiveBpm = 120.0f;
};

class GremlinDriverEngine {
public:
    void init(double sampleRate) {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() {
        rngState_ = 0x6d2b79f5u;
        for (size_t i = 0; i < kLaneCount; ++i) {
            laneStates_[i].phase = randomUnit();
            laneStates_[i].held = randomUnit();
            laneStates_[i].walk = randomUnit();
            laneStates_[i].logistic = 0.2f + 0.6f * randomUnit();
        }
        for (size_t i = 0; i < kTriggerCount; ++i) {
            triggerStates_[i].phase = randomUnit();
            triggerStates_[i].flash = 0.0f;
        }
    }

    ProcessResult process(uint32_t nframes,
                          const ClockState& clock,
                          const std::array<LaneConfig, kLaneCount>& lanes,
                          const std::array<TriggerConfig, kTriggerCount>& triggers) {
        ProcessResult result{};
        result.effectiveBpm = clock.bpm;
        result.transportIndicator = clock.transportRunning ? 1.0f : 0.0f;

        const float safeRate = static_cast<float>(sampleRate_ > 1.0 ? sampleRate_ : 44100.0);
        const float beatsThisBlock = clock.running
            ? (clock.bpm / 60.0f) * (static_cast<float>(nframes) / safeRate)
            : 0.0f;
        const float barsThisBlock = beatsThisBlock / 4.0f;

        std::array<float, kTargetCount> targetSums{};
        std::array<float, kTargetCount> targetWeights{};

        for (size_t i = 0; i < kLaneCount; ++i) {
            LaneState& state = laneStates_[i];
            const LaneConfig& cfg = lanes[i];
            const LaneShape shape = sanitizeShape(cfg.shape);

            if (clock.running) {
                advanceLaneState(state, shape, rateToCyclesPerBar(cfg.rate), barsThisBlock);
            }

            const float normalizedWave = evaluateLane(shape, state);
            const float depth = clampf(cfg.depth);
            const float center = clampf(cfg.center);
            const float laneValue = clampf(center + (normalizedWave - 0.5f) * depth);
            result.laneValues[i] = laneValue;

            const int targetIndex = sanitizeTarget(cfg.target);
            if (targetIndex > 0) {
                result.activeTargets[static_cast<size_t>(targetIndex)] = true;
                targetSums[static_cast<size_t>(targetIndex)] += laneValue;
                targetWeights[static_cast<size_t>(targetIndex)] += 1.0f;
            }
        }

        for (size_t target = 1; target < kTargetCount; ++target) {
            if (targetWeights[target] > 0.0f) {
                result.targetValues[target] = clampf(targetSums[target] / targetWeights[target]);
            }
        }

        const float flashDrop = static_cast<float>(nframes) / std::max(1.0, sampleRate_ * 0.15);
        for (size_t i = 0; i < kTriggerCount; ++i) {
            TriggerState& state = triggerStates_[i];
            state.flash = clampf(state.flash - flashDrop);

            const TriggerConfig& cfg = triggers[i];
            const int action = sanitizeAction(cfg.action);
            if (!clock.running || action == 0) {
                result.triggerFlashes[i] = state.flash;
                continue;
            }

            const float delta = rateToCyclesPerBar(cfg.rate) * barsThisBlock;
            if (delta <= 0.0f) {
                result.triggerFlashes[i] = state.flash;
                continue;
            }

            const float startPhase = state.phase;
            state.phase += delta;

            if (state.phase >= 1.0f) {
                const float blockFraction = std::clamp((1.0f - startPhase) / delta, 0.0f, 1.0f);
                const uint32_t fireFrame = static_cast<uint32_t>(std::lround(blockFraction * static_cast<float>(nframes)));
                state.phase -= std::floor(state.phase);

                if (randomUnit() <= clampf(cfg.chance)) {
                    result.triggerFired[i] = true;
                    result.triggerFrames[i] = std::min(fireFrame, nframes > 0 ? nframes - 1 : 0u);
                    state.flash = 1.0f;
                }
            }

            result.triggerFlashes[i] = state.flash;
        }

        return result;
    }

private:
    struct LaneState {
        float phase = 0.0f;
        float held = 0.5f;
        float walk = 0.5f;
        float logistic = 0.5f;
    };

    struct TriggerState {
        float phase = 0.0f;
        float flash = 0.0f;
    };

    double sampleRate_ = 44100.0;
    uint32_t rngState_ = 0x6d2b79f5u;
    std::array<LaneState, kLaneCount> laneStates_{};
    std::array<TriggerState, kTriggerCount> triggerStates_{};

    static float clampf(float value, float minValue = 0.0f, float maxValue = 1.0f) {
        return std::clamp(value, minValue, maxValue);
    }

    static LaneShape sanitizeShape(int shape) {
        if (shape < static_cast<int>(LaneShape::Sine) || shape > static_cast<int>(LaneShape::Logistic)) {
            return LaneShape::Sine;
        }
        return static_cast<LaneShape>(shape);
    }

    static int sanitizeTarget(int target) {
        if (target < static_cast<int>(LaneTarget::Off) || target > static_cast<int>(LaneTarget::Master)) {
            return static_cast<int>(LaneTarget::Off);
        }
        return target;
    }

    static int sanitizeAction(int action) {
        if (action < static_cast<int>(TriggerAction::Off) || action > static_cast<int>(TriggerAction::ModeUp)) {
            return static_cast<int>(TriggerAction::Off);
        }
        return action;
    }

    static float rateToCyclesPerBar(float normalized) {
        const float clamped = clampf(normalized);
        return std::pow(2.0f, (clamped * 7.0f) - 3.0f);
    }

    float randomUnit() {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return static_cast<float>((rngState_ >> 8) & 0x00ffffffu) / 16777215.0f;
    }

    void advanceLaneState(LaneState& state, LaneShape shape, float cyclesPerBar, float barsThisBlock) {
        const float delta = cyclesPerBar * barsThisBlock;
        if (delta <= 0.0f) {
            return;
        }

        const float startPhase = state.phase;
        state.phase += delta;
        const bool wrapped = state.phase >= 1.0f;
        state.phase -= std::floor(state.phase);

        switch (shape) {
            case LaneShape::SampleHold:
                if (wrapped) {
                    state.held = randomUnit();
                }
                break;
            case LaneShape::RandomWalk: {
                const float step = (randomUnit() - 0.5f) * std::max(0.02f, delta * 2.5f);
                state.walk = clampf(state.walk + step);
                break;
            }
            case LaneShape::Logistic:
                if (wrapped || startPhase == state.phase) {
                    const float r = 3.91f;
                    state.logistic = clampf(r * state.logistic * (1.0f - state.logistic));
                    if (state.logistic < 0.001f || state.logistic > 0.999f) {
                        state.logistic = 0.2f + 0.6f * randomUnit();
                    }
                }
                break;
            default:
                break;
        }
    }

    static float evaluateLane(LaneShape shape, const LaneState& state) {
        constexpr float twoPi = 6.28318530717958647692f;
        switch (shape) {
            case LaneShape::Sine:
                return 0.5f + 0.5f * std::sin(state.phase * twoPi);
            case LaneShape::Triangle:
                return 1.0f - std::fabs((state.phase * 2.0f) - 1.0f);
            case LaneShape::Ramp:
                return state.phase;
            case LaneShape::SampleHold:
                return state.held;
            case LaneShape::RandomWalk:
                return state.walk;
            case LaneShape::Logistic:
                return state.logistic;
            default:
                return 0.5f;
        }
    }
};

} // namespace flues::gremlindriver
