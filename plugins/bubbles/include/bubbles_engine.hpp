#pragma once

#include "bubbles_core_types.hpp"

#include <cstdint>

namespace downspout::bubbles {

[[nodiscard]] Parameters clampParameters(const Parameters& raw);

void activate(EngineState& state, double sampleRate);

void processBlock(EngineState&           state,
                  const Parameters&      params,
                  const TransportSnapshot& transport,
                  uint32_t               nframes,
                  double                 sampleRate,
                  float*                 outL,
                  float*                 outR,
                  const InputMidiEvent*  midiEvents,
                  uint32_t               midiEventCount);

}  // namespace downspout::bubbles
