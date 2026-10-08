#include "sprout_core.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <set>
#include <tuple>
#include <vector>

using namespace downspout::sprout;

namespace {

using Params = std::array<float, kParameterCount>;

Params defaults()
{
    Params p {};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue;
    return p;
}

struct Event {
    long long frame;
    int status, d1, d2;
    bool operator<(const Event& o) const { return std::tie(frame, status, d1, d2) < std::tie(o.frame, o.status, o.d1, o.d2); }
};

// Runs `bars` bars at 120 bpm in blocks of `block` frames at 48 kHz.
std::vector<Event> run(const Params& p, const int bars, const std::uint32_t block, State& state, int* lastGeneration = nullptr)
{
    constexpr double sr = 48000.0;
    constexpr double bpm = 120.0;
    const long long total = static_cast<long long>(std::llround(bars * 4.0 * 60.0 / bpm * sr));
    std::vector<Event> events;
    for (long long pos = 0; pos < total; pos += block) {
        const double quarter = static_cast<double>(pos) / sr * bpm / 60.0;
        downspout::generative::Transport t;
        t.valid = true;
        t.playing = true;
        t.bpm = bpm;
        t.bar = std::floor(quarter / 4.0);
        t.barBeat = quarter - t.bar * 4.0;
        const auto out = process(state, p, t, block, sr);
        for (std::uint32_t i = 0; i < out.count; ++i)
            events.push_back({pos + out.events[i].frame, out.events[i].data[0], out.events[i].data[1], out.events[i].data[2]});
        if (lastGeneration != nullptr) *lastGeneration = state.statusGeneration;
    }
    return events;
}

void testExpansions()
{
    prepare();
    assert(expansion(4, 0) == "F");
    assert(expansion(4, 1) == "FfF");
    assert(expansion(4, 2) == "FfFfffFfF");
    assert(expansion(1, 1) == "F+F-F-F+F");
    assert(expansion(0, 1) == "F+[[X]-X]-F[-FX]+X");
    assert(expansion(3, 1) == "F-G+F+G-F-GG-GG");
    assert(std::string(presetName(0)) == "Plant");
    assert(std::string(presetRules(4)) == "F; F -> FfF; f -> fff");
}

void testTurtleInterpretation()
{
    // Koch generation 1 is F+F-F-F+F: five notes at pitch units 0, 1, 0, -1, 0.
    const Sequence& koch = sequenceFor(1, 1);
    assert(koch.steps.size() == 5);
    const int units[] = {0, 1, 0, -1, 0};
    for (std::size_t i = 0; i < 5; ++i) assert(koch.steps[i].unit == units[i] && koch.steps[i].note);

    // Tree generation 1 is F[+F]F[-F]F: the branches return to the trunk pitch.
    const Sequence& tree = sequenceFor(6, 1);
    assert(tree.steps.size() == 5);
    const int treeUnits[] = {0, 1, 0, -1, 0};
    const int depths[] = {0, 1, 0, 1, 0};
    for (std::size_t i = 0; i < 5; ++i) assert(tree.steps[i].unit == treeUnits[i] && tree.steps[i].depth == depths[i]);

    // Cantor generation 2 is FfFfffFfF: rests are steps that do not sound.
    const Sequence& cantor = sequenceFor(4, 2);
    assert(cantor.steps.size() == 9);
    const bool sounds[] = {true, false, true, false, false, false, true, false, true};
    for (std::size_t i = 0; i < 9; ++i) assert(cantor.steps[i].note == sounds[i]);

    // Variables such as X make no sound: the Plant axiom alone has no steps.
    assert(sequenceFor(0, 0).steps.empty());
}

void testSizeCapAndFallback()
{
    for (int preset = 0; preset < kPresetCount; ++preset) {
        std::size_t previous = 0;
        for (int g = 0; g <= kMaxGeneration; ++g) {
            const Sequence& seq = sequenceFor(preset, g);
            assert(seq.steps.size() <= kMaxSymbols);
            assert(seq.generation <= g);
            assert(seq.steps.size() >= previous || seq.generation < g);
            previous = seq.steps.size();
        }
    }
    // The Plant doubles in length quickly, so the deepest generation falls back.
    assert(sequenceFor(0, kMaxGeneration).generation < kMaxGeneration);
}

void testFoldDegree()
{
    for (int range = 3; range <= 28; ++range) {
        for (int d = -500; d <= 500; ++d) {
            const int f = foldDegree(d, range);
            assert(f >= -range && f <= range);
            if (d >= -range && d <= range) assert(f == d);
        }
        assert(foldDegree(range + 1, range) == range - 1);
        assert(foldDegree(-range - 1, range) == -range + 1);
        // Neighbouring degrees stay neighbours across the reflection (no jumps).
        for (int d = -200; d < 200; ++d) assert(std::abs(foldDegree(d + 1, range) - foldDegree(d, range)) <= 1);
    }
}

void testScalesAndPins()
{
    static_assert(kScaleMajor == 1 && kScaleMinor == 3 && kScaleBebopMinor == 23 && kScaleCount == 24);
    // An independent copy of the docs/scales.md sets, in canonical order.
    const std::vector<std::vector<int>> expected = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10},
        {0, 2, 3, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
        {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 1, 4, 5, 7, 8, 10},
        {0, 1, 3, 5, 7, 9, 11}, {0, 1, 3, 5, 7, 8, 11}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10},
        {0, 2, 4, 6, 8, 10}, {0, 1, 3, 4, 6, 8, 10}, {0, 1, 3, 4, 6, 7, 9, 10}, {0, 2, 3, 5, 6, 8, 9, 11},
        {0, 2, 4, 5, 7, 9, 10, 11}, {0, 2, 4, 5, 7, 8, 9, 11}, {0, 2, 3, 4, 5, 7, 9, 10},
    };
    assert(expected.size() == static_cast<std::size_t>(kScaleCount));
    for (int scale = 0; scale < kScaleCount; ++scale) {
        const auto& set = expected[static_cast<std::size_t>(scale)];
        int count = 0;
        const std::uint8_t* intervals = scaleIntervals(scale, count);
        assert(count == static_cast<int>(set.size()));
        for (int i = 0; i < count; ++i) assert(intervals[i] == set[static_cast<std::size_t>(i)]);
        // Every degree, above and below the root, lands in the scale.
        const std::set<int> pitchClasses(set.begin(), set.end());
        for (int degree = -14; degree <= 14; ++degree) {
            const int note = degreeToNote(scale, 60, degree);
            assert(pitchClasses.count(((note - 60) % 12 + 12) % 12) == 1);
        }
    }
    assert(degreeToNote(kScaleMajor, 60, 0) == 60);
    assert(degreeToNote(kScaleMajor, 60, 1) == 62);
    assert(degreeToNote(kScaleMajor, 60, 7) == 72);
    assert(degreeToNote(kScaleMajor, 60, -1) == 59);
    assert(degreeToNote(kScaleMajor, 60, -7) == 48);
    assert(degreeToNote(14, 60, 5) == 72);  // pentatonic major wraps after five degrees
}

