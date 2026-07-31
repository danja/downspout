#pragma once

#include "t_mix_core_types.hpp"

namespace downspout::tmix {

[[nodiscard]] Parameters clampParameters(const Parameters& parameters);
[[nodiscard]] float decibelsToGain(float decibels);
void activate(EngineState& state);
[[nodiscard]] bool handleMidi(EngineState& state,
                              const std::uint8_t* data,
                              std::uint32_t size,
                              const Parameters& parameters);
[[nodiscard]] OutputStatus processBlock(EngineState& state,
                                        const Parameters& parameters,
                                        std::uint32_t frameCount,
                                        double sampleRate,
                                        const AudioBlock& audio,
                                        const MidiControlEvent* midiEvents = nullptr,
                                        std::uint32_t midiEventCount = 0);

}  // namespace downspout::tmix
