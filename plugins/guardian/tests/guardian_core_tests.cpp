#include "guardian_core.hpp"
#include <cassert>
#include <limits>
using namespace downspout::guardian;

static std::array<float,kParameterCount> defaultParams()
{
    std::array<float,kParameterCount> p{};
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = kParameterSpecs[i].defaultValue;
    return p;
}

int main()
{
    // Lookahead frames
    auto p = defaultParams();
    assert(lookaheadFrames(p, 48000) == 240);

    // Basic limiting: signal above ceiling is reduced
    State s;
    prepare(s, 48000);
    std::array<float,4096> in{}, l{}, r{};
    in.fill(3);
    process(s, p, in.size(), 48000, in.data(), in.data(), l.data(), r.data());
    assert(s.overload && s.reductionDb > 0);
    for (float v : l) assert(std::isfinite(v) && std::fabs(v) <= 1);

    // Non-finite input increments fault counter
    in.fill(0);
    in[0] = std::numeric_limits<float>::quiet_NaN();
    process(s, p, in.size(), 48000, in.data(), in.data(), l.data(), r.data());
    assert(s.faults > 0);
    for (float v : l) assert(std::isfinite(v));

    // Reset clears faults and overload
    p[kReset] = 1;
    process(s, p, in.size(), 48000, in.data(), in.data(), l.data(), r.data());
    assert(s.faults <= 1 && !s.overload);

    // Silence detection
    in.fill(0.25f);
    for (int i = 0; i < 30; ++i)
        process(s, p, in.size(), 48000, in.data(), in.data(), l.data(), r.data());
    assert(std::fabs(l.back()) < 0.01f);

    // Bypass passes audio unchanged
    p = defaultParams();
    p[kBypass] = 1.0f;
    in.fill(0.5f);
    prepare(s, 48000);
    process(s, p, in.size(), 48000, in.data(), in.data(), l.data(), r.data());
    for (float v : l) assert(std::fabs(v - 0.5f) < 1e-6f);
    assert(s.reductionDb == 0.0f);

    // Input gain: +6 dB doubles amplitude into limiter
    p = defaultParams();
    p[kInputDb] = 6.0f;  // ~2x gain
    in.fill(0.1f);
    prepare(s, 48000);
    process(s, p, 1, 48000, in.data(), in.data(), l.data(), r.data());
    // Output should be ~0.2 (0.1 * 2), well below ceiling so no limiting yet
    assert(std::fabs(l[0] - 0.1f * std::pow(10.0f, 6.0f / 20.0f)) < 0.01f);

    // Attack: with instant attack (kAttackMs=0), gain reduction is immediate
    p = defaultParams();
    p[kAttackMs] = 0.0f;
    in.fill(3.0f);
    prepare(s, 48000);
    process(s, p, 1, 48000, in.data(), in.data(), l.data(), r.data());
    assert(std::fabs(l[0]) <= std::pow(10.0f, p[kCeilingDb] / 20.0f) + 0.001f);

    // Attack: with slow attack, the first sample is NOT fully limited
    p = defaultParams();
    p[kAttackMs] = 50.0f;  // very slow
    in.fill(3.0f);
    prepare(s, 48000);
    // Just one sample — gain barely moves yet, so output may exceed ceiling
    process(s, p, 1, 48000, in.data(), in.data(), l.data(), r.data());
    // With slow attack the gain is still near 1.0 after one sample, output > ceiling
    const float ceiling = std::pow(10.0f, p[kCeilingDb] / 20.0f);
    assert(std::fabs(l[0]) > ceiling * 0.5f); // not yet fully limited

    // Clipper shape=1 (near-hard clip) — output stays within ceiling
    p = defaultParams();
    p[kClipperShape] = 1.0f;
    in.fill(0.5f);
    prepare(s, 48000);
    process(s, p, in.size(), 48000, in.data(), in.data(), l.data(), r.data());
    for (float v : l) assert(std::isfinite(v) && std::fabs(v) <= ceiling + 0.001f);

    // Clipper shape=0 (off): signal below ceiling passes through unchanged (no knee shaping)
    p = defaultParams();
    p[kLookaheadMs] = 0.0f; // remove delay so we can compare immediately
    p[kAttackMs] = 0.0f;
    std::array<float,64> l0{}, l1{};
    in.fill(0.1f); // well below ceiling, limiter won't engage
    p[kClipperShape] = 0.0f;
    prepare(s, 48000);
    process(s, p, in.size(), 48000, in.data(), in.data(), l0.data(), r.data());
    for (float v : l0) assert(std::fabs(v - 0.1f) < 0.001f); // shape=0: linear pass-through

    // Clipper shape=1: signal at threshold gets shaped (knee at 0, everything curves)
    in.fill(0.8f * ceiling); // 80% of ceiling — above knee for shape=1 (knee=0)
    p[kClipperShape] = 1.0f;
    prepare(s, 48000);
    process(s, p, in.size(), 48000, in.data(), in.data(), l1.data(), r.data());
    // Output should be below 0.8*ceiling (knee shaping pulled it down) and above 0
    assert(l1.back() > 0.0f && l1.back() < 0.8f * ceiling + 0.001f);

    return 0;
}
