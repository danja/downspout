#include "grist_core.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace downspout::grist;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

using Params = std::array<float, kParameterCount>;
using Signal = std::vector<float>;

Params defaults()
{
    Params p {};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue;
    return p;
}

Signal sine(const double hz, const std::size_t n, const float amp = 0.5f)
{
    Signal s(n);
    for (std::size_t i = 0; i < n; ++i) s[i] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / kSr));
    return s;
}

Signal noise(const std::size_t n, const float amp = 0.5f)
{
    Signal s(n);
    std::uint32_t x = 12345;
    for (auto& v : s) {
        x = x * 1664525u + 1013904223u;
        v = amp * (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
    }
    return s;
}

struct Output {
    Signal left, right;
    int lastGrains = 0;
    float lastLatency = 0.0f;
    float lastWindow = 0.0f;
    float lastDelay = 0.0f;
};

// Left input is `in`; right is the same signal scaled by 0.5 so the channels differ.
Output run(const Params& p, const Signal& in, const std::uint32_t block = 480, const double bpm = 120.0)
{
    State s;
    prepare(s, kSr);
    Output out;
    out.left.assign(in.size(), 0.0f);
    out.right.assign(in.size(), 0.0f);
    Signal right(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) right[i] = 0.5f * in[i];
    for (std::size_t pos = 0; pos < in.size(); pos += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, in.size() - pos));
        process(s, p, n, kSr, bpm, in.data() + pos, right.data() + pos, out.left.data() + pos, out.right.data() + pos);
    }
    out.lastGrains = s.statusGrains;
    out.lastLatency = s.statusLatency;
    out.lastWindow = s.statusWindow;
    out.lastDelay = s.statusDelay;
    return out;
}

double goertzel(const Signal& x, const std::size_t from, const std::size_t to, const double hz)
{
    const double w = 2.0 * kPi * hz / kSr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / static_cast<double>(to - from) * 2.0;
}

double rms(const Signal& x, const std::size_t from, const std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

double distance(const Signal& a, const Signal& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) sum += std::fabs(static_cast<double>(a[i]) - b[i]);
    return sum;
}

// Grains that lie on the input untouched: Hann at 50 % overlap sums to one, no blur or pitch.
Params transparent()
{
    Params p = defaults();
    p[kMix] = 1.0f;
    p[kBlur] = 0.0f;
    p[kSpread] = 0.0f;
    p[kDensity] = 2;
    p[kShape] = 0.0f;
    return p;
}

void testLatencyFormulaPins()
{
    assert(std::fabs(latencyMs(100, 0, 0)) < 1e-9);
    assert(std::fabs(latencyMs(100, 1, 0) - 50.0) < 1e-9);
    assert(std::fabs(latencyMs(100, 2, 0) - 100.0) < 1e-9);
    assert(std::fabs(latencyMs(100, 1, 12) - 150.0) < 1e-9);
    assert(std::fabs(latencyMs(100, 1, -12) - 50.0) < 1e-9);
    assert(std::fabs(latencyMs(50, 1, 7) - 49.92) < 0.01);
}

void testShapeKeepsAverageLevel()
{
    for (float shape = 0.0f; shape <= 1.0f; shape += 0.25f) {
        assert(shapeAt(shape, 0.0f) < 0.01f && shapeAt(shape, 1.0f) < 0.01f);
        double mean = 0.0;
        for (int i = 0; i < 4000; ++i) mean += shapeAt(shape, (static_cast<float>(i) + 0.5f) / 4000.0f);
        mean /= 4000.0;
        assert(std::fabs(mean - 0.5) < 0.01);
    }
    assert(std::fabs(shapeAt(0.0f, 0.5f) - 1.0f) < 1e-3f);  // a bell peaks in the middle
    assert(shapeAt(1.0f, 0.1f) > 1.4f && shapeAt(1.0f, 0.5f) < 0.3f);  // percussive: early peak, quick decay
}

