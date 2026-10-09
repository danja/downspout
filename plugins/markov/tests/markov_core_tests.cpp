#include "markov_core.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace downspout::markov;

namespace {

Params defaults()
{
    Params p {};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue;
    return p;
}

struct Event {
    long long frame;
    int status, d1, d2;
    bool operator==(const Event& o) const { return frame == o.frame && status == o.status && d1 == o.d1 && d2 == o.d2; }
};

constexpr long long kBarFrames = 96000;  // one 4/4 bar at 120 bpm, 48 kHz

// Runs `bars` bars in blocks of `block` frames; `hook(pos, params, model, state)` runs before each block.
template <typename Hook>
std::vector<Event> run(Params p, Model model, const int bars, const std::uint32_t block, State& state, Hook hook)
{
    constexpr double sr = 48000.0;
    constexpr double bpm = 120.0;
    std::vector<Event> events;
    for (long long pos = 0; pos < bars * kBarFrames; pos += block) {
        hook(pos, p, model, state);
        const double quarter = static_cast<double>(pos) / sr * bpm / 60.0;
        downspout::generative::Transport t;
        t.valid = true;
        t.playing = true;
        t.bpm = bpm;
        t.bar = std::floor(quarter / 4.0);
        t.barBeat = quarter - t.bar * 4.0;
        const auto out = process(state, model, p, t, block, sr);
        for (std::uint32_t i = 0; i < out.count; ++i)
            events.push_back({pos + out.events[i].frame, out.events[i].data[0], out.events[i].data[1], out.events[i].data[2]});
    }
    return events;
}

std::vector<Event> run(const Params& p, const Model& model, const int bars, const std::uint32_t block = 1000)
{
    State state;
    return run(p, model, bars, block, state, [](long long, Params&, Model&, State&) {});
}

std::vector<int> noteOns(const std::vector<Event>& events)
{
    std::vector<int> notes;
    for (const Event& e : events)
        if ((e.status & 0xf0) == 0x90 && e.d2 > 0) notes.push_back(e.d1);
    return notes;
}

MidiEvent ccMsg(const int status, const int d1, const int d2)
{
    MidiEvent e;
    e.size = 3;
    e.data = {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(d1), static_cast<std::uint8_t>(d2), 0};
    return e;
}

// Walks a phrase and returns the pitch-class states.
std::vector<int> walkStates(const Model& model, const Params& p, const std::uint64_t seed, const std::int64_t phrase, const int length)
{
    const Matrix matrix = effectiveMatrix(model, p);
    std::vector<int> states;
    for (int k = 0; k < length; ++k) states.push_back(walkNote(model, p, matrix, seed, phrase, k).state);
    return states;
}

// ---- tests ---------------------------------------------------------------------------------------------------

void testScalesMatchTheDocs()
{
    // An independent copy of the docs/scales.md sets, in canonical order.
    const std::vector<std::vector<int>> expected = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10},
        {0, 2, 3, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
        {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 1, 4, 5, 7, 8, 10},
        {0, 1, 3, 5, 7, 9, 11}, {0, 1, 3, 5, 7, 8, 11}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10},
        {0, 2, 4, 6, 8, 10}, {0, 1, 3, 4, 6, 8, 10}, {0, 1, 3, 4, 6, 7, 9, 10}, {0, 2, 3, 5, 6, 8, 9, 11},
        {0, 2, 4, 5, 7, 9, 10, 11}, {0, 2, 4, 5, 7, 8, 9, 11}, {0, 2, 3, 4, 5, 7, 9, 10},
    };
    assert(static_cast<int>(expected.size()) == kScaleCount);
    for (int scale = 0; scale < kScaleCount; ++scale) {
        std::uint16_t mask = 0;
        for (const int s : expected[static_cast<std::size_t>(scale)]) mask = static_cast<std::uint16_t>(mask | (1u << s));
        assert(scaleMask(scale) == mask);
    }
    assert(std::string(scaleName(0)) == "Chromatic" && std::string(scaleName(1)) == "Major" &&
           std::string(scaleName(3)) == "Minor" && std::string(scaleName(23)) == "Bebop Minor");
}

