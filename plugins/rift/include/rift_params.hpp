#pragma once

#include <cstdint>

namespace downspout::rift {

enum ParameterIndex : std::uint32_t {
    kParamGrid = 0,
    kParamDensity,
    kParamDamage,
    kParamMemoryBars,
    kParamDrift,
    kParamPitch,
    kParamMix,
    kParamHold,
    kParamScatter,
    kParamRecover,
    kParamBlend,
    kParamSourceMode,
    kParamSampleBeats,
    kParamStatusAction,
    kParamStatusActivity,
    kParamChop,
    kParamDub,
    kParamStatusSequenceCell,
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateSamplePath,
    kStateSequence,
    kStateCCDensity,   // CC number → Density (default 3 = Drift lane 3)
    kStateCCReach,     // CC number → Reach/Drift (default 4 = Drift lane 4)
    kStateCCChannel,   // MIDI channel for CC input (default 1)
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";
inline constexpr const char* kStateKeySamplePath = "sample_path";
inline constexpr const char* kStateKeySequence   = "sequence";
inline constexpr const char* kStateKeyCCDensity  = "cc_density";
inline constexpr const char* kStateKeyCCReach    = "cc_reach";
inline constexpr const char* kStateKeyCCChannel  = "cc_channel";

}  // namespace downspout::rift
