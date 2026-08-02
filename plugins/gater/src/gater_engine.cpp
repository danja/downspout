#include "gater_engine.hpp"

namespace downspout::gater {

void processBlock(EngineState& state,
                  const Parameters& parameters,
                  const TransportSnapshot& transport,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio)
{
    for (std::uint32_t i = 0; i < nframes; ++i)
    {
        float inputL = audio.inputL[i];
        float inputR = audio.inputR[i];

        if (state.activeOutput == 0)
        {
            audio.output1L[i] = inputL;
            audio.output1R[i] = inputR;
            audio.output2L[i] = 0.0f;
            audio.output2R[i] = 0.0f;
        }
        else
        {
            audio.output1L[i] = 0.0f;
            audio.output1R[i] = 0.0f;
            audio.output2L[i] = inputL;
            audio.output2R[i] = inputR;
        }
    }
}

} // namespace downspout::gater
