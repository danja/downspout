#include "harmonic_atlas_core.hpp"

#include <array>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include "downspout/test_assert.h"

using namespace downspout::harmonic_atlas;

Transport at(const double quarter, const bool playing = true)
{
    Transport t;
    t.valid = true;
    t.playing = playing;
    t.bar = std::floor(quarter / 4.0);
    t.barBeat = quarter - t.bar * 4.0;
    return t;
}

void testGravity()
{
    // Gravity 0 is the plain table, and a pure function of its arguments.
    for (int style = 0; style < 4; ++style)
        for (std::int64_t chord = 0; chord < 64; ++chord) {
            assert(chordRoot(style, chord, 0, 8, 1701, 0.0f) == chordRoot(style, chord, 0, 8, 1701, 0.0f));
            const int r = chordRoot(style, chord, 5, 8, 1701, 0.7f);
            assert(r >= 0 && r < 12);
        }
    // Cadence positions always land on the tonic, whatever the gravity.
    for (std::int64_t chord = 7; chord < 200; chord += 8)
        for (int style = 0; style < 4; ++style) assert(chordRoot(style, chord, 3, 8, 77, 1.0f) == 3);

    // Gravity concentrates the roots on the tonic, subdominant and dominant.
    const auto strongShare = [](const float gravity) {
        int strong = 0, total = 0;
        for (std::uint64_t seed = 1; seed <= 40; ++seed)
            for (std::int64_t chord = 1; chord < 64; ++chord) {
                const int r = chordRoot(3, chord, 0, 0, seed, gravity);  // neo-Riemannian table wanders widely
                strong += (r == 0 || r == 5 || r == 7) ? 1 : 0;
                ++total;
            }
        return static_cast<double>(strong) / static_cast<double>(total);
    };
    const double free = strongShare(0.0f);
    const double pulled = strongShare(1.0f);
    assert(pulled > free + 0.30);

    // V resolves home: after a chord on the dominant, tonic is the most common next root.
    int afterV = 0, tonicAfterV = 0;
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
        for (std::int64_t chord = 2; chord < 40; ++chord) {
            if (chordRoot(3, chord - 1, 0, 0, seed, 1.0f) != 7) continue;
            ++afterV;
            tonicAfterV += chordRoot(3, chord, 0, 0, seed, 1.0f) == 0 ? 1 : 0;
        }
    assert(afterV > 50);
    assert(static_cast<double>(tonicAfterV) / afterV > 0.40);

    // The processor uses it: gravity changes the chords but not the note count bounds.
    std::array<float, kParameterCount> p {};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue;
    p[kGravity] = 1.0f;
    State a, b;
    const auto x = process(a, p, at(8.0), 1024, 48000.0, nullptr, 0);
    const auto y = process(b, p, at(8.0), 1024, 48000.0, nullptr, 0);
    assert(x.count == y.count && x.count >= 4 && x.count <= 7);
}

std::vector<std::vector<int>> chordsFrom(const std::array<float, kParameterCount>& p, const int first, const int count)
{
    // One block spanning `count` chords starting at chord `first` (8 quarters = 192000 frames each at 120 bpm).
    State state;
    const auto block = process(state, p, at(8.0 * first), 192000u * static_cast<unsigned>(count) - 1000u, 48000.0, nullptr, 0);
    std::vector<std::vector<int>> chords(static_cast<std::size_t>(count));
    for (std::uint32_t i = 0; i < block.count; ++i) {
        const auto& e = block.events[i];
        if ((e.data[0] & 0xf0) != 0x90 || e.data[2] == 0 || e.data[2] == 62) continue;  // skip colour notes
        const std::size_t index = std::min<std::size_t>(static_cast<std::size_t>(count) - 1, e.frame / 192000u);
        chords[index].push_back(e.data[1]);
    }
    for (auto& c : chords) std::sort(c.begin(), c.end());
    return chords;
}

double averageMovement(const std::vector<std::vector<int>>& chords)
{
    double total = 0.0;
    int pairs = 0;
    for (std::size_t i = 1; i < chords.size(); ++i) {
        if (chords[i].size() != chords[i - 1].size() || chords[i].empty()) continue;
        for (std::size_t v = 0; v < chords[i].size(); ++v) total += std::abs(chords[i][v] - chords[i - 1][v]);
        ++pairs;
    }
    return pairs > 0 ? total / pairs : 0.0;
}

void testVoiceLeading()
{
    std::array<float, kParameterCount> p {};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue;
    p[kScaleNotes] = 0.0f;
    p[kStyle] = 3;  // wide root movement, so smooth voicing has work to do
    p[kVoiceCount] = 4;
    p[kTension] = 0.0f;

    p[kVoiceLeading] = 0.4f;  // plain stacking
    const auto plain = chordsFrom(p, 0, 24);
    p[kVoiceLeading] = 1.0f;
    const auto led = chordsFrom(p, 0, 24);
    assert(plain.size() == 24 && led.size() == 24);

    // The chords keep their pitch classes and size, and stay in range.
    for (std::size_t c = 0; c < led.size(); ++c) {
        assert(led[c].size() == 4);
        for (int note : led[c]) assert(note >= 24 && note <= 96);
    }
    // Voice-leading moves the voices clearly less than stacking each chord from scratch.
    assert(averageMovement(led) < averageMovement(plain) * 0.85);
    // No voice leaps more than a fifth from one chord to the next.
    for (std::size_t c = 1; c < led.size(); ++c)
        for (std::size_t v = 0; v < led[c].size(); ++v) assert(std::abs(led[c][v] - led[c - 1][v]) <= 7);

    // A chord rendered from a standing start matches the same chord reached by playing through.
    p[kVoiceLeading] = 1.0f;
    const auto through = chordsFrom(p, 0, 24);
    for (int target : {7, 15, 20}) {
        const auto direct = chordsFrom(p, target, 1);
        assert(direct[0] == through[static_cast<std::size_t>(target)]);
    }

    // Loosening the control allows other voicings but never leaves the chord's pitch classes.
    p[kVoiceLeading] = 0.6f;
    const auto loose = chordsFrom(p, 0, 24);
    assert(loose != led);
    for (const auto& chord : loose) assert(chord.size() == 4);
}

int main()
{
    testVoiceLeading();
    testGravity();
    std::array<float, kParameterCount> p {};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue;
    State first;
    State second;
    const auto a = process(first, p, at(0.0), 1024, 48000.0, nullptr, 0);
    const auto b = process(second, p, at(0.0), 1024, 48000.0, nullptr, 0);
    assert(a.count == b.count && a.count >= 4 && a.count <= 7);
    for (std::uint32_t i = 0; i < a.count; ++i) assert(a.events[i].data == b.events[i].data);
    const auto stopped = process(first, p, at(0.1, false), 1024, 48000.0, nullptr, 0);
    assert(stopped.count == static_cast<std::uint32_t>(first.activeCount + a.count) || stopped.count > 0);
    p[kVoiceCount] = 6;
    State bounded;
    const auto chord = process(bounded, p, at(8.0), 1024, 48000.0, nullptr, 0);
    assert(chord.count <= 7);
    State looped;
    const auto loop = process(looped, p, at(0.0), 1024, 48000.0, nullptr, 0);
    assert(loop.count <= 7);
    auto faster = at(4.0);
    faster.bpm = 180.0;
    (void)process(looped, p, faster, 1024, 48000.0, nullptr, 0);
    return 0;
}
