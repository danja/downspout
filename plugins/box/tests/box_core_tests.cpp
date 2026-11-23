#include "box_core_types.hpp"
#include "box_params.hpp"
#include "downspout/test_assert.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace box = downspout::box;

namespace {

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

std::uint32_t nextRandom(std::uint32_t& state) noexcept
{
    std::uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

// Noise exercises the whole modal bank; a sine at one pitch only excites modes
// that happen to sit near it, which hid several inert controls in early probes.
Render renderNoise(const box::Parameters& params, const int frames,
                   const int blockSize, const std::uint32_t seed = 12345u)
{
    Render out;
    out.left = std::vector<float>(static_cast<std::size_t>(frames));
    out.right = std::vector<float>(static_cast<std::size_t>(frames));
    std::vector<float> input = std::vector<float>(static_cast<std::size_t>(frames));
    std::uint32_t rng = seed;
    for (int i = 0; i < frames; ++i) {
        const float raw = static_cast<float>(nextRandom(rng) & 0x00ffffffu)
                        / static_cast<float>(0x01000000u);
        input[static_cast<std::size_t>(i)] = 0.25f * (2.0f * raw - 1.0f);
    }

    box::EngineState state {};
    for (int i = 0; i < frames; i += blockSize) {
        const int count = std::min(blockSize, frames - i);
        // Advance the pointers per block exactly as a host does.
        const float* inputs[2] = { input.data() + i, input.data() + i };
        float* outputs[2] = { out.left.data() + i, out.right.data() + i };
        box::processBlock(state, params, static_cast<std::uint32_t>(count), 48000.0, inputs, outputs);
    }
    return out;
}

Render renderImpulse(const box::Parameters& params, const int frames, const int blockSize)
{
    Render out;
    out.left = std::vector<float>(static_cast<std::size_t>(frames), 0.0f);
    out.right = std::vector<float>(static_cast<std::size_t>(frames), 0.0f);
    std::vector<float> input = std::vector<float>(static_cast<std::size_t>(frames), 0.0f);
    input[0] = 1.0f;

    box::EngineState state {};
    for (int i = 0; i < frames; i += blockSize) {
        const int count = std::min(blockSize, frames - i);
        const float* inputs[2] = { input.data() + i, input.data() + i };
        float* outputs[2] = { out.left.data() + i, out.right.data() + i };
        box::processBlock(state, params, static_cast<std::uint32_t>(count), 48000.0, inputs, outputs);
    }
    return out;
}

float rms(const std::vector<float>& v, const int from = 0, const int to = -1)
{
    const int end = to < 0 ? static_cast<int>(v.size()) : to;
    double sum = 0.0;
    for (int i = from; i < end; ++i) {
        const double x = v[static_cast<std::size_t>(i)];
        sum += x * x;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(std::max(1, end - from))));
}

float difference(const Render& a, const Render& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.left.size(); ++i) {
        const double dl = a.left[i] - b.left[i];
        const double dr = a.right[i] - b.right[i];
        sum += dl * dl + dr * dr;
    }
    return static_cast<float>(std::sqrt(sum / (2.0 * static_cast<double>(a.left.size()))));
}

bool allFinite(const Render& r)
{
    return std::all_of(r.left.begin(), r.left.end(), [](float x) { return std::isfinite(x); })
        && std::all_of(r.right.begin(), r.right.end(), [](float x) { return std::isfinite(x); });
}

template <typename Mutator>
box::Parameters changed(const box::Parameters& base, Mutator&& mutator)
{
    box::Parameters out = base;
    mutator(out);
    return out;
}

float peak(const Render& r)
{
    float p = 0.0f;
    for (const float x : r.left)
        p = std::max(p, std::fabs(x));
    for (const float x : r.right)
        p = std::max(p, std::fabs(x));
    return p;
}

void assertAudible(const char* name, const box::Parameters& baseline,
                   const box::Parameters& changed, const float threshold = 2.0e-4f)
{
    const Render a = renderNoise(baseline, 24000, 256);
    const Render b = renderNoise(changed, 24000, 256);
    const float d = difference(a, b);
    if (!(d > threshold)) {
        std::fprintf(stderr, "%s inaudible: diff %.8f (need > %.8f)\n", name, d, threshold);
    }
    assert(d > threshold);
}

