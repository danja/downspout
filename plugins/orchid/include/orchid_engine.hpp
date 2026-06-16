#pragma once

#include "orchid_core_types.hpp"

namespace downspout::orchid {

[[nodiscard]] Parameters clampParameters(const Parameters& raw);
void activate(EngineState& state, double sampleRate, std::uint32_t channelCount);
[[nodiscard]] OutputStatus processBlock(EngineState& state,
                                        const Parameters& parameters,
                                        const TransportSnapshot& transport,
                                        std::uint32_t nframes,
                                        double sampleRate,
                                        const AudioBlock& audio);

}  // namespace downspout::orchid
