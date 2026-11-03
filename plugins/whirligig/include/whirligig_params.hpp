#pragma once

#include "whirligig_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::whirligig {

// Parameter indices are stable: never reorder, only append.
// 0-23 controls, then five output (status) parameters the panel reads.
enum ParameterId : std::uint32_t {
    kSpeed = 0, kHornSlow, kHornFast, kDrumSlow, kDrumFast, kHornAccel, kDrumAccel, kCrossover,
    kHornDepth, kHornDoppler, kDrumDepth, kDrumDoppler, kMicAngle, kHornLevel, kDrumLevel, kMix, kOutput, kCcChannel,
    kSync, kHornSlowDiv, kHornFastDiv, kDrumSlowDiv, kDrumFastDiv, kSyncPhase,
    kStatusHornHz, kStatusDrumHz, kStatusHornTurn, kStatusDrumTurn, kStatusLocked,
    kParameterCount
};
inline constexpr std::uint32_t kControlCount = kStatusHornHz;

[[nodiscard]] constexpr bool isOutput(const std::uint32_t index) noexcept { return index >= kStatusHornHz && index < kParameterCount; }

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
        {"speed", "Speed", "", 0.0f, 2.0f, d.speed, true, false},
        {"horn_slow", "Horn Slow", "Hz", 0.3f, 1.5f, d.hornSlowHz, false, false},
        {"horn_fast", "Horn Fast", "Hz", 4.0f, 9.0f, d.hornFastHz, false, false},
        {"drum_slow", "Drum Slow", "Hz", 0.3f, 1.2f, d.drumSlowHz, false, false},
        {"drum_fast", "Drum Fast", "Hz", 3.0f, 8.0f, d.drumFastHz, false, false},
        {"horn_accel", "Horn Accel", "s", 0.1f, 4.0f, d.hornAccelS, false, false},
        {"drum_accel", "Drum Accel", "s", 0.5f, 10.0f, d.drumAccelS, false, false},
        {"crossover", "Crossover", "Hz", 300.0f, 2000.0f, d.crossoverHz, false, false},
        {"horn_depth", "Horn Depth", "", 0.0f, 1.0f, d.hornDepth, false, false},
        {"horn_doppler", "Horn Doppler", "", 0.0f, 1.0f, d.hornDoppler, false, false},
        {"drum_depth", "Drum Depth", "", 0.0f, 1.0f, d.drumDepth, false, false},
        {"drum_doppler", "Drum Doppler", "", 0.0f, 1.0f, d.drumDoppler, false, false},
        {"mic_angle", "Mic Angle", "deg", 0.0f, 180.0f, d.micAngle, false, false},
        {"horn_level", "Horn Level", "dB", -24.0f, 12.0f, d.hornLevelDb, false, false},
        {"drum_level", "Drum Level", "dB", -24.0f, 12.0f, d.drumLevelDb, false, false},
        {"mix", "Mix", "", 0.0f, 1.0f, d.mix, false, false},
        {"output", "Output", "dB", -24.0f, 12.0f, d.outputDb, false, false},
        {"cc_channel", "CC Channel", "", 0.0f, 16.0f, d.ccChannel, true, false},
        {"sync", "Sync", "", 0.0f, 1.0f, d.sync, true, false},
        {"horn_slow_div", "Horn Slow Beats", "", 0.0f, static_cast<float>(kDivisionCount - 1), d.hornSlowDiv, true, false},
        {"horn_fast_div", "Horn Fast Beats", "", 0.0f, static_cast<float>(kDivisionCount - 1), d.hornFastDiv, true, false},
        {"drum_slow_div", "Drum Slow Beats", "", 0.0f, static_cast<float>(kDivisionCount - 1), d.drumSlowDiv, true, false},
        {"drum_fast_div", "Drum Fast Beats", "", 0.0f, static_cast<float>(kDivisionCount - 1), d.drumFastDiv, true, false},
        {"sync_phase", "Sync Phase", "deg", 0.0f, 360.0f, d.syncPhase, false, false},
        {"status_horn_hz", "Horn Rate", "Hz", 0.0f, 9.0f, 0.0f, false, true},
        {"status_drum_hz", "Drum Rate", "Hz", 0.0f, 8.0f, 0.0f, false, true},
        {"status_horn_turn", "Horn Angle", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_drum_turn", "Drum Angle", "", 0.0f, 1.0f, 0.0f, false, true},
        {"status_locked", "Locked", "", 0.0f, 1.0f, 0.0f, true, true},
    }};
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    switch (index) {
    case kSpeed:       return p.speed;
    case kHornSlow:    return p.hornSlowHz;
    case kHornFast:    return p.hornFastHz;
    case kDrumSlow:    return p.drumSlowHz;
    case kDrumFast:    return p.drumFastHz;
    case kHornAccel:   return p.hornAccelS;
    case kDrumAccel:   return p.drumAccelS;
    case kCrossover:   return p.crossoverHz;
    case kHornDepth:   return p.hornDepth;
    case kHornDoppler: return p.hornDoppler;
    case kDrumDepth:   return p.drumDepth;
    case kDrumDoppler: return p.drumDoppler;
    case kMicAngle:    return p.micAngle;
    case kHornLevel:   return p.hornLevelDb;
    case kDrumLevel:   return p.drumLevelDb;
    case kMix:         return p.mix;
    case kOutput:      return p.outputDb;
    case kCcChannel:   return p.ccChannel;
    case kSync:        return p.sync;
    case kHornSlowDiv: return p.hornSlowDiv;
    case kHornFastDiv: return p.hornFastDiv;
    case kDrumSlowDiv: return p.drumSlowDiv;
    case kDrumFastDiv: return p.drumFastDiv;
    case kSyncPhase:   return p.syncPhase;
    default:           return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::whirligig
