#include "downspout/test_assert.h"

#include "splay_core_types.hpp"
#include "splay_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using namespace downspout::splay;

constexpr double kSr = 44100.0;

struct Render {
    EngineState state {};
    std::vector<float> left {};
    std::vector<float> right {};
    std::size_t faults = 0;
};

Render render(const Parameters& params, const std::vector<float>& inL, const std::vector<float>& inR,
              const std::size_t block, const double sampleRate = kSr, EngineState initial = {})
{
    Render result;
    result.state = initial;
    prepare(result.state, sampleRate);
    result.left.assign(inL.size(), 0.0f);
    result.right.assign(inR.size(), 0.0f);

    for (std::size_t offset = 0; offset < inL.size();) {
        const std::size_t count = std::min(block, inL.size() - offset);
        const float* inputs[2] = {inL.data() + offset, inR.data() + offset};
        float* outputs[2] = {result.left.data() + offset, result.right.data() + offset};
        processBlock(result.state, params, static_cast<std::uint32_t>(count), inputs, outputs);
        offset += count;
    }
    result.faults = faultCount(result.state);
    return result;
}

std::vector<float> tone(const std::size_t frames, const float amplitude = 0.4f)
{
    std::vector<float> signal(frames);
    for (std::size_t i = 0; i < frames; ++i)
        signal[i] = amplitude * std::sin(2.0 * 3.14159265358979323846 * 220.0 * static_cast<double>(i) / kSr);
    return signal;
}

void near(const float actual, const float expected, const float tolerance = 1.0e-5f)
{
    assert(std::fabs(actual - expected) <= tolerance);
}

void testDefaultIsExactPassThrough()
{
    const std::vector<float> inL = tone(2048, 0.5f);
    std::vector<float> inR = tone(2048, 0.3f);
    for (std::size_t i = 0; i < inR.size(); ++i)
        inR[i] *= -1.0f;

    const Render r = render(Parameters {}, inL, inR, 128);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(r.left[i], inL[i]);
        near(r.right[i], inR[i]);
    }
    assert(r.faults == 0);
}

void testMatrixRoutesEveryInput()
{
    const std::vector<float> inL(256, 1.0f);
    const std::vector<float> inR(256, 2.0f);

    Parameters swap {};
    swap.matrixLL = 0.0f;
    swap.matrixRR = 0.0f;
    swap.matrixLR = 1.0f;
    swap.matrixRL = 1.0f;
    const Render swapped = render(swap, inL, inR, 64);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(swapped.left[i], 2.0f);
        near(swapped.right[i], 1.0f);
    }

    Parameters leftOnly {};
    leftOnly.matrixLL = 0.5f;
    leftOnly.matrixRR = 0.0f;
    const Render left = render(leftOnly, inL, inR, 37);
    // With width at unity the mid/side stage is transparent: L contributes
    // to L only, and the silent R row stays silent.
    near(left.left[200], 0.5f);
    near(left.right[200], 0.0f);

    Parameters rightOnly {};
    rightOnly.matrixLL = 0.0f;
    rightOnly.matrixRL = 0.5f;
    rightOnly.matrixRR = 0.0f;
    const Render right = render(rightOnly, inL, inR, 11);
    near(right.left[200], 1.0f);
    near(right.right[200], 0.0f);
}

void testWidthIsMidSideGain()
{
    const std::vector<float> mono(256, 1.0f);
    const std::vector<float> silent(256, 0.0f);
    const std::vector<float> leftOnly(256, 1.0f);

    for (const float width : {0.0f, 0.5f, 1.0f, 2.0f}) {
        Parameters p {};
        p.width = width;
        const Render m = render(p, mono, mono, 64);
        near(m.left[200], 1.0f);
        near(m.right[200], 1.0f);

        const Render s = render(p, leftOnly, silent, 64);
        near(s.left[200], 0.5f + 0.5f * width);
        near(s.right[200], 0.5f - 0.5f * width);
    }
}

