#pragma once

#include "t_mix_core_types.hpp"

namespace downspout::tmix {

[[nodiscard]] Parameters clampParameters(const Parameters& parameters);
[[nodiscard]] float decibelsToGain(float decibels);
void activate(EngineState& state);
[[nodiscard]] OutputStatus processBlock(EngineState& state,
                                        const Parameters& parameters,
                                        std::uint32_t frameCount,
                                        double sampleRate,
                                        const AudioBlock& audio);

}  // namespace downspout::tmix
