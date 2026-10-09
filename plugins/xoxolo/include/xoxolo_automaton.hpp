#pragma once

#include "downspout/cellular_automaton.hpp"
#include "xoxolo_core_types.hpp"

#include <array>
#include <cstdint>

namespace downspout::xoxolo {

// Cellular-automaton layer. A lane marked "evolve" treats the pattern you programmed as generation
// 0; each further generation replaces the lane's hits with the next row of a one-dimensional
// automaton (Wolfram rule numbering, wrapping edges over the pattern length). A generation lasts
// `every` passes through the pattern. The pass number comes from the absolute step, so playback,
// loop restarts and offline renders agree, and after kCaGenerations generations the lane returns
// to the programmed pattern. Only what is played changes: the grid keeps the programmed pattern.

// The arithmetic, the rule table and the pass/generation helpers are shared with drumgen and polymeter
// (downspout/cellular_automaton.hpp); this layer decides which lanes play it.
inline constexpr int kCaRuleCount = downspout::ca::kNamedRuleCount;
inline constexpr int kCaGenerations = downspout::ca::kGenerations;
inline constexpr int kMinCaEvery = 1;
inline constexpr int kMaxCaEvery = 8;

// Rule index (the Evolve rule selector / parameter) -> Wolfram rule number. Index 0 is off.
inline constexpr const std::array<int, kCaRuleCount>& kCaRules = downspout::ca::kNamedRules;

[[nodiscard]] const char* caRuleName(int ruleIndex) noexcept;

// The pass (loop number) an absolute step falls in.
[[nodiscard]] std::int64_t caPassForStep(std::int64_t absoluteStep, int totalSteps) noexcept;

// Whether `step` of `lane` plays on the given pass. With the rule off, or a lane that is not
// evolving, this is just the programmed cell. A lane with no hits stays silent.
// `shift` adds generations on top of the pass count (the Conductor's Mutation CC).
[[nodiscard]] bool caStepActive(const LaneState& lane, int totalSteps, int ruleIndex, int every, std::int64_t pass,
                                int step, int shift = 0) noexcept;

}  // namespace downspout::xoxolo