void testDivisionsAndSync()
{
    assert(std::fabs(divisionQuarters(10) - 1.0) < 1e-12);  // 1/4
    assert(std::fabs(divisionQuarters(11) - 1.5) < 1e-12);  // 1/4D
    assert(std::fabs(divisionQuarters(9) - 2.0 / 3.0) < 1e-12);  // 1/4T
    // Ordered by note value; within one value the triplet is shorter and the dotted one longer.
    const int straight[] = {0, 2, 4, 7, 10, 13, 15, 16, 17, 18};
    for (std::size_t i = 1; i < std::size(straight); ++i) assert(divisionQuarters(straight[i]) == 2.0 * divisionQuarters(straight[i - 1]));
    for (const int d : {4, 7, 10, 13}) {
        assert(std::fabs(divisionQuarters(d - 1) - divisionQuarters(d) * 2.0 / 3.0) < 1e-12);
        assert(std::fabs(divisionQuarters(d + 1) - divisionQuarters(d) * 1.5) < 1e-12);
    }
    assert(std::string(divisionName(9)) == "1/4T" && std::string(divisionName(11)) == "1/4D");
    Params p = defaults();
    p[kWindowSync] = 1;
    p[kWindowDivision] = 10;  // a quarter note
    assert(std::fabs(windowMs(p, 120.0) - 500.0) < 1e-6);
    assert(std::fabs(windowMs(p, 60.0) - 1000.0) < 1e-6);
    assert(std::fabs(windowMs(p, 30.0) - 1000.0) < 1e-6);  // clamped to the parameter range
    p[kWindowSync] = 0;
    p[kWindow] = 80;
    assert(std::fabs(windowMs(p, 120.0) - 80.0) < 1e-9);
    p[kDelaySync] = 1;
    p[kDelayDivision] = 7;  // an eighth
    assert(std::fabs(delayMs(p, 120.0) - 250.0) < 1e-6);
}

void testCloudDrawFollowsScaleAndDirection()
{
    Rng rng;
    rng.seed(7);
    for (int scale = 1; scale < kScaleCount; ++scale) {
        for (int direction = 0; direction < 3; ++direction) {
            for (int centre = -5; centre <= 5; ++centre) {
                std::set<int> seen;
                for (int i = 0; i < 400; ++i) {
                    const float o = drawOffset(rng, centre, 12.0f, direction, scale);
                    const int offset = static_cast<int>(o);
                    assert(static_cast<float>(offset) == o);
                    assert(std::fabs(o) <= 12.0f);
                    if (direction == kUp) assert(o >= 0.0f);
                    if (direction == kDown) assert(o <= 0.0f);
                    if (offset != 0) assert(scaleAllows(scale, centre + offset));
                    seen.insert(offset);
                }
                assert(seen.size() >= 2 || scale == 2);  // octaves from a centre may offer only +-12
            }
        }
    }
    // Free: continuous values inside the range; no range: always on PITCH.
    std::set<int> tenths;
    for (int i = 0; i < 500; ++i) {
        const float o = drawOffset(rng, 0, 5.0f, kBoth, 0);
        assert(o >= -5.0f && o <= 5.0f);
        tenths.insert(static_cast<int>(o * 10.0f));
        assert(drawOffset(rng, 3, 0.0f, kBoth, 4) == 0.0f);
    }
    assert(tenths.size() > 30);
    // Octaves from PITCH 0 land only on 0 and +-12.
    for (int i = 0; i < 200; ++i) {
        const float o = drawOffset(rng, 0, 24.0f, kBoth, 2);
        assert(o == 0.0f || o == 12.0f || o == -12.0f || o == 24.0f || o == -24.0f);
    }
}

void testCloudPreviewIsDeterministic()
{
    const auto a = cloudPreview(0.0f, 12.0f, kBoth, 0.5f, 4, 16, 3);
    const auto b = cloudPreview(0.0f, 12.0f, kBoth, 0.5f, 4, 16, 3);
    const auto c = cloudPreview(0.0f, 12.0f, kBoth, 0.5f, 4, 16, 4);
    assert(a.size() == 16 && b.size() == 16);
    bool same = true, differs = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        same = same && a[i].offset == b[i].offset && a[i].size == b[i].size;
        differs = differs || a[i].offset != c[i].offset || a[i].size != c[i].size;
        assert(a[i].size <= 1.0f && a[i].size >= 0.25f);
    }
    assert(same && differs);
    for (const auto& g : cloudPreview(0.0f, 12.0f, kBoth, 0.0f, 4, 16, 3)) assert(g.size == 1.0f);
}

void testDryPassesWithZeroMix()
{
    Params p = defaults();
    p[kMix] = 0.0f;
    const Signal in = noise(20000);
    const Output out = run(p, in);
    for (std::size_t i = 0; i < in.size(); ++i) assert(out.left[i] == in[i] && out.right[i] == 0.5f * in[i]);
}