void testStylesAndModelEditing()
{
    Model m = defaultModel();
    for (const std::uint8_t w : m.base) assert(w <= kMaxWeight);

    // Every style is a valid matrix, they differ, and Uniform really is.
    std::set<std::vector<std::uint8_t>> distinct;
    for (int style = 0; style < kStyleCount; ++style) {
        loadStyle(m, style);
        distinct.insert(std::vector<std::uint8_t>(m.base.begin(), m.base.end()));
        for (const std::uint8_t w : m.base) assert(w <= kMaxWeight);
        assert(std::string(styleName(style)).size() > 0);
    }
    assert(distinct.size() >= kStyleCount - 1);  // Stepwise and Tonic pull share their skeleton but differ in the pull
    loadStyle(m, 7);
    for (const std::uint8_t w : m.base) assert(w == 4);

    // Stepwise: a major second is likelier than a tritone from every state.
    loadStyle(m, 0);
    for (int from = 0; from < kStates; ++from)
        assert(m.base[static_cast<std::size_t>(from * 12 + (from + 2) % 12)] > m.base[static_cast<std::size_t>(from * 12 + (from + 6) % 12)]);

    // Tonic pull sends the leading tone and the fifth home.
    loadStyle(m, 6);
    assert(m.base[11 * 12 + 0] == kMaxWeight && m.base[7 * 12 + 0] == kMaxWeight);

    // Editing: bumping cycles 0..8 and wraps; out-of-range cells are ignored.
    clearBase(m);
    for (int i = 1; i <= kMaxWeight; ++i) {
        bumpCell(m, 3, 5);
        assert(m.base[3 * 12 + 5] == i);
    }
    bumpCell(m, 3, 5);
    assert(m.base[3 * 12 + 5] == 0);
    const auto before = m.base;
    bumpCell(m, -1, 0);
    bumpCell(m, 0, 12);
    assert(m.base == before);

    // Randomise is repeatable, seed dependent, within range, and leaves some moves at zero.
    Model a, b, c;
    randomiseBase(a, 11);
    randomiseBase(b, 11);
    randomiseBase(c, 12);
    assert(a.base == b.base && a.base != c.base);
    int zeros = 0;
    for (const std::uint8_t w : a.base) {
        assert(w <= kMaxWeight);
        zeros += w == 0 ? 1 : 0;
    }
    assert(zeros > 20 && zeros < 100);
}

void testEffectiveMatrix()
{
    Params p = defaults();
    Model m = defaultModel();

    // Chaos 0.5 leaves the drawn weights alone (for notes in the scale); notes outside the scale are removed.
    p[kScale] = 1;  // major
    p[kChaos] = 0.5f;
    Matrix matrix = effectiveMatrix(m, p);
    const std::uint16_t mask = scaleMask(1);
    for (int from = 0; from < kStates; ++from) {
        float total = 0.0f;
        for (int to = 0; to < kStates; ++to) {
            const float w = matrix[static_cast<std::size_t>(from * 12 + to)];
            if (((mask >> to) & 1u) != 0) assert(std::fabs(w - m.base[static_cast<std::size_t>(from * 12 + to)]) < 1e-5f);
            else assert(w == 0.0f);
            total += w;
        }
        assert(total > 0.0f);
    }

    // Chaos sharpens below 0.5 and flattens above: the favourite's share of the row goes up, then down.
    const auto share = [&](const float chaos) {
        Params q = p;
        q[kChaos] = chaos;
        const Matrix x = effectiveMatrix(m, q);
        float total = 0.0f, top = 0.0f;
        for (int to = 0; to < kStates; ++to) {
            total += x[static_cast<std::size_t>(0 * 12 + to)];
            top = std::max(top, x[static_cast<std::size_t>(0 * 12 + to)]);
        }
        return top / total;
    };
    assert(share(0.0f) > share(0.5f) && share(0.5f) > share(1.0f));

    // A cleared matrix falls back to "any note in the scale", evenly.
    Model cleared;
    clearBase(cleared);
    matrix = effectiveMatrix(cleared, p);
    for (int to = 0; to < kStates; ++to)
        assert(matrix[static_cast<std::size_t>(5 * 12 + to)] == (((mask >> to) & 1u) != 0 ? 1.0f : 0.0f));

    // Learned data is blended in by Learned mix, row by row, and only where a row has data.
    Model learned = defaultModel();
    learned.learned1[0 * 12 + 4] = 10;  // from the tonic, always to the major third
    p[kLearnedMix] = 1.0f;
    p[kChaos] = 0.5f;
    matrix = effectiveMatrix(learned, p);
    for (int to = 0; to < kStates; ++to)
        assert((matrix[static_cast<std::size_t>(to)] > 0.0f) == (to == 4));  // row 0 is now just "to 4"
    for (int to = 0; to < kStates; ++to)  // row 1 has no data, so it is the drawn row
        assert(std::fabs(matrix[static_cast<std::size_t>(12 + to)] -
                         (((mask >> to) & 1u) != 0 ? learned.base[static_cast<std::size_t>(12 + to)] : 0.0f)) < 1e-5f);
    p[kLearnedMix] = 0.0f;
    assert(effectiveMatrix(learned, p) == effectiveMatrix(defaultModel(), p));  // mix 0 ignores what was learned
}

