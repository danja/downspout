#include "worms_pattern.hpp"

#include "worms_tonnetz.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace downspout::worms {
namespace {

// Scale definitions matching BassGen/Melgen canonical ordering (docs/scales.md)
constexpr int kScaleMajor[]                = {0, 2, 4, 5, 7, 9, 11};
constexpr int kScaleIonian[]               = {0, 2, 4, 5, 7, 9, 11};
constexpr int kScaleMinor[]                = {0, 2, 3, 5, 7, 8, 10};
constexpr int kScaleHarmonicMinor[]        = {0, 2, 3, 5, 7, 8, 11};
constexpr int kScaleMelodicMinor[]         = {0, 2, 3, 5, 7, 9, 11};
constexpr int kScaleDorian[]               = {0, 2, 3, 5, 7, 9, 10};
constexpr int kScalePhrygian[]             = {0, 1, 3, 5, 7, 8, 10};
constexpr int kScaleLydian[]               = {0, 2, 4, 6, 7, 9, 11};
constexpr int kScaleMixolydian[]           = {0, 2, 4, 5, 7, 9, 10};
constexpr int kScaleLocrian[]              = {0, 1, 3, 5, 6, 8, 10};
constexpr int kScalePhrygianDominant[]     = {0, 1, 4, 5, 7, 8, 10};
constexpr int kScaleNeapolitanMajor[]      = {0, 1, 4, 5, 7, 9, 11};
constexpr int kScaleNeapolitanMinor[]      = {0, 1, 3, 5, 7, 8, 10};
constexpr int kScalePentMajor[]            = {0, 2, 4, 7, 9};
constexpr int kScalePentMinor[]            = {0, 3, 5, 7, 10};
constexpr int kScaleBlues[]                = {0, 3, 5, 6, 7, 10};
constexpr int kScaleWholeTone[]            = {0, 2, 4, 6, 8, 10};
constexpr int kScaleAltered[]              = {0, 1, 3, 4, 6, 8, 10};
constexpr int kScaleHalfWholeDiminished[]  = {0, 1, 3, 4, 6, 7, 9, 10};
constexpr int kScaleWholeHalfDiminished[]  = {0, 2, 3, 5, 6, 8, 9, 11};
constexpr int kScaleBebopDominant[]        = {0, 2, 4, 5, 7, 9, 10, 11};
constexpr int kScaleBebopMajor[]           = {0, 2, 4, 5, 7, 8, 9, 11};
constexpr int kScaleBebopMinor[]           = {0, 2, 3, 4, 5, 7, 9, 10};

struct ScaleDef { const int* intervals; int count; };

constexpr ScaleDef kScales[] = {
    {kScaleMajor,               7},   // 0 major
    {kScaleIonian,              7},   // 1 ionian
    {kScaleMinor,               7},   // 2 minor
    {kScaleHarmonicMinor,       7},   // 3 harmonicMinor
    {kScaleMelodicMinor,        7},   // 4 melodicMinor
    {kScaleDorian,              7},   // 5 dorian
    {kScalePhrygian,            7},   // 6 phrygian
    {kScaleLydian,              7},   // 7 lydian
    {kScaleMixolydian,          7},   // 8 mixolydian
    {kScaleLocrian,             7},   // 9 locrian
    {kScalePhrygianDominant,    7},   // 10 phrygianDominant
    {kScaleNeapolitanMajor,     7},   // 11 neapolitanMajor
    {kScaleNeapolitanMinor,     7},   // 12 neapolitanMinor
    {kScalePentMajor,           5},   // 13 pentMajor
    {kScalePentMinor,           5},   // 14 pentMinor
    {kScaleBlues,               6},   // 15 blues
    {kScaleWholeTone,           6},   // 16 wholeTone
    {kScaleAltered,             7},   // 17 altered
    {kScaleHalfWholeDiminished, 8},   // 18 halfWholeDiminished
    {kScaleWholeHalfDiminished, 8},   // 19 wholeHalfDiminished
    {kScaleBebopDominant,       8},   // 20 bebopDominant
    {kScaleBebopMajor,          8},   // 21 bebopMajor
    {kScaleBebopMinor,          8},   // 22 bebopMinor
};
inline constexpr int kScaleCount = static_cast<int>(std::size(kScales));

// Avalanche hash for seeded randomness
std::uint64_t mix64(std::uint64_t v) noexcept
{
    v += 0x9e3779b97f4a7c15ULL;
    v = (v ^ (v >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27U)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31U);
}

float randomUnit(std::uint64_t seed, std::uint64_t index) noexcept
{
    const std::uint64_t v = mix64(seed ^ mix64(index));
    return static_cast<float>((v >> 40U) * (1.0 / 16777216.0));
}

int randomInt(std::uint64_t seed, std::uint64_t index, int min, int max) noexcept
{
    if (max <= min) return min;
    const int span = max - min + 1;
    return min + static_cast<int>(mix64(seed ^ mix64(index)) % static_cast<std::uint64_t>(span));
}

std::uint64_t controlSeed(const Controls& controls)
{
    // Map 0-1 float seed to a wide integer seed
    const auto s = static_cast<std::uint64_t>(controls.seed * 65535.0f);
    return mix64(s + 1u);
}

}  // namespace