// With no blur, pitch or randomness two Hann grains per window rebuild the input, two samples late.
void testTransparentGrainsRebuildTheInput()
{
    const Params p = transparent();
    const Signal in = sine(440.0, 48000);
    const Output out = run(p, in);
    for (std::size_t i = 6000; i < in.size(); ++i) {
        assert(std::fabs(out.left[i] - in[i - 2]) < 0.01f);
        assert(std::fabs(out.right[i] - 0.5f * in[i - 2]) < 0.005f);
    }
    // A longer window and a different grid change nothing about that.
    Params q = p;
    q[kWindow] = 200;
    const Output longer = run(q, in);
    for (std::size_t i = 20000; i < in.size(); ++i) assert(std::fabs(longer.left[i] - in[i - 2]) < 0.01f);
}

void testPitchShiftsTheGrains()
{
    // 400 Hz fits a whole number of cycles in the 25 ms between grain starts at ratios 2 and 1/2,
    // so overlapping grains add in phase and the shifted tone is clean.
    const Signal in = sine(400.0, 96000);
    const std::array<std::pair<float, double>, 2> cases {{{12.0f, 800.0}, {-12.0f, 200.0}}};
    for (const auto& [semis, expected] : cases) {
        Params p = transparent();
        p[kPitch] = semis;
        const Output out = run(p, in);
        const double at = goertzel(out.left, 24000, 96000, expected);
        const double original = goertzel(out.left, 24000, 96000, 400.0);
        assert(at > 0.3);
        assert(at > 5.0 * original);
    }
}

// The history is a ring, so a grain that read ahead of the write position would find audio
// from seven seconds earlier. Put a burst at the start, wait for the ring to wrap, and check
// that nothing of it comes back, however hard the grains are pitched or reversed.
void testGrainsReadOnlyThePast()
{
    Signal in(static_cast<std::size_t>(9.0 * kSr), 0.0f);
    const Signal burst = noise(48000, 0.5f);
    std::copy(burst.begin(), burst.end(), in.begin());
    for (const float semis : {24.0f, 12.0f, 0.0f, -24.0f}) {
        for (const float reverse : {0.0f, 1.0f}) {
            Params p = defaults();
            p[kMix] = 1.0f;
            p[kPitch] = semis;
            p[kReverse] = reverse;
            p[kBlur] = 2.0f;
            p[kRange] = 12.0f;
            p[kWindow] = 100;
            const Output out = run(p, in, 4800);
            // Legitimate reads reach back about a second; the burst is gone by then.
            for (std::size_t i = 96000; i < in.size(); ++i) assert(out.left[i] == 0.0f && out.right[i] == 0.0f);
        }
    }
}

void testReverseReadsBackwards()
{
    Signal ramp(40000);
    for (std::size_t i = 0; i < ramp.size(); ++i) ramp[i] = 1e-5f * static_cast<float>(i);
    Params p = transparent();
    p[kDensity] = 1;
    p[kWindow] = 48;  // 2304 samples; the second grain is centred on sample 3456
    const Output forward = run(p, ramp);
    p[kReverse] = 1.0f;
    const Output reversed = run(p, ramp);
    assert(forward.left[3457] - forward.left[3455] > 0.0f);
    assert(reversed.left[3457] - reversed.left[3455] < 0.0f);
}

void testRateOfGrainsFollowsWindowAndDensity()
{
    Params p = transparent();
    p[kWindow] = 100;
    for (const int density : {1, 2, 4}) {
        p[kDensity] = density;
        const Output out = run(p, noise(48000));
        assert(out.lastGrains >= density && out.lastGrains <= density + 1);
        assert(std::fabs(out.lastWindow - 100.0f) < 1e-3f);
    }
}

void testFeedbackRepeatsAndStaysBounded()
{
    Signal burst(96000, 0.0f);
    const Signal tone = sine(440.0, 2400, 0.5f);
    std::copy(tone.begin(), tone.end(), burst.begin());
    Params p = transparent();
    p[kDelay] = 250;
    p[kWindow] = 20;
    const Output none = run(p, burst);
    p[kFeedback] = 0.7f;
    const Output echoes = run(p, burst);
    // Without feedback the burst is gone by 0.4 s; with it the tail is still there.
    assert(rms(none.left, 24000, 48000) < 1e-4);
    assert(rms(echoes.left, 24000, 48000) > 0.01);
    assert(rms(echoes.left, 12000, 24000) > rms(echoes.left, 48000, 72000));  // and it decays

    // Maximum feedback on loud noise with a shifted, reversed cloud cannot run away.
    Params hot = defaults();
    hot[kMix] = 1.0f;
    hot[kFeedback] = 0.95f;
    hot[kDelay] = 40;
    hot[kPitch] = 7.0f;
    hot[kReverse] = 0.5f;
    hot[kRange] = 12.0f;
    hot[kFilterOn] = 1;
    const Output loud = run(hot, noise(480000, 1.0f));
    for (std::size_t i = 0; i < loud.left.size(); ++i) {
        assert(std::isfinite(loud.left[i]) && std::isfinite(loud.right[i]));
        assert(std::fabs(loud.left[i]) < 8.0f && std::fabs(loud.right[i]) < 8.0f);
    }
}

