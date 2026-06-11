#pragma once

#include "ambo_core_types.hpp"

#include <cstdint>

namespace downspout::ambo {

[[nodiscard]] Parameters clampParameters(const Parameters& raw);
void activate(EngineState& state, double sampleRate);
[[nodiscard]] OutputStatus processBlock(EngineState& state,
                                        const Parameters& parameters,
                                        std::uint32_t nframes,
                                        double sampleRate,
                                        const AudioBlock& audio);

}  // namespace downspout::ambo
