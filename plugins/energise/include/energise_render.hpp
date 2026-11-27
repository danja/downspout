#pragma once

#include "energise_core.hpp"

#include <cstdint>

namespace downspout::energise {

// One deterministic class voice. The returned buffer may be shorter than the
// published tail when the class decays early, but never longer.
[[nodiscard]] RenderedBuffer renderVoice(const DrumEvent& event,
                                         const RenderSettings& settings,
                                         const std::uint32_t seed,
                                         const int sampleRate);

// Full offline render: transient/body voices plus a crossfaded residual slice
// of the source hit, with per-class gain, pan, mute/solo and output trim.
[[nodiscard]] RenderedBuffer renderBreak(const CaptureBuffer& capture,
                                         const std::vector<DrumEvent>& events,
                                         const RenderSettings& settings,
                                         const std::uint32_t seed,
                                         const std::uint16_t muteMask,
                                         const std::uint16_t soloMask,
                                         const float outputTrim);

[[nodiscard]] double tailSeconds(const DrumClass cls) noexcept;

}  // namespace downspout::energise
