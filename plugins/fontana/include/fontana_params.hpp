#pragma once

#include <cstdint>

namespace downspout::fontana {

enum ParameterIndex : std::uint32_t {
    kParamMasterVolume = 0,
    kParamMidiChannel,
    kParamBank,
    kParamProgram,
    kParamTranspose,
    kParamBendRange,
    kParamAttack,
    kParamRelease,
    kParamSourceMode,
    kParamSliceBaseNote,
    kParamSliceLoop,
    kParamFilterEnabled,
    kParamFilterCutoff,
    kParamStatusBank,
    kParamStatusProgram,
    kParamStatusVoices,
    kParamStatusLastNote,
    kParamStatusLoad,
    kParameterCount
};

enum StateIndex : std::uint32_t {
    kStateSettings = 0,
    kStateSoundFontPath,
    kStateSamplePath,
    kStateSliceMap,
    kStatePreviewPath,
    kStatePreviewTrigger,
    kStateCount
};

inline constexpr const char* kStateKeySettings = "settings";
inline constexpr const char* kStateKeySoundFontPath = "soundfont_path";
inline constexpr const char* kStateKeySamplePath = "sample_path";
inline constexpr const char* kStateKeySliceMap = "slice_map";
inline constexpr const char* kStateKeyPreviewPath = "preview_path";
inline constexpr const char* kStateKeyPreviewTrigger = "preview_trigger";

inline constexpr const char* kFileFilterAudio = "Audio & SoundFont files|*.wav;*.WAV;*.ogg;*.OGG;*.sf2;*.SF2;*.sf3;*.SF3";
inline constexpr const char* kFileFilterSoundFont = "SoundFont files|*.sf2;*.SF2;*.sf3;*.SF3";

}  // namespace downspout::fontana
