#pragma once

#include <cstdint>

namespace downspout::ground {

enum ParameterIndex : std::uint32_t {
    kParamRootNote = 0,
    kParamScale,
    kParamStyle,
    kParamChannel,
    kParamFormBars,
    kParamPhraseBars,
    kParamDensity,
    kParamMotion,
    kParamTension,
    kParamCadence,
    kParamRegister,
    kParamRegisterArc,
    kParamSequence,
    kParamSeed,
    kParamVary,
    kParamActionNewForm,
    kParamActionNewPhrase,
    kParamActionMutateCell,
    kParamStatusPhrase,
    kParamStatusRole,
    kParamColor,
    kParamNoteLength,
    kParamNoteLengthVariation,
    kParamFormShape,
    kParamPhraseRole0,
    kParamClamp = kParamPhraseRole0 + 32,
    kParamConductorChannel,
    kParameterCount
};

inline constexpr std::uint32_t kParamPhraseRoleStart = kParamPhraseRole0;
inline constexpr std::uint32_t kParamPhraseRoleCount = 32;

enum StateIndex : std::uint32_t {
    kStateControls = 0,
    kStateForm,
    kStateVariation,
    kStateCount
};

inline constexpr const char* kStateKeyControls = "controls";
inline constexpr const char* kStateKeyForm = "form";
inline constexpr const char* kStateKeyVariation = "variation";

}  // namespace downspout::ground
