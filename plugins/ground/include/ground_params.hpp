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
    kParameterCount
};

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