void testImageDelayIsSampleAccurate()
{
    std::vector<float> inL(1024, 0.0f);
    std::vector<float> inR(1024, 0.0f);
    inR[100] = 1.0f;

    Parameters p {};
    p.imageDelay = 10.0f;  // 441 samples at 44.1 kHz
    const Render r = render(p, inL, inR, 128);
    near(r.right[541], 1.0f);
    near(r.right[100], 0.0f);
    near(r.right[542], 0.0f);
    for (const float value : r.left)
        near(value, 0.0f);
}

void testFractionalDelayInterpolates()
{
    std::vector<float> inL(64, 0.0f);
    std::vector<float> inR(64, 0.0f);
    inR[10] = 1.0f;
    inR[11] = 0.0f;

    Parameters p {};
    // Half a sample at 44.1 kHz: the impulse should be split between neighbours.
    p.imageDelay = static_cast<float>(0.5 * 1000.0 / kSr);
    const Render r = render(p, inL, inR, 64);
    near(r.right[10], 0.5f);
    near(r.right[11], 0.5f);
}

void testInvertAndOutputLevel()
{
    const std::vector<float> inL = tone(512, 0.5f);
    const std::vector<float> inR = tone(512, 0.25f);

    Parameters invert {};
    invert.invertR = 1.0f;
    const Render flipped = render(invert, inL, inR, 64);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(flipped.left[i], inL[i]);
        near(flipped.right[i], -inR[i]);
    }

    Parameters quiet {};
    quiet.level = 0.25f;
    const Render quieter = render(quiet, inL, inR, 64);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(quieter.left[i], 0.25f * inL[i]);
        near(quieter.right[i], 0.25f * inR[i]);
    }
}

void testBypassIsInertAtDefaultAndExactWhenOn()
{
    const std::vector<float> inL = tone(512, 0.6f);
    const std::vector<float> inR = tone(512, 0.4f);

    const Render normal = render(Parameters {}, inL, inR, 64);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(normal.left[i], inL[i], 1.0e-6f);
        near(normal.right[i], inR[i], 1.0e-6f);
    }

    Parameters p {};
    p.bypass = 1.0f;
    p.level = 0.3f;
    p.invertR = 1.0f;
    p.imageDelay = 5.0f;
    const Render bypassed = render(p, inL, inR, 64);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(bypassed.left[i], inL[i], 0.0f);
        near(bypassed.right[i], inR[i], 0.0f);
    }
}

void testBlockSizeAndSampleRateIndependence()
{
    const std::vector<float> inL = tone(1024, 0.5f);
    const std::vector<float> inR = tone(1024, 0.7f);
    Parameters p {};
    p.width = 1.5f;
    p.imageDelay = 3.0f;
    p.level = 0.8f;

    const Render one = render(p, inL, inR, 1);
    const Render seven = render(p, inL, inR, 7);
    const Render many = render(p, inL, inR, 256);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(one.left[i], seven.left[i], 0.0f);
        near(one.left[i], many.left[i], 0.0f);
        near(one.right[i], seven.right[i], 0.0f);
        near(one.right[i], many.right[i], 0.0f);
    }

    std::vector<float> impulse(1024, 0.0f);
    impulse[40] = 1.0f;
    const std::vector<float> silent(1024, 0.0f);
    Parameters delayOnly {};
    delayOnly.imageDelay = 10.0f;
    const Render fast = render(delayOnly, silent, impulse, 64, 44100.0);
    const Render slow = render(delayOnly, silent, impulse, 64, 48000.0);
    near(fast.right[40 + 441], 1.0f);  // 10 ms at 44.1 kHz
    near(slow.right[40 + 480], 1.0f);  // 10 ms at 48 kHz
}

