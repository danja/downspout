#include "sprout_core.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
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
    bool operator==(const Event& o) const { return std::tie(frame, status, d1, d2) == std::tie(o.frame, o.status, o.d1, o.d2); }
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

// The grid is a list of note values, and "1 bar" follows the time signature.
void testGridDivisions()
{
    assert(kGridCount == 12 && kParameterSpecs[kGrid].defaultValue == static_cast<float>(kGridSixteenth));
    assert(std::string(gridName(kGridSixteenth)) == "1/16" && std::string(gridName(kGridBar)) == "1 bar");
    assert(gridQuarters(kGridSixteenth, 4.0) == 0.25);
    assert(std::abs(gridQuarters(1, 4.0) - 1.0 / 6.0) < 1e-12);   // 1/16T
    assert(std::abs(gridQuarters(4, 4.0) - 1.0 / 3.0) < 1e-12);   // 1/8T
    assert(gridQuarters(6, 4.0) == 0.75);                          // 1/8.
    assert(gridQuarters(kGridBar, 4.0) == 4.0 && gridQuarters(kGridBar, 3.0) == 3.0);

    // Every grid keeps note-ons on its own lattice. 120 bpm, 48 kHz: one quarter = 24000 frames.
    for (int g = 0; g < kGridCount - 1; ++g) {
        Params p = defaults();
        p[kGrid] = static_cast<float>(g);
        State s;
        reset(s);
        const auto events = run(p, 4, 480, s);
        const double lattice = gridQuarters(g, 4.0) * 24000.0;
        int ons = 0;
        for (const Event& e : events) {
            if ((e.status & 0xF0) != 0x90) continue;
            ++ons;
            const double cells = static_cast<double>(e.frame) / lattice;
            assert(std::abs(cells - std::round(cells)) < 0.02);
        }
        assert(ons > 0);
    }

    // 1 bar in 3/4: onsets only on bar lines, 1.5 s apart.
    constexpr double sr = 48000.0;
    Params p = defaults();
    p[kGrid] = static_cast<float>(kGridBar);
    State s;
    reset(s);
    int ons = 0;
    for (long long pos = 0; pos < static_cast<long long>(6 * 1.5 * sr); pos += 480) {
        const double quarter = static_cast<double>(pos) / sr * 2.0;
        downspout::generative::Transport t;
        t.valid = t.playing = true;
        t.bpm = 120.0;
        t.beatsPerBar = 3.0;
        t.beatType = 4.0;
        t.bar = std::floor(quarter / 3.0);
        t.barBeat = quarter - t.bar * 3.0;
        const auto out = process(s, p, t, 480, sr);
        for (std::uint32_t i = 0; i < out.count; ++i) {
            if ((out.events[i].data[0] & 0xF0) != 0x90) continue;
            ++ons;
            const double at = static_cast<double>(pos + out.events[i].frame) / (1.5 * sr);
            assert(std::abs(at - std::round(at)) < 0.001);
        }
    }
    assert(ons > 0);
}

// Many more generations are allowed; each grammar stops at the deepest one that fits.
void testDeepGenerations()
{
    static_assert(kMaxGeneration >= 16 && kMaxSymbols >= 131072);
    int slowest = 0;
    for (int preset = 0; preset < kPresetCount; ++preset) {
        const int usable = usableGeneration(preset, kMaxGeneration);
        assert(usable >= 4 && usable <= kMaxGeneration);
        assert(usableGeneration(preset, 2) == 2);
        assert(usableGeneration(preset, 0) == 0);
        const Sequence& deepest = sequenceFor(preset, kMaxGeneration);
        assert(deepest.generation == usable && deepest.steps.size() <= kMaxSymbols);
        // One generation deeper would not have fit (unless the limit is the slider).
        if (usable < kMaxGeneration) {
            const std::string next = expansion(preset, usable);
            assert(!next.empty());
        }
        slowest = std::max(slowest, usable);
    }
    // Levy doubles its notes each generation, so it goes far past the old limit of 7.
    assert(usableGeneration(5, kMaxGeneration) >= 12);
    assert(sequenceFor(5, 12).steps.size() == 4096);

    // Growth stops restarting once a grammar tops out: Plant fits only a few generations,
    // so with Grow every bar and 16 generations asked the pattern must not restart past it.
    Params p = defaults();
    p[kPreset] = 0;
    p[kGenerations] = kMaxGeneration;
    p[kGrowBars] = 1;
    p[kGrid] = 5;  // 1/8
    State s;
    reset(s);
    int last = 0;
    run(p, 40, 480, s, &last);
    assert(last == usableGeneration(0, kMaxGeneration));
}

}  // namespace