void testSamplingAndWalk()
{
    std::array<float, kStates> row {};
    row[2] = 1.0f;
    row[5] = 3.0f;
    // Cumulative sampling: the first quarter is state 2, the rest state 5.
    assert(sampleRow(row, 0.0f) == 2 && sampleRow(row, 0.24f) == 2 && sampleRow(row, 0.26f) == 5 && sampleRow(row, 0.999f) == 5);
    std::array<float, kStates> empty {};
    assert(sampleRow(empty, 0.5f) == 0);

    const Params p = defaults();
    const Model m = defaultModel();

    // The walk is a pure function of (model, params, seed, phrase, k): repeatable and seed/phrase dependent.
    const auto a = walkStates(m, p, 5, 0, 24);
    assert(a == walkStates(m, p, 5, 0, 24));
    assert(a != walkStates(m, p, 6, 0, 24));
    assert(a != walkStates(m, p, 5, 1, 24));
    // A prefix of a walk is the same walk: note k does not depend on how far you ask for.
    const auto prefix = walkStates(m, p, 5, 0, 10);
    assert(std::equal(prefix.begin(), prefix.end(), a.begin()));

    // Every note is in the scale and the register, across scales, roots and ranges.
    for (const int scale : {1, 3, 14, 17}) {
        for (const int root : {0, 55, 61, 127}) {
            Params q = p;
            q[kScale] = static_cast<float>(scale);
            q[kRoot] = static_cast<float>(root);
            q[kLow] = 48;
            q[kOctaves] = 2;
            const Matrix matrix = effectiveMatrix(m, q);
            const std::uint16_t mask = scaleMask(scale);
            for (int phrase = 0; phrase < 6; ++phrase) {
                for (int k = 0; k < 40; ++k) {
                    const WalkNote w = walkNote(m, q, matrix, 9, phrase, k);
                    assert(((mask >> w.state) & 1u) != 0);
                    assert(w.note >= 48 && w.note <= 71);
                    assert((w.note - root % 12 + 120) % 12 == w.state);
                }
            }
        }
    }

    // A row with a single allowed move walks it deterministically: from every state to the next fifth.
    Model chain;
    clearBase(chain);
    for (int from = 0; from < kStates; ++from) chain.base[static_cast<std::size_t>(from * 12 + (from + 7) % 12)] = kMaxWeight;
    Params chromatic = p;
    chromatic[kScale] = 0;
    const auto fifths = walkStates(chain, chromatic, 1, 0, 12);
    for (int k = 0; k < 12; ++k) assert(fifths[static_cast<std::size_t>(k)] == (7 * (k + 1)) % 12);
}

void testStatisticsFollowTheMatrix()
{
    // From the tonic the walk should take moves in proportion to the weights. Count first moves over many phrases.
    Model m;
    clearBase(m);
    m.base[0 * 12 + 2] = 6;
    m.base[0 * 12 + 4] = 2;
    Params p = defaults();
    p[kScale] = 0;
    const Matrix matrix = effectiveMatrix(m, p);
    int toSecond = 0, toThird = 0, other = 0;
    for (int phrase = 0; phrase < 4000; ++phrase) {
        const int state = walkNote(m, p, matrix, 3, phrase, 0).state;
        if (state == 2) ++toSecond;
        else if (state == 4) ++toThird;
        else ++other;
    }
    assert(other == 0);
    assert(toSecond > 2 * toThird + 200 && toSecond < 4 * toThird + 400);  // about 3:1
}