void testNonFiniteInputAndClamping()
{
    std::vector<float> inL(128, 0.2f);
    std::vector<float> inR(128, 0.1f);
    inL[10] = std::numeric_limits<float>::quiet_NaN();
    inR[11] = std::numeric_limits<float>::infinity();
    inR[12] = -std::numeric_limits<float>::infinity();

    const Render r = render(Parameters {}, inL, inR, 32);
    assert(r.faults == 3);
    near(r.left[10], 0.0f);
    near(r.right[11], 0.0f);
    near(r.right[12], 0.0f);

    Parameters wild {};
    wild.matrixLL = -100.0f;
    wild.matrixLR = std::numeric_limits<float>::quiet_NaN();
    wild.width = 99.0f;
    wild.imageDelay = -5.0f;
    wild.invertR = 9.0f;
    wild.level = -1.0f;
    wild.bypass = 2.0f;
    const Parameters clamped = clampParameters(wild);
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        const ParamSpec& spec = kParameterSpecs[i];
        assert(clamped.*kParameterFields[i] >= spec.minimum);
        assert(clamped.*kParameterFields[i] <= spec.maximum);
    }
    const Render safe = render(wild, inL, inR, 32);
    for (const float value : safe.left)
        assert(std::isfinite(value));
    for (const float value : safe.right)
        assert(std::isfinite(value));
}

void testResetClearsRuntimeButKeepsThePatch()
{
    std::vector<float> inL(512, 0.0f);
    std::vector<float> inR(512, 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    Parameters p {};
    p.imageDelay = 10.0f;
    p.matrixLL = 0.0f;
    p.matrixRR = 0.0f;
    p.matrixLR = 1.0f;
    p.matrixRL = 1.0f;

    // The swapped matrix sends R straight to L and L to the delayed R output.
    const Render first = render(p, inL, inR, 128);
    near(first.left[0], 1.0f);
    near(first.right[441], 1.0f);

    EngineState carried = first.state;
    reset(carried);
    // Silence on the second pass: any delayed impulse here would be stale
    // state left over from the first render.
    const std::vector<float> silent(512, 0.0f);
    const Render second = render(p, silent, silent, 128, kSr, carried);
    near(second.left[0], 0.0f);
    near(second.right[441], 0.0f);
    assert(faultCount(second.state) == 0);

    // The patch still applies after the reset: input R is still routed to L.
    std::vector<float> steadyL(64, 0.0f);
    std::vector<float> steadyR(64, 2.0f);
    const Render third = render(p, steadyL, steadyR, 64, kSr, second.state);
    near(third.left[60], 2.0f);
}

void testTinyValuesAreFlushed()
{
    const std::vector<float> inL(64, 1.0e-40f);
    const std::vector<float> inR(64, -1.0e-40f);
    const Render r = render(Parameters {}, inL, inR, 32);
    for (const float value : r.left)
        near(value, 0.0f, 0.0f);
    for (const float value : r.right)
        near(value, 0.0f, 0.0f);
}

float meanFrom(const std::vector<float>& v, const std::size_t from)
{
    double sum = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        sum += v[i];
    return static_cast<float>(sum / static_cast<double>(v.size() - from));
}

// Amplitude of an alternating (+/-) component, which is the high band in the
// two-tone test below. Multiplying by the same sign recovers the amplitude.
float alternatingFrom(const std::vector<float>& v, const std::size_t from)
{
    double sum = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        sum += v[i] * ((i % 2 == 0) ? 1.0 : -1.0);
    return static_cast<float>(sum / static_cast<double>(v.size() - from));
}

void testRotationAndPanLaw()
{
    const std::vector<float> oneTwo(256, 1.0f);
    std::vector<float> two(256, 2.0f);

    Parameters plus90 {};
    plus90.rotation = 90.0f;
    const Render forward = render(plus90, oneTwo, two, 64);
    near(forward.left[200], -2.0f);
    near(forward.right[200], 1.0f);

    Parameters minus90 {};
    minus90.rotation = -90.0f;
    const Render back = render(minus90, oneTwo, two, 64);
    near(back.left[200], 2.0f);
    near(back.right[200], -1.0f);

    const std::vector<float> leftOnly(256, 1.0f);
    const std::vector<float> silent(256, 0.0f);

    Parameters centre {};
    centre.matrixLR = 1.0f;
    centre.panLaw = 1.0f;
    const Render centred = render(centre, leftOnly, silent, 64);
    const float kRootHalf = 0.70710678f;
    near(centred.left[200], kRootHalf);
    near(centred.right[200], kRootHalf);

    Parameters bothToLeft {};
    bothToLeft.matrixRR = 0.0f;
    bothToLeft.matrixRL = 1.0f;
    bothToLeft.panLaw = 1.0f;
    const Render folded = render(bothToLeft, leftOnly, leftOnly, 64);
    near(folded.left[200], 2.0f * kRootHalf);
    near(folded.right[200], 0.0f);

    // A single boosted path is deliberate gain, not a duplicate route.
    Parameters boost {};
    boost.matrixLL = 2.0f;
    boost.panLaw = 1.0f;
    const Render loud = render(boost, leftOnly, silent, 64);
    near(loud.left[200], 2.0f);
    near(loud.right[200], 0.0f);
}

void testMidSideTrimsAndSolo()
{
    const std::vector<float> stereo(256, 1.0f);
    const std::vector<float> leftOnly(256, 1.0f);
    const std::vector<float> silent(256, 0.0f);

    Parameters trimmed {};
    trimmed.midGain = 0.5f;
    const Render quiet = render(trimmed, stereo, stereo, 64);
    near(quiet.left[200], 0.5f);
    near(quiet.right[200], 0.5f);

    Parameters soloMid {};
    soloMid.msSolo = 1.0f;
    const Render mid = render(soloMid, leftOnly, silent, 64);
    near(mid.left[200], 0.5f);
    near(mid.right[200], 0.5f);

    Parameters soloSide {};
    soloSide.msSolo = 2.0f;
    const Render side = render(soloSide, leftOnly, silent, 64);
    near(side.left[200], 0.5f);
    near(side.right[200], -0.5f);

    Parameters boostedMid {};
    boostedMid.midGain = 2.0f;
    boostedMid.msSolo = 1.0f;
    const Render loud = render(boostedMid, leftOnly, silent, 64);
    near(loud.left[200], 1.0f);
    near(loud.right[200], 1.0f);
}

void testSpectralWidthSeparatesBands()
{
    std::vector<float> inL(1024);
    std::vector<float> inR(1024);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        const float alternating = (i % 2 == 0) ? 1.0f : -1.0f;
        inL[i] = 0.5f + 0.5f * alternating;  // DC mid + alternating side
        inR[i] = 0.5f - 0.5f * alternating;
    }

    const Render plain = render(Parameters {}, inL, inR, 128);
    Parameters wide {};
    wide.spectralWidth = 1.0f;
    wide.crossover = 100.0f;
    wide.lowWidth = 0.0f;
    wide.highWidth = 2.0f;
    const Render shaped = render(wide, inL, inR, 128);

    constexpr std::size_t kFrom = 768;
    near(meanFrom(plain.left, kFrom), 0.5f, 1.0e-3f);
    near(meanFrom(shaped.left, kFrom), 0.5f, 1.0e-3f);
    near(alternatingFrom(plain.left, kFrom), 0.5f, 0.02f);
    near(alternatingFrom(plain.right, kFrom), -0.5f, 0.02f);
    assert(alternatingFrom(shaped.left, kFrom) > 1.7f * alternatingFrom(plain.left, kFrom));
    assert(alternatingFrom(shaped.right, kFrom) < 1.7f * alternatingFrom(plain.right, kFrom));
}