// ---- MIDI input: held-note pitch source, CC sets, restart ----------------------------------

constexpr long long kBarFrames = 96000;  // one 4/4 bar at 120 bpm, 48 kHz

struct BlockTrace {
    long long pos;
    int step;
    int generation;
};

// Like run(), but calls `hook(pos, params, state)` before each block so a test can inject MIDI
// (via handleMidi) or change parameters at an exact position, and records where the pattern is.
template <typename Hook>
std::vector<Event> runWith(Params p, const int bars, const std::uint32_t block, State& state, Hook hook,
                           std::vector<BlockTrace>* trace = nullptr)
{
    constexpr double sr = 48000.0;
    constexpr double bpm = 120.0;
    const long long total = static_cast<long long>(bars) * kBarFrames;
    std::vector<Event> events;
    for (long long pos = 0; pos < total; pos += block) {
        hook(pos, p, state);
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
        if (trace != nullptr) trace->push_back({pos, state.statusStep, state.statusGeneration});
    }
    return events;
}

void send(State& s, Params& p, const int status, const int d1, const int d2)
{
    downspout::generative::MidiEvent e;
    e.size = 3;
    e.data = {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(d1), static_cast<std::uint8_t>(d2), 0};
    handleMidi(s, p, &e, 1);
}

std::vector<int> noteOns(const std::vector<Event>& events)
{
    std::vector<int> notes;
    for (const Event& e : events)
        if ((e.status & 0xf0) == 0x90 && e.d2 > 0) notes.push_back(e.d1);
    return notes;
}

void testHeldNotePitchSource()
{
    prepare();
    Params p = defaults();
    p[kPreset] = 5;  // Levy wanders widely at generation 5 (Cantor stays on one pitch)
    p[kGenerations] = 5;
    p[kPitchSource] = kPitchHeld;

    // A held C major triad: every note Sprout plays is a chord tone, in several octaves.
    State s;
    const auto events = runWith(p, 2, 1000, s, [](long long pos, Params& q, State& st) {
        if (pos == 0) {
            send(st, q, 0x90, 60, 100);
            send(st, q, 0x90, 64, 100);
            send(st, q, 0x90, 67, 100);
        }
    });
    const auto notes = noteOns(events);
    assert(notes.size() > 4);
    int lowest = 127, highest = 0;
    for (const int n : notes) {
        const int pc = n % 12;
        assert(pc == 0 || pc == 4 || pc == 7);
        lowest = std::min(lowest, n);
        highest = std::max(highest, n);
    }
    assert(highest - lowest >= 12);  // the contour spans octaves, not just the triad

    // With nothing held it rests; the scale is NOT used as a fallback.
    State silent;
    assert(noteOns(runWith(p, 2, 1000, silent, [](long long, Params&, State&) {})).empty());

    // Notes arriving later start the line: nothing in bar 0, notes in bar 1.
    State late;
    const auto lateEvents = runWith(p, 2, 1000, late, [](long long pos, Params& q, State& st) {
        if (pos == kBarFrames) send(st, q, 0x90, 60, 100);
    });
    assert(!noteOns(lateEvents).empty());
    for (const Event& e : lateEvents) assert(e.frame >= kBarFrames);
}

void testScaleSourceIgnoresHeldNotes()
{
    prepare();
    Params p = defaults();
    p[kPreset] = 1;
    p[kGenerations] = 3;
    State plain;
    const auto without = runWith(p, 2, 1000, plain, [](long long, Params&, State&) {});
    State withNotes;
    const auto with = runWith(p, 2, 1000, withNotes, [](long long pos, Params& q, State& st) {
        if (pos == 0) {
            send(st, q, 0x90, 40, 100);
            send(st, q, 0x90, 47, 100);
        }
    });
    assert(!without.empty());
    assert(std::set<Event>(without.begin(), without.end()) == std::set<Event>(with.begin(), with.end()));
}

