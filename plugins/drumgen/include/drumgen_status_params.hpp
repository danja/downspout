#pragma once

#include <cstdint>

namespace downspout::drumgen::status_params {

// Appended read-only parameters. Both the DPF wrapper and the UI anchor their
// ParameterIndex enums to these values, so saved project indices stay stable.
enum : std::uint32_t {
    kDensity = 25,
    kVariation = 26,
    kVary = 27,
    kParameterCount = 28,
};

// The Pattern Mix sliders that show the effective (post-Conductor) values.
enum : std::uint32_t {
    kDensityControl = 6,
    kVariationControl = 7,
    kVaryControl = 19,
};

[[nodiscard]] inline std::uint32_t displayControlFor(const std::uint32_t statusIndex) noexcept
{
    switch (statusIndex) {
    case kDensity:   return kDensityControl;
    case kVariation: return kVariationControl;
    case kVary:      return kVaryControl;
    default:         return statusIndex;
    }
}

}  // namespace downspout::drumgen::status_params