void testMetersTrackCorrelationAndMono()
{
    const std::vector<float> signal(8192, 0.5f);
    std::vector<float> inverted = signal;
    for (float& v : inverted)
        v = -v;

    const Render same = render(Parameters {}, signal, signal, 128);
    near(correlation(same.state), 1.0f, 0.02f);
    near(monoCompatibility(same.state), 1.0f, 0.02f);

    const Render anti = render(Parameters {}, signal, inverted, 128);
    near(correlation(anti.state), -1.0f, 0.02f);
    near(monoCompatibility(anti.state), 0.0f, 0.02f);

    // With Bypass on, the meters describe the untouched input, not the patch.
    Parameters bypassed {};
    bypassed.invertR = 1.0f;
    bypassed.bypass = 1.0f;
    const Render passthrough = render(bypassed, signal, signal, 128);
    near(correlation(passthrough.state), 1.0f, 0.02f);
    assert(faultCount(same.state) == 0);
}

void testResetClearsMetersAndCrossoverMemory()
{
    // Long enough for the 20 ms meters to settle before reset.
    const std::vector<float> signal(8192, 0.5f);
    Parameters p {};
    p.spectralWidth = 1.0f;
    p.lowWidth = 2.0f;
    const Render first = render(p, signal, signal, 64);
    assert(correlation(first.state) > 0.5f);
    assert(monoCompatibility(first.state) > 0.5f);

    EngineState carried = first.state;
    reset(carried);
    near(correlation(carried), 0.0f, 0.0f);
    near(monoCompatibility(carried), 0.0f, 0.0f);

    const std::vector<float> silent(8192, 0.0f);
    const Render second = render(p, silent, silent, 64, kSr, carried);
    near(correlation(second.state), 0.0f, 0.02f);
    near(monoCompatibility(second.state), 0.0f, 0.02f);
}

