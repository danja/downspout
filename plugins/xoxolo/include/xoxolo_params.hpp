#pragma once

#include <cstdint>

namespace downspout::xoxolo {

enum ParameterIndex : std::uint32_t {
    kParamSteps = 0,
    kParamResolution,
    kParamChannel,
    kParamNotePreset,
    kParamClear,
    kParamPreviewLane,
    kParamPreview,
    kParamCurrentStep,
    kParamCaRule,   // appended: indices above are saved in projects
    kParamCaEvery,
    kParamConductorCh,  // appended: Conductor channel (0 = off) and the two controls its CCs drive
    kParamDensity,
    kParamEnergy,
    kParameterCount
};

inline constexpr const char* kStateKeyPattern = "pattern";

}  // namespace downspout::xoxolo
