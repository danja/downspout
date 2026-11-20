#include "fontana_slices.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace downspout::fontana {
namespace {

constexpr std::uint32_t kRmsWindowFrames = 256;
constexpr float kRmsFloor = 1e-4f;

struct Candidate {
    std::uint32_t frame = 0;
    float jump = 0.0f;
};

[[nodiscard]] SliceSet makeSliceSet(const SampleSource& source)
{
    SliceSet slices;
    slices.sourcePath = source.path;
    slices.sampleRate = source.sampleRate;
    slices.frameCount = source.frameCount();
    return slices;
}

[[nodiscard]] Slice makeSlice(const std::uint32_t start, const std::uint32_t end, const int rootNote)
{
    Slice slice;
    slice.start = start;
    slice.end = end;
    slice.rootNote = rootNote;
    return slice;
}

}  // namespace

SliceSet equalSlices(const SampleSource& source, const int count, const int baseNote)
{
    SliceSet slices = makeSliceSet(source);
    const std::uint32_t frames = slices.frameCount;
    if (frames == 0u)
        return slices;

    const int sliceCount = std::clamp(count, 1, kMaxSlices);
    for (int index = 0; index < sliceCount; ++index) {
        const std::uint64_t first = static_cast<std::uint64_t>(index) * frames / static_cast<std::uint64_t>(sliceCount);
        const std::uint64_t pastLast = static_cast<std::uint64_t>(index + 1) * frames / static_cast<std::uint64_t>(sliceCount);
        if (pastLast <= first)
            continue;  // more slices than frames: drop the empty ranges
        slices.slices.push_back(makeSlice(static_cast<std::uint32_t>(first),
                                          static_cast<std::uint32_t>(pastLast - 1u),
                                          baseNote + index));
    }
    return slices;
}

SliceSet transientSlices(const SampleSource& source, const float sensitivity, const int baseNote)
{
    SliceSet slices = makeSliceSet(source);
    const std::uint32_t frames = slices.frameCount;
    if (frames == 0u)
        return slices;

    const std::uint32_t window = kRmsWindowFrames;
    const std::uint32_t minLength = std::max<std::uint32_t>(window, source.sampleRate / 100u);  // 10 ms
    const std::size_t channels = static_cast<std::size_t>(std::max(1, source.channels));
    const std::size_t windowCount = (static_cast<std::size_t>(frames) + window - 1u) / window;

    std::vector<float> envelope(windowCount, 0.0f);
    for (std::size_t index = 0; index < windowCount; ++index) {
        const std::uint64_t first = static_cast<std::uint64_t>(index) * window;
        const std::uint64_t last = std::min<std::uint64_t>(first + window, frames);
        double sum = 0.0;
        for (std::uint64_t frame = first; frame < last; ++frame) {
            double average = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel)
                average += source.data[static_cast<std::size_t>(frame) * channels + channel];
            average /= static_cast<double>(channels);
            sum += average * average;
        }
        if (last > first)
            envelope[index] = static_cast<float>(std::sqrt(sum / static_cast<double>(last - first)));
    }

    const float amount = std::clamp(sensitivity, 0.0f, 1.0f);
    // Low sensitivity accepts modest jumps (many slices); high sensitivity
    // requires a strong onset (few slices).
    const float threshold = 1.05f + 3.5f * amount;

    std::vector<Candidate> boundaries;
    boundaries.push_back(Candidate { 0u, 0.0f });
    std::uint64_t lastBoundary = 0u;
    for (std::size_t index = 1; index < windowCount; ++index) {
        const float previous = std::max(envelope[index - 1u], kRmsFloor);
        const float jump = envelope[index] / previous;
        if (jump <= threshold)
            continue;
        const std::uint64_t boundary = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(index) * window, static_cast<std::uint64_t>(frames - 1u));
        if (boundary < lastBoundary + minLength)
            continue;
        boundaries.push_back(Candidate { static_cast<std::uint32_t>(boundary), jump });
        lastBoundary = boundary;
    }

    // Do not leave a trailing slice shorter than the minimum length.
    while (boundaries.size() > 1u &&
           static_cast<std::uint64_t>(frames - 1u) - boundaries.back().frame < minLength)
        boundaries.pop_back();

    if (boundaries.size() > static_cast<std::size_t>(kMaxSlices)) {
        std::vector<Candidate> internal(boundaries.begin() + 1u, boundaries.end());
        const std::size_t keep = static_cast<std::size_t>(kMaxSlices - 1);
        std::nth_element(internal.begin(), internal.begin() + static_cast<std::ptrdiff_t>(keep),
                         internal.end(), [](const Candidate& a, const Candidate& b) {
                             return a.jump > b.jump;
                         });
        internal.resize(keep);
        std::sort(internal.begin(), internal.end(), [](const Candidate& a, const Candidate& b) {
            return a.frame < b.frame;
        });
        boundaries.clear();
        boundaries.push_back(Candidate { 0u, 0.0f });
        boundaries.insert(boundaries.end(), internal.begin(), internal.end());
    }

    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const std::uint32_t start = boundaries[index].frame;
        const std::uint32_t end = index + 1u < boundaries.size()
            ? boundaries[index + 1u].frame - 1u
            : frames - 1u;
        if (end < start)
            continue;
        slices.slices.push_back(makeSlice(start, end, baseNote + static_cast<int>(index)));
    }
    return slices;
}

SliceSet normalizeSlices(SliceSet slices, const std::uint32_t frameCount)
{
    slices.frameCount = frameCount;
    if (frameCount == 0u) {
        slices.slices.clear();
        return slices;
    }

    const std::uint32_t lastFrame = frameCount - 1u;
    std::vector<Slice> kept;
    kept.reserve(std::min<std::size_t>(slices.slices.size(), static_cast<std::size_t>(kMaxSlices)));
    for (Slice& slice : slices.slices) {
        slice.start = std::min(slice.start, lastFrame);
        slice.end = std::min(slice.end, lastFrame);
        if (slice.end < slice.start)
            continue;
        kept.push_back(std::move(slice));
        if (kept.size() >= static_cast<std::size_t>(kMaxSlices))
            break;
    }
    slices.slices = std::move(kept);
    return slices;
}

int sliceIndexForNote(const SliceSet& slices, const int note, const int baseNote) noexcept
{
    const int index = note - baseNote;
    if (index < 0 || index >= static_cast<int>(slices.slices.size()))
        return -1;
    return index;
}

}  // namespace downspout::fontana