void testGenerationSchedule()
{
    assert(generationAtBar(4, 0, 0) == 4);
    assert(generationAtBar(4, 0, 100) == 4);
    assert(generationAtBar(4, 2, 0) == 1);
    assert(generationAtBar(4, 2, 1) == 1);
    assert(generationAtBar(4, 2, 2) == 2);
    assert(generationAtBar(4, 2, 5) == 3);
    assert(generationAtBar(4, 2, 6) == 4);
    assert(generationAtBar(4, 2, 600) == 4);
    assert(generationAtBar(4, 2, -3) == 1);
}

void testBlockSizeIndependence()
{
    Params p = defaults();
    p[kPreset] = 1;
    p[kGenerations] = 3;
    p[kGate] = 0.6f;
    p[kProbability] = 0.8f;
    State a, b;
    reset(a);
    reset(b);
    auto one = run(p, 8, 1536, a);
    auto two = run(p, 8, 480, b);
    assert(!one.empty());
    std::sort(one.begin(), one.end());
    std::sort(two.begin(), two.end());
    assert(one.size() == two.size());
    for (std::size_t i = 0; i < one.size(); ++i) {
        assert(std::llabs(one[i].frame - two[i].frame) <= 1);
        assert(one[i].status == two[i].status && one[i].d1 == two[i].d1 && one[i].d2 == two[i].d2);
    }
}

