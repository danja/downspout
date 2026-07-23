#pragma once

#include "xoxolo_core_types.hpp"

#include <cstdint>

namespace downspout::xoxolo {

enum class GenerationStyle : std::int32_t {
    jazz = 0,
    drumAndBass,
    house,
    funk,
    rock,
    latin,
    count
};

struct GenerationSettings {
    GenerationStyle style = GenerationStyle::jazz;
    float density = 0.55f;
    float tension = 0.35f;
};

[[nodiscard]] GenerationSettings clampGenerationSettings(const GenerationSettings& settings);
[[nodiscard]] const char* generationStyleName(GenerationStyle style);

// Replaces the visible grid with a generated pattern. Templates provide the
// style anchors; seed-controlled variation adds density and syncopated tension.
void generatePattern(PatternState& pattern,
                     const GenerationSettings& settings,
                     std::uint32_t seed);

}  // namespace downspout::xoxolo
