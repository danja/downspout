#include "campione_pitch_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace downspout::campione {
namespace {

float channel0At(const std::vector<float>& data, int channelCount, std::uint32_t frame) noexcept {
    const auto idx = static_cast<std::size_t>(frame) * channelCount;
    return idx < data.size() ? data[idx] : 0.0f;
}

}  // namespace

std::uint32_t snapToZeroCrossing(const std::vector<float>& data,
                                  int channelCount,
                                  std::uint32_t frame,
                                  std::uint32_t windowFrames,
                                  bool preferRising)
{
    if (data.empty() || channelCount <= 0) return frame;
    const auto totalFrames = static_cast<std::uint32_t>(data.size() / channelCount);
    if (frame >= totalFrames) frame = totalFrames > 0 ? totalFrames - 1 : 0;

    const std::uint32_t lo = frame > windowFrames ? frame - windowFrames : 1;
    const std::uint32_t hi = std::min(frame + windowFrames, totalFrames - 1);

    std::uint32_t bestPos = frame;
    std::uint32_t bestDist = windowFrames + 1;

    for (std::uint32_t f = lo; f <= hi; ++f) {
        const float prev = channel0At(data, channelCount, f - 1);
        const float cur  = channel0At(data, channelCount, f);
        const bool rising  = prev <= 0.0f && cur > 0.0f;
        const bool falling = prev >= 0.0f && cur < 0.0f;
        if (!rising && !falling) continue;
        if (preferRising != rising) continue;

        const std::uint32_t dist = f >= frame ? f - frame : frame - f;
        if (dist < bestDist) {
            bestDist = dist;
            bestPos = f;
        }
    }

    return bestPos;
}

std::uint32_t findLoopStart(const std::vector<float>& data,
                              int channelCount,
                              std::uint32_t loopEnd,
                              std::uint32_t searchWindowFrames)
{
    if (data.empty() || channelCount <= 0 || loopEnd < 4) return 0;

    const auto totalFrames = static_cast<std::uint32_t>(data.size() / channelCount);
    if (loopEnd >= totalFrames) loopEnd = totalFrames - 1;

    // Collect zero-crossings up to loopEnd
    std::vector<std::uint32_t> crossings;
    crossings.reserve(2048);
    float prev = channel0At(data, channelCount, 0);
    for (std::uint32_t f = 1; f < loopEnd; ++f) {
        const float cur = channel0At(data, channelCount, f);
        if ((prev <= 0.0f && cur > 0.0f) || (prev >= 0.0f && cur < 0.0f))
            crossings.push_back(f);
        prev = cur;
    }

    if (crossings.size() < 4) return 0;

    // Reference stencil: last crossing before loopEnd
    const std::size_t refIdx = crossings.size() - 1;
    const std::uint32_t refPos = crossings[refIdx];

    const float atRef    = channel0At(data, channelCount, refPos);
    const float beforeRef = refPos > 0 ? channel0At(data, channelCount, refPos - 1) : 0.0f;
    const bool refRising = beforeRef <= 0.0f && atRef > 0.0f;

    if (refIdx < 3) return 0;
    const std::uint32_t d1 = crossings[refIdx]     - crossings[refIdx - 1];
    const std::uint32_t d2 = crossings[refIdx - 1] - crossings[refIdx - 2];
    const std::uint32_t d3 = crossings[refIdx - 2] - crossings[refIdx - 3];
    if (d1 == 0) return 0;

    float bestScore = 1.0e9f;
    std::uint32_t bestPos = 0;

    for (std::uint32_t k = 3; k <= 30; ++k) {
        const double center = static_cast<double>(loopEnd) - static_cast<double>(k) * d1;
        if (center <= 0.0) break;

        const std::uint32_t winLo = center > 2.0 * d1
                                    ? static_cast<std::uint32_t>(center - 2.0 * d1)
                                    : 0u;
        const std::uint32_t winHi = static_cast<std::uint32_t>(
            std::min(static_cast<double>(loopEnd - 1), center + 2.0 * d1));

        for (std::size_t ci = 3; ci < crossings.size(); ++ci) {
            const std::uint32_t pos = crossings[ci];
            if (pos < winLo || pos > winHi) continue;

            const float atPos    = channel0At(data, channelCount, pos);
            const float beforePos = pos > 0 ? channel0At(data, channelCount, pos - 1) : 0.0f;
            const bool rising = beforePos <= 0.0f && atPos > 0.0f;
            if (rising != refRising) continue;

            const std::uint32_t cd1 = crossings[ci]     - crossings[ci - 1];
            const std::uint32_t cd2 = crossings[ci - 1] - crossings[ci - 2];
            const std::uint32_t cd3 = crossings[ci - 2] - crossings[ci - 3];

            float score = 0.0f;
            score += 3.0f * std::abs(static_cast<float>(cd1) / static_cast<float>(d1) - 1.0f);
            if (d2 > 0) score += 2.0f * std::abs(static_cast<float>(cd2) / static_cast<float>(d2) - 1.0f);
            if (d3 > 0) score += 1.0f * std::abs(static_cast<float>(cd3) / static_cast<float>(d3) - 1.0f);
            score /= 6.0f;

            if (score < bestScore) {
                bestScore = score;
                bestPos = pos;
            }
        }
        if (bestScore < 0.01f && k >= 10) break;
    }

    return bestPos;
}

