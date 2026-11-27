#pragma once

#include "energise_core.hpp"

#include <cstdint>

namespace downspout::energise {

// A deterministic break used by the Demo button and by the offline accuracy
// fixtures. It contains kicks, snares, hats, a ghost note, an overlapping
// kick+hat and a cymbal tail, with a short third-bar tag for tempo coverage.
[[nodiscard]] CaptureBuffer makeDemoBreak(const int sampleRate, const std::uint32_t seed);

}  // namespace downspout::energise
