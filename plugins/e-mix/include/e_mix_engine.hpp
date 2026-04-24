#pragma once

#include "e_mix_core_types.hpp"

#include <cstdint>

namespace downspout::emix {

[[nodiscard]] Parameters clampParameters(const Parameters& raw);
[[nodiscard]] bool blockIsActive(const Parameters& rawParameters, int blockIndex);
void activate(EngineState& state);
void processBlock(EngineState& state,
                  const Parameters& parameters,
                  const TransportSnapshot& transport,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio);

}  // namespace downspout::emix
