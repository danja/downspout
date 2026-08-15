#pragma once

#include "campione_core_types.hpp"

#include <vector>

namespace downspout::campione {

[[nodiscard]] Parameters clampParameters(const Parameters& parameters);

void activate(EngineState& state);

void processBlock(EngineState& state,
                  const Parameters& params,
                  const std::vector<SampleZone>& zones,
                  const MidiInputEvent* midiEvents,
                  std::uint32_t midiCount,
                  std::uint32_t frames,
                  double hostSampleRate,
                  AudioBlock audio);

}  // namespace downspout::campione
