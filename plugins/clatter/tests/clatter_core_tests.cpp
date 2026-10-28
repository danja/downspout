#include "downspout/test_assert.h"
#include "clatter_core_types.hpp"
#include "clatter_serialization.hpp"

#ifdef CLATTER_TEST_HAS_THUMPER
#include "thumper_core_types.hpp"
#endif
#ifdef CLATTER_TEST_HAS_SNATCH
#include "snatch_core_types.hpp"
#endif
#ifdef CLATTER_TEST_HAS_TOPS
#include "tops_core_types.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace {

constexpr int kSr = 44100;

struct Render {
    std::vector<float> l, r;
};

Render render(const clatter::Params& p, const int sound, const float velocity = 1.0f, const int n = kSr)
{
    clatter::ClatterEngine e;
    e.setSampleRate(kSr);
    e.setParams(p);
    e.trigger(sound, velocity);
    Render out {std::vector<float>(static_cast<std::size_t>(n), 0.0f), std::vector<float>(static_cast<std::size_t>(n), 0.0f)};
    e.render(out.l.data(), out.r.data(), n);
    return out;
}

double energy(const std::vector<float>& v, const int from, const int to)
{
    double s = 0.0;
    for (int i = from; i < to && i < static_cast<int>(v.size()); ++i) s += double(v[static_cast<std::size_t>(i)]) * v[static_cast<std::size_t>(i)];
    return s;
}
double energy(const std::vector<float>& v) { return energy(v, 0, static_cast<int>(v.size())); }

float peak(const std::vector<float>& v)
{
    float m = 0.0f;
    for (const float x : v) m = std::max(m, std::fabs(x));
    return m;
}

bool differs(const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) d += std::fabs(double(a[i]) - b[i]);
    return d > 1e-3;
}

// Magnitude of one frequency over [from, to), Hann-windowed.
double goertzel(const std::vector<float>& v, const int from, const int to, const double hz)
{
    const int len = to - from;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < len; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (len - 1));
        const double ph = 2.0 * M_PI * hz * i / kSr;
        re += w * v[static_cast<std::size_t>(from + i)] * std::cos(ph);
        im += w * v[static_cast<std::size_t>(from + i)] * std::sin(ph);
    }
    return std::sqrt(re * re + im * im);
}

int soundOf(const int note)
{
    const int s = clatter::ClatterEngine::soundForNote(note);
    assert(s >= 0);
    return s;
}

}  // namespace

