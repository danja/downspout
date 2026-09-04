#pragma once

#include "worms_core_types.hpp"

namespace downspout::worms {

// Generate a full pattern by walking the worm from origin
void generatePattern(PatternState& pattern,
                     const Controls& controls,
                     const ::downspout::Meter& meter);

// Randomize all 6 rule entries using seed + serial
void randomizeRules(WormRule& rule, std::uint64_t seed, int serial);

// Mutate one random rule entry
void mutateRule(WormRule& rule, std::uint64_t seed, int serial);

// Find the NoteEvent active at the given local (fractional) step position, or nullptr
const NoteEvent* findActiveEvent(const PatternState& pattern, double localStep);

// Wrap an absolute step to a local pattern step
double localStepFromAbsolute(const PatternState& pattern, double absSteps);

}  // namespace downspout::worms