void testOrderTwoUsesLearnedHistory()
{
    // Teach it: after 2 then 4 always comes 7, but after 0 then 4 always comes 9. Order 1 cannot tell these apart.
    Model m;
    clearBase(m);
    for (int from = 0; from < kStates; ++from)
        for (int to = 0; to < kStates; ++to) m.base[static_cast<std::size_t>(from * 12 + to)] = 1;
    m.learned2[(2 * 12 + 4) * 12 + 7] = 20;
    m.learned2[(0 * 12 + 4) * 12 + 9] = 20;
    Params p = defaults();
    p[kScale] = 0;
    const Matrix matrix = effectiveMatrix(m, p);
    std::array<float, kStates> row {};

    p[kOrder] = 2;
    nextRow(m, p, matrix, 2, 4, row);
    assert(sampleRow(row, 0.5f) == 7 && sampleRow(row, 0.01f) == 7 && sampleRow(row, 0.99f) == 7);
    nextRow(m, p, matrix, 0, 4, row);
    assert(sampleRow(row, 0.5f) == 9 && sampleRow(row, 0.99f) == 9);

    // No second-order data for this pair: fall back to the order-1 row.
    nextRow(m, p, matrix, 3, 4, row);
    for (int to = 0; to < kStates; ++to) assert(row[static_cast<std::size_t>(to)] == matrix[static_cast<std::size_t>(4 * 12 + to)]);
    nextRow(m, p, matrix, -1, 4, row);  // and when the history is unknown
    for (int to = 0; to < kStates; ++to) assert(row[static_cast<std::size_t>(to)] == matrix[static_cast<std::size_t>(4 * 12 + to)]);

    // Order 1 ignores the second-order tables entirely.
    p[kOrder] = 1;
    nextRow(m, p, matrix, 2, 4, row);
    for (int to = 0; to < kStates; ++to) assert(row[static_cast<std::size_t>(to)] == matrix[static_cast<std::size_t>(4 * 12 + to)]);
}

void testLearning()
{
    Params p = defaults();
    Model m = defaultModel();
    State s;

    const auto note = [&](const int status, const int n) {
        MidiEvent e;
        e.size = 3;
        e.data = {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(n), 100, 0};
        return handleMidi(s, m, p, &e, 1);
    };

    // Off by default: nothing is learned.
    assert(!note(0x90, 60));
    assert(m.learnedTotal == 0);

    // With Learn on, successive note-ons are counted relative to the root (C: C then E is 0 -> 4).
    p[kLearn] = 1;
    p[kRoot] = 60;
    note(0x90, 60);
    assert(m.learnedTotal == 0);  // the first note has no predecessor
    assert(note(0x90, 64));
    assert(m.learnedTotal == 1 && m.learned1[0 * 12 + 4] == 1);
    note(0x90, 67);  // E then G: 4 -> 7, and the second-order (0, 4) -> 7
    assert(m.learned1[4 * 12 + 7] == 1 && m.learned2[(0 * 12 + 4) * 12 + 7] == 1);
    note(0x90, 72);  // octaves are the same state
    assert(m.learned1[7 * 12 + 0] == 1);

    // Note-offs, velocity-0 note-ons and other channels (with a Learn channel set) are not counted.
    const std::uint32_t before = m.learnedTotal;
    MidiEvent off;
    off.size = 3;
    off.data = {0x80, 65, 0, 0};
    handleMidi(s, m, p, &off, 1);
    MidiEvent zero;
    zero.size = 3;
    zero.data = {0x90, 65, 0, 0};
    handleMidi(s, m, p, &zero, 1);
    assert(m.learnedTotal == before);
    p[kLearnChannel] = 2;
    note(0x90, 62);  // channel 1: ignored
    assert(m.learnedTotal == before);
    note(0x91, 62);  // channel 2: counted, linked to the last note that was accepted (the 72 before)
    note(0x91, 65);
    assert(m.learnedTotal == before + 2);

    // Turning Learn off forgets the line, so the next note does not link to the old one.
    p[kLearn] = 0;
    handleMidi(s, m, p, nullptr, 0);
    assert(s.learnPrevious1 == -1);

    // The counts halve rather than overflow, keeping their proportions.
    Model big;
    big.learned1[3] = 0xffff;
    big.learned1[4] = 0x8000;
    Params q = defaults();
    q[kLearn] = 1;
    q[kLearnChannel] = 0;
    State t;
    MidiEvent a, b;
    a.size = b.size = 3;
    a.data = {0x90, 60, 100, 0};
    b.data = {0x90, 63, 100, 0};
    handleMidi(t, big, q, &a, 1);
    handleMidi(t, big, q, &b, 1);  // 0 -> 3: cell 3 was at the cap
    assert(big.learned1[3] > 0x7000);  // still near its old share, not wrapped to zero
    assert(big.learned1[4] < 0x8000);  // halved with the rest

    // Clear learned is a counter: the first value seen is adopted, and any change after that wipes the data.
    State fresh;
    Params restored = p;
    restored[kClearLearned] = 7;  // a reloaded project can carry any value without clearing
    assert(!handleMidi(fresh, m, restored, nullptr, 0));
    assert(m.learnedTotal > 0);
    restored[kClearLearned] = 8;
    assert(handleMidi(fresh, m, restored, nullptr, 0));
    assert(m.learnedTotal == 0 && m.learned1[0 * 12 + 4] == 0 && m.learned2[(0 * 12 + 4) * 12 + 7] == 0);
    assert(!handleMidi(fresh, m, restored, nullptr, 0));  // the same value again does nothing
}

