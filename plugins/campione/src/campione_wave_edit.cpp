#include "campione_wave_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace downspout::campione {

std::string normalizeZone(SampleZone& zone)
{
    if (zone.data.empty())
        return "zone has no audio data";

    float peak = 0.0f;
    for (float s : zone.data)
        peak = std::max(peak, std::abs(s));

    if (peak < 1e-9f)
        return "zone is silent — nothing to normalize";

    const float gain = 1.0f / peak;
    for (float& s : zone.data)
        s *= gain;

    return {};
}

void trimZone(SampleZone& zone, float thresholdLinear)
{
    if (zone.data.empty()) return;
    const int ch = std::max(1, zone.channelCount);
    const auto totalFrames = static_cast<int>(zone.data.size()) / ch;

    // Find first frame above threshold
    int firstFrame = 0;
    for (int f = 0; f < totalFrames; ++f) {
        for (int c = 0; c < ch; ++c) {
            if (std::abs(zone.data[static_cast<std::size_t>(f * ch + c)]) >= thresholdLinear) {
                firstFrame = f;
                goto found_start;
            }
        }
    }
    found_start:;

    // Find last frame above threshold
    int lastFrame = totalFrames - 1;
    for (int f = totalFrames - 1; f >= firstFrame; --f) {
        for (int c = 0; c < ch; ++c) {
            if (std::abs(zone.data[static_cast<std::size_t>(f * ch + c)]) >= thresholdLinear) {
                lastFrame = f;
                goto found_end;
            }
        }
    }
    found_end:;

    if (firstFrame >= lastFrame) return;

    // Erase tail first (to keep indices valid)
    zone.data.erase(zone.data.begin() + (lastFrame + 1) * ch, zone.data.end());
    zone.data.erase(zone.data.begin(), zone.data.begin() + firstFrame * ch);

    // Adjust loop points to stay within new bounds
    const auto trimmedFrames = static_cast<uint32_t>(lastFrame - firstFrame + 1);
    const auto offset = static_cast<uint32_t>(firstFrame);

    if (zone.loopStart >= offset)
        zone.loopStart -= offset;
    else
        zone.loopStart = 0;

    if (zone.loopEnd >= offset)
        zone.loopEnd = std::min(zone.loopEnd - offset, trimmedFrames - 1);
    else
        zone.loopEnd = trimmedFrames > 0 ? trimmedFrames - 1 : 0;
}

void fadeZone(SampleZone& zone, uint32_t fadeInFrames, uint32_t fadeOutFrames)
{
    if (zone.data.empty()) return;
    const int ch = std::max(1, zone.channelCount);
    const auto totalFrames = static_cast<uint32_t>(zone.data.size()) / static_cast<uint32_t>(ch);

    fadeInFrames  = std::min(fadeInFrames,  totalFrames / 2);
    fadeOutFrames = std::min(fadeOutFrames, totalFrames / 2);

    for (uint32_t f = 0; f < fadeInFrames; ++f) {
        const float g = static_cast<float>(f) / static_cast<float>(fadeInFrames);
        for (int c = 0; c < ch; ++c)
            zone.data[static_cast<std::size_t>(f * static_cast<uint32_t>(ch) + static_cast<uint32_t>(c))] *= g;
    }

    for (uint32_t f = 0; f < fadeOutFrames; ++f) {
        const uint32_t frame = totalFrames - 1 - f;
        const float g = static_cast<float>(f) / static_cast<float>(fadeOutFrames);
        for (int c = 0; c < ch; ++c)
            zone.data[static_cast<std::size_t>(frame * static_cast<uint32_t>(ch) + static_cast<uint32_t>(c))] *= g;
    }
}

void reverseZone(SampleZone& zone)
{
    if (zone.data.empty()) return;
    const int ch = std::max(1, zone.channelCount);
    const auto totalFrames = static_cast<uint32_t>(zone.data.size()) / static_cast<uint32_t>(ch);

    for (uint32_t a = 0, b = totalFrames - 1; a < b; ++a, --b) {
        for (int c = 0; c < ch; ++c) {
            std::swap(
                zone.data[static_cast<std::size_t>(a * static_cast<uint32_t>(ch) + static_cast<uint32_t>(c))],
                zone.data[static_cast<std::size_t>(b * static_cast<uint32_t>(ch) + static_cast<uint32_t>(c))]
            );
        }
    }

    // Mirror loop points
    if (zone.loopEnd > zone.loopStart) {
        const uint32_t newStart = totalFrames - 1 - zone.loopEnd;
        const uint32_t newEnd   = totalFrames - 1 - zone.loopStart;
        zone.loopStart = newStart;
        zone.loopEnd   = newEnd;
    }
}

}  // namespace downspout::campione