void testDefaultAndPhysicalLaws()
{
    const box::Parameters p;
    const box::BodyState s = box::analyseBody(p, 48000.0);
    assert(s.valid);
    assert(s.bodyHz > 60.0f && s.bodyHz < 100.0f);   // ~77 Hz guitar top
    assert(s.airHz > 100.0f && s.airHz < 160.0f);    // ~126 Hz soundhole
    assert(s.decaySec > 0.5f && s.decaySec < 2.0f);
    assert(s.q > 10.0f && s.q < 60.0f);
    for (const auto& channel : s.modes) {
        for (const auto& mode : channel) {
            assert(std::isfinite(mode.filter.b0 + mode.filter.b1 + mode.filter.b2
                                 + mode.filter.a1 + mode.filter.a2));
            assert(std::isfinite(mode.gain));
        }
    }

    // f ~ size^-2, thickness, sqrt(stiffness) and 1/sqrt(density).
    auto bodyHz = [](const box::Parameters& q) { return box::analyseBody(q, 48000.0).bodyHz; };
    box::Parameters q = p;
    q.size = 2.0f;
    assert(std::fabs(bodyHz(q) / s.bodyHz - 0.25f) < 0.02f);
    q = p; q.thickness = 6.0f;
    assert(std::fabs(bodyHz(q) / s.bodyHz - 2.0f) < 0.05f);
    // The test values sit inside the published ranges so the law is checked
    // against the exposed control, not against a clamped automation value.
    q = p; q.stiffness = 40.0f;
    assert(std::fabs(bodyHz(q) / s.bodyHz - std::sqrt(40.0f / 11.0f)) < 0.05f);
    q = p; q.density = 1500.0f;
    assert(std::fabs(bodyHz(q) / s.bodyHz - std::sqrt(450.0f / 1500.0f)) < 0.05f);
}

void testAirResonance()
{
    box::Parameters p;
    p.hole = 0.0f;
    box::BodyState s = box::analyseBody(p, 48000.0);
    assert(s.airHz == 0.0f);
    for (const auto& air : s.air)
        assert(air.gain == 0.0f);

    p = box::Parameters {};
    const float base = box::analyseBody(p, 48000.0).airHz;
    p.hole = 180.0f;
    assert(std::fabs(box::analyseBody(p, 48000.0).airHz / base - std::sqrt(2.0f)) < 0.1f);
    p = box::Parameters {};
    p.volume = 60.0f;
    assert(std::fabs(box::analyseBody(p, 48000.0).airHz / base - 0.5f) < 0.1f);

    p = box::Parameters {};
    p.body = 1.0f;  // Piano: no soundhole.
    assert(box::analyseBody(p, 48000.0).airHz == 0.0f);
}

void testEveryControlAudible()
{
    const box::Parameters p;

    assertAudible("Body=Piano", p, changed(p, [](box::Parameters& q) { q.body = 1.0f; }));
    assertAudible("Body=Cabinet", p, changed(p, [](box::Parameters& q) { q.body = 2.0f; }));
    assertAudible("Body=Plate", p, changed(p, [](box::Parameters& q) { q.body = 3.0f; }));
    assertAudible("Body Size", p, changed(p, [](box::Parameters& q) { q.size = 2.0f; }));
    assertAudible("Thickness", p, changed(p, [](box::Parameters& q) { q.thickness = 8.0f; }));
    assertAudible("Stiffness", p, changed(p, [](box::Parameters& q) { q.stiffness = 30.0f; }));
    assertAudible("Density", p, changed(p, [](box::Parameters& q) { q.density = 1000.0f; }));
    assertAudible("Damping low", p, changed(p, [](box::Parameters& q) { q.damping = 0.1f; }));
    assertAudible("Damping high", p, changed(p, [](box::Parameters& q) { q.damping = 6.0f; }));
    assertAudible("Soundhole shut", p, changed(p, [](box::Parameters& q) { q.hole = 0.0f; }));
    assertAudible("Soundhole wide", p, changed(p, [](box::Parameters& q) { q.hole = 220.0f; }));
    assertAudible("Air Volume", p, changed(p, [](box::Parameters& q) { q.volume = 120.0f; }));
    assertAudible("Body Depth", p, changed(p, [](box::Parameters& q) { q.depth = 400.0f; }));
    assertAudible("Drive", p, changed(p, [](box::Parameters& q) { q.drive = 18.0f; }));

    box::Parameters driven = p;
    driven.drive = 24.0f;
    assertAudible("Wood Nonlinearity", driven,
                  changed(driven, [](box::Parameters& q) { q.nonlinear = 100.0f; }));
    assertAudible("Excitation X", p, changed(p, [](box::Parameters& q) { q.excX = 10.0f; }));
    assertAudible("Excitation Y", p, changed(p, [](box::Parameters& q) { q.excY = 90.0f; }));
    assertAudible("Mic X", p, changed(p, [](box::Parameters& q) { q.micX = 10.0f; }));
    assertAudible("Mic Y", p, changed(p, [](box::Parameters& q) { q.micY = 90.0f; }));
    assertAudible("Stereo Width", p, changed(p, [](box::Parameters& q) { q.width = 100.0f; }));
    assertAudible("Mix", p, changed(p, [](box::Parameters& q) { q.mix = 0.0f; }));
    assertAudible("Output", p, changed(p, [](box::Parameters& q) { q.output = 12.0f; }));
    assertAudible("Bypass", p, changed(p, [](box::Parameters& q) { q.bypass = 1.0f; }));
    assertAudible("Randomise", p, box::randomiseParameters(p));
}

