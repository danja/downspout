#pragma once

#include "gater_core_types.hpp"
#include <cstdint>

namespace downspout::gater {

struct AudioBlock {
    // 1 input, 2 outputs. 
    // Wait, the prompt said "one audio input and two audio outputs"
    // Let's assume stereo for everything for now.
    const float* inputL = nullptr;
    const float* inputR = nullptr;
    
    float* output1L = nullptr;
    float* output1R = nullptr;
    
    float* output2L = nullptr;
    float* output2R = nullptr;
    
    std::uint32_t channelCount = 2;
};

void processBlock(EngineState& state,
                  const Parameters& parameters,
                  const TransportSnapshot& transport,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio);

}  // namespace downspout::gater