void testHeldNoteTrackingAndChannel()
{
    Params p = defaults();
    State s;
    send(s, p, 0x90, 60, 100);
    send(s, p, 0x90, 64, 100);
    assert(s.heldCount == 2);
    send(s, p, 0x90, 64, 100);  // a repeated note-on does not count twice
    assert(s.heldCount == 2);
    send(s, p, 0x80, 64, 0);
    assert(s.heldCount == 1);
    send(s, p, 0x90, 67, 0);  // velocity 0 is a note-off
    assert(s.heldCount == 1);
    send(s, p, 0x90, 60, 0);
    assert(s.heldCount == 0);
    send(s, p, 0x90, 60, 100);
    send(s, p, 0x90, 62, 100);
    send(s, p, 0xb0, 123, 0);  // all notes off
    assert(s.heldCount == 0);

    // Input channel filter: only channel 2 counts.
    p[kInputChannel] = 2;
    send(s, p, 0x90, 60, 100);  // channel 1
    assert(s.heldCount == 0);
    send(s, p, 0x91, 60, 100);  // channel 2
    assert(s.heldCount == 1);
    send(s, p, 0xb0, 123, 0);   // channel 1 all-notes-off must not clear channel 2
    assert(s.heldCount == 1);
    send(s, p, 0xb1, 123, 0);
    assert(s.heldCount == 0);

    // Degree mapping: degree 0 is the lowest held note, and it wraps by octaves.
    State chord;
    Params q = defaults();
    send(chord, q, 0x90, 67, 100);
    send(chord, q, 0x90, 60, 100);
    send(chord, q, 0x90, 64, 100);
    assert(heldDegreeToNote(chord, 0) == 60);
    assert(heldDegreeToNote(chord, 7) == 72);    // seven degrees up is the next octave
    assert(heldDegreeToNote(chord, -7) == 48);
    assert(heldDegreeToNote(chord, 3) == 64);    // 3 triad tones spread across 7 degrees
    assert(heldDegreeToNote(chord, 5) == 67);
    State none;
    assert(heldDegreeToNote(none, 0) == -1);
}

void testDriftCcSet()
{
    State s;
    Params p = defaults();

    // Off by default: CC on any channel is ignored.
    send(s, p, 0xb0, 1, 0);
    assert(std::fabs(p[kProbability] - 1.0f) < 1e-6f);

    p[kCcChannel] = 3;
    send(s, p, 0xb2, 1, 0);      // channel 3: CC 1 -> Probability 0
    assert(std::fabs(p[kProbability]) < 1e-6f);
    send(s, p, 0xb2, 1, 127);
    assert(std::fabs(p[kProbability] - 1.0f) < 1e-6f);
    send(s, p, 0xb2, 1, 64);
    assert(std::fabs(p[kProbability] - 64.0f / 127.0f) < 1e-5f);

    send(s, p, 0xb0, 1, 0);      // channel 1 is not the CC channel
    assert(std::fabs(p[kProbability] - 64.0f / 127.0f) < 1e-5f);

    send(s, p, 0xb2, 2, 0);      // Gate spans 0.1 .. 1.0
    assert(std::fabs(p[kGate] - 0.1f) < 1e-6f);
    send(s, p, 0xb2, 2, 127);
    assert(std::fabs(p[kGate] - 1.0f) < 1e-6f);
    send(s, p, 0xb2, 3, 0);      // Range 3 .. 28, integer
    assert(p[kRange] == 3.0f);
    send(s, p, 0xb2, 3, 127);
    assert(p[kRange] == 28.0f);
    send(s, p, 0xb2, 4, 0);      // Generations 1 .. 16
    assert(p[kGenerations] == 1.0f);
    send(s, p, 0xb2, 4, 127);
    assert(p[kGenerations] == 16.0f);

    // Unmapped CCs and the Conductor CCs do nothing on this channel set.
    const Params before = p;
    send(s, p, 0xb2, 21, 0);
    send(s, p, 0xb2, 7, 0);
    assert(p == before);
}

