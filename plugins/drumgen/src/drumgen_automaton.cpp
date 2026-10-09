#include "drumgen_automaton.hpp"

#include <algorithm>

namespace downspout::drumgen {
namespace {

constexpr const char* kTargetNames[kCaTargetCount] = {"Hats", "Percussion", "Toms", "Hats + perc + toms", "All lanes"};

}  // namespace

const char* caRuleName(const int ruleIndex) noexcept
{
    return downspout::ca::namedRuleName(ruleIndex);
}

const char* caTargetName(const int target) noexcept
{
    return kTargetNames[std::clamp(target, 0, kCaTargetCount - 1)];
}

bool caTargetsLane(const int target, const int lane) noexcept
{
    const auto id = static_cast<LaneId>(lane);
    const bool hat = id == LaneId::closedHat || id == LaneId::openHat;
    const bool perc = id == LaneId::bash || id == LaneId::cowbell || id == LaneId::clave;
    const bool tom = id == LaneId::lowTom || id == LaneId::highTom;
    switch (std::clamp(target, 0, kCaTargetCount - 1)) {
    case 0: return hat;
    case 1: return perc;
    case 2: return tom;
    case 3: return hat || perc || tom;
    default: return true;
    }
}

std::int64_t caPassForStep(const std::int64_t absoluteStep, const int totalSteps) noexcept
{
    return downspout::ca::passForStep(absoluteStep, totalSteps);
}

std::uint8_t caVelocity(const DrumLaneState& lane, const int totalSteps, const int ruleIndex, const std::int64_t pass,
                        const int step) noexcept
{
    const int n = std::clamp(totalSteps, 1, kMaxPatternSteps);
    if (step < 0 || step >= n) return 0;
    const std::uint8_t own = lane.steps[static_cast<std::size_t>(step)].velocity;

    const int rule = downspout::ca::namedRule(ruleIndex);
    const int generation = downspout::ca::generationForPass(pass);
    if (rule <= 0 || generation == 0) return own;

    std::array<std::uint8_t, downspout::ca::kMaxCells> row {};
    int hits = 0;
    int velocitySum = 0;
    for (int i = 0; i < n; ++i) {
        const std::uint8_t v = lane.steps[static_cast<std::size_t>(i)].velocity;
        row[static_cast<std::size_t>(i)] = v > 0 ? 1 : 0;
        if (v > 0) {
            ++hits;
            velocitySum += v;
        }
    }
    if (hits == 0) return 0;  // an empty lane stays silent whatever the rule does to an empty row
    downspout::ca::evolve(row.data(), n, rule, generation);

    if (row[static_cast<std::size_t>(step)] == 0) return 0;
    if (own > 0) return own;
    const int typical = hits > 0 ? velocitySum / hits : 72;
    return static_cast<std::uint8_t>(std::clamp(typical, 1, 127));
}

}  // namespace downspout::drumgen