void testNotesStayInScaleAndPair()
{
    Params p = defaults();
    p[kPreset] = 3;  // Sierpinski has a wide pitch spread
    p[kGenerations] = 4;
    p[kScale] = 14;  // pentatonic major
    p[kRange] = 6;
    p[kStepSize] = 2;
    p[kGate] = 1.0f;
    State s;
    reset(s);
    const auto events = run(p, 16, 1024, s);
    const std::set<int> pentatonic = {0, 2, 4, 7, 9};
    int held = 0;
    int ons = 0;
    for (const Event& e : events) {
        const int pc = ((e.d1 - 60) % 12 + 12) % 12;
        assert(pentatonic.count(pc) == 1);
        if ((e.status & 0xF0) == 0x90) {
            assert(held == 0);  // monophonic: the previous note is off before the next on
            ++held;
            ++ons;
            // The fold keeps every note within 6 degrees of the root.
            assert(e.d1 >= degreeToNote(14, 60, -6) && e.d1 <= degreeToNote(14, 60, 6));
        } else {
            assert(held == 1);
            --held;
        }
    }
    assert(ons > 20);
    assert(held == 0 || held == 1);
}

void testProbabilityZeroIsSilent()
{
    Params p = defaults();
    p[kProbability] = 0.0f;
    State s;
    reset(s);
    assert(run(p, 4, 1024, s).empty());
}

void testGrowthRestartsAndReachesTarget()
{
    Params p = defaults();
    p[kPreset] = 1;
    p[kGenerations] = 3;
    p[kGrowBars] = 2;
    State s;
    reset(s);
    int generation = 0;
    (void)run(p, 1, 1024, s, &generation);
    assert(generation == 1);
    (void)run(p, 1, 1024, s, &generation);  // new run restarts from bar 0 in this helper
    assert(generation == 1);
    State later;
    reset(later);
    (void)run(p, 8, 1024, later, &generation);
    assert(generation == 3);
    assert(later.statusLength == static_cast<int>(sequenceFor(1, 3).steps.size()));
}

void testStopAndJumpReleaseNotes()
{
    Params p = defaults();
    p[kGate] = 1.0f;
    State s;
    reset(s);
    downspout::generative::Transport t;
    t.valid = true;
    t.playing = true;
    t.bpm = 120.0;
    // Start just before a step boundary so a note sounds at the end of the block.
    t.bar = 0;
    t.barBeat = 0.0;
    auto out = process(s, p, t, 6000, 48000.0);  // 0.125 s = a quarter note at 120 bpm
    int ons = 0, offs = 0;
    for (std::uint32_t i = 0; i < out.count; ++i) {
        if ((out.events[i].data[0] & 0xF0) == 0x90) ++ons;
        else ++offs;
    }
    assert(ons >= 1 && ons - offs <= 1);
    const bool active = s.activeNote >= 0;

    t.playing = false;
    out = process(s, p, t, 512, 48000.0);
    assert(s.activeNote == -1);
    assert(out.count == (active ? 1u : 0u));

    // A jump backwards is a discontinuity and must not leave a stuck note.
    State j;
    reset(j);
    t.playing = true;
    t.bar = 4;
    t.barBeat = 0.0;
    (void)process(j, p, t, 6000, 48000.0);
    t.bar = 1;
    out = process(j, p, t, 6000, 48000.0);
    int onsAfter = 0, offsAfter = 0;
    for (std::uint32_t i = 0; i < out.count; ++i) {
        if ((out.events[i].data[0] & 0xF0) == 0x90) ++onsAfter;
        else ++offsAfter;
    }
    assert(onsAfter - offsAfter <= 1);
}

}  // namespace

int main()
{
    testExpansions();
    testTurtleInterpretation();
    testSizeCapAndFallback();
    testFoldDegree();
    testScalesAndPins();
    testGenerationSchedule();
    testBlockSizeIndependence();
    testNotesStayInScaleAndPair();
    testProbabilityZeroIsSilent();
    testGrowthRestartsAndReachesTarget();
    testStopAndJumpReleaseNotes();
    return 0;
}
