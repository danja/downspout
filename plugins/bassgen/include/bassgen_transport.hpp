#pragma once

#include "bassgen_core_types.hpp"

#include <cstdint>

namespace downspout::bassgen {

[[nodiscard]] bool transportRestartDetected(bool wasPlaying,
                                            std::int64_t lastTransportStep,
                                            std::int64_t startStepFloor);

[[nodiscard]] double localStepFromAbsolute(const PatternState& pattern, double absSteps);
[[nodiscard]] const NoteEvent* findActiveEvent(const PatternState& pattern, double localStep);
[[nodiscard]] const NoteEvent* findEventStartingAt(const PatternState& pattern, int localStep);
[[nodiscard]] bool anyEventEndsAt(const PatternState& pattern, int localStep);
[[nodiscard]] std::uint32_t frameForBoundary(double absStepsStart,
                                             double absStepsEnd,
                                             std::uint32_t nframes,
                                             std::int64_t boundary);
[[nodiscard]] int localStepForBoundary(const PatternState& pattern, std::int64_t boundary);

}  // namespace downspout::bassgen
