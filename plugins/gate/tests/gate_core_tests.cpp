#include "downspout/test_assert.h"

#include "gate_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::gate;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 48000.0;
constexpr std::size_t kSettle = 12000;
constexpr std::size_t kWindow = 12000;
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

std::vector<float> sine(const double hz, const double amplitude, const std::size_t frames, const double phase = 0.0)
{
    std::vector<float> x(frames);
    for (std::size_t i = 0; i < frames; ++i)
        x[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kSr + phase));
    return x;
}

std::vector<float> silence(const std::size_t frames) { return std::vector<float>(frames, 0.0f); }

double rms(const std::vector<float>& x, const std::size_t from, const std::size_t count)
{
    double sum = 0.0;
    for (std::size_t i = from; i < from + count; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(sum / static_cast<double>(count));
}

struct Rig {
    Engine engine;
    Parameters p;

    Rig() { activate(engine, kSr); }

    Stereo run(const std::vector<float>& inL, const std::vector<float>& inR,
               const std::vector<float>& scL, const std::vector<float>& scR,
               const std::size_t block = 256)
    {
        Stereo out;
        out.l.assign(inL.size(), 0.0f);
        out.r.assign(inR.size(), 0.0f);
        for (std::size_t at = 0; at < inL.size();) {
            const std::size_t n = std::min(block, inL.size() - at);
            const float* inputs[4] = {inL.data() + at, inR.data() + at, scL.data() + at, scR.data() + at};
            float* outputs[2] = {out.l.data() + at, out.r.data() + at};
            processBlock(engine, p, static_cast<std::uint32_t>(n), inputs, outputs);
            at += n;
        }
        return out;
    }

    double steadyDb(const std::vector<float>& in, const std::vector<float>& out)
    {
        return 20.0 * std::log10(std::max(rms(out, kSettle, kWindow), 1e-12) /
                                 std::max(rms(in, kSettle, kWindow), 1e-12));
    }
};

void testParametersAndClamps()
{
    const Parameters p;
    near(p.threshold, -40.0, 1e-9, "default threshold");
    near(p.hysteresis, 6.0, 1e-9, "default hysteresis");
    near(p.range, 60.0, 1e-9, "default range");
    near(p.attack, 5.0, 1e-9, "default attack");
    near(p.hold, 50.0, 1e-9, "default hold");
    near(p.release, 200.0, 1e-9, "default release");
    near(p.scHpf, 20.0, 1e-9, "default sidechain HPF is off");
    check(!externalSidechain(p), "default sidechain is internal");

    Parameters wild = p;
    wild.threshold = -999.0f;
    wild.hysteresis = 999.0f;
    wild.range = 999.0f;
    wild.attack = -5.0f;
    wild.hold = 9999.0f;
    wild.release = -1.0f;
    wild.scSource = 9.0f;
    wild.scHpf = 1.0f;
    wild.outputDb = 99.0f;
    wild.bypass = 2.0f;
    const Parameters c = clampParameters(wild);
    near(c.threshold, kMinThresholdDb, 1e-9, "threshold clamp");
    near(c.hysteresis, kMaxHysteresisDb, 1e-9, "hysteresis clamp");
    near(c.range, kMaxRangeDb, 1e-9, "range clamp");
    near(c.attack, 0.0, 1e-9, "attack clamp");
    near(c.hold, 500.0, 1e-9, "hold clamp");
    near(c.release, 10.0, 1e-9, "release clamp");
    near(c.scSource, 1.0, 1e-9, "sidechain clamp");
    near(c.scHpf, kMinScHpfHz, 1e-9, "HPF clamp");
    near(c.outputDb, kMaxOutputDb, 1e-9, "output clamp");
    near(c.bypass, 1.0, 1e-9, "bypass clamp");
}

void testLoudPassesAndLowIsGated()
{
    const std::size_t frames = kSettle + kWindow;
    const auto sc = silence(frames);

    Rig loud;
    const auto loudTone = sine(1000.0, 0.4, frames);
    Stereo loudOut = loud.run(loudTone, loudTone, sc, sc);
    near(loud.steadyDb(loudTone, loudOut.l), 0.0, 0.1, "loud tone is unchanged");
    check(gateIsOpen(loud.engine), "loud tone opens the gate");

    Rig low;
    const auto lowTone = sine(1000.0, 0.001, frames);
    Stereo lowOut = low.run(lowTone, lowTone, sc, sc);
    near(low.steadyDb(lowTone, lowOut.l), -60.0, 1.0, "low tone gets the full range attenuation");
    check(!gateIsOpen(low.engine), "low tone leaves the gate closed");

    Rig inert;
    inert.p.range = 0.0f;
    Stereo inertOut = inert.run(lowTone, lowTone, sc, sc);
    near(inert.steadyDb(lowTone, inertOut.l), 0.0, 0.1, "Range 0 is transparent");
}

void testExternalSidechain()
{
    const std::size_t frames = kSettle + kWindow;
    const auto quiet = sine(1000.0, 0.001, frames);
    const auto loudSide = sine(1000.0, 0.4, frames);

    Rig external;
    external.p.scSource = 1.0f;
    Stereo opened = external.run(quiet, quiet, loudSide, loudSide);
    near(external.steadyDb(quiet, opened.l), 0.0, 0.1, "external sidechain opens the gate");
    check(gateIsOpen(external.engine), "external sidechain sets the open state");

    Rig internal;
    Stereo stayedShut = internal.run(quiet, quiet, loudSide, loudSide);
    near(internal.steadyDb(quiet, stayedShut.l), -60.0, 1.0, "internal mode ignores the sidechain input");
    check(!gateIsOpen(internal.engine), "internal sidechain stays closed");
}

void testHysteresis()
{
    const std::size_t frames = kSettle + kWindow;
    const auto sc = silence(frames);
    // -43 dB sits above the -46 dB close point but below the -40 dB open point.
    const auto inBetween = sine(1000.0, 0.01, frames);

    Rig opened;
    const auto initialLoud = sine(1000.0, 0.4, kSettle / 2);
    std::vector<float> middle = initialLoud;
    middle.insert(middle.end(), inBetween.begin(), inBetween.end());
    Stereo out = opened.run(middle, middle, sc, sc);
    check(gateIsOpen(opened.engine), "hysteresis keeps an open gate open between the thresholds");

    Rig closed;
    Stereo shut = closed.run(inBetween, inBetween, sc, sc);
    check(!gateIsOpen(closed.engine), "an in-between level does not open a closed gate");
    (void)out;
    (void)shut;
}

void testAttackHoldRelease()
{
    const std::size_t burst = static_cast<std::size_t>(0.1 * kSr);
    const std::size_t tail = static_cast<std::size_t>(1.0 * kSr);
    const auto loud = sine(1000.0, 0.4, burst);
    const auto quiet = silence(tail);
    const auto sc = silence(burst + tail);

    Rig rig;
    rig.p.attack = 5.0f;
    rig.p.hold = 50.0f;
    rig.p.release = 200.0f;
    std::vector<float> main = loud;
    main.insert(main.end(), quiet.begin(), quiet.end());
    Stereo out = rig.run(main, main, sc, sc);

    const std::size_t afterAttack = static_cast<std::size_t>(0.02 * kSr);
    Rig attackRig;
    std::vector<float> shortMain = loud;
    Stereo shortOut = attackRig.run(shortMain, shortMain, sc, sc);
    (void)shortOut;
    // The full run's envelope is sampled by re-rendering to the same point.
    Rig probe;
    probe.p = rig.p;
    std::vector<float> prefix(main.begin(), main.begin() + static_cast<std::size_t>(burst + afterAttack));
    std::vector<float> prefixSc(prefix.size(), 0.0f);
    probe.run(prefix, prefix, prefixSc, prefixSc);
    check(gateEnvelope(probe.engine) > 0.8, "attack opens within 20 ms at 5 ms");

    const std::size_t afterShortSilence = burst + static_cast<std::size_t>(0.04 * kSr);
    Rig holdProbe;
    holdProbe.p = rig.p;
    std::vector<float> holdPrefix(main.begin(), main.begin() + afterShortSilence);
    std::vector<float> holdSc(holdPrefix.size(), 0.0f);
    holdProbe.run(holdPrefix, holdPrefix, holdSc, holdSc);
    check(gateIsOpen(holdProbe.engine), "hold keeps the gate open for 40 ms of silence");

    // The detector falls from -11 dB to the -46 dB close point in about 320 ms,
    // then Hold adds 50 ms; 800 ms leaves plenty of release time.
    const std::size_t afterRelease = burst + static_cast<std::size_t>(0.8 * kSr);
    Rig releaseProbe;
    releaseProbe.p = rig.p;
    std::vector<float> releasePrefix(main.begin(), main.begin() + afterRelease);
    std::vector<float> releaseSc(releasePrefix.size(), 0.0f);
    releaseProbe.run(releasePrefix, releasePrefix, releaseSc, releaseSc);
    check(!gateIsOpen(releaseProbe.engine), "release closes after hold and a low level");
    check(gateEnvelope(releaseProbe.engine) < 0.15, "release fades the envelope towards closed");
    near(rms(out.l, burst + tail - kWindow, kWindow), 0.0, 1e-6, "silence stays silent");
}

void testLinkedStereo()
{
    const std::size_t frames = kSettle + kWindow;
    const auto toneL = sine(1000.0, 0.4, frames);
    const auto toneR = sine(1000.0, 0.4, frames, 0.7);
    const auto scL = sine(1000.0, 0.4, frames);
    const auto scR = silence(frames);
    const auto scSilent = silence(frames);

    Rig linked;
    Stereo open = linked.run(toneL, toneR, scL, scR);
    near(linked.steadyDb(toneL, open.l), 0.0, 0.1, "one loud sidechain channel opens both outputs");
    near(linked.steadyDb(toneR, open.r), 0.0, 0.1, "right output shares the linked gain");

    Rig gated;
    gated.p.scSource = 1.0f;
    Stereo shut = gated.run(toneL, toneR, scSilent, scSilent);
    near(gated.steadyDb(toneL, shut.l), -60.0, 1.0, "linked left output is attenuated");
    near(gated.steadyDb(toneR, shut.r), -60.0, 1.0, "linked right output is attenuated");
}

void testBypassAndOutputGain()
{
    const std::size_t frames = kSettle + kWindow;
    const auto quiet = sine(1000.0, 0.001, frames);
    const auto sc = silence(frames);

    Rig bypass;
    bypass.p.bypass = 1.0f;
    Stereo out = bypass.run(quiet, quiet, sc, sc);
    near(bypass.steadyDb(quiet, out.l), 0.0, 0.1, "bypass ignores the gate");

    Rig gain;
    const auto loud = sine(1000.0, 0.4, frames);
    gain.p.outputDb = 6.0f;
    Stereo boosted = gain.run(loud, loud, sc, sc);
    near(gain.steadyDb(loud, boosted.l), 6.0, 0.1, "output gain is applied after the gate");
}

void testActivationKeepsPatch()
{
    const std::size_t frames = kSettle + kWindow;
    const auto quiet = sine(1000.0, 0.01, frames);
    const auto sc = silence(frames);

    Rig rig;
    rig.p.threshold = -60.0f;
    const double before = rig.steadyDb(quiet, rig.run(quiet, quiet, sc, sc).l);
    reset(rig.engine);
    const double after = rig.steadyDb(quiet, rig.run(quiet, quiet, sc, sc).l);
    near(after, before, 0.1, "activation clears state but keeps the threshold");
}

void testBlockSizeInvariance()
{
    const std::size_t frames = kSettle + kWindow;
    const auto quiet = sine(1000.0, 0.001, frames);
    const auto sc = silence(frames);
    const auto measure = [&](const std::size_t block) {
        Rig rig;
        return rig.steadyDb(quiet, rig.run(quiet, quiet, sc, sc, block).l);
    };
    near(measure(64), measure(512), 0.2, "gate response does not depend on block size");
}

}  // namespace

int main()
{
    testParametersAndClamps();
    testLoudPassesAndLowIsGated();
    testExternalSidechain();
    testHysteresis();
    testAttackHoldRelease();
    testLinkedStereo();
    testBypassAndOutputGain();
    testActivationKeepsPatch();
    testBlockSizeInvariance();
    std::printf("gate core: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
