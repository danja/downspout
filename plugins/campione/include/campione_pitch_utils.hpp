#pragma once

#include "campione_core_types.hpp"

#include <cstdint>
#include <vector>

namespace downspout::campione {

// Snap frame to nearest zero-crossing within windowFrames.
[[nodiscard]] std::uint32_t snapToZeroCrossing(const std::vector<float>& data,
                                                int channelCount,
                                                std::uint32_t frame,
                                                std::uint32_t windowFrames,
                                                bool preferRising);

// Stencil algorithm from docs/zero-crossing-algorithm.md.
// Returns a phase-coherent loop start for the given loop end.
[[nodiscard]] std::uint32_t findLoopStart(const std::vector<float>& data,
                                           int channelCount,
                                           std::uint32_t loopEnd,
                                           std::uint32_t searchWindowFrames);

// Set loopEnd (zero-crossing snapped), find loopStart via stencil, compute crossfadeFrames.
void applyLoopPoints(SampleZone& zone, float crossfadeDurationMs);

[[nodiscard]] double freqToMidi(double freqHz);

[[nodiscard]] double computePlaybackRate(const SampleZone& zone,
                                          int midiNote,
                                          double hostSampleRate);

// Autocorrelation pitch detection; used for auto-mapping recorded notes.
[[nodiscard]] double detectFundamentalHz(const std::vector<float>& data,
                                          int channelCount,
                                          double sampleRate);

}  // namespace downspout::campione