void testConductorCcSet()
{
    State s;
    Params p = defaults();
    send(s, p, 0xbf, 21, 0);     // off until Conductor ch is set
    assert(std::fabs(p[kProbability] - 1.0f) < 1e-6f);

    p[kConductorCh] = 16;
    send(s, p, 0xbf, 21, 30);    // Density -> Probability
    assert(std::fabs(p[kProbability] - 30.0f / 127.0f) < 1e-5f);
    send(s, p, 0xbf, 22, 127);   // Energy -> Velocity
    assert(p[kVelocity] == 127.0f);
    send(s, p, 0xbf, 22, 0);
    assert(p[kVelocity] == 1.0f);
    send(s, p, 0xbf, 23, 0);     // Mutation -> Seed
    assert(p[kSeed] == 1.0f);
    send(s, p, 0xbf, 23, 127);
    assert(p[kSeed] == 65535.0f);
    send(s, p, 0xbf, 20, 64);    // Scene is unused
    send(s, p, 0xb0, 21, 5);     // wrong channel
    assert(std::fabs(p[kProbability] - 30.0f / 127.0f) < 1e-5f);

    assert(!s.restartPending);
    send(s, p, 0xbf, 24, 100);   // only 127 restarts
    assert(!s.restartPending);
    send(s, p, 0xbf, 24, 127);
    assert(s.restartPending);
}

void testConductorRestartAtNextBar()
{
    prepare();
    Params p = defaults();
    p[kPreset] = 4;       // Cantor: 81 steps at generation 4, so bar 1 starts mid-pattern
    p[kGenerations] = 4;
    p[kConductorCh] = 16;

    State baseline;
    std::vector<BlockTrace> base;
    runWith(p, 3, 1000, baseline, [](long long, Params&, State&) {}, &base);
    const auto stepAt = [](const std::vector<BlockTrace>& t, const long long pos) {
        for (const BlockTrace& b : t)
            if (b.pos == pos) return b.step;
        return -1;
    };
    assert(stepAt(base, kBarFrames) == 16);  // the pattern simply runs on: 16 steps per bar

    // CC 24 = 127 during bar 0: bar 1 starts the pattern again from its first step.
    State s;
    std::vector<BlockTrace> trace;
    runWith(p, 3, 1000, s, [](long long pos, Params& q, State& st) {
        if (pos == 40000) send(st, q, 0xbf, 24, 127);
    }, &trace);
    assert(stepAt(trace, 39000) == stepAt(base, 39000));  // nothing changes before the bar line
    assert(stepAt(trace, kBarFrames) == 0);
    assert(stepAt(trace, kBarFrames + 6000) == 1);
    assert(stepAt(trace, 2 * kBarFrames) == 16);          // one restart only, not a loop

    // A restart that never reached a bar line is dropped when the transport stops.
    State stopped;
    Params q = p;
    send(stopped, q, 0xbf, 24, 127);
    downspout::generative::Transport stoppedT;
    stoppedT.valid = true;
    stoppedT.playing = false;
    (void)process(stopped, q, stoppedT, 1000, 48000.0);
    assert(!stopped.restartPending);
}

void testGenerationChangeLandsOnBarLine()
{
    prepare();
    Params p = defaults();
    p[kPreset] = 4;
    p[kGenerations] = 2;
    State s;
    std::vector<BlockTrace> trace;
    runWith(p, 3, 1000, s, [](long long pos, Params& q, State&) {
        if (pos == 50000) q[kGenerations] = 4;  // mid-bar 0
    }, &trace);
    const auto generationAt = [&](const long long pos) {
        for (const BlockTrace& b : trace)
            if (b.pos == pos) return b.generation;
        return -1;
    };
    assert(generationAt(60000) == 2);               // still bar 0: not yet applied
    assert(generationAt(kBarFrames - 1000) == 2);
    assert(generationAt(kBarFrames + 6000) == 4);   // bar 1: applied on the bar line
}