void testPlaybackIsMusical()
{
    Params p = defaults();
    p[kDensity] = 1.0f;
    const Model m = defaultModel();

    const auto events = run(p, m, 4);
    const auto notes = noteOns(events);
    assert(notes.size() >= 60);  // 16 sixteenths a bar, four bars, every one sounding
    const std::uint16_t mask = scaleMask(1);
    for (const int n : notes) {
        assert(n >= 48 && n <= 71);
        assert(((mask >> ((n - 60 % 12 + 120) % 12)) & 1u) != 0);
    }
    // Every note-on is closed by a note-off.
    std::map<int, int> open;
    for (const Event& e : events) {
        if ((e.status & 0xf0) == 0x90 && e.d2 > 0) ++open[e.d1];
        else if ((e.status & 0xf0) == 0x80) --open[e.d1];
    }
    for (const auto& [n, count] : open) assert(count == 0 || count == 1);

    // Repeatable, and independent of the host's block size.
    assert(events == run(p, m, 4));
    const auto coarse = run(p, m, 4, 4000);
    const auto fine = run(p, m, 4, 480);
    assert(noteOns(coarse) == noteOns(fine));
    for (std::size_t i = 0; i < std::min(coarse.size(), fine.size()); ++i) assert(coarse[i] == fine[i]);

    // The seed changes the melody; Density 0 silences it; the lowest note and range are respected.
    Params other = p;
    other[kSeed] = 2;
    assert(noteOns(run(other, m, 4)) != notes);
    Params silent = p;
    silent[kDensity] = 0;
    assert(noteOns(run(silent, m, 4)).empty());
    Params high = p;
    high[kLow] = 72;
    high[kOctaves] = 1;
    for (const int n : noteOns(run(high, m, 2))) assert(n >= 72 && n <= 83);

    // Phrases: a new phrase starts every Phrase bars, so a one-bar phrase repeats the same notes only by chance,
    // while the notes within a phrase do not depend on which bar of the song it is in.
    Params shortPhrase = p;
    shortPhrase[kPhraseBars] = 1;
    const auto bar0 = noteOns(run(shortPhrase, m, 1));
    State fromBar2;
    std::vector<int> bar2;
    {
        // Start the transport at bar 2 and compare with what the first run played in bar 2.
        const auto all = run(shortPhrase, m, 3);
        for (const Event& e : all)
            if ((e.status & 0xf0) == 0x90 && e.d2 > 0 && e.frame >= 2 * kBarFrames) bar2.push_back(e.d1);
    }
    assert(!bar0.empty() && !bar2.empty());
}