void testControllerMap()
{
    using box::ParamId;
    assert(box::controllerTarget(1) == ParamId::size);
    assert(box::controllerTarget(2) == ParamId::damping);
    assert(box::controllerTarget(3) == ParamId::hole);
    assert(box::controllerTarget(4) == ParamId::drive);
    assert(!box::controllerTarget(5).has_value());
    assert(!box::controllerTarget(0).has_value());

    box::Parameters p;
    assert(box::applyController(p, 1, 64));
    assert(std::fabs(p.size - box::controllerToValue(ParamId::size, 64)) < 1.0e-6f);
    assert(box::applyController(p, 2, 0));
    assert(std::fabs(p.damping - 0.02f) < 1.0e-6f);
    assert(box::applyController(p, 3, 127));
    assert(std::fabs(p.hole - 300.0f) < 1.0e-6f);
    assert(box::applyController(p, 4, 127));
    assert(std::fabs(p.drive - 36.0f) < 1.0e-6f);

    const box::Parameters before = p;
    assert(!box::applyController(p, 5, 64));
    assert(p.size == before.size && p.damping == before.damping
           && p.hole == before.hole && p.drive == before.drive);

    // The four mapped controls really move the sound.
    for (std::uint8_t cc = 1; cc <= 4; ++cc) {
        box::Parameters q;
        assert(box::applyController(q, cc, 96));
        assertAudible("CC mapped control", box::Parameters {}, q);
    }
}

void testImpulseDecaysAndStaysFinite()
{
    const Render impulse = renderImpulse(box::Parameters {}, 192000, 256);
    assert(allFinite(impulse));
    assert(peak(impulse) > 0.1f);             // the body is actually excited
    assert(rms(impulse.left, 48000, 96000) < 1.0e-4f);
    assert(rms(impulse.left, 144000, 192000) < 1.0e-6f);

    box::Parameters wild;
    wild.size = 0.05f;
    wild.thickness = 40.0f;
    wild.stiffness = 40.0f;
    wild.density = 100.0f;
    wild.damping = 0.02f;
    wild.hole = 300.0f;
    wild.volume = 2000.0f;
    wild.depth = 500.0f;
    wild.drive = 36.0f;
    wild.nonlinear = 100.0f;
    wild.mix = 100.0f;
    wild.output = 24.0f;
    const Render extreme = renderNoise(wild, 48000, 64);
    assert(allFinite(extreme));
    assert(peak(extreme) < 32.0f);  // loud, but not runaway

    // Finite values outside the range clamp to the endpoint.
    wild.size = 99.0f;
    wild.damping = 99.0f;
    const box::Parameters limited = box::clampParameters(wild);
    assert(limited.size == 4.0f && limited.damping == 8.0f);

    // Non-finite automation falls back to a usable published default.
    wild.size = std::nanf("");
    wild.damping = std::numeric_limits<float>::infinity();
    const box::Parameters clamped = box::clampParameters(wild);
    assert(std::isfinite(clamped.size)
           && clamped.size == box::kParameterSpecs[box::index(box::ParamId::size)].defaultValue);
    assert(std::isfinite(clamped.damping)
           && clamped.damping == box::kParameterSpecs[box::index(box::ParamId::damping)].defaultValue);
}

void testBlockSizeInvariance()
{
    const Render a = renderNoise(box::Parameters {}, 48000, 64);
    const Render b = renderNoise(box::Parameters {}, 48000, 512);
    assert(difference(a, b) < 1.0e-5f);
}

void testStereoAndActivation()
{
    box::Parameters p;
    p.width = 100.0f;
    const Render wide = renderNoise(p, 24000, 128);
    assert(allFinite(wide));
    float sideEnergy = 0.0f;
    for (std::size_t i = 0; i < wide.left.size(); ++i) {
        const float d = wide.left[i] - wide.right[i];
        sideEnergy += d * d;
    }
    sideEnergy = std::sqrt(sideEnergy / static_cast<float>(wide.left.size()));
    assert(sideEnergy > 1.0e-4f);  // width produces a real stereo difference

    box::EngineState state {};
    box::Parameters q;
    q.size = 2.5f;
    q.damping = 0.2f;
    const Render first = renderNoise(q, 12000, 128);
    (void)first;
    box::activate(state);
    assert(q.size == 2.5f && q.damping == 0.2f);  // activate() cannot touch the patch
    assert(!state.initialised);
    assert(state.modes[0][0].haveCoeffs == false);
    assert(state.air[0].haveCoeffs == false);
}

}  // namespace

int main()
{
    testDefaultAndPhysicalLaws();
    testAirResonance();
    testEveryControlAudible();
    testControllerMap();
    testImpulseDecaysAndStaysFinite();
    testBlockSizeInvariance();
    testStereoAndActivation();
    std::puts("box core tests passed");
    return 0;
}
