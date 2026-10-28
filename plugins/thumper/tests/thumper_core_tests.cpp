#include "downspout/test_assert.h"
#include "thumper_core_types.hpp"
#include "thumper_serialization.hpp"
#include <cmath>
#include <vector>

namespace {
constexpr int kLen = 22050;

// Renders one hit. The click and noise are deterministic, so a render repeats exactly.
std::vector<float> render(const thumper::Params& p, float pitchHz = 0.0f, float velocity = 1.0f)
{
    thumper::ThumperEngine e;
    e.setSampleRate(44100);
    e.setParams(p);
    e.trigger(pitchHz, velocity);
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
    const thumper::Params base;
    const auto ref = render(base);
    assert(energy(ref) > 0.0);

    // Velocity scales level (amplitude is linear in it).
    const double half = std::sqrt(energy(render(base, 0.0f, 0.5f)) / energy(ref));
    assert(std::fabs(half - 0.5) < 0.01);

    // A per-hit pitch changes the sound without touching the parameter.
    thumper::ThumperEngine e;
    e.setParams(base);
    e.trigger(110.0f);
    assert(e.params().pitch == base.pitch);
    assert(differs(render(base, 110.0f), ref));

    // Every sound control has an audible effect on the engine it belongs to.
    const auto changed = [&](const thumper::Params& p) { return differs(render(p), ref); };
    thumper::Params p;
    p = base; p.pitch = 80;          assert(changed(p));
    p = base; p.pitchDrop = 2;       assert(changed(p));
    p = base; p.decay = 0.2f;        assert(changed(p));
    p = base; p.click = 0;           assert(changed(p));
    p = base; p.body = 0.3f;         assert(changed(p));
    p = base; p.drive = 0.9f;        assert(changed(p));
    p = base; p.tone = 0.9f;         assert(changed(p));
    p = base; p.volume = 0.4f;       assert(changed(p));
    p = base; p.subMix = 0;          assert(changed(p));
    p = base; p.distortion = 0.8f;   assert(changed(p));
    p = base; p.pitchEnvDecay = 1;   assert(changed(p));
    p = base; p.filterEnvDecay = 0.02f; assert(changed(p));
    p = base; p.ampEnvDecay = 0.1f;  assert(changed(p));
    p = base; p.engine2 = 3; p.layerMix = 0.7f; assert(changed(p));
    p = base; p.engine = (int)thumper::Engine::FM;
    const auto fm = render(p);
    p.fmRatio = 5;  assert(differs(render(p), fm));
    p = base; p.engine = (int)thumper::Engine::FM; p.fmIndex = 8; assert(differs(render(p), fm));
    // Modal Decay lengthens the upper partials of the Modal engine, so its tail carries more energy.
    p = base; p.engine = (int)thumper::Engine::Modal; p.click = 0; p.drive = 0; p.subMix = 0; p.modalDecay = 0.1f;
    const double shortTail = energy(render(p), 2000, 12000);
    p.modalDecay = 1.5f;
    assert(energy(render(p), 2000, 12000) > shortTail * 1.01);
    // All ten engines sound different from each other.
    const auto sig = [&](int engine) { thumper::Params q = base; q.engine = engine; q.click = 0; return render(q); };
    for (int a = 0; a < 10; ++a)
        for (int b = a + 1; b < 10; ++b) assert(differs(sig(a), sig(b)));

    // The second layer is inert by default: a fresh patch has no layer and a zero mix.
    assert(thumper::Params().engine2 < 0 && thumper::Params().layerMix == 0.0f);

    // Presets still resolve to distinct sounds.
    thumper::ThumperEngine pe;
    thumper::Params gab; gab.preset = (int)thumper::Preset::Gabber;
    pe.setParams(gab); pe.applyPreset();
    assert(pe.params().drive > 0.7f && pe.params().pitch == 60.0f);

    // General MIDI kick notes are 35 and 36 only.
    assert(thumper::ThumperEngine::isKickNote(35) && thumper::ThumperEngine::isKickNote(36));
    assert(!thumper::ThumperEngine::isKickNote(34) && !thumper::ThumperEngine::isKickNote(37) && !thumper::ThumperEngine::isKickNote(60));

    // Serialization round-trips every field, including "no layer", and rejects bad input.
    thumper::Params s; s.engine = 4; s.engine2 = -1; s.pitch = 71; s.modalDecay = 1.2f; s.layerMix = 0.25f; s.noteMap = (int)thumper::NoteMap::Selected;
    thumper::Params r;
    assert(thumper::deserialize(thumper::serialize(s), r));
    assert(r.engine == 4 && r.engine2 == -1 && r.pitch == 71 && r.modalDecay == 1.2f && r.layerMix == 0.25f && r.noteMap == 0);
    thumper::Params untouched = base;
    assert(!thumper::deserialize("99 0 55 12 0.5 0.5 0.7 0.3 0.5 0.8 2 3 0.4 0.5 0 0.1 0.3 0.5 -1 0", untouched));
    assert(!thumper::deserialize("garbage", untouched) && untouched.engine == base.engine);
    assert(thumper::deserialize("0 0 9999 12 0.5 0.5 0.7 0.3 0.5 0.8 2 3 0.4 0.5 0 0.1 0.3 0.5 -1 0", untouched));
    assert(untouched.pitch == 120.0f && untouched.noteMap == 1);  // pre-note-map string: General MIDI, the default
    return 0;
}
