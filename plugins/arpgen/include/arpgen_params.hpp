#pragma once

#include <cstdint>

namespace downspout::arpgen {

enum ParameterIndex : std::uint32_t {
    kParamMode = 0,
    kParamOrder,
    kParamRate,
    kParamCaptureSlice,
    kParamKey,
    kParamScale,
    kParamScaleShape,
    kParamOctaves,
    kParamGate,
    kParamVelocityFollow,
    kParamPassInput,
    kParamOutputChannel,
    kParamStatusMaterial,
    kParamStatusNote,
    kParamStatusInput,
    kParamStatusOutput,
    kParamConductorChannel,  // appended after the status outputs so existing indices stay put
    kParameterCount
};

}  // namespace downspout::arpgen
