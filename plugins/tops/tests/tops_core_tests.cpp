#include "downspout/test_assert.h"
#include "tops_core_types.hpp"
#include "tops_serialization.hpp"
#include <cmath>
#include <vector>

namespace {
constexpr int kLen = 44100;

// The engine's noise generator is deterministic, so a render repeats exactly.
std::vector<float> render(const tops::Params& p, int instrument = -1, float velocity = 1.0f)
{
    tops::TopsEngine e;
    e.setSampleRate(44100);
    e.setParams(p);
    e.trigger(instrument, velocity);
    std::vector<float> l(kLen, 0.0f), r(kLen, 0.0f);
    e.render(l.data(), r.data(), kLen);
    return l;
}
double energy(const std::vector<float>& v, int from = 0, int to = kLen)
{
    double s = 0;
    for (int i = from; i < to; ++i) s += double(v[i]) * v[i];
    return s;
}
bool differs(const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0;
    for (int i = 0; i < kLen; ++i) d += std::fabs(double(a[i]) - b[i]);
    return d > 1e-3;
}
}  // namespace

int main()
{
    tops::Params base;
    base.instrument = (int)tops::Instrument::Crash;
    const auto ref = render(base);
    assert(energy(ref) > 0.0);

    const double half = std::sqrt(energy(render(base, -1, 0.5f)) / energy(ref));
    assert(std::fabs(half - 0.5) < 0.01);

    // Every control changes the sound.
    const auto changed = [&](tops::Params p) { return differs(render(p), ref); };
    tops::Params p;
    p = base; p.pitch = 5000;     assert(changed(p));
    p = base; p.decay = 0.3f;     assert(changed(p));
    p = base; p.tone = 0.1f;      assert(changed(p));
    p = base; p.tone = 1.0f;      assert(changed(p));
    p = base; p.noise = 0.1f;     assert(changed(p));
    p = base; p.brightness = 0.2f; assert(changed(p));
    p = base; p.volume = 0.3f;    assert(changed(p));
    p = base; p.distortion = 0.8f; assert(changed(p));
    for (int e = 1; e < 6; ++e) { p = base; p.engine = e; assert(changed(p)); }
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j) assert(differs(render(base, i), render(base, j)));

    // The envelope really decays: a ride's last tenth is far below its first tenth (not a plateau,
    // and not speeding up as the old decay maths did).
    p = base; p.instrument = (int)tops::Instrument::Ride; p.decay = 0.2f;
    const auto ride = render(p);
    const int rideLen = int(0.2f * 2.5f * 44100);
    const double head = energy(ride, 0, rideLen / 10), tail = energy(ride, rideLen * 9 / 10, rideLen);
    assert(tail < head * 1e-3);
    // Halfway through, the level has fallen by about 4 e-folds (e^-4), whatever the decay setting.
    const double mid = energy(ride, rideLen / 2 - 200, rideLen / 2 + 200) / energy(ride, 0, 400);
    assert(mid > 1e-5 && mid < 1e-1);

    // A closed hat chokes an open hat that is still sounding.
    p = base; p.instrument = (int)tops::Instrument::OpenHH;
    const auto openAlone = render(p);
    tops::TopsEngine e;
    e.setSampleRate(44100);
    e.setParams(p);
    e.trigger((int)tops::Instrument::OpenHH);
    std::vector<float> l(kLen, 0.0f), r(kLen, 0.0f);
    e.render(l.data(), r.data(), 512);
    e.trigger((int)tops::Instrument::ClosedHH);
    e.render(l.data() + 512, r.data() + 512, kLen - 512);
    assert(energy(openAlone, 3000, 6500) > 0.0);  // the open hat alone would still be sounding here
    assert(energy(l, 3000, kLen) == 0.0);         // the closed hat (0.05 s) has ended and the open one was cut

    // General MIDI drum notes.
    assert(tops::TopsEngine::instrumentForNote(42) == (int)tops::Instrument::ClosedHH);
    assert(tops::TopsEngine::instrumentForNote(44) == (int)tops::Instrument::PedalHH);
    assert(tops::TopsEngine::instrumentForNote(46) == (int)tops::Instrument::OpenHH);
    assert(tops::TopsEngine::instrumentForNote(49) == (int)tops::Instrument::Crash);
    assert(tops::TopsEngine::instrumentForNote(51) == (int)tops::Instrument::Ride);
    assert(tops::TopsEngine::instrumentForNote(55) == (int)tops::Instrument::Splash);
    assert(tops::TopsEngine::instrumentForNote(52) == (int)tops::Instrument::China);
    assert(tops::TopsEngine::instrumentForNote(56) == (int)tops::Instrument::Cowbell);
    assert(tops::TopsEngine::instrumentForNote(54) == (int)tops::Instrument::Tambourine);
    assert(tops::TopsEngine::instrumentForNote(81) == (int)tops::Instrument::Triangle);
    assert(tops::TopsEngine::instrumentForNote(36) == -1 && tops::TopsEngine::instrumentForNote(60) == -1);
    // A note-selected instrument sounds like the same instrument chosen by parameter.
    tops::Params viaParam = base; viaParam.instrument = (int)tops::Instrument::Splash;
    assert(!differs(render(viaParam), render(base, tops::TopsEngine::instrumentForNote(55))));

    tops::TopsEngine pe;
    tops::Params metal; metal.preset = (int)tops::Preset::Metal;
    pe.setParams(metal); pe.applyPreset();
    assert(pe.params().decay == 0.3f && pe.params().brightness == 0.8f);

    tops::Params s; s.instrument = 4; s.engine = 2; s.pitch = 7777; s.noteMap = (int)tops::NoteMap::Selected;
    tops::Params r2;
    assert(tops::deserialize(tops::serialize(s), r2));
    assert(r2.instrument == 4 && r2.engine == 2 && r2.pitch == 7777 && r2.noteMap == 0);
    tops::Params untouched = base;
    assert(!tops::deserialize("99 0 0 8000 0.08 0.6 0.5 0.7 0.7 0", untouched));
    assert(!tops::deserialize("nope", untouched) && untouched.instrument == base.instrument);
    assert(tops::deserialize("0 0 0 8000 0.08 0.6 0.5 0.7 0.7 0", untouched) && untouched.noteMap == 1);  // pre-note-map string: General MIDI, the default
    return 0;
}
