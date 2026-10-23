// DCBlocker.hpp
// High-pass filter to remove DC offset; critical for feedback loops and distortion stages.
// The implementation is shared (include/downspout/dsp/dc_blocker.hpp).

#pragma once

#include "downspout/dsp/dc_blocker.hpp"

namespace downspout::syrinx {

using DCBlocker = downspout::dsp::DCBlocker;

} // namespace downspout::syrinx
