#include "resonance_garden_core.hpp"
#include "downspout/test_assert.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>
using namespace downspout::resonance_garden;
static void testBasics(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;State s;prepare(s,48000);reset(s);std::array<float,4096>in{},l{},r{};process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data(),nullptr,0);for(float v:l)assert(std::isfinite(v)&&std::fabs(v)<1e-6);in[0]=1;process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data(),nullptr,0);bool tail=false;for(float v:l){assert(std::isfinite(v)&&std::fabs(v)<=1);tail|=std::fabs(v)>1e-5;}assert(tail);MidiEvent e;e.size=3;e.data={0x90,72,100,0};process(s,p,512,48000,in.data(),in.data(),l.data(),r.data(),&e,1);assert(s.statusVoices<=kMaxVoices);in[0]=std::numeric_limits<float>::infinity();process(s,p,512,48000,in.data(),in.data(),l.data(),r.data(),nullptr,0);for(int i=0;i<512;++i)assert(std::isfinite(l[i]));}

namespace {
using Params = std::array<float, kParameterCount>;
Params defaults() { Params p{}; for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue; return p; }
double hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// Independent copy of BassGen's scales (docs/scales.md), index order included.
const std::vector<std::vector<int>> kExpected {
    {0,2,4,5,7,9,11}, {0,2,4,5,7,9,11}, {0,2,3,5,7,8,10}, {0,2,3,5,7,8,11}, {0,2,3,5,7,9,11},
    {0,2,3,5,7,9,10}, {0,1,3,5,7,8,10}, {0,2,4,6,7,9,11}, {0,2,4,5,7,9,10}, {0,1,3,5,6,8,10},
    {0,1,4,5,7,8,10}, {0,1,3,5,7,9,11}, {0,1,3,5,7,8,11}, {0,2,4,7,9}, {0,3,5,7,10}, {0,3,5,6,7,10},
    {0,2,4,6,8,10}, {0,1,3,4,6,8,10}, {0,1,3,4,6,7,9,10}, {0,2,3,5,6,8,9,11},
    {0,2,4,5,7,9,10,11}, {0,2,4,5,7,8,9,11}, {0,2,3,4,5,7,9,10}};

void testScaleListMatchesBassGen()
{
    assert(kScaleCount == 23 && kExpected.size() == 23);
    assert(kParameterSpecs[kScale].maximum == 22);
    for (int i = 0; i < kScaleCount; ++i) {
        assert(scaleSize(i) == static_cast<int>(kExpected[i].size()));
        for (int d = 0; d < scaleSize(i); ++d) assert(scaleIntervals(i)[d] == kExpected[i][d]);
    }
    // Ordinals are saved in projects: pin the ends and the names the dropdown shows.
    assert(std::string(scaleName(0)) == "Major" && std::string(scaleName(2)) == "Minor");
    assert(std::string(scaleName(22)) == "Bebop Minor");
    std::set<std::string> names; for (int i = 0; i < kScaleCount; ++i) names.insert(scaleName(i));
    assert(names.size() == 23);
}

void testFallbackNotesClimbTheScale()
{
    // Voice 0 is the root; degrees continue into the next octave; the root shifts the lot.
    assert(fallbackNote(0, 0, 0) == 48 && fallbackNote(0, 0, 2) == 52 && fallbackNote(0, 0, 7) == 60);
    assert(fallbackNote(0, 2, 2) == 51);          // minor third, not major
    assert(fallbackNote(5, 13, 5) == 48 + 5 + 12); // pentatonic wraps after five degrees
    for (int sc = 0; sc < kScaleCount; ++sc)
        for (int v = 0; v < kMaxVoices; ++v) {
            const int n = fallbackNote(0, sc, v);
            assert(n >= 48 && n < 90);
            if (v > 0) assert(n > fallbackNote(0, sc, v - 1));
        }
}

void testScaleSetsThePitchOfTheInternalBank()
{
    Params p = defaults();
    p[kInharmonicity] = 0; p[kVoiceLimit] = 3; p[kMix] = 1;
    auto third = [&](float scale, std::vector<float>& out) {
        p[kScale] = scale;
        State s; prepare(s, 48000); reset(s);
        std::vector<float> in(48000, 0.0f), r(48000);
        out.assign(48000, 0.0f);
        in[0] = 1;
        process(s, p, 48000, 48000, in.data(), in.data(), out.data(), r.data(), nullptr, 0);
        return s.voices[2].delay;      // the resonator's period in samples is its pitch
    };
    std::vector<float> major, ionian, minor;
    assert(third(0, major) == static_cast<std::uint32_t>(48000 / hz(52)));   // E
    assert(third(2, minor) == static_cast<std::uint32_t>(48000 / hz(51)));   // Eb
    third(1, ionian);
    double diff = 0, same = 0, total = 0;
    for (std::size_t i = 0; i < major.size(); ++i) {
        diff += std::fabs(major[i] - minor[i]); same += std::fabs(major[i] - ionian[i]); total += std::fabs(major[i]);
    }
    assert(total > 1e-3 && diff > 0.05 * total);   // the scale is audible
    assert(same == 0);                              // Major and Ionian are the same notes
}

void testTwelveNotesRing()
{
    assert(kMaxVoices == 12 && kParameterSpecs[kVoiceLimit].maximum == 12);
    Params p = defaults();
    p[kVoiceLimit] = 12; p[kInharmonicity] = 0; p[kMix] = 1;
    const int notes[12] = {40, 43, 47, 50, 54, 57, 61, 64, 68, 71, 75, 78};
    std::array<MidiEvent, 12> ev{};
    for (int i = 0; i < 12; ++i) { ev[i].frame = 0; ev[i].size = 3; ev[i].data = {0x90, static_cast<std::uint8_t>(notes[i]), 100, 0}; }

    State s; prepare(s, 48000); reset(s);
    std::vector<float> in(48000, 0.0f), l(48000), r(48000);
    in[0] = 1;
    process(s, p, 48000, 48000, in.data(), in.data(), l.data(), r.data(), ev.data(), 12);
    std::set<int> held; for (int i = 0; i < 12; ++i) held.insert(s.voices[i].note);
    assert(held == std::set<int>(notes, notes + 12));   // none stolen, none left on the scale
    assert(s.statusVoices == 12);
    // The twelfth note, which an eight-voice bank could not hold, adds sound of its own.
    State s11; prepare(s11, 48000); reset(s11);
    std::vector<float> l11(48000), r11(48000);
    process(s11, p, 48000, 48000, in.data(), in.data(), l11.data(), r11.data(), ev.data(), 11);
    double diff = 0, total = 0;
    for (std::size_t i = 0; i < l.size(); ++i) { diff += std::fabs(l[i] - l11[i]); total += std::fabs(l[i]); }
    assert(total > 1e-3 && diff > 0.02 * total);
}

void testMidiOverridesTheScaleAndTheScaleReturns()
{
    Params p = defaults();
    p[kVoiceLimit] = 2; p[kDecay] = 0.05f; p[kInharmonicity] = 0;
    State s; prepare(s, 48000); reset(s);
    std::vector<float> in(48000, 0.0f), l(48000), r(48000);
    process(s, p, 256, 48000, in.data(), in.data(), l.data(), r.data(), nullptr, 0);
    assert(s.voices[0].internal && s.voices[0].note == 48);

    MidiEvent on; on.frame = 0; on.size = 3; on.data = {0x90, 72, 100, 0};
    process(s, p, 256, 48000, in.data(), in.data(), l.data(), r.data(), &on, 1);
    assert(s.voices[0].note == 72 && !s.voices[0].internal);   // took an internal voice
    assert(s.voices[1].internal);                              // the other stays on the scale

    MidiEvent off = on; off.data = {0x80, 72, 0, 0};
    process(s, p, 256, 48000, in.data(), in.data(), l.data(), r.data(), &off, 1);
    process(s, p, 48000, 48000, in.data(), in.data(), l.data(), r.data(), nullptr, 0);
    assert(s.voices[0].internal && s.voices[0].note == 48);    // released tail over: back to the scale
}

void testChangingScaleRetunesLive()
{
    Params p = defaults();
    p[kVoiceLimit] = 3; p[kScale] = 0;
    State s; prepare(s, 48000); reset(s);
    std::vector<float> in(2048, 0.0f), l(2048), r(2048);
    process(s, p, 2048, 48000, in.data(), in.data(), l.data(), r.data(), nullptr, 0);
    assert(s.voices[2].note == 52);
    p[kScale] = 2; p[kRoot] = 2;
    process(s, p, 2048, 48000, in.data(), in.data(), l.data(), r.data(), nullptr, 0);
    assert(s.voices[2].note == 53);     // D minor's third is F
}

void testInternalBankSustains()
{
    // With no MIDI the bank should still be there long after the start-up, not fade out.
    Params p = defaults();
    p[kDecay] = 0.05f; p[kMix] = 1;
    State s; prepare(s, 48000); reset(s);
    std::vector<float> in(48000, 0.0f), l(48000), r(48000);
    process(s, p, 48000, 48000, in.data(), in.data(), l.data(), r.data(), nullptr, 0);
    process(s, p, 48000, 48000, in.data(), in.data(), l.data(), r.data(), nullptr, 0);
    in[0] = 1;
    process(s, p, 48000, 48000, in.data(), in.data(), l.data(), r.data(), nullptr, 0);
    double e = 0; for (float v : l) e += std::fabs(v);
    assert(e > 1e-3);
}
}

int main()
{
    testBasics();
    testScaleListMatchesBassGen();
    testFallbackNotesClimbTheScale();
    testScaleSetsThePitchOfTheInternalBank();
    testTwelveNotesRing();
    testMidiOverridesTheScaleAndTheScaleReturns();
    testChangingScaleRetunesLive();
    testInternalBankSustains();
    return 0;
}
