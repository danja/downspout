#pragma once

// One-dimensional cellular automata for rhythm generators.
//
// A row of cells (each 0 or 1) is advanced by a Wolfram elementary rule (0-255) with wrapping
// edges: cell i's next state is bit ((left << 2) | (self << 1) | right) of the rule number.
// Everything here is a pure function of its arguments, so a generator that derives the
// generation from its playback position gets the same result on every pass, restart and
// offline render.
//
// Shared by polymeter, drumgen and xoxolo. They keep their own selector tables and decide what
// the row means (which lane, what velocity); only the arithmetic lives here.

#include <algorithm>
#include <array>
#include <cstdint>

namespace downspout::ca {

// Longest row any generator uses (drumgen's 128-step pattern).
inline constexpr int kMaxCells = 128;

// Generations before a lane returns to its starting row.
inline constexpr int kGenerations = 64;

// The rules offered by the selectors, in selector order. Index 0 is "off".
inline constexpr int kNamedRuleCount = 11;
inline constexpr std::array<int, kNamedRuleCount> kNamedRules {{0, 30, 90, 110, 150, 18, 54, 60, 22, 105, 126}};

[[nodiscard]] inline const char* namedRuleName(const int index) noexcept
{
    static constexpr const char* kNames[kNamedRuleCount] = {"Off",     "Rule 30", "Rule 90",  "Rule 110", "Rule 150", "Rule 18",
                                                            "Rule 54", "Rule 60", "Rule 22", "Rule 105", "Rule 126"};
    return kNames[std::clamp(index, 0, kNamedRuleCount - 1)];
}

// The Wolfram rule number for a selector index (0 for off or out of range).
[[nodiscard]] inline int namedRule(const int index) noexcept
{
    return kNamedRules[static_cast<std::size_t>(std::clamp(index, 0, kNamedRuleCount - 1))];
}

// The loop pass an absolute step falls in (floors for negative steps).
[[nodiscard]] inline std::int64_t passForStep(const std::int64_t absoluteStep, const int totalSteps) noexcept
{
    const std::int64_t total = std::max(1, totalSteps);
    std::int64_t pass = absoluteStep / total;
    if (absoluteStep % total != 0 && absoluteStep < 0) --pass;
    return pass;
}

// The generation to play on `pass` when each generation lasts `every` passes: 0 is the starting
// row, and it wraps after kGenerations.
[[nodiscard]] inline int generationForPass(const std::int64_t pass, const int every = 1) noexcept
{
    std::int64_t generation = (pass / std::max(1, every)) % kGenerations;
    if (generation < 0) generation += kGenerations;
    return static_cast<int>(generation);
}

// Advances `row` (n cells of 0/1, n <= kMaxCells) by `generations` steps of `rule`, in place.
inline void evolve(std::uint8_t* row, const int n, const int rule, const int generations) noexcept
{
    if (row == nullptr || n <= 0 || n > kMaxCells) return;
    std::array<std::uint8_t, kMaxCells> next {};
    for (int g = 0; g < generations; ++g) {
        for (int i = 0; i < n; ++i) {
            const int l = (i + n - 1) % n;
            const int r = (i + 1) % n;
            const int pattern = (row[l] << 2) | (row[i] << 1) | row[r];
            next[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((rule >> pattern) & 1);
        }
        std::copy_n(next.begin(), n, row);
    }
}

}  // namespace downspout::ca
