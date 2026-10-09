#include "xoxolo_automaton.hpp"

#include <algorithm>

namespace downspout::xoxolo {

const char* caRuleName(const int ruleIndex) noexcept
{
    return downspout::ca::namedRuleName(ruleIndex);
}

std::int64_t caPassForStep(const std::int64_t absoluteStep, const int totalSteps) noexcept
{
    return downspout::ca::passForStep(absoluteStep, totalSteps);
}

bool caStepActive(const LaneState& lane, const int totalSteps, const int ruleIndex, const int every,
                  const std::int64_t pass, const int step, const int shift) noexcept
{
    const int n = std::clamp(totalSteps, 1, kMaxSteps);
    if (step < 0 || step >= n) return false;
    const bool own = lane.steps[static_cast<std::size_t>(step)] != 0;

    const int rule = downspout::ca::namedRule(ruleIndex);
    if (rule <= 0 || lane.evolve == 0) return own;

    const int generation = (downspout::ca::generationForPass(pass, std::clamp(every, kMinCaEvery, kMaxCaEvery)) +
                            std::clamp(shift, 0, kCaGenerations - 1)) %
                           kCaGenerations;
    if (generation == 0) return own;

    std::array<std::uint8_t, downspout::ca::kMaxCells> row {};
    int hits = 0;
    for (int i = 0; i < n; ++i) {
        row[static_cast<std::size_t>(i)] = lane.steps[static_cast<std::size_t>(i)] != 0 ? 1 : 0;
        hits += row[static_cast<std::size_t>(i)];
    }
    if (hits == 0) return false;  // an empty lane stays silent whatever the rule does to an empty row
    downspout::ca::evolve(row.data(), n, rule, generation);
    return row[static_cast<std::size_t>(step)] != 0;
}

}  // namespace downspout::xoxolo