void applyLoopPoints(SampleZone& zone, float crossfadeDurationMs)
{
    const auto totalFrames = static_cast<std::uint32_t>(zone.data.size() / zone.channelCount);
    if (totalFrames < 4) return;

    // Snap loopEnd near end of sample
    const std::uint32_t snapWindow = static_cast<std::uint32_t>((3.0f / 1000.0f) * zone.sampleRate);
    const std::uint32_t endFrame = totalFrames > 0 ? totalFrames - 1 : 0;
    zone.loopEnd = snapToZeroCrossing(zone.data, zone.channelCount, endFrame, snapWindow, false);
    if (zone.loopEnd == 0 || zone.loopEnd >= totalFrames) zone.loopEnd = endFrame;

    // Stencil search for loop start
    const std::uint32_t searchWindow = static_cast<std::uint32_t>((100.0f / 1000.0f) * zone.sampleRate);
    zone.loopStart = findLoopStart(zone.data, zone.channelCount, zone.loopEnd, searchWindow);
    if (zone.loopStart >= zone.loopEnd) zone.loopStart = 0;

    // Crossfade frames
    zone.crossfadeFrames = static_cast<std::uint32_t>((crossfadeDurationMs / 1000.0f) * zone.sampleRate);
    const std::uint32_t loopLength = zone.loopEnd - zone.loopStart;
    if (zone.crossfadeFrames > loopLength / 2) zone.crossfadeFrames = loopLength / 2;
}

double freqToMidi(const double freqHz)
{
    if (freqHz <= 0.0) return 69.0;
    return 69.0 + 12.0 * std::log2(freqHz / 440.0);
}

double computePlaybackRate(const SampleZone& zone, const int midiNote, const double hostSampleRate)
{
    const int semitones = midiNote - zone.rootNote;
    const double pitchRatio = std::pow(2.0, static_cast<double>(semitones) / 12.0);
    const double sampleRateRatio = hostSampleRate > 0.0 ? zone.sampleRate / hostSampleRate : 1.0;
    return pitchRatio * sampleRateRatio;
}

double detectFundamentalHz(const std::vector<float>& data, const int channelCount, const double sampleRate)
{
    if (data.empty() || channelCount <= 0 || sampleRate <= 0.0) return 440.0;

    const int totalFrames = static_cast<int>(data.size() / channelCount);
    const int analysisFrames = std::min(totalFrames, 8192);
    if (analysisFrames < 64) return 440.0;

    // Frequency range 50 Hz – 1000 Hz
    const int minLag = std::max(1, static_cast<int>(sampleRate / 1000.0));
    const int maxLag = std::min(analysisFrames / 2, static_cast<int>(sampleRate / 50.0));

    float bestR = -1.0e9f;
    int bestLag = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        float r = 0.0f;
        for (int i = 0; i < analysisFrames - lag; ++i)
            r += data[static_cast<std::size_t>(i) * channelCount] *
                 data[static_cast<std::size_t>(i + lag) * channelCount];
        if (r > bestR) {
            bestR = r;
            bestLag = lag;
        }
    }

    return sampleRate / bestLag;
}

}  // namespace downspout::campione
