#include "worms_tonnetz.hpp"

#include <algorithm>
#include <cstdlib>

namespace downspout::worms {

TonnetzPos stepWorm(TonnetzPos pos, Direction inDir, const WormRule& rule, Direction& outDir)
{
    const int d = static_cast<int>(inDir);
    const int turn = std::clamp(rule.turn[d], 0, 4);
    const int nextD = (d + kTurnOffset[turn] + 6) % 6;
    outDir = static_cast<Direction>(nextD);
    return { pos.q + kDelta[nextD].dq, pos.r + kDelta[nextD].dr };
}

int tonnetzPitchClass(TonnetzPos pos)
{
    return ((pos.q * 7 + pos.r * 4) % 12 + 12) % 12;
}

int midiNoteForPos(TonnetzPos pos, int rootPc, int reg)
{
    const int pc = tonnetzPitchClass(pos);
    const int offset = (pc - rootPc + 12) % 12;
    // reg=2 → target octave 4 (base MIDI 48), reg maps to octave 2..6
    const int baseNote = rootPc + (reg + 2) * 12;
    return std::clamp(baseNote + offset, 0, 127);
}

int quantizePitchClass(int pc, int rootPc, const int* intervals, int count)
{
    if (count <= 0)
        return pc;
    // Relative pitch class from root
    const int rel = (pc - rootPc + 12) % 12;
    int bestInterval = intervals[0];
    int bestDistance = std::abs(rel - intervals[0]);
    for (int i = 1; i < count; ++i) {
        const int d = std::abs(rel - intervals[i]);
        if (d < bestDistance) {
            bestDistance = d;
            bestInterval = intervals[i];
        }
    }
    return (rootPc + bestInterval + 12) % 12;
}

int stepsPerBeatForSize(int stepSize)
{
    switch (stepSize) {
    case 0: return 1;   // quarter
    case 1: return 2;   // eighth
    case 2: return 4;   // sixteenth
    case 3: return 8;   // thirty-second
    default: return 2;
    }
}

int patternStepsForLen(int patLen)
{
    switch (patLen) {
    case 0: return 16;
    case 1: return 32;
    case 2: return 64;
    case 3: return 128;
    default: return 32;
    }
}

}  // namespace downspout::worms
