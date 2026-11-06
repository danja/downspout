#pragma once

// Shared voice-leading helpers (portable C++, no DPF). Ported from the
// Auricle VoiceLeading design (see docs/auricle-algo-review.md). Header-only
// so plugins can use them without touching the build graph.
//
// Two tools:
//   resolvePitch  - place a single melodic note (pick the octave of `target`
//                   closest to `previous`, with a smoothness/max-leap control).
//   resolveChord  - place a chord's voices with register constraints and
//                   minimal movement from `previous`.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace downspout::generative {

// Resolve one melodic note.
//
// smoothness in [0, 1]: 0 = free movement (return target unchanged),
// 1 = maximum smoothing (follow the resolved voice path). maxLeap is the
// largest allowed step at full smoothness (24 st at smoothness 0).
//
// Returns the resolved pitch (may be fractional-free: always a whole
// semitone offset from target, since candidates are target + 12*n).
inline float resolvePitch(const float previous, const float target,
                          const float smoothness, const float maxLeap = 7.0f) noexcept
{
    const float s = std::clamp(smoothness, 0.0f, 1.0f);
    if (s <= 0.0f)
        return target;
    float best = target;
    float bestKey = 1.0e30f;
    for (int n = -4; n <= 4; ++n)
    {
        const float candidate = target + 12.0f * static_cast<float>(n);
        const float key = s * std::fabs(candidate - previous) + (1.0f - s) * std::fabs(candidate - target);
        if (key < bestKey || (key == bestKey && std::fabs(candidate - target) < std::fabs(best - target)))
        {
            bestKey = key;
            best = candidate;
        }
    }
    const float limit = maxLeap + (24.0f - maxLeap) * (1.0f - s);
    if (std::fabs(best - previous) > limit)
        best = previous + std::clamp(best - previous, -limit, limit);
    return best;
}

// Resolve a chord's voices.
//
// notes: the chord's pitch classes (or absolute notes) to place.
// previous: the previous chord's resolved notes (may be empty).
// lo, hi: register bounds (inclusive). Returns placed notes (sorted, ascending,
// distinct pitch classes preserved as separate octaves).
inline std::vector<int> resolveChord(const std::vector<int>& notes,
                                     const std::vector<int>& previous,
                                     const int lo = 0, const int hi = 127) noexcept
{
    if (notes.empty())
        return {};
    std::vector<int> sorted = notes;
    std::sort(sorted.begin(), sorted.end());
    // Keep pitch classes distinct across octaves.
    for (std::size_t i = 1; i < sorted.size(); ++i)
        while (sorted[i] <= sorted[i - 1])
            sorted[i] += 12;

    const int loC = std::max(0, std::min(127, lo));
    const int hiC = std::max(0, std::min(127, hi));
    int lowOctave = static_cast<int>(std::ceil(-static_cast<double>(sorted.front()) / 12.0));
    int highOctave = static_cast<int>(std::floor(static_cast<double>(127 - sorted.back()) / 12.0));
    if (highOctave < lowOctave)
        highOctave = lowOctave;

    // Candidate octave translations.
    std::vector<std::vector<int>> candidates;
    for (int oct = lowOctave; oct <= highOctave; ++oct)
    {
        std::vector<int> chord;
        chord.reserve(sorted.size());
        for (int n : sorted)
            chord.push_back(n + 12 * oct);
        // distinct pitch classes
        std::vector<int> uniq;
        for (int v : chord)
            if (std::find(uniq.begin(), uniq.end(), v) == uniq.end())
                uniq.push_back(v);
        if (uniq.size() == chord.size())
            candidates.push_back(chord);
    }

    // Fallback: search a wider range with per-voice octave choice.
    if (candidates.empty())
    {
        // Simple: one octave block.
        for (int oct = lowOctave - 2; oct <= highOctave + 2; ++oct)
        {
            std::vector<int> chord;
            for (int n : sorted)
                chord.push_back(std::clamp(n + 12 * oct, 0, 127));
            candidates.push_back(chord);
        }
    }

    auto score = [&](const std::vector<int>& chord) -> double
    {
        double cost = 0.0;
        // register penalty
        for (int v : chord)
        {
            if (v < loC) cost += (loC - v);
            if (v > hiC) cost += (v - hiC);
        }
        // movement from previous
        for (std::size_t i = 0; i < chord.size(); ++i)
        {
            const int target = i < previous.size() ? previous[i] : (previous.empty() ? chord[i] : previous.back());
            cost += std::fabs(static_cast<double>(chord[i] - target));
        }
        // spacing preservation
        for (std::size_t i = 1; i < chord.size(); ++i)
        {
            const double wanted = static_cast<double>(sorted[i] - sorted[i - 1]);
            const double got = static_cast<double>(chord[i] - chord[i - 1]);
            cost += std::fabs(wanted - got) * 0.25;
        }
        return cost;
    };

    auto best = candidates.front();
    double bestCost = score(best);
    for (const auto& c : candidates)
    {
        const double sc = score(c);
        if (sc < bestCost)
        {
            bestCost = sc;
            best = c;
        }
    }
    for (int& v : best)
        v = std::clamp(v, 0, 127);
    return best;
}

} // namespace downspout::generative
