#pragma once

#include "downspout/cellular_automaton.hpp"
#include "drumgen_core_types.hpp"

#include <array>
#include <cstdint>

namespace downspout::drumgen {

// Cellular-automaton layer. The generated pattern is generation 0: on the lanes picked by the
// target, each pass through the pattern replaces that lane's hits with the next generation of a
// one-dimensional automaton (Wolfram rule numbering, wrapping edges over the pattern length)
// seeded from the lane's own hits. The pass number comes from the absolute step, so playback,
// loop restarts and offline renders agree, and after kCaGenerations passes the lane returns to
// the pattern as generated. The automaton only changes what is played: the pattern is untouched.

// The arithmetic, the rule table and the pass/generation helpers are shared with xoxolo and polymeter
// (downspout/cellular_automaton.hpp); this layer decides which lanes play it and at what velocity.
inline constexpr int kCaRuleCount = downspout::ca::kNamedRuleCount;
inline constexpr int kCaTargetCount = 5;
inline constexpr int kCaGenerations = downspout::ca::kGenerations;
inline constexpr int kCaMinEvery = 1;
inline constexpr int kCaMaxEvery = 8;

// Rule index (the Automaton selector / parameter) -> Wolfram rule number. Index 0 is off.
inline constexpr const std::array<int, kCaRuleCount>& kCaRules = downspout::ca::kNamedRules;

[[nodiscard]] const char* caRuleName(int ruleIndex) noexcept;
[[nodiscard]] const char* caTargetName(int target) noexcept;

// Targets: 0 hats, 1 percussion (bash, cowbell, clave), 2 toms, 3 hats + percussion + toms,
// 4 every lane.
[[nodiscard]] bool caTargetsLane(int target, int lane) noexcept;

// The pass (loop number) an absolute step falls in.
[[nodiscard]] std::int64_t caPassForStep(std::int64_t absoluteStep, int totalSteps) noexcept;

// Velocity of `step` of `lane` on the given pass: 0 for a rest. `every` loops share one generation
// (1-8). Generation 0 (and every 64th) is the pattern itself. New hits take the lane's average velocity; hits that survive keep theirs.
[[nodiscard]] std::uint8_t caVelocity(const DrumLaneState& lane, int totalSteps, int ruleIndex, std::int64_t pass,
                                      int step, int every = 1) noexcept;

}  // namespace downspout::drumgen