void testMidiInputIsOffByDefault()
{
    // The new parameters default to the original behaviour.
    const Params p = defaults();
    assert(p[kPitchSource] == 0.0f);
    assert(p[kInputChannel] == 0.0f);
    assert(p[kCcChannel] == 0.0f);
    assert(p[kConductorCh] == 0.0f);
}

void testLatchedPitchSource()
{
    prepare();
    Params p = defaults();
    p[kPitchSource] = kPitchLatched;
    State s;

    send(s, p, 0x90, 60, 100);
    send(s, p, 0x90, 64, 100);
    assert(s.heldCount == 2);
    send(s, p, 0x80, 60, 0);
    send(s, p, 0x80, 64, 0);
    assert(s.heldCount == 2 && s.downCount == 0);  // the chord survives the release

    send(s, p, 0x90, 67, 100);                     // a fresh press replaces it...
    assert(s.heldCount == 1 && heldDegreeToNote(s, 0) == 67);
    send(s, p, 0x90, 71, 100);                     // ...but notes added while a key is down join it
    assert(s.heldCount == 2);
    send(s, p, 0x80, 67, 0);
    send(s, p, 0x80, 71, 0);
    send(s, p, 0x90, 71, 0);                       // note-on velocity 0 is a note-off too
    assert(s.heldCount == 2);

    send(s, p, 0xb0, 123, 0);                      // all notes off is a real panic: it clears the latch
    assert(s.heldCount == 0 && s.downCount == 0);

    // Leaving Latched drops the latched chord (no key is down), without waiting for a note-off.
    send(s, p, 0x90, 60, 100);
    send(s, p, 0x80, 60, 0);
    assert(s.heldCount == 1);
    p[kPitchSource] = kPitchHeld;
    handleMidi(s, p, nullptr, 0);
    assert(s.heldCount == 0);

    // Held mode is unchanged: the chord is just the keys down.
    send(s, p, 0x90, 60, 100);
    send(s, p, 0x80, 60, 0);
    assert(s.heldCount == 0);

    // It plays on after the keys are gone: notes in bar 1 although every key was released in bar 0.
    Params q = defaults();
    q[kPreset] = 5;
    q[kGenerations] = 5;
    q[kPitchSource] = kPitchLatched;
    State chord;
    const auto events = runWith(q, 2, 1000, chord, [](long long pos, Params& r, State& st) {
        if (pos == 0) {
            send(st, r, 0x90, 60, 100);
            send(st, r, 0x90, 64, 100);
            send(st, r, 0x90, 67, 100);
        }
        if (pos == 20000) {
            send(st, r, 0x80, 60, 0);
            send(st, r, 0x80, 64, 0);
            send(st, r, 0x80, 67, 0);
        }
    });
    int inSecondBar = 0;
    for (const Event& e : events) {
        if ((e.status & 0xf0) != 0x90 || e.d2 == 0) continue;
        const int pc = e.d1 % 12;
        assert(pc == 0 || pc == 4 || pc == 7);
        if (e.frame >= kBarFrames) ++inSecondBar;
    }
    assert(inSecondBar > 0);

    // The same keys in Held mode go quiet once released.
    Params r2 = q;
    r2[kPitchSource] = kPitchHeld;
    State gone;
    const auto goneEvents = runWith(r2, 2, 1000, gone, [](long long pos, Params& r, State& st) {
        if (pos == 0) send(st, r, 0x90, 60, 100);
        if (pos == 20000) send(st, r, 0x80, 60, 0);
    });
    for (const Event& e : goneEvents)
        if ((e.status & 0xf0) == 0x90 && e.d2 > 0) assert(e.frame < 20000 + 1000);
}

int main()
{
    testMidiInputIsOffByDefault();
    testHeldNotePitchSource();
    testScaleSourceIgnoresHeldNotes();
    testHeldNoteTrackingAndChannel();
    testLatchedPitchSource();
    testDriftCcSet();
    testConductorCcSet();
    testConductorRestartAtNextBar();
    testGenerationChangeLandsOnBarLine();
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
    testGridDivisions();
    testDeepGenerations();
    return 0;
}
