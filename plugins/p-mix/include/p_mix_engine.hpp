#pragma once

#include "p_mix_core_types.hpp"

#include <array>
#include <cstdint>

namespace downspout::pmix {

struct AudioBlock {
    std::array<const float*, kMaxChannels> inputs {};
    std::array<float*, kMaxChannels> outputs {};
    std::uint32_t channelCount = kMaxChannels;
};

[[nodiscard]] Parameters clampParameters(const Parameters& raw);
void activate(EngineState& state);
void processBlock(EngineState& state,
                  const Parameters& parameters,
                  const TransportSnapshot& transport,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio);

}  // namespace downspout::pmix
