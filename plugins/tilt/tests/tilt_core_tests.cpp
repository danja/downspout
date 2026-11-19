#include "downspout/test_assert.h"

#include "tilt_engine.hpp"
#include "tilt_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::tilt;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 48000.0;
constexpr std::size_t kSettle = 8192;
constexpr std::size_t kWindow = 24000;
int g_failures = 0, g_checks = 0;

void check(const bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void near(const double got, const double want, const double tol, const std::string& what)
{
    char buf[192];
    std::snprintf(buf, sizeof(buf), " (got %.5f, want %.5f +-%.5f)", got, want, tol);
    check(std::fabs(got - want) <= tol, what + buf);
}

struct Stereo {
    std::vector<float> l, r;
};

struct Rig {
    Engine engine;
    Parameters p;

    Rig() { activate(engine, kSr); }

    Stereo run(const std::vector<float>& inL, const std::vector<float>& inR, const std::size_t block = 256)
    {
        Stereo out;
        out.l.assign(inL.size(), 0.0f);
        out.r.assign(inR.size(), 0.0f);
        for (std::size_t at = 0; at < inL.size();) {
            const std::size_t n = std::min(block, inL.size() - at);
            const float* inputs[2] = {inL.data() + at, inR.data() + at};
            float* outputs[2] = {out.l.data() + at, out.r.data() + at};
            processBlock(engine, p, static_cast<std::uint32_t>(n), inputs, outputs);
            at += n;
        }
        return out;
    }
};

std::vector<float> tone(const double hz, const double amplitude = 0.4, const double phase = 0.0,
                        const std::size_t frames = kSettle + kWindow)
{
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kSr + phase));
    return out;
}

