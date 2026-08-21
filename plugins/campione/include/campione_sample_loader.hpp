#pragma once

#include "campione_core_types.hpp"

#include <string>

namespace downspout::campione {

inline constexpr std::uint32_t kMaxChannels = 8;

struct ZoneLoadResult {
    SampleZone zone;
    std::string error;
};

// Load a WAV file (PCM 8/16/24/32-bit or IEEE float32) into a SampleZone.
[[nodiscard]] ZoneLoadResult loadWavZone(const std::string& path);

// Write a SampleZone's data to a 16-bit mono or stereo WAV file.
// Returns an error string, empty on success.
[[nodiscard]] std::string saveWavZone(const SampleZone& zone, const std::string& path);

// Import a single-cycle wavetable WAV (Serum/clm  format or any short WAV).
// Extracts the first frame, saves a loopable copy under destDir/<stem>_wt.wav,
// and returns a zone with loop and rootNote pre-configured.
[[nodiscard]] ZoneLoadResult importWavetableZone(const std::string& srcPath,
                                                  const std::string& destDir);

}  // namespace downspout::campione