void testFeedbackFilterShapesTheRepeats()
{
    Signal in = noise(96000, 0.5f);
    Params p = defaults();
    p[kMix] = 1.0f;
    p[kFeedback] = 0.9f;
    p[kDelay] = 30;
    p[kFilterOn] = 1;
    p[kFilterMode] = kLowpass;
    p[kFilterFreq] = 0.2f;  // about 130 Hz
    const Output low = run(p, in);
    p[kFilterMode] = kHighpass;
    p[kFilterFreq] = 0.8f;
    const Output high = run(p, in);
    p[kFilterOn] = 0;
    const Output open = run(p, in);
    assert(distance(low.left, open.left) > 1.0 && distance(high.left, open.left) > 1.0);
    // A low-passed loop is duller than an open one: less of its energy is in the sample-to-sample change.
    const auto tilt = [](const Signal& x) {
        double change = 0.0;
        for (std::size_t i = 48001; i < 96000; ++i) change += (static_cast<double>(x[i]) - x[i - 1]) * (static_cast<double>(x[i]) - x[i - 1]);
        return std::sqrt(change / 47999.0) / rms(x, 48000, 96000);
    };
    assert(tilt(low.left) < 0.85 * tilt(open.left));
}

void testSeedGivesTheSamePassAndAnotherSeedAnother()
{
    Params p = defaults();
    p[kMix] = 1.0f;
    p[kBlur] = 1.5f;
    p[kSpread] = 1.0f;
    p[kRange] = 7.0f;
    p[kReverse] = 0.5f;
    p[kVariance] = 0.5f;
    const Signal in = noise(48000);
    const Output a = run(p, in);
    const Output b = run(p, in);
    assert(a.left == b.left && a.right == b.right);
    p[kSeed] = 2;
    const Output c = run(p, in);
    assert(distance(a.left, c.left) > 10.0);
}

void testBlockSizeIndependence()
{
    Params p = defaults();
    p[kMix] = 0.8f;
    p[kBlur] = 1.0f;
    p[kSpread] = 0.6f;
    p[kRange] = 7.0f;
    p[kPitch] = 3.0f;
    p[kReverse] = 0.3f;
    p[kFeedback] = 0.5f;
    p[kFilterOn] = 1;
    p[kWindowSync] = 1;
    p[kDelaySync] = 1;
    const Signal in = noise(48000);
    const Output ref = run(p, in, 480);
    for (const std::uint32_t block : {1u, 64u, 333u, 4096u}) {
        const Output other = run(p, in, block);
        assert(other.left == ref.left && other.right == ref.right);
    }
}

void testStatusReportsLatency()
{
    Params p = defaults();
    p[kWindow] = 100;
    p[kBlur] = 1.0f;
    p[kPitch] = 12.0f;
    const Output out = run(p, noise(4800));
    assert(std::fabs(out.lastLatency - 150.0f) < 0.01f);
    p[kPitch] = 0.0f;
    assert(std::fabs(run(p, noise(4800)).lastLatency - 50.0f) < 0.01f);

    // The synced window and delay are reported at the host tempo, so the panel can show them.
    p[kWindowSync] = 1;
    p[kWindowDivision] = 10;  // 1/4
    p[kDelaySync] = 1;
    p[kDelayDivision] = 7;  // 1/8
    const Output at90 = run(p, noise(4800), 480, 90.0);
    assert(std::fabs(at90.lastWindow - 666.667f) < 0.01f);
    assert(std::fabs(at90.lastDelay - 333.333f) < 0.01f);
}