int main()
{
    const clatter::Params base;

    // ---- The note table, against an independent list --------------------------------------------------------
    const std::set<int> expected = {39, 41, 43, 45, 47, 48, 50, 58, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
                                    73, 74, 75, 76, 77, 80, 82, 83, 84, 85, 86, 87};
    assert(static_cast<int>(expected.size()) == clatter::kSoundCount);
    std::set<int> seen;
    for (int i = 0; i < clatter::kSoundCount; ++i) {
        assert(expected.count(clatter::kPatches[i].note) == 1);
        assert(seen.insert(clatter::kPatches[i].note).second);  // no note twice
        assert(clatter::ClatterEngine::soundForNote(clatter::kPatches[i].note) == i);
        assert(i == 0 || static_cast<int>(clatter::kPatches[i].family) >= static_cast<int>(clatter::kPatches[i - 1].family));
    }
    for (const int note : {0, 35, 36, 37, 38, 40, 42, 44, 46, 49, 71, 72, 78, 79, 81, 88, 127})
        assert(clatter::ClatterEngine::soundForNote(note) == -1);

    // ---- Together with Thumper, Snatch and Tops every GM note 35-87 has exactly one owner, bar four -------------
#if defined(CLATTER_TEST_HAS_THUMPER) && defined(CLATTER_TEST_HAS_SNATCH) && defined(CLATTER_TEST_HAS_TOPS)
    const std::set<int> notCovered = {71, 72, 78, 79};  // whistles and cuicas
    for (int note = 35; note <= 87; ++note) {
        int owners = 0;
        owners += clatter::ClatterEngine::soundForNote(note) >= 0;
        owners += thumper::ThumperEngine::isKickNote(note);
        owners += snatch::SnatchEngine::engineForNote(note, 0) >= 0;
        owners += tops::TopsEngine::instrumentForNote(note) >= 0;
        assert(owners == (notCovered.count(note) ? 0 : 1));
    }
#endif

    // ---- Every sound: finite, sensibly loud, ends, repeatable ------------------------------------------------
    for (int i = 0; i < clatter::kSoundCount; ++i) {
        const Render a = render(base, i, 1.0f, kSr * 2);
        assert(std::all_of(a.l.begin(), a.l.end(), [](float x) { return std::isfinite(x); }));
        const float pk = peak(a.l);
        assert(pk > 0.2f && pk < 0.75f);              // calibrated: loud enough to hear, nowhere near clipping
        assert(energy(a.l, kSr * 2 - 2000, kSr * 2) == 0.0);  // it has ended, not a stuck drone
        assert(!differs(a.l, render(base, i, 1.0f, kSr * 2).l));  // deterministic
    }

    // ---- Velocity ----------------------------------------------------------------------------------------------
    const int tom = soundOf(41);
    const double full = energy(render(base, tom).l);
    assert(std::fabs(std::sqrt(energy(render(base, tom, 0.5f).l) / full) - 0.5) < 0.01);
    assert(energy(render(base, tom, 0.0f).l) == 0.0);

    // ---- Pitch: the tom rings at its tuned note, and Tune moves it -----------------------------------------------
    {
        const double f0 = clatter::kPatches[tom].freq;
        const Render r = render(base, tom);
        const double at = goertzel(r.l, 4400, 17600, f0);
        assert(at > 4.0 * goertzel(r.l, 4400, 17600, f0 * 1.3));
        assert(at > 4.0 * goertzel(r.l, 4400, 17600, f0 * 0.75));
        clatter::Params up = base;
        up.family[0].tune = 12.0f;
        const Render u = render(up, tom);
        assert(goertzel(u.l, 4400, 17600, 2.0 * f0) > 4.0 * goertzel(u.l, 4400, 17600, f0));
    }
    {   // the claves ring near 2.45 kHz; a high tune moves them up
        const int claves = soundOf(75);
        const Render r = render(base, claves);
        assert(goertzel(r.l, 0, 2000, 2450) > 3.0 * goertzel(r.l, 0, 2000, 1900));
        clatter::Params up = base;
        up.family[2].tune = 12.0f;
        const Render u = render(up, claves);
        assert(goertzel(u.l, 0, 2000, 4900) > 3.0 * goertzel(u.l, 0, 2000, 2450));
    }

    // ---- Every control of every family changes the sound ---------------------------------------------------------
    const int representative[clatter::kFamilyCount] = {soundOf(41), soundOf(67), soundOf(75), soundOf(70), soundOf(39)};
    for (int f = 0; f < clatter::kFamilyCount; ++f) {
        const int s = representative[f];
        const auto ref = render(base, s).l;
        clatter::Params p = base; p.family[f].tune = 5.0f;   assert(differs(render(p, s).l, ref));
        p = base; p.family[f].decay = 2.0f;                   assert(differs(render(p, s).l, ref));
        p = base; p.family[f].tone = 0.9f;                    assert(differs(render(p, s).l, ref));
        p = base; p.family[f].tone = 0.1f;                    assert(differs(render(p, s).l, ref));
        // decay lengthens the sound: more energy late
        p = base; p.family[f].decay = 3.0f;
        const int late = static_cast<int>(clatter::kPatches[s].decay * kSr);
        assert(energy(render(p, s).l, late, late + 8000) > energy(ref, late, late + 8000));
        // a trim on another family leaves this one alone
        p = base; p.family[(f + 1) % clatter::kFamilyCount].tune = 7.0f;
        assert(!differs(render(p, s).l, ref));
    }
    {
        clatter::Params p = base; p.level = 0.4f;
        assert(std::fabs(std::sqrt(energy(render(p, tom).l) / full) - 0.5) < 0.01);
    }

    // ---- Pan and width ---------------------------------------------------------------------------------------------
    {
        const Render r = render(base, tom);  // the low floor tom sits right of centre
        assert(energy(r.r) > energy(r.l) * 1.2);
        clatter::Params mono = base; mono.width = 0.0f;
        const Render m = render(mono, tom);
        assert(std::fabs(energy(m.l) - energy(m.r)) < 1e-6 * energy(m.l));
        clatter::Params wide = base; wide.width = 1.0f;
        const Render w = render(wide, tom);
        assert(energy(w.r) / energy(w.l) > energy(r.r) / energy(r.l));
    }

    // ---- Choke groups ----------------------------------------------------------------------------------------------
    {
        const auto groupChokes = [&](const int firstNote, const int secondNote, const int checkFrom) {
            clatter::ClatterEngine e;
            e.setSampleRate(kSr);
            e.setParams(base);
            std::vector<float> l(kSr, 0.0f), r(kSr, 0.0f), la(kSr, 0.0f), ra(kSr, 0.0f);
            e.trigger(soundOf(firstNote));
            e.render(l.data(), r.data(), 1000);
            e.trigger(soundOf(secondNote));
            e.render(l.data() + 1000, r.data() + 1000, kSr - 1000);
            clatter::ClatterEngine alone;  // the first sound by itself keeps ringing past `checkFrom`
            alone.setSampleRate(kSr);
            alone.setParams(base);
            alone.trigger(soundOf(firstNote));
            alone.render(la.data(), ra.data(), kSr);
            return energy(la, checkFrom, kSr) > 0.0 && energy(l, checkFrom, kSr) == 0.0;
        };
        assert(groupChokes(63, 62, 12000));  // mute conga cuts the open conga
        assert(groupChokes(87, 86, 14000));  // mute surdo cuts the open surdo
        assert(groupChokes(74, 73, 8000));   // short guiro cuts the long one
        // sounds outside a group do not choke each other
        clatter::ClatterEngine e;
        e.setSampleRate(kSr);
        e.setParams(base);
        std::vector<float> l(kSr, 0.0f), r(kSr, 0.0f);
        e.trigger(soundOf(64));
        e.render(l.data(), r.data(), 1000);
        e.trigger(soundOf(61));
        e.render(l.data() + 1000, r.data() + 1000, kSr - 1000);
        assert(energy(l, 12000, kSr) > 0.0);
    }

    // ---- Restrikes: castanets hit twice, 13 ms apart; the bell tree keeps re-striking ------------------------------
    {
        const Render c = render(base, soundOf(85));
        const int gap = static_cast<int>(0.013 * kSr);
        assert(energy(c.l, gap, gap + 150) > 2.0 * energy(c.l, gap - 200, gap - 50));  // a new strike lands
        const Render t = render(base, soundOf(84), 1.0f, kSr);
        const int step = static_cast<int>(0.017 * kSr);
        int lifts = 0;
        for (int k = 1; k < 8; ++k)
            if (energy(t.l, k * step, k * step + 150) > 1.1 * energy(t.l, k * step - 200, k * step - 50)) ++lifts;
        assert(lifts >= 5);
    }

    // ---- Voice pool: more hits than voices steals the oldest and stays bounded --------------------------------------
    {
        clatter::ClatterEngine e;
        e.setSampleRate(kSr);
        e.setParams(base);
        std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
        for (int i = 0; i < 40; ++i) {
            e.trigger(i % clatter::kSoundCount);
            e.render(l.data(), r.data(), 50);
        }
        assert(e.active());
        std::vector<float> big(kSr * 3, 0.0f), big2(kSr * 3, 0.0f);
        e.render(big.data(), big2.data(), kSr * 3);
        assert(!e.active());
        assert(std::all_of(big.begin(), big.end(), [](float x) { return std::isfinite(x) && std::fabs(x) < 8.0f; }));
        e.trigger(-1);
        e.trigger(clatter::kSoundCount);
        assert(!e.active());
    }

    // ---- Serialization ------------------------------------------------------------------------------------------------
    clatter::Params s;
    s.sound = 17; s.noteMap = (int)clatter::NoteMap::Selected; s.level = 0.55f; s.width = 0.9f;
    s.family[3].tune = -4.5f; s.family[3].decay = 2.5f; s.family[3].tone = 0.2f;
    clatter::Params r;
    assert(clatter::deserialize(clatter::serialize(s), r));
    assert(r.sound == 17 && r.noteMap == 0 && r.level == 0.55f && r.width == 0.9f);
    assert(r.family[3].tune == -4.5f && r.family[3].decay == 2.5f && r.family[3].tone == 0.2f);
    clatter::Params untouched = base;
    assert(!clatter::deserialize("99 1", untouched));
    assert(!clatter::deserialize("0 7 0 1 0.5 0 1 0.5 0 1 0.5 0 1 0.5 0 1 0.5 0.8 0.6", untouched));
    assert(!clatter::deserialize("nope", untouched) && untouched.sound == base.sound);
    assert(clatter::deserialize("0 1 99 9 5 0 1 0.5 0 1 0.5 0 1 0.5 0 1 0.5 9 9", untouched));
    assert(untouched.family[0].tune == 12.0f && untouched.family[0].decay == 4.0f && untouched.family[0].tone == 1.0f);
    assert(untouched.level == 1.0f && untouched.width == 1.0f);

    // ---- The noise generator: uniform, centred, repeatable, not stuck -----------------------------------------------
    downspout::clatter::Rng a, b;
    double sum = 0.0, sumSq = 0.0;
    std::set<float> distinct;
    for (int i = 0; i < 100000; ++i) {
        const float x = a.nextSigned();
        assert(x >= -1.0f && x < 1.0f && x == b.nextSigned());
        sum += x;
        sumSq += double(x) * x;
        if (i < 1000) distinct.insert(x);
    }
    assert(std::fabs(sum / 100000) < 0.02);
    assert(std::fabs(sumSq / 100000 - 1.0 / 3.0) < 0.01);  // uniform on [-1,1) has variance 1/3
    assert(distinct.size() > 990);
    return 0;
}
