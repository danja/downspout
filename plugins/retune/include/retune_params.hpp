#pragma once

#include "generative_common.hpp"
#include "retune_engine.hpp"

#include <array>
#include <cstdint>

namespace downspout::retune {

using downspout::generative::ParamSpec;

enum Param : std::uint32_t {
    kRoot,
    kBendRange,
    kStatusDegrees,
    kStatusNotes,
    kStatusLoaded,
    kParameterCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"root", "Root note", 0, 127, 60, true},
    {"bend_range", "Bend range", 1, 24, 2, true},
    {"status_degrees", "Scale degrees", 0, 1024, 12, true, true},
    {"status_notes", "Sounding notes", 0, 15, 0, true, true},
    {"status_loaded", "Scale loaded", 0, 1, 0, true, true},
}};

// Output channels 2-16 (0-based 1-15); channel 1 is left free as an MPE master.
inline Settings toSettings(const float root, const float bendRange)
{
    Settings s;
    s.rootNote = static_cast<int>(root);
    s.bendRange = static_cast<double>(bendRange);
    s.firstChannel = 1;
    s.lastChannel = 15;
    return s;
}

}  // namespace downspout::retune
