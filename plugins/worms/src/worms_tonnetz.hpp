#pragma once

#include "worms_core_types.hpp"

namespace downspout::worms {

// Semitone delta for each direction (indexed by Direction enum)
// E=+fifth, NE=+majThird, NW=+minThird, W=-fifth, SW=-majThird, SE=-minThird
struct DirDelta { int dq; int dr; };
inline constexpr DirDelta kDelta[6] = {
    {+1,  0},  // E
    { 0, +1},  // NE
    {-1, +1},  // NW
    {-1,  0},  // W
    { 0, -1},  // SW
    {+1, -1},  // SE
};

// Relative turn offsets for rule values 0-4 (added mod 6 to incoming direction)
// 0=L120(+2), 1=L60(+1), 2=Fwd(+0), 3=R60(+5), 4=R120(+4)
inline constexpr int kTurnOffset[5] = {2, 1, 0, 5, 4};

// Advance worm one step: apply rule to incoming direction, move to new position
// Returns new position; outDir receives the new direction
TonnetzPos stepWorm(TonnetzPos pos, Direction inDir, const WormRule& rule, Direction& outDir);

// Absolute pitch class from Tonnetz position (0-11)
int tonnetzPitchClass(TonnetzPos pos);

// MIDI note from pitch class, root pitch class, and register (0-4)
// Base octave for reg=2 targets MIDI note ~60 (C4)
int midiNoteForPos(TonnetzPos pos, int rootPc, int reg);

// Nearest in-scale pitch class to the given Tonnetz pitch class
// intervals: semitone offsets in scale (relative to root), count: number of intervals
int quantizePitchClass(int pc, int rootPc, const int* intervals, int count);

// stepsPerBeat for a given StepSizeId
int stepsPerBeatForSize(int stepSize);

// Pattern length in steps for a given PatternLenId
int patternStepsForLen(int patLen);

}  // namespace downspout::worms