void testEveryControlHasAnAudibleEffect()
{
    const std::vector<float> inL = tone(512, 0.7f);
    const std::vector<float> inR = tone(512, 0.3f);

    Parameters base {};
    base.matrixLL = 1.0f;
    base.matrixLR = 0.6f;
    base.matrixRL = 0.0f;
    base.matrixRR = 0.5f;
    base.rotation = 10.0f;
    base.panLaw = 0.0f;
    base.midGain = 1.0f;
    base.width = 1.2f;
    base.msSolo = 0.0f;
    base.spectralWidth = 1.0f;
    base.crossover = 500.0f;
    base.lowWidth = 0.6f;
    base.highWidth = 1.4f;
    base.imageDelay = 0.0f;
    base.invertR = 0.0f;
    base.level = 0.8f;
    base.bypass = 0.0f;
    const Render reference = render(base, inL, inR, 128);

    for (std::size_t id = 0; id < kInputParameterCount; ++id) {
        Parameters changed = base;
        const float current = changed.*kParameterFields[id];
        float next = current;
        switch (static_cast<ParamId>(id)) {
        case ParamId::matrixLL: next = 0.2f; break;
        case ParamId::matrixLR: next = 0.2f; break;
        case ParamId::matrixRL: next = 0.3f; break;
        case ParamId::matrixRR: next = 1.0f; break;
        case ParamId::width: next = 0.0f; break;
        case ParamId::imageDelay: next = 4.0f; break;
        case ParamId::invertR: next = 1.0f; break;
        case ParamId::level: next = 1.2f; break;
        case ParamId::bypass: next = 1.0f; break;
        case ParamId::rotation: next = 40.0f; break;
        case ParamId::panLaw: next = 1.0f; break;
        case ParamId::midGain: next = 0.4f; break;
        case ParamId::msSolo: next = 1.0f; break;
        case ParamId::spectralWidth: next = 0.0f; break;
        case ParamId::crossover: next = 2000.0f; break;
        case ParamId::lowWidth: next = 1.2f; break;
        case ParamId::highWidth: next = 0.8f; break;
        case ParamId::outCorrelation:
        case ParamId::outMonoCompat:
        case ParamId::count: break;
        }
        changed.*kParameterFields[id] = next;
        assert(changed.*kParameterFields[id] != current);

        const Render r = render(changed, inL, inR, 128);
        float biggest = 0.0f;
        for (std::size_t i = 0; i < inL.size(); ++i) {
            biggest = std::max(biggest, std::fabs(r.left[i] - reference.left[i]));
            biggest = std::max(biggest, std::fabs(r.right[i] - reference.right[i]));
        }
        assert(biggest > 1.0e-3f);
    }
}

}  // namespace

int main()
{
    testDefaultIsExactPassThrough();
    testMatrixRoutesEveryInput();
    testWidthIsMidSideGain();
    testImageDelayIsSampleAccurate();
    testFractionalDelayInterpolates();
    testInvertAndOutputLevel();
    testBypassIsInertAtDefaultAndExactWhenOn();
    testBlockSizeAndSampleRateIndependence();
    testNonFiniteInputAndClamping();
    testResetClearsRuntimeButKeepsThePatch();
    testTinyValuesAreFlushed();
    testRotationAndPanLaw();
    testMidSideTrimsAndSolo();
    testSpectralWidthSeparatesBands();
    testMetersTrackCorrelationAndMono();
    testResetClearsMetersAndCrossoverMemory();
    testEveryControlHasAnAudibleEffect();
    return 0;
}
