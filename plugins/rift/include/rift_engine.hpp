#pragma once

#include "rift_core_types.hpp"

namespace downspout::rift {

[[nodiscard]] Parameters clampParameters(const Parameters& raw);
[[nodiscard]] ActionWeights calculateActionWeights(const Parameters& rawParameters);
[[nodiscard]] ActionType previewActionForBlock(const Parameters& rawParameters, std::uint64_t blockIndex);
[[nodiscard]] bool isSampleSourceUsable(const SampleSource& source);
[[nodiscard]] double sampleLoopBeats(const SampleSource& source);

void activate(EngineState& state, double sampleRate, std::uint32_t channelCount);
[[nodiscard]] OutputStatus processBlock(EngineState& state,
                                        const Parameters& parameters,
                                        const Triggers& triggers,
                                        const TransportSnapshot& transport,
                                        std::uint32_t nframes,
                                        double sampleRate,
                                        const AudioBlock& audio);
[[nodiscard]] OutputStatus processBlock(EngineState& state,
                                        const Parameters& parameters,
                                        const Triggers& triggers,
                                        const TransportSnapshot& transport,
                                        std::uint32_t nframes,
                                        double sampleRate,
                                        const AudioBlock& audio,
                                        const SamplePlayback& samplePlayback);

}  // namespace downspout::rift