// Every control has an audible effect, in a context where it is engaged.
void testEveryControlIsAudible()
{
    Params context = defaults();
    context[kMix] = 0.8f;
    context[kWindow] = 60;
    context[kPitch] = 3.0f;
    context[kBlur] = 1.0f;
    context[kReverse] = 0.4f;
    context[kSpread] = 0.5f;
    context[kRange] = 12.0f;
    context[kVariance] = 0.4f;
    context[kFeedback] = 0.5f;
    context[kDelay] = 80;
    context[kFilterOn] = 1;
    context[kFilterFreq] = 0.5f;
    const Signal in = noise(48000);
    const Output baseline = run(context, in);

    Params synced = context;
    synced[kWindowSync] = 1;
    synced[kDelaySync] = 1;
    const Output syncedBaseline = run(synced, in);

    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParamSpec& spec = kParameterSpecs[i];
        if (spec.output) continue;
        const bool division = i == kWindowDivision || i == kDelayDivision;
        const Params& base = division ? synced : context;
        const Output& reference = division ? syncedBaseline : baseline;
        Params changed = base;
        // Move to whichever end is further from the context value.
        const float low = spec.minimum, high = spec.maximum;
        changed[i] = std::fabs(base[i] - low) > std::fabs(base[i] - high) ? low : high;
        if (i == kWindowSync || i == kDelaySync) changed[i] = 1.0f - base[i];
        assert(changed[i] != base[i]);
        const Output out = run(changed, in);
        assert(distance(out.left, reference.left) > 1.0 || distance(out.right, reference.right) > 1.0);
    }
}

// Controls that only matter when something else is on must be inert while it is off.
void testDefaultsLeaveEverythingElseInert()
{
    const Signal in = noise(24000);
    const Params base = defaults();  // Feedback 0, filter off, range 0
    const Output ref = run(base, in);
    Params p = base;
    p[kDelay] = 700;
    p[kDelaySync] = 1;
    p[kDelayDivision] = 17;
    p[kFilterMode] = kBandpass;
    p[kFilterFreq] = 1.0f;
    p[kFilterWidth] = 0.0f;
    p[kFilterOn] = 1;  // no feedback: the filter has nothing to act on
    assert(run(p, in).left == ref.left);
    p = base;
    p[kScale] = 6;
    p[kDirection] = kUp;  // Range 0: nothing to scale or steer
    assert(run(p, in).left == ref.left);
    p = base;
    p[kWindowDivision] = 18;  // Sync off: the division is not used
    assert(run(p, in).left == ref.left);
}

void testExtremesStayFiniteAndBounded()
{
    const Signal in = noise(240000, 1.0f);
    for (const float window : {1.0f, 1000.0f}) {
        for (const float pitch : {-24.0f, 24.0f}) {
            Params p = defaults();
            p[kMix] = 1.0f;
            p[kWindow] = window;
            p[kPitch] = pitch;
            p[kBlur] = 2.0f;
            p[kRange] = 24.0f;
            p[kReverse] = 1.0f;
            p[kVariance] = 1.0f;
            p[kDensity] = kMaxDensity;
            p[kSpread] = 1.0f;
            const Output out = run(p, in, 1000);
            for (std::size_t i = 0; i < out.left.size(); ++i) {
                assert(std::isfinite(out.left[i]) && std::isfinite(out.right[i]));
                assert(std::fabs(out.left[i]) < 12.0f);
            }
        }
    }
}

void testResetClearsHistory()
{
    State s;
    prepare(s, kSr);
    Params p = defaults();
    p[kMix] = 1.0f;
    p[kFeedback] = 0.8f;
    const Signal in = noise(9600);
    Signal l(in.size()), r(in.size());
    process(s, p, static_cast<std::uint32_t>(in.size()), kSr, 120.0, in.data(), in.data(), l.data(), r.data());
    reset(s);
    const Signal silence(9600, 0.0f);
    process(s, p, static_cast<std::uint32_t>(silence.size()), kSr, 120.0, silence.data(), silence.data(), l.data(), r.data());
    for (const float v : l) assert(v == 0.0f);
}

}  // namespace

int main()
{
    testLatencyFormulaPins();
    testShapeKeepsAverageLevel();
    testDivisionsAndSync();
    testCloudDrawFollowsScaleAndDirection();
    testCloudPreviewIsDeterministic();
    testDryPassesWithZeroMix();
    testTransparentGrainsRebuildTheInput();
    testPitchShiftsTheGrains();
    testGrainsReadOnlyThePast();
    testReverseReadsBackwards();
    testRateOfGrainsFollowsWindowAndDensity();
    testFeedbackRepeatsAndStaysBounded();
    testFeedbackFilterShapesTheRepeats();
    testSeedGivesTheSamePassAndAnotherSeedAnother();
    testBlockSizeIndependence();
    testStatusReportsLatency();
    testEveryControlIsAudible();
    testDefaultsLeaveEverythingElseInert();
    testExtremesStayFiniteAndBounded();
    testResetClearsHistory();
    return 0;
}
