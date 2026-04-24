#pragma once

#include "drumgen_core_types.hpp"

#include <cstdint>

namespace downspout::drumgen {

[[nodiscard]] bool transportRestartDetected(bool wasPlaying,
                                            std::int64_t lastTransportStep,
                                            std::int64_t startStepFloor);

[[nodiscard]] double localStepFromAbsolute(const PatternState& pattern, double absSteps);
[[nodiscard]] int localStepForBoundary(const PatternState& pattern, std::int64_t boundary);
[[nodiscard]] std::uint32_t frameForBoundary(double absStepsStart,
                                             double absStepsEnd,
                                             std::uint32_t nframes,
                                             std::int64_t boundary);

}  // namespace downspout::drumgen