// Goertzel magnitude over an integer number of cycles at one of the test frequencies.
double magnitude(const std::vector<float>& x, const double hz)
{
    const double coeff = 2.0 * std::cos(2.0 * kPi * hz / kSr);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = kSettle; i < kSettle + kWindow; ++i) {
        const double s = static_cast<double>(x[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) / static_cast<double>(kWindow);
}

double measuredDb(const Rig& rig, const double hz, const std::size_t block = 256)
{
    const std::vector<float> inL = tone(hz), inR = tone(hz, 0.4, 0.4);
    const Stereo out = const_cast<Rig&>(rig).run(inL, inR, block);
    const double a = magnitude(inL, hz), b = magnitude(out.l, hz);
    return 20.0 * std::log10(std::max(b, 1e-12) / std::max(a, 1e-12));
}

void testParametersAndStraightLine()
{
    const Parameters p;
    near(p.pivot, 1000.0, 1e-9, "default pivot");
    near(p.angle, 0.0, 1e-9, "default angle");
    near(specs()[kId(ParamId::slope)].defaultValue, 0.0, 1e-9, "default 6 dB/oct");
    near(idealDb(p, 20.0), 0.0, 1e-9, "flat at angle 0");

    Parameters tilt;
    tilt.angle = 0.5f;
    near(idealDb(tilt, 2000.0), 3.0, 1e-9, "6 dB/oct, half angle: +3 dB an octave up");
    near(idealDb(tilt, 500.0), -3.0, 1e-9, "6 dB/oct, half angle: -3 dB an octave down");
    tilt.slope = 1.0f;
    near(idealDb(tilt, 2000.0), 6.0, 1e-9, "12 dB/oct setting");
    tilt.slope = 2.0f;
    tilt.angle = -1.0f;
    near(idealDb(tilt, 2000.0), -24.0, 1e-9, "24 dB/oct setting clamps at -24 dB");
    near(idealDb(tilt, 20.0), 24.0, 1e-9, "cut clamps at +24 dB at the low end");

    Parameters clamped = tilt;
    clamped.angle = 9.0f;
    clamped.pivot = 1.0f;
    clamped.outputDb = -99.0f;
    const Parameters c = clampParameters(clamped);
    near(c.angle, 1.0, 1e-9, "angle clamp");
    near(c.pivot, kMinPivotHz, 1e-9, "pivot clamp");
    near(c.outputDb, kMinOutputDb, 1e-9, "output clamp");
}

void testDefaultIsExactPassThrough()
{
    Rig rig;
    const std::vector<float> inL = tone(1000.0), inR = tone(1000.0, 0.3, 1.0);
    const Stereo out = rig.run(inL, inR);
    for (std::size_t i = 0; i < inL.size(); ++i) {
        near(out.l[i], inL[i], 1e-6, "default left is transparent");
        near(out.r[i], inR[i], 1e-6, "default right is transparent");
    }
}

void testIIRMatchesStraightLine()
{
    Rig rig;
    rig.p.angle = 0.5f;
    static constexpr double kFreqs[] = {100.0, 250.0, 1000.0, 4000.0, 10000.0};
    for (const double hz : kFreqs) {
        const double got = measuredDb(rig, hz);
        const double want = idealDb(rig.p, hz);
        near(got, want, 1.6, "IIR magnitude follows the drawn line at " + std::to_string(static_cast<int>(hz)) + " Hz");
        near(responseDb(rig.engine, rig.p, hz), want, 1.6, "IIR response helper matches the audio");
    }
    // The pivot itself is normalised exactly.
    near(measuredDb(rig, 1000.0), 0.0, 0.3, "IIR crosses unity at the pivot");
}

void testSlopeControlIsAudible()
{
    static constexpr double kFreqs[] = {250.0, 4000.0};
    double prevLow = 0.0, prevHigh = 0.0;
    for (int s = 0; s < 3; ++s) {
        Rig rig;
        rig.p.angle = 0.5f;
        rig.p.slope = static_cast<float>(s);
        const double low = measuredDb(rig, 250.0);
        const double high = measuredDb(rig, 4000.0);
        near(low, idealDb(rig.p, 250.0), 1.6, "slope setting reaches low frequencies");
        near(high, idealDb(rig.p, 4000.0), 1.6, "slope setting reaches high frequencies");
        if (s > 0) {
            check(low < prevLow - 0.5, "a steeper slope cuts more below the pivot");
            check(high > prevHigh + 0.5, "a steeper slope boosts more above the pivot");
        }
        prevLow = low;
        prevHigh = high;
    }
}

void testPivotAndAngleAreAudible()
{
    Rig base;
    base.p.angle = 0.5f;
    const double baseHigh = measuredDb(base, 4000.0);
    base.p.pivot = 4000.0f;
    near(measuredDb(base, 4000.0), 0.0, 0.3, "pivot moves the unity point");
    check(baseHigh > 1.0, "moving the pivot changes the high-frequency gain");

    Rig cut;
    cut.p.angle = -0.5f;
    check(measuredDb(cut, 4000.0) < -2.0, "negative angle leans the other way");
}

void testFFTUnityRoundTripAndLatency()
{
    Rig rig;
    rig.p.mode = 1.0f;
    near(latencySamples(rig.engine, rig.p), kFftSize, 1e-9, "FFT mode reports its latency");
    const std::vector<float> inL = tone(1000.0), inR = tone(1000.0, 0.3, 0.7);
    const Stereo out = rig.run(inL, inR);
    const std::size_t lag = static_cast<std::size_t>(kFftSize);
    double signal = 0.0, error = 0.0, energy = 0.0;
    for (std::size_t i = lag + kFftHop; i + 1 < inL.size(); ++i) {
        const double e = static_cast<double>(out.l[i]) - static_cast<double>(inL[i - lag]);
        error += e * e;
        signal += static_cast<double>(inL[i - lag]) * static_cast<double>(out.l[i]);
        energy += static_cast<double>(inL[i - lag]) * static_cast<double>(inL[i - lag]);
    }
    check(std::sqrt(error / energy) < 0.02, "unity FFT round trip differs only by the reported delay");
    near(signal / energy, 1.0, 0.02, "unity round-trip correlation");
}

void testFFTFollowsIdealLine()
{
    Rig rig;
    rig.p.mode = 1.0f;
    rig.p.angle = 0.5f;
    static constexpr double kFreqs[] = {250.0, 1000.0, 4000.0};
    for (const double hz : kFreqs)
        near(measuredDb(rig, hz), idealDb(rig.p, hz), 0.8, "FFT magnitude is the exact straight line");
}

void testLinkedStereo()
{
    Rig rig;
    rig.p.mode = 1.0f;
    rig.p.angle = 0.5f;
    const std::vector<float> inL = tone(4000.0), inR = tone(250.0, 0.25);
    const Stereo out = rig.run(inL, inR);
    const double gl = 20.0 * std::log10(magnitude(out.l, 4000.0) / magnitude(inL, 4000.0));
    const double gr = 20.0 * std::log10(magnitude(out.r, 250.0) / magnitude(inR, 250.0));
    near(gl, idealDb(rig.p, 4000.0), 0.8, "left channel gets the shared tilt");
    near(gr, idealDb(rig.p, 250.0), 0.8, "right channel gets the shared tilt at its own frequency");
}

void testBypassAndOutputGain()
{
    Rig rig;
    rig.p.mode = 1.0f;
    rig.p.angle = -0.8f;
    rig.p.slope = 2.0f;
    rig.p.bypass = 1.0f;
    rig.p.outputDb = 0.0f;
    const std::vector<float> inL = tone(1000.0), inR = tone(1000.0, 0.2, 1.0);
    const Stereo bypassed = rig.run(inL, inR, 512);
    for (std::size_t i = kSettle; i < inL.size(); ++i) {
        near(bypassed.l[i], inL[i], 2e-4, "bypass passes the left input unchanged");
        near(bypassed.r[i], inR[i], 2e-4, "bypass passes the right input unchanged");
    }

    Rig gainRig;
    gainRig.p.outputDb = 6.0f;
    near(measuredDb(gainRig, 1000.0), 6.0, 0.1, "output gain is applied after the tilt");
}

void testActivationKeepsThePatch()
{
    Rig rig;
    rig.p.angle = 0.75f;
    const double before = measuredDb(rig, 4000.0);
    reset(rig.engine);
    const double after = measuredDb(rig, 4000.0);
    near(after, before, 0.2, "activation clears state but keeps the tilt");
}

void testBlockSizeInvariance()
{
    const double a = [] {
        Rig rig;
        rig.p.angle = 0.6f;
        return measuredDb(rig, 2000.0, 64);
    }();
    const double b = [] {
        Rig rig;
        rig.p.angle = 0.6f;
        return measuredDb(rig, 2000.0, 512);
    }();
    near(a, b, 0.2, "the response does not depend on the host block size");
    near(a, idealDb(Rig{}.p, 2000.0) + 3.6, 1.6, "the 64-sample block reached the expected gain");
}

void testSpectrumBands()
{
    near(spectrumBandEdgeHz(0), 20.0, 1e-9, "spectrum starts at 20 Hz");
    near(spectrumBandEdgeHz(kSpectrumBins), 20000.0, 1e-6, "spectrum ends at 20 kHz");
    for (int k = 1; k < static_cast<int>(kSpectrumBins); ++k)
        check(spectrumBandEdgeHz(k) > spectrumBandEdgeHz(k - 1), "spectrum bands are ordered");
}

}  // namespace

int main()
{
    testParametersAndStraightLine();
    testDefaultIsExactPassThrough();
    testIIRMatchesStraightLine();
    testSlopeControlIsAudible();
    testPivotAndAngleAreAudible();
    testFFTUnityRoundTripAndLatency();
    testFFTFollowsIdealLine();
    testLinkedStereo();
    testBypassAndOutputGain();
    testActivationKeepsThePatch();
    testBlockSizeInvariance();
    testSpectrumBands();
    std::printf("tilt core: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