void generatePattern(PatternState& pattern,
                     const Controls& controls,
                     const ::downspout::Meter& meter)
{
    const int stepsPerBeat = stepsPerBeatForSize(controls.stepSize);
    const int patternSteps = patternStepsForLen(controls.patLen);
    const int beatsPerBar  = meter.numerator > 0 ? meter.numerator : 4;
    const int stepsPerBar  = stepsPerBeat * beatsPerBar;

    pattern.stepsPerBeat  = stepsPerBeat;
    pattern.patternSteps  = patternSteps;
    pattern.stepsPerBar   = stepsPerBar;
    pattern.meter         = meter;
    ++pattern.generationSerial;

    const std::uint64_t seed = controlSeed(controls);
    const int scaleIdx = std::clamp(controls.scale, 0, kScaleCount - 1);
    const ScaleDef& scaleDef = kScales[scaleIdx];

    TonnetzPos pos {};
    Direction  dir = Direction::E;
    int eventCount = 0;

    for (int step = 0; step < patternSteps && eventCount < kMaxPatternEvents; ++step) {
        // Decide rest vs note using density + seeded RNG
        const float r = randomUnit(seed ^ static_cast<std::uint64_t>(pattern.generationSerial), static_cast<std::uint64_t>(step));
        if (r > controls.density) {
            // Rest: advance worm position but don't emit a note
            Direction nextDir;
            pos = stepWorm(pos, dir, controls.rule, nextDir);
            dir = nextDir;
            continue;
        }

        // Compute pitch
        int pc = tonnetzPitchClass(pos);
        if (controls.quantize && scaleDef.count > 0) {
            pc = quantizePitchClass(pc, controls.root, scaleDef.intervals, scaleDef.count);
        }
        const int note = midiNoteForPos(pos, controls.root, controls.reg);
        const int vel  = std::clamp(static_cast<int>(controls.velocity * 127.0f), 1, 127);

        NoteEvent& event    = pattern.events[eventCount++];
        event.startStep     = step;
        event.durationSteps = 1;  // one step; note-off at step+1
        event.note          = std::clamp(note, 0, 127);
        event.velocity      = vel;

        // Advance worm
        Direction nextDir;
        pos = stepWorm(pos, dir, controls.rule, nextDir);
        dir = nextDir;
    }

    // Advance worm for any remaining rest steps so endPos/endDir is correct
    // (already done above since we advance even on rests)
    pattern.endPos = pos;
    pattern.endDir = dir;
    pattern.eventCount = eventCount;
}

void randomizeRules(WormRule& rule, std::uint64_t seed, int serial)
{
    const std::uint64_t s = mix64(seed ^ static_cast<std::uint64_t>(serial + 7919));
    for (int d = 0; d < 6; ++d) {
        rule.turn[d] = randomInt(s, static_cast<std::uint64_t>(d + 100), 0, 4);
    }
}

void mutateRule(WormRule& rule, std::uint64_t seed, int serial)
{
    const std::uint64_t s = mix64(seed ^ static_cast<std::uint64_t>(serial + 3571));
    const int d = randomInt(s, 0, 0, 5);
    rule.turn[d] = randomInt(s, 1, 0, 4);
}

const NoteEvent* findActiveEvent(const PatternState& pattern, double localStep)
{
    const NoteEvent* found = nullptr;
    for (int i = 0; i < pattern.eventCount; ++i) {
        const NoteEvent& ev = pattern.events[i];
        if (localStep >= static_cast<double>(ev.startStep) &&
            localStep <  static_cast<double>(ev.startStep + ev.durationSteps))
        {
            found = &ev;
        }
    }
    return found;
}

double localStepFromAbsolute(const PatternState& pattern, double absSteps)
{
    if (pattern.patternSteps <= 0)
        return 0.0;
    const double wrapped = std::fmod(absSteps, static_cast<double>(pattern.patternSteps));
    return wrapped < 0.0 ? wrapped + static_cast<double>(pattern.patternSteps) : wrapped;
}

}  // namespace downspout::worms