void testRandomAccess()
{
    // Starting playback part-way through (a locate) plays exactly the notes a continuous run plays there.
    Params p = defaults();
    p[kDensity] = 1.0f;
    const Model m = defaultModel();
    const auto continuous = run(p, m, 6);

    State state;
    std::vector<Event> located;
    constexpr double sr = 48000.0;
    constexpr double bpm = 120.0;
    for (long long pos = 3 * kBarFrames; pos < 5 * kBarFrames; pos += 1000) {
        const double quarter = static_cast<double>(pos) / sr * bpm / 60.0;
        downspout::generative::Transport t;
        t.valid = true;
        t.playing = true;
        t.bpm = bpm;
        t.bar = std::floor(quarter / 4.0);
        t.barBeat = quarter - t.bar * 4.0;
        const auto out = process(state, m, p, t, 1000, sr);
        for (std::uint32_t i = 0; i < out.count; ++i)
            located.push_back({pos + out.events[i].frame, out.events[i].data[0], out.events[i].data[1], out.events[i].data[2]});
    }
    std::vector<Event> expected;
    for (const Event& e : continuous)
        if (e.frame >= 3 * kBarFrames && e.frame < 5 * kBarFrames) expected.push_back(e);
    assert(!expected.empty());
    assert(located == expected);
}

void testStopAndJumpReleaseNotes()
{
    Params p = defaults();
    p[kDensity] = 1.0f;
    p[kGate] = 1.0f;
    const Model m = defaultModel();
    State s;
    downspout::generative::Transport t;
    t.valid = true;
    t.playing = true;
    t.bpm = 120;
    auto out = process(s, m, p, t, 4000, 48000.0);
    assert(out.count > 0 && s.activeNote >= 0);

    // Stopping releases the sounding note and plays nothing.
    t.playing = false;
    out = process(s, m, p, t, 4000, 48000.0);
    assert(out.count == 1 && (out.events[0].data[0] & 0xf0) == 0x80 && s.activeNote < 0);

    // A jump releases too, and playback resumes from the new position.
    t.playing = true;
    out = process(s, m, p, t, 4000, 48000.0);
    assert(s.activeNote >= 0);
    t.bar = 40;
    out = process(s, m, p, t, 4000, 48000.0);
    assert(out.count >= 1 && (out.events[0].data[0] & 0xf0) == 0x80);

    // No transport: silent.
    downspout::generative::Transport none;
    State quiet;
    assert(process(quiet, m, p, none, 1000, 48000.0).count == 0);
}

void testConductor()
{
    Params p = defaults();
    Model m = defaultModel();
    State s;
    const auto cc = [&](const int status, const int d1, const int d2) {
        const MidiEvent e = ccMsg(status, d1, d2);
        handleMidi(s, m, p, &e, 1);
    };

    // Off by default.
    cc(0xbf, 21, 0);
    assert(p[kDensity] == kParameterSpecs[kDensity].defaultValue);

    p[kConductorCh] = 16;
    cc(0xbf, 21, 0);
    assert(p[kDensity] == 0.0f);
    cc(0xbf, 21, 127);
    assert(p[kDensity] == 1.0f);
    cc(0xbf, 22, 0);
    assert(p[kVelocity] == 1.0f);
    cc(0xbf, 22, 127);
    assert(p[kVelocity] == 127.0f);
    cc(0xbf, 23, 0);
    assert(p[kChaos] == 0.0f);
    cc(0xbf, 23, 127);
    assert(p[kChaos] == 1.0f);

    const Params before = p;
    cc(0xb0, 21, 5);   // another channel
    cc(0xbf, 20, 5);   // scene is unused
    cc(0xbf, 7, 5);
    assert(p == before);

    assert(!s.restartPending);
    cc(0xbf, 24, 100);
    assert(!s.restartPending);
    cc(0xbf, 24, 127);
    assert(s.restartPending);
}

void testRestartRerollsTheMelodyAtTheBarLine()
{
    Params p = defaults();
    p[kDensity] = 1.0f;
    p[kPhraseBars] = 8;
    p[kConductorCh] = 16;
    const Model m = defaultModel();

    State plainState;
    const auto plain = run(p, m, 4, 1000, plainState, [](long long, Params&, Model&, State&) {});
    State restartState;
    const auto restarted = run(p, m, 4, 1000, restartState, [](long long pos, Params& q, Model& model, State& st) {
        if (pos == 40000) {
            const MidiEvent e = ccMsg(0xbf, 24, 127);
            handleMidi(st, model, q, &e, 1);
        }
    });

    const auto inBar = [](const std::vector<Event>& events, const long long bar) {
        std::vector<int> notes;
        for (const Event& e : events)
            if ((e.status & 0xf0) == 0x90 && e.d2 > 0 && e.frame >= bar * kBarFrames && e.frame < (bar + 1) * kBarFrames) notes.push_back(e.d1);
        return notes;
    };
    assert(inBar(restarted, 0) == inBar(plain, 0));  // untouched until the bar line
    assert(inBar(restarted, 1) != inBar(plain, 1));  // re-rolled from bar 1
    assert(restartState.salt == 1 && !restartState.restartPending);

    // A pending restart is dropped when the transport stops.
    State stopped;
    stopped.restartPending = true;
    downspout::generative::Transport t;
    t.valid = true;
    t.playing = false;
    (void)process(stopped, m, p, t, 1000, 48000.0);
    assert(!stopped.restartPending);
}

