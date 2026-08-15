#pragma once

#include <cstdint>

namespace downspout::campione {

enum ParameterIndex : std::uint32_t {
    kParamMasterVolume = 0,
    kParamMidiChannel,
    kParamCrossfadeDuration,
    kParamPitchBendRange,
    kParamRecording,   // boolean: 1=recording, 0=idle (UI→DSP via parameter channel)
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateParameters = 0,
    kStateZones,
    kStateZoneLoad,
    kStateZoneRemove,    // UI sends index string
    kStateZoneUpdate,    // UI sends "idx|rootNote|rangeLow|rangeHigh|loopEnabled[|loopStart|loopEnd]"
    kStateZoneNormalize, // UI sends index string
    kStateZoneTrim,      // UI sends "idx|threshold_db"
    kStateZoneFade,      // UI sends "idx|fade_in_ms|fade_out_ms"
    kStateZoneReverse,   // UI sends index string
    kStateCount
};

inline constexpr const char* kStateKeyParameters    = "parameters";
inline constexpr const char* kStateKeyZones         = "zones";
inline constexpr const char* kStateKeyZoneLoad      = "zone_load";
inline constexpr const char* kStateKeyZoneRemove    = "zone_remove";
inline constexpr const char* kStateKeyZoneUpdate    = "zone_update";
inline constexpr const char* kStateKeyZoneNormalize = "zone_normalize";
inline constexpr const char* kStateKeyZoneTrim      = "zone_trim";
inline constexpr const char* kStateKeyZoneFade      = "zone_fade";
inline constexpr const char* kStateKeyZoneReverse   = "zone_reverse";

}  // namespace downspout::campione
