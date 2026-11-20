#pragma once

#include "fontana_core_types.hpp"

#include <cstddef>

namespace downspout::fontana {

[[nodiscard]] SliceSet equalSlices(const SampleSource& source, int count, int baseNote);

// Energy/transient detection: a slice begins where a short-window RMS jump
// exceeds `sensitivity` (0..1, higher = fewer slices). Always keeps the first
// frame and the sample end as boundaries.
[[nodiscard]] SliceSet transientSlices(const SampleSource& source, float sensitivity, int baseNote);

// Clamps every slice to the source, drops empty slices and keeps at most
// kMaxSlices entries.
[[nodiscard]] SliceSet normalizeSlices(SliceSet slices, std::uint32_t frameCount);

[[nodiscard]] int sliceIndexForNote(const SliceSet& slices, int note, int baseNote) noexcept;

}  // namespace downspout::fontana