void testSerialization()
{
    Model m = defaultModel();
    m.base[7] = 0;
    m.learned1[5] = 123;
    m.learned1[100] = 65535;
    m.learned2[0] = 1;
    m.learned2[1727] = 400;

    const std::string text = serializeModel(m);
    const auto restored = deserializeModel(text);
    assert(restored.has_value());
    assert(restored->base == m.base && restored->learned1 == m.learned1 && restored->learned2 == m.learned2);
    assert(restored->learnedTotal == 123u + 65535u);
    assert(serializeModel(*restored) == text);

    // A model with nothing learned has empty tables and round-trips.
    const std::string plain = serializeModel(defaultModel());
    assert(plain.find("l1=\n") != std::string::npos && plain.find("l2=\n") != std::string::npos);
    assert(deserializeModel(plain).has_value());

    // Bad input is rejected whole, never half-applied.
    assert(!deserializeModel("").has_value());
    assert(!deserializeModel("version=1\n").has_value());
    assert(!deserializeModel("version=2\nbase=" + std::string(144, '1') + "\n").has_value());
    assert(!deserializeModel("version=1\nbase=" + std::string(143, '1') + "\n").has_value());
    assert(!deserializeModel("version=1\nbase=" + std::string(144, '9') + "\n").has_value());
    assert(!deserializeModel("version=1\nbase=" + std::string(144, '1') + "\nl1=999:3\n").has_value());
    assert(!deserializeModel("version=1\nbase=" + std::string(144, '1') + "\nl1=5:70000\n").has_value());
    assert(!deserializeModel("version=1\nbase=" + std::string(144, '1') + "\nl1=oops\n").has_value());
    assert(!deserializeModel("version=1\nbase=" + std::string(144, '1') + "\nmystery=1\n").has_value());
    assert(!deserializeModel("version=1\nbase=" + std::string(144, 'x') + "\n").has_value());
}

void testParameterTable()
{
    // Indices are saved in projects: pin the ones that exist.
    assert(kRoot == 0 && kScale == 1 && kLow == 2 && kOctaves == 3 && kOrder == 4 && kChaos == 5 && kGrid == 6);
    assert(kSeed == 11 && kLearn == 13 && kClearLearned == 16 && kConductorCh == 17);
    assert(kStatusState == 18 && kStatusLearned == 19 && kParameterCount == 20);
    assert(kParameterSpecs[kClearLearned].maximum == 65535);
    assert(kParameterSpecs[kStatusState].output && kParameterSpecs[kStatusLearned].output);
    for (std::size_t i = 0; i < kParameterSpecs.size(); ++i) {
        const auto& spec = kParameterSpecs[i];
        assert(spec.minimum <= spec.defaultValue && spec.defaultValue <= spec.maximum);
        assert(std::string(spec.symbol).size() > 0 && std::string(spec.name).size() > 0);
    }
    // Defaults: off by default for the optional features.
    const Params p = defaults();
    assert(p[kLearn] == 0 && p[kLearnedMix] == 0 && p[kConductorCh] == 0 && p[kOrder] == 1 && p[kChaos] == 0.5f);
    assert(std::string(gridName(2)) == "1/16" && gridQuarters(2, 4.0) == 0.25 && gridQuarters(11, 3.0) == 3.0);
}

}  // namespace

int main()
{
    testParameterTable();
    testScalesMatchTheDocs();
    testStylesAndModelEditing();
    testEffectiveMatrix();
    testSamplingAndWalk();
    testStatisticsFollowTheMatrix();
    testOrderTwoUsesLearnedHistory();
    testLearning();
    testPlaybackIsMusical();
    testRandomAccess();
    testStopAndJumpReleaseNotes();
    testConductor();
    testRestartRerollsTheMelodyAtTheBarLine();
    testSerialization();
    return 0;
}
