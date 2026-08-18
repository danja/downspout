#pragma once

#include <cstdint>

namespace downspout::campione {

enum ParameterIndex : std::uint32_t {
    kParamMasterVolume = 0,
    kParamMidiChannel,
    kParamCrossfadeDuration,
    kParamPitchBendRange,
    kParamRecording,   // boolean: 1=recording, 0=idle (UI→DSP via parameter channel)
    kParamMcpEnabled,  // boolean: 1=MCP server running (default on), 0=disabled
    kParamZonesVersion, // hidden counter bumped on every zone list change; drives UI refresh
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
    kStateZoneDsp,       // UI sends "idx|attackMs|decayMs|sustainLevel|releaseMs|filterEnabled|filterType|filterCutoff|filterQ"
    kStateZoneSlice,     // UI sends "idx|numSlices|startNote"  (numSlices=0 → auto-detect)
    kStatePatchSave,     // UI sends file path → DSP writes Turtle patch
    kStatePatchLoad,     // UI sends file path → DSP reads Turtle patch + reloads zones
    kStateDataDir,       // UI sends data directory path → DSP updates recording/patch output dir
    kStateZonePreview,   // UI sends zone index (or -1 to stop) → DSP injects synthetic note
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
inline constexpr const char* kStateKeyZoneDsp       = "zone_dsp";
inline constexpr const char* kStateKeyZoneSlice     = "zone_slice";
inline constexpr const char* kStateKeyPatchSave     = "patch_save";
inline constexpr const char* kStateKeyPatchLoad     = "patch_load";
inline constexpr const char* kStateKeyDataDir        = "data_dir";
inline constexpr const char* kStateKeyZonePreview    = "zone_preview";

inline constexpr const char* kDefaultDataDir        = "/.vst3/campione-data";

}  // namespace downspout::campione
