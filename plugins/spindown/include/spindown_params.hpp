#pragma once

#include "spindown_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::spindown {

// Parameter indices are stable: never reorder, only append.
// 0-7 controls, then two output (status) parameters the panel reads.
enum ParameterId : std::uint32_t {
    kTrigger = 0, kStop, kStopTime, kStartTime, kCurve, kCycle, kLength, kCcChannel,
    kStatusSpeed, kStatusDelay,
    kParameterCount
};
inline constexpr std::uint32_t kControlCount = kStatusSpeed;

[[nodiscard]] constexpr bool isOutput(const std::uint32_t index) noexcept { return index >= kStatusSpeed && index < kParameterCount; }

// Index into the CC takeover table for a control, or -1 when the control has no CC.
[[nodiscard]] constexpr int overrideIndex(const std::uint32_t index) noexcept
{
    switch (index) {
    case kStopTime:  return 0;
    case kStartTime: return 1;
    case kCurve:     return 2;
    case kStop:      return 3;
    default:         return -1;
    }
}

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool integer;
    bool output;
};

[[nodiscard]] inline const std::array<ParameterSpec, kParameterCount>& specs()
{
    static const Parameters d;
    static const std::array<ParameterSpec, kParameterCount> s = {{
        {"trigger", "Trigger", "", 0.0f, static_cast<float>(kTriggerCount - 1), d.trigger, true, false},
        {"stop", "Stop", "", 0.0f, 1.0f, d.stop, true, false},
        {"stop_time", "Stop Time", "ms", kMinStopMs, kMaxStopMs, d.stopMs, false, false},
        {"start_time", "Start Time", "ms", kMinStopMs, kMaxStopMs, d.startMs, false, false},
        {"curve", "Curve", "", kMinCurve, kMaxCurve, d.curve, false, false},
        {"cycle", "Cycle", "", 0.0f, static_cast<float>(kCycleCount - 1), d.cycle, true, false},
        {"length", "Length", "beats", kMinLengthBeats, kMaxLengthBeats, d.length, false, false},
        {"cc_channel", "CC Channel", "", 0.0f, 16.0f, d.ccChannel, true, false},
        {"status_speed", "Tape Speed", "", 0.0f, 1.0f, 1.0f, false, true},
        {"status_delay", "Tape Lag", "ms", 0.0f, kBufferSeconds * 1000.0f, 0.0f, false, true},
    }};
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kTrigger:   return p.trigger;
    case kStop:      return p.stop;
    case kStopTime:  return p.stopMs;
    case kStartTime: return p.startMs;
    case kCurve:     return p.curve;
    case kCycle:     return p.cycle;
    case kLength:    return p.length;
    case kCcChannel: return p.ccChannel;
    default:         return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::spindown
