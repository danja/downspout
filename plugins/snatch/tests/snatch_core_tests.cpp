#include "downspout/test_assert.h"
#include "snatch_core_types.hpp"
#include "snatch_serialization.hpp"
#include <cmath>
#include <vector>

namespace {
constexpr int kLen = 22050;

// The engine's noise generator is deterministic, so a render repeats exactly.
std::vector<float> render(const snatch::Params& p, float velocity = 1.0f, int engine = -1)
{
    snatch::SnatchEngine e;
    e.setSampleRate(44100);
    e.setParams(p);
    e.trigger(engine, velocity);
    std::vector<float> l(kLen, 0.0f), r(kLen, 0.0f);
    e.render(l.data(), r.data(), kLen);
    return l;
}
double energy(const std::vector<float>& v)
{
    double s = 0;
    for (float x : v) s += double(x) * x;
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
    const snatch::Params base;
    const auto ref = render(base);
    assert(energy(ref) > 0.0);

    const double half = std::sqrt(energy(render(base, 0.5f)) / energy(ref));
    assert(std::fabs(half - 0.5) < 0.01);

    // Every control changes the sound (Tone belongs to the Tone + Noise engine, which is the default).
    const auto changed = [&](snatch::Params p) { return differs(render(p), ref); };
    snatch::Params p;
    p = base; p.pitch = 300;       assert(changed(p));
    p = base; p.tone = 0.1f;       assert(changed(p));
    p = base; p.noise = 0.2f;      assert(changed(p));
    p = base; p.snap = 0.0f;       assert(changed(p));
    p = base; p.body = 0.3f;       assert(changed(p));
    p = base; p.decay = 0.1f;      assert(changed(p));
    p = base; p.volume = 0.4f;     assert(changed(p));
    p = base; p.distortion = 0.8f; assert(changed(p));
    for (int a = 0; a < 6; ++a)
        for (int b = a + 1; b < 6; ++b) {
            snatch::Params x = base, y = base;
            x.engine = a; y.engine = b;
            assert(differs(render(x), render(y)));
        }

    // The decay control sets how long the hit rings: the last quarter is quiet for a short decay.
    p = base; p.decay = 0.1f;
    const auto shortHit = render(p);
    double tail = 0;
    for (int i = kLen * 3 / 4; i < kLen; ++i) tail += double(shortHit[i]) * shortHit[i];
    assert(tail == 0.0);

    // Side Stick is a short click: silent after 0.06 s even with the longest Decay.
    p = base; p.engine = (int)snatch::Engine::SideStick; p.decay = 1.0f;
    const auto stick = render(p);
    double stickTail = 0;
    for (int i = 4000; i < kLen; ++i) stickTail += double(stick[i]) * stick[i];
    assert(energy(stick) > 0.0 && stickTail == 0.0);

    // General MIDI snare notes: 37 stick, 38 the selected engine, 40 electric; others are not ours.
    assert(snatch::SnatchEngine::engineForNote(37, 1) == (int)snatch::Engine::SideStick);
    assert(snatch::SnatchEngine::engineForNote(38, 2) == 2);
    assert(snatch::SnatchEngine::engineForNote(40, 0) == (int)snatch::Engine::Electronic);
    assert(snatch::SnatchEngine::engineForNote(36, 0) == -1 && snatch::SnatchEngine::engineForNote(39, 0) == -1);
    // A note-selected engine sounds like the same engine chosen by parameter.
    snatch::Params viaParam = base; viaParam.engine = (int)snatch::Engine::SideStick;
    assert(!differs(render(viaParam), render(base, 1.0f, snatch::SnatchEngine::engineForNote(37, 0))));

    snatch::SnatchEngine pe;
    snatch::Params jazz; jazz.preset = (int)snatch::Preset::Jazz;
    pe.setParams(jazz); pe.applyPreset();
    assert(pe.params().engine == 2 && pe.params().pitch == 180.0f);

    snatch::Params s; s.engine = 5; s.pitch = 333; s.snap = 0.15f; s.noteMap = (int)snatch::NoteMap::Selected;
    snatch::Params r;
    assert(snatch::deserialize(snatch::serialize(s), r));
    assert(r.engine == 5 && r.pitch == 333 && r.snap == 0.15f && r.noteMap == 0);
    snatch::Params untouched = base;
    assert(!snatch::deserialize("9 0 200 0.5 0.7 0.6 0.6 0.25 0.8 0", untouched));
    assert(!snatch::deserialize("6 0 200 0.5 0.7 0.6 0.6 0.25 0.8 0", untouched));
    assert(!snatch::deserialize("nope", untouched) && untouched.engine == base.engine);
    assert(snatch::deserialize("0 0 9999 0.5 0.7 0.6 0.6 0.25 0.8 0", untouched) && untouched.pitch == 400.0f && untouched.noteMap == 1);  // pre-note-map string: General MIDI, the default
    return 0;
}
