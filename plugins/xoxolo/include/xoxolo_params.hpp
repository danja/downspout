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
    kParameterCount
};

inline constexpr const char* kStateKeyPattern = "pattern";

}  // namespace downspout::xoxolo
