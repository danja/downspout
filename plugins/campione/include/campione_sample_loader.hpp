#pragma once

#include "campione_core_types.hpp"

#include <string>
#include <vector>

namespace downspout::campione {

inline constexpr std::uint32_t kMaxChannels = 8;

struct ZoneLoadResult {
    SampleZone zone;
    std::string error;
    bool hasEmbeddedRootNote = false;  // smpl unityNote was present; skip pitch detection
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

// Return true if the WAV file contains a Serum/clm wavetable marker chunk.
// Fast: only scans chunk headers, does not decode audio data.
[[nodiscard]] bool wavHasClmChunk(const std::string& path);

// Load all renderable slices from a REX2 (.rx2) file into separate SampleZones.
// Slices are assigned consecutive MIDI notes starting at 36 (C1), one note per
// slice, with rangeLow == rangeHigh == rootNote so each slice plays only on its
// own note. Muted slices are skipped but still consume a MIDI note number.
// Returns an empty string on success; a non-empty error string on failure.
[[nodiscard]] std::string loadRex2Zones(const std::string& path,
                                         std::vector<SampleZone>& zones);

}  // namespace downspout::campione
