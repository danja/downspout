#pragma once

#include <cstdint>

namespace downspout::campione {

enum ParameterIndex : std::uint32_t {
    kParamMasterVolume = 0,
    kParamMidiChannel,
    kParamCrossfadeDuration,
    kParamPitchBendRange,
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateZones,
    kStateZoneLoad,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";
inline constexpr const char* kStateKeyZones      = "zones";
inline constexpr const char* kStateKeyZoneLoad   = "zone_load";

}  // namespace downspout::campione
