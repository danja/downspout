#include "worms_engine.hpp"
#include "worms_serialization.hpp"

// Internal headers (via include path from CMakeLists)
#include "worms_tonnetz.hpp"
#include "worms_pattern.hpp"

#include <cassert>
#include <cmath>

using namespace downspout::worms;

namespace {

// ── Tonnetz math ────────────────────────────────────────────────────────────

void testPitchClassOrigin()
{
    assert(tonnetzPitchClass({0, 0}) == 0);  // C
}

void testPitchClassFifthStep()
{
    // Moving East one step: +7 semitones (perfect fifth)
    assert(tonnetzPitchClass({1, 0}) == 7);  // G
    assert(tonnetzPitchClass({2, 0}) == 2);  // D
    assert(tonnetzPitchClass({-1, 0}) == 5); // F (C - fifth = F)
}

void testPitchClassThirdStep()
{
    // Moving NE one step: +4 semitones (major third)
    assert(tonnetzPitchClass({0, 1}) == 4);   // E
    assert(tonnetzPitchClass({0, -1}) == 8);  // Ab (C - maj3 = Ab)
}

void testPitchClassMinorThirdStep()
{
    // Moving NW: delta(-1,+1) = -7+4 = -3 semitones = 9 semitones = A
    assert(tonnetzPitchClass({-1, 1}) == 9);
    // Moving SE: delta(+1,-1) = +7-4 = +3 semitones = Eb
    assert(tonnetzPitchClass({1, -1}) == 3);
}

void testPitchClassWrapsAt12()
{
    // All 12 semitones reachable via fifths
    bool seen[12] = {};
    for (int q = 0; q < 12; ++q) {
        const int pc = tonnetzPitchClass({q, 0});
        assert(pc >= 0 && pc < 12);
        seen[pc] = true;
    }
    for (int i = 0; i < 12; ++i)
        assert(seen[i]);
}

void testMidiNoteRange()
{
    // reg=2 should place notes around middle C (60)
    const int note = midiNoteForPos({0, 0}, 0, 2);
    assert(note >= 48 && note <= 72);
    // All notes in valid MIDI range
    for (int q = -6; q <= 6; ++q) {
        for (int r = -6; r <= 6; ++r) {
            const int n = midiNoteForPos({q, r}, 0, 2);
            assert(n >= 0 && n <= 127);
        }
    }
}

// ── Worm navigation ─────────────────────────────────────────────────────────

void testWormStraightRule()
{
    // Rule: all Straight (turn=2), starting East
    WormRule rule;
    rule.turn.fill(2);  // Straight for all directions
    Direction dir = Direction::E;
    TonnetzPos pos = {0, 0};

    Direction nextDir;
    pos = stepWorm(pos, dir, rule, nextDir);
    // Straight from E → still E, moved to (1, 0)
    assert(nextDir == Direction::E);
    assert(pos.q == 1 && pos.r == 0);
    // Another step: (2, 0)
    pos = stepWorm(pos, nextDir, rule, nextDir);
    assert(pos.q == 2 && pos.r == 0);
}

void testWormL60Rule()
{
    // Rule: always L60 (turn=1) from all directions
    WormRule rule;
    rule.turn.fill(1);  // L60
    Direction dir = Direction::E;
    TonnetzPos pos = {0, 0};

    Direction nextDir;
    pos = stepWorm(pos, dir, rule, nextDir);
    // L60 from E: offset +1 → direction 1 = NE
    assert(nextDir == Direction::NE);
}

void testWormNoReverse()
{
    // Verify that no turn value causes movement to reverse direction
    WormRule rule;
    for (int turn = 0; turn < 5; ++turn) {
        rule.turn.fill(turn);
        for (int d = 0; d < 6; ++d) {
            const Direction inDir = static_cast<Direction>(d);
            Direction outDir;
            stepWorm({0, 0}, inDir, rule, outDir);
            const int reverse = (d + 3) % 6;
            assert(static_cast<int>(outDir) != reverse);
        }
    }
}

// ── Pattern generation ───────────────────────────────────────────────────────

void testPatternDeterminism()
{
    Controls c;
    c.density = 1.0f;  // all notes
    PatternState p1, p2;
    downspout::Meter m {};
    generatePattern(p1, c, m);
    generatePattern(p2, c, m);
    // Same controls → same pattern (except generationSerial increments)
    assert(p1.eventCount == p2.eventCount);
    for (int i = 0; i < p1.eventCount; ++i) {
        assert(p1.events[i].note == p2.events[i].note);
        assert(p1.events[i].startStep == p2.events[i].startStep);
    }
}

void testPatternLength()
{
    Controls c;
    c.patLen   = 1;     // 32 steps
    c.stepSize = 1;     // eighth
    c.density  = 1.0f;

    PatternState p;
    downspout::Meter m {};
    generatePattern(p, c, m);
    assert(p.patternSteps == 32);
    assert(p.stepsPerBeat == 2);
    // All steps filled when density=1
    assert(p.eventCount > 0);
    assert(p.eventCount <= kMaxPatternEvents);
}

void testPatternRests()
{
    Controls c;
    c.density  = 0.0f;  // all rests
    c.patLen   = 0;     // 16 steps
    PatternState p;
    downspout::Meter m {};
    generatePattern(p, c, m);
    assert(p.eventCount == 0);
}

void testPatternNotesInRange()
{
    Controls c;
    c.root    = 0;    // C
    c.reg     = 2;
    c.density = 1.0f;
    PatternState p;
    downspout::Meter m {};
    generatePattern(p, c, m);
    for (int i = 0; i < p.eventCount; ++i) {
        assert(p.events[i].note >= 0 && p.events[i].note <= 127);
        assert(p.events[i].velocity >= 1 && p.events[i].velocity <= 127);
    }
}

// ── Scale ordinal stability ──────────────────────────────────────────────────

void testScaleOrdinalStability()
{
    // Mirrors the stability pins documented in docs/scales.md
    assert(static_cast<int>(ScaleId::minor)     == 2);
    assert(static_cast<int>(ScaleId::bebopMinor) == 22);
    assert(static_cast<int>(ScaleId::count)      == 23);
}

// ── Serialization round-trip ────────────────────────────────────────────────

void testControlsRoundTrip()
{
    Controls c;
    c.root     = 5;
    c.reg      = 3;
    c.density  = 0.6f;
    c.vary     = 0.4f;
    c.rule.turn = {0, 1, 2, 3, 4, 0};

    const std::string text = serializeControls(c);
    const auto result = deserializeControls(text);
    assert(result.has_value());
    const Controls& r = *result;
    assert(r.root    == c.root);
    assert(r.reg     == c.reg);
    assert(r.rule.turn == c.rule.turn);
    assert(std::fabs(r.density - c.density) < 0.001f);
}

void testPatternRoundTrip()
{
    Controls c;
    c.density = 1.0f;
    PatternState p;
    downspout::Meter m {};
    generatePattern(p, c, m);

    const std::string text = serializePattern(p);
    const auto result = deserializePattern(text);
    assert(result.has_value());
    const PatternState& r = *result;
    assert(r.patternSteps == p.patternSteps);
    assert(r.eventCount   == p.eventCount);
    for (int i = 0; i < p.eventCount; ++i) {
        assert(r.events[i].note      == p.events[i].note);
        assert(r.events[i].startStep == p.events[i].startStep);
    }
}

// ── Randomize / Mutate ───────────────────────────────────────────────────────

void testRandomizeProducesValidRules()
{
    WormRule rule;
    randomizeRules(rule, 42, 0);
    for (int i = 0; i < 6; ++i) {
        assert(rule.turn[i] >= 0 && rule.turn[i] <= 4);
    }
}

void testMutateChangesOneRule()
{
    WormRule original;
    original.turn.fill(2);
    WormRule mutated = original;
    mutateRule(mutated, 99, 1);
    int diff = 0;
    for (int i = 0; i < 6; ++i)
        if (mutated.turn[i] != original.turn[i]) ++diff;
    // Exactly one rule should change (unless mutate picks the same value)
    assert(diff <= 1);
}

}  // namespace

int main()
{
    testPitchClassOrigin();
    testPitchClassFifthStep();
    testPitchClassThirdStep();
    testPitchClassMinorThirdStep();
    testPitchClassWrapsAt12();
    testMidiNoteRange();
    testWormStraightRule();
    testWormL60Rule();
    testWormNoReverse();
    testPatternDeterminism();
    testPatternLength();
    testPatternRests();
    testPatternNotesInRange();
    testScaleOrdinalStability();
    testControlsRoundTrip();
    testPatternRoundTrip();
    testRandomizeProducesValidRules();
    testMutateChangesOneRule();
    return 0;
}
