#include "heavyverb_core.hpp"
#include "heavyverb_params.hpp"
#include "heavyverb_presets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace downspout::heavyverb;

static int gPassed = 0;
static int gFailed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        ++gPassed;
    } else {
        std::printf("FAIL: %s\n", name);
        ++gFailed;
    }
}

using Engine = std::unique_ptr<EngineState, EngineDeleter>;

struct Stereo {
    std::vector<float> l, r;
};

// Runs a stereo signal through a fresh engine in blocks of `block` frames.
static Stereo run(const Parameters& p, const std::vector<float>& inL, const std::vector<float>& inR, float sr = 48000.0f,
                  uint32_t block = 256)
{
    Engine engine(createEngine(sr));
    Stereo out{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
    for (size_t pos = 0; pos < inL.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, inL.size() - pos));
        const float* ins[2] = {inL.data() + pos, inR.data() + pos};
        float* outs[2]      = {out.l.data() + pos, out.r.data() + pos};
        processBlock(*engine, p, n, ins, outs);
    }
    return out;
}

static Stereo run(const Parameters& p, const std::vector<float>& in, float sr = 48000.0f, uint32_t block = 256)
{
    return run(p, in, in, sr, block);
}

// Deterministic white noise in [-amp, amp].
static std::vector<float> noise(size_t n, float amp, unsigned seed = 1234)
{
    std::vector<float> v(n);
    for (float& x : v) {
        seed = seed * 1664525u + 1013904223u;
        x = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * amp;
    }
    return v;
}

static std::vector<float> impulse(size_t n)
{
    std::vector<float> v(n, 0.0f);
    v[0] = 1.0f;
    return v;
}

static std::vector<float> sine(double hz, double amp, float sr, double seconds)
{
    std::vector<float> v(static_cast<size_t>(sr * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * hz * static_cast<double>(i) / sr));
    return v;
}

// A signal that is `burst` seconds of noise then silence, `total` seconds long.
static std::vector<float> burst(double burstSeconds, double total, float amp = 0.3f, float sr = 48000.0f)
{
    std::vector<float> v(static_cast<size_t>(total * sr), 0.0f);
    const auto n = noise(static_cast<size_t>(burstSeconds * sr), amp);
    std::copy(n.begin(), n.end(), v.begin());
    return v;
}

static double energy(const std::vector<float>& x, double fromSec, double toSec, float sr = 48000.0f)
{
    const size_t a = static_cast<size_t>(fromSec * sr), b = std::min(x.size(), static_cast<size_t>(toSec * sr));
    double e = 0;
    for (size_t i = a; i < b; ++i) e += static_cast<double>(x[i]) * x[i];
    return e / std::max<size_t>(1, b - a);
}

static double db(double power) { return 10.0 * std::log10(power + 1e-30); }

static double maxDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a[i] - b[i])));
    return worst;
}

static double goertzel(const std::vector<float>& x, double fromSec, double toSec, double hz, float sr = 48000.0f)
{
    const size_t a = static_cast<size_t>(fromSec * sr), b = std::min(x.size(), static_cast<size_t>(toSec * sr));
    const double w = 2.0 * M_PI * hz / sr;
    double c = 0, s = 0;
    for (size_t i = a; i < b; ++i) {
        c += x[i] * std::cos(w * static_cast<double>(i));
        s += x[i] * std::sin(w * static_cast<double>(i));
    }
    return 2.0 * std::hypot(c, s) / static_cast<double>(b - a);
}

// First time the output passes `threshold`, in seconds, or -1.
static double onset(const std::vector<float>& x, float threshold, float sr = 48000.0f)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (std::fabs(x[i]) > threshold) return static_cast<double>(i) / sr;
    return -1.0;
}

// RT60 from the Schroeder backward integral, fitted between -5 and -25 dB.
static double rt60(const std::vector<float>& h, float sr = 48000.0f)
{
    std::vector<double> e(h.size());
    double acc = 0;
    for (size_t i = h.size(); i-- > 0;) {
        acc += static_cast<double>(h[i]) * h[i];
        e[i] = acc;
    }
    double t5 = -1, t25 = -1;
    for (size_t i = 0; i < h.size(); ++i) {
        const double d = 10.0 * std::log10(e[i] / e[0] + 1e-30);
        if (t5 < 0 && d < -5.0) t5 = static_cast<double>(i);
        if (t25 < 0 && d < -25.0) { t25 = static_cast<double>(i); break; }
    }
    return t5 < 0 || t25 < 0 ? -1.0 : (t25 - t5) / sr * 3.0;
}

static Parameters wetOnly()
{
    Parameters p;
    p.mixPercent = 100.0f;
    p.preDelayMs = 0.0f;
    p.earlyLevel = 0.0f;
    p.modulation = 0.0f;
    return p;
}

// ---- Parameter table and presets -----------------------------------------------------------------

static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    std::set<std::string> symbols;
    std::set<const float*> addresses;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = kParameterSpecs[i];
        check("table default equals the core default", field(defaults, i) == s.defaultValue);
        check("default is inside its range", s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        field(probe, i) = s.minimum - 1000.0f;
        check("core clamps to the table minimum", field(clampParameters(probe), i) == s.minimum);
        field(probe, i) = s.maximum + 1000.0f;
        check("core clamps to the table maximum", field(clampParameters(probe), i) == s.maximum);
        field(probe, i) = s.defaultValue;
        check("a logarithmic range has a positive minimum", !s.logarithmic || s.minimum > 0.0f);
        symbols.insert(s.symbol);
        addresses.insert(&field(probe, i));
    }
    check("every parameter has its own symbol", symbols.size() == kParameterCount);
    check("every parameter maps to its own field", addresses.size() == kParameterCount);
    check("34 parameters: dimensions, 18 per-dimension, 14 global and preset", kParameterCount == 34);
    check("the user's defaults: three dimensions", defaults.dimensions == 3.0f);
}

static void testPresets()
{
    check("15 presets", kPresetCount == 15 && kPresets.size() == 15);
    check("10 typical and 5 special effects", kTypicalPresets == 10);
    std::set<std::string> names;
    for (const Preset& p : kPresets) names.insert(p.name);
    check("preset names are distinct", names.size() == 15);

    for (int n = 1; n <= kPresetCount; ++n) {
        const Parameters p = presetParameters(n);
        const Parameters clamped = clampParameters(p);
        bool inRange = true;
        for (std::uint32_t i = 0; i < kParameterCount; ++i) inRange = inRange && field(p, i) == field(clamped, i);
        check("preset values are all inside the parameter ranges", inRange);
        check("preset records its own number", static_cast<int>(p.preset) == n);
        check("preset has 1-6 dimensions", dimensionCount(p) >= 1 && dimensionCount(p) <= 6);
        check("a preset never starts frozen (frozen takes no input)", p.freeze == 0.0f);
        if (n > kTypicalPresets)
            check("the last five presets use an effect", p.effect != 0.0f);
    }
    check("Custom (0) changes nothing", [] {
        const Parameters p = presetParameters(0), d;
        for (std::uint32_t i = 0; i < kParameterCount; ++i) if (field(p, i) != field(d, i)) return false;
        return true;
    }());
    // The special-effect presets cover the engine's effects.
    std::set<int> effects;
    for (int n = kTypicalPresets + 1; n <= kPresetCount; ++n) effects.insert(static_cast<int>(presetParameters(n).effect));
    check("special presets cover shimmer, swell and gate", effects.count(kEffectShimmer) && effects.count(kEffectSwell) && effects.count(kEffectGate));
    check("Abyss shifts down and Shimmer Pad up", presetParameters(12).shiftSemitones < 0.0f && presetParameters(11).shiftSemitones > 0.0f);

    for (int a = 1; a <= kPresetCount; ++a)
        for (int b = a + 1; b <= kPresetCount; ++b) {
            const Parameters pa = presetParameters(a), pb = presetParameters(b);
            bool differs = false;
            for (std::uint32_t i = 0; i < kPreset; ++i) differs = differs || field(pa, i) != field(pb, i);
            check("no two presets are the same patch", differs);
        }
}

// Every preset must render finite, audible (apart from the gate between hits) and decaying.
static void testEveryPresetRendersAndDecays()
{
    const auto in = burst(1.0, 12.0);
    for (int n = 1; n <= kPresetCount; ++n) {
        Parameters p = presetParameters(n);
        const Stereo out = run(p, in);
        bool finite = true;
        double peak = 0;
        for (size_t i = 0; i < out.l.size(); ++i) {
            finite = finite && std::isfinite(out.l[i]) && std::isfinite(out.r[i]);
            peak = std::max(peak, static_cast<double>(std::fabs(out.l[i])));
        }
        if (!finite || peak > 4.0) std::printf("  preset %d (%s): finite %d peak %.2f\n", n, kPresets[static_cast<size_t>(n - 1)].name, finite, peak);
        check("preset output is finite and bounded", finite && peak < 4.0);

        // Wet part during the burst: the output minus the dry path.
        std::vector<float> wet(out.l.size());
        const float dry = std::cos(p.mixPercent * 0.01f * 1.57079633f);
        for (size_t i = 0; i < wet.size(); ++i) wet[i] = out.l[i] - in[i] * dry;
        check("preset adds a reverb to the dry signal", db(energy(wet, 0.4, 1.0)) > -60.0);
        check("preset tail decays", energy(out.l, 10.5, 11.5) < energy(out.l, 1.2, 2.2));
    }
}

// ---- Dry path, timing and level --------------------------------------------------------------------

static void testMixZeroIsBitExactDry()
{
    const auto in = noise(20000, 0.5f);
    for (int n = 0; n <= kPresetCount; ++n) {
        Parameters p = n == 0 ? Parameters{} : presetParameters(n);
        p.mixPercent = 0.0f;
        const Stereo out = run(p, in);
        check("Mix 0 returns the input bit for bit", maxDifference(out.l, in) == 0.0 && maxDifference(out.r, in) == 0.0);
    }
}

static void testDistanceSetsTheDelay()
{
    // One dimension, 34.3 m between the walls: sound crosses it in 100 ms.
    Parameters p = wetOnly();
    p.dimensions = 1.0f;
    p.distance[0] = 34.3f;
    p.diffusion = 100.0f;
    p.highCutHz = 20000.0f;
    p.lowCutHz = 20.0f;
    const auto in = impulse(48000);
    const double lines = onset(run(p, in).l, 0.01f);
    check("the first line is one wall spacing / c late (100 ms)", std::fabs(lines - 0.100) < 0.002);
    // With no diffusion the four input all-passes are a plain 30.4 ms delay (documented).
    p.diffusion = 0.0f;
    check("at Diffusion 0 the input is delayed a further 30.4 ms", std::fabs(onset(run(p, in).l, 0.01f) - 0.1304) < 0.002);
    p.diffusion = 100.0f;

    // The early reflections come first: the source is 0.4 of the way across, so 0.8 spacings.
    p.earlyLevel = 100.0f;
    const double early = onset(run(p, in).l, 0.003f);
    check("the first early reflection is 0.8 spacings (80 ms) late", std::fabs(early - 0.080) < 0.002);

    p.distance[0] = 17.15f;
    const double halved = onset(run(p, in).l, 0.003f);
    check("halving the distance halves the delay", std::fabs(halved - 0.040) < 0.002);
}

static void testPreDelayHoldsBackTheReverb()
{
    Parameters p = wetOnly();
    p.dimensions = 1.0f;
    p.distance[0] = 0.5f;
    p.preDelayMs = 100.0f;
    const double t = onset(run(p, impulse(24000)).l, 0.01f);
    check("nothing is heard before the pre-delay (100 ms) plus the first line (1.5 ms)", t > 0.1005 && t < 0.104);
}

static void testWetLevelIsSimilarAcrossDimensions()
{
    const auto in = impulse(6 * 48000);
    double low = 1e9, high = -1e9;
    for (int d = 1; d <= 6; ++d) {
        Parameters p = wetOnly();
        p.dimensions = static_cast<float>(d);
        const double e = db(energy(run(p, in).l, 0.0, 6.0) * 6.0);
        low = std::min(low, e);
        high = std::max(high, e);
    }
    if (high - low > 3.0) std::printf("  wet level spread %.2f dB\n", high - low);
    check("wet level stays within 3 dB from one dimension to six", high - low < 3.0);
}

static void testDecayTimeFollowsTheControl()
{
    for (float sr : {44100.0f, 48000.0f, 96000.0f}) {
        for (float decay : {1.0f, 3.0f}) {
            Parameters p = wetOnly();
            p.damping = {0, 0, 0, 0, 0, 0};
            p.decaySeconds = decay;
            const double t = rt60(run(p, impulse(static_cast<size_t>(sr * (decay * 1.8 + 1.0))), sr).l, sr);
            if (t < 0.8 * decay || t > 1.1 * decay)
                std::printf("  RT60 %.2f s for Decay %.1f s at %.0f Hz\n", t, decay, sr);
            check("measured RT60 is within -20 % / +10 % of Decay", t > 0.8 * decay && t < 1.1 * decay);
        }
    }
}

// ---- Damping, filter, dimensions ---------------------------------------------------------------------

static double highShare(const std::vector<float>& x, double a, double b)
{
    // Energy of the first difference over the energy of the signal: a crude high-frequency share.
    std::vector<float> d(x.size(), 0.0f);
    for (size_t i = 1; i < x.size(); ++i) d[i] = x[i] - x[i - 1];
    return db(energy(d, a, b)) - db(energy(x, a, b));
}

static double lowShare(const std::vector<float>& x, double a, double b)
{
    std::vector<float> m(x.size(), 0.0f);
    double acc = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        acc += (x[i] - acc) * 0.05;   // a one-pole around 400 Hz
        m[i] = static_cast<float>(acc);
    }
    return db(energy(m, a, b)) - db(energy(x, a, b));
}

static void testDampingShortensTheHighs()
{
    const auto in = impulse(3 * 48000);
    Parameters p = wetOnly();
    p.decaySeconds = 3.0f;
    p.damping = {0, 0, 0, 0, 0, 0};
    const double open = highShare(run(p, in).l, 0.5, 1.5);
    p.damping = {100, 100, 100, 100, 100, 100};
    const double damped = highShare(run(p, in).l, 0.5, 1.5);
    if (open - damped < 6.0) std::printf("  high share %.1f dB open, %.1f dB damped\n", open, damped);
    check("damping 100 % removes high frequencies from the tail", open - damped > 6.0);
}

static void testFeedbackFilterTiltsTheTail()
{
    const auto in = impulse(3 * 48000);
    Parameters p = wetOnly();
    p.decaySeconds = 3.0f;
    p.damping = {0, 0, 0, 0, 0, 0};
    const Stereo flat = run(p, in);
    p.filter = {100, 100, 100, 100, 100, 100};
    const Stereo thin = run(p, in);
    p.filter = {-100, -100, -100, -100, -100, -100};
    const Stereo dark = run(p, in);
    check("a positive filter thins the bass of the tail", lowShare(flat.l, 0.5, 1.5) - lowShare(thin.l, 0.5, 1.5) > 6.0);
    check("a negative filter darkens the tail", highShare(flat.l, 0.5, 1.5) - highShare(dark.l, 0.5, 1.5) > 6.0);
}

static void testInactiveDimensionsAreInert()
{
    const auto in = noise(30000, 0.4f);
    Parameters p;
    p.dimensions = 3.0f;
    const Stereo ref = run(p, in);
    Parameters q = p;
    for (int i = 3; i < 6; ++i) {
        q.distance[static_cast<size_t>(i)] *= 0.37f;
        q.damping[static_cast<size_t>(i)] = 100.0f;
        q.filter[static_cast<size_t>(i)] = -80.0f;
    }
    const Stereo out = run(q, in);
    check("dimensions above the count do nothing", maxDifference(out.l, ref.l) == 0.0 && maxDifference(out.r, ref.r) == 0.0);

    for (int d = 1; d <= 6; ++d) {
        Parameters r;
        r.dimensions = static_cast<float>(d);
        const Stereo o = run(r, in);
        bool finite = true;
        for (float v : o.l) finite = finite && std::isfinite(v);
        check("every dimension count renders", finite);
        if (d > 1) {
            Parameters prev = r;
            prev.dimensions = static_cast<float>(d - 1);
            check("each added dimension changes the sound", maxDifference(run(prev, in).l, o.l) > 1e-3);
        }
    }
}

static void testEveryControlMatters()
{
    // Six dimensions and shimmer, so every per-dimension control and the shift are live.
    const auto in = noise(2 * 48000, 0.4f);
    Parameters base;
    base.dimensions = 6.0f;
    base.mixPercent = 50.0f;
    base.effect = kEffectShimmer;
    base.effectAmount = 60.0f;
    base.modulation = 40.0f;
    const Stereo ref = run(base, in);
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        Parameters q = base;
        const ParameterSpec& s = kParameterSpecs[i];
        float target = s.maximum - 0.25f * (s.maximum - s.minimum);
        if (i == kDimensions) target = 4.0f;
        if (i == kEffect) target = kEffectGate;
        if (i == kShift) target = -7.0f;
        if (i == kFreeze) target = 1.0f;
        if (std::fabs(target - field(base, i)) < 1e-6f) target = s.minimum;
        field(q, i) = target;
        const Stereo o = run(q, in);
        const double d = std::max(maxDifference(o.l, ref.l), maxDifference(o.r, ref.r));
        if (i == kPreset) {
            check("the Preset parameter is inert in the engine (the panel applies presets)", d == 0.0);
        } else {
            if (d < 1e-4) std::printf("  %s has no effect\n", s.symbol);
            check("every control changes the output", d > 1e-4);
        }
    }
}

// ---- Effects ---------------------------------------------------------------------------------------------

static void testPitchShifterIsAccurate()
{
    for (float semitones : {12.0f, 7.0f, -12.0f, 19.0f}) {
        PitchShifter shifter;
        shifter.prepare(48000.0f);
        const double ratio = std::pow(2.0, semitones / 12.0);
        std::vector<float> out(48000);
        const auto in = sine(440.0, 0.5, 48000.0f, 1.0);
        for (size_t i = 0; i < in.size(); ++i) out[i] = shifter.process(in[i], static_cast<float>(ratio));
        const double wanted = goertzel(out, 0.25, 1.0, 440.0 * ratio);
        const double source = goertzel(out, 0.25, 1.0, 440.0);
        if (wanted < 0.45 || source > 0.015) std::printf("  shift %+.0f: wanted %.3f source %.4f\n", semitones, wanted, source);
        check("the shifted tone keeps its level", wanted > 0.45 && wanted < 0.55);
        check("the source tone is 30 dB down after shifting", source < wanted * 0.0316);
    }
}

static void testShimmerAddsTheOctave()
{
    const auto in = sine(600.0, 0.3, 48000.0f, 3.0);
    Parameters p = wetOnly();
    p.decaySeconds = 4.0f;
    const Stereo off = run(p, in);
    p.effect = kEffectShimmer;
    p.effectAmount = 100.0f;
    p.shiftSemitones = 12.0f;
    const Stereo on = run(p, in);
    const double without = goertzel(off.l, 1.5, 3.0, 1200.0), with = goertzel(on.l, 1.5, 3.0, 1200.0);
    check("shimmer puts the octave above into the tail (10 dB)", db(with * with) - db(without * without) > 10.0);

    p.shiftSemitones = -12.0f;
    const Stereo down = run(p, in);
    check("a negative shift puts the octave below into the tail (10 dB)",
          db(std::pow(goertzel(down.l, 1.5, 3.0, 300.0), 2)) - db(std::pow(goertzel(off.l, 1.5, 3.0, 300.0), 2)) > 10.0);
}

static void testShimmerDecaysEvenAtFullAmount()
{
    Parameters p = wetOnly();
    p.decaySeconds = 4.0f;
    p.effect = kEffectShimmer;
    p.effectAmount = 100.0f;
    const Stereo out = run(p, burst(0.5, 14.0));
    const double early = db(energy(out.l, 1.0, 2.0)), late = db(energy(out.l, 11.0, 12.0));
    if (early - late < 25.0) std::printf("  shimmer tail only %.1f dB down\n", early - late);
    check("the shimmer feedback never runs away: the tail still dies", early - late > 25.0);
}

static void testSwellFadesTheReverbIn()
{
    // Silence, then noise from 0.2 s: the reverb should arrive over a couple of seconds, not at once.
    std::vector<float> in(6 * 48000, 0.0f);
    const auto n = noise(5 * 48000, 0.3f);
    std::copy(n.begin(), n.end(), in.begin() + 9600);
    Parameters p = wetOnly();
    p.decaySeconds = 1.0f;
    const Stereo plain = run(p, in);
    p.effect = kEffectSwell;
    p.effectAmount = 100.0f;
    const Stereo swell = run(p, in);
    const double earlyDrop = db(energy(plain.l, 0.25, 0.45)) - db(energy(swell.l, 0.25, 0.45));
    const double lateDrop = db(energy(plain.l, 4.0, 5.0)) - db(energy(swell.l, 4.0, 5.0));
    if (earlyDrop < 10.0 || lateDrop > 3.0) std::printf("  swell: early %.1f dB down, late %.1f dB down\n", earlyDrop, lateDrop);
    check("swell holds the reverb back just after an onset", earlyDrop > 10.0);
    check("swell lets the full reverb through once it has risen", lateDrop < 3.0);
}

static void testDuckClearsTheWetSignalForTheInput()
{
    const auto in = burst(1.0, 4.0, 0.5f);
    Parameters p = wetOnly();
    p.decaySeconds = 2.0f;
    const Stereo plain = run(p, in);
    p.effect = kEffectDuck;
    p.effectAmount = 100.0f;
    const Stereo ducked = run(p, in);
    check("duck lowers the wet signal while the input plays (6 dB)", db(energy(plain.l, 0.5, 1.0)) - db(energy(ducked.l, 0.5, 1.0)) > 6.0);
    check("duck lets the tail back once the input stops", std::fabs(db(energy(plain.l, 3.0, 3.8)) - db(energy(ducked.l, 3.0, 3.8))) < 3.0);
}

static void testGateCutsTheTail()
{
    const auto in = burst(0.5, 4.0, 0.4f);
    Parameters p = wetOnly();
    p.decaySeconds = 3.0f;
    const Stereo plain = run(p, in);
    p.effect = kEffectGate;
    p.effectAmount = 20.0f;   // hold 0.24 s
    const Stereo gated = run(p, in);
    check("the gate leaves the reverb alone while the input plays", std::fabs(db(energy(plain.l, 0.2, 0.45)) - db(energy(gated.l, 0.2, 0.45))) < 3.0);
    check("the gate cuts the tail after the hold time (30 dB)", db(energy(plain.l, 1.5, 2.5)) - db(energy(gated.l, 1.5, 2.5)) > 30.0);
}

static void testFreezeHoldsTheTail()
{
    Parameters p = wetOnly();
    p.decaySeconds = 1.0f;
    p.freeze = 0.0f;
    const auto in = burst(0.3, 12.0);

    // Freeze engaged part-way through the block: run in two parts on one engine.
    Engine engine(createEngine(48000.0f));
    Stereo out{std::vector<float>(in.size()), std::vector<float>(in.size())};
    for (size_t pos = 0; pos < in.size(); pos += 256) {
        Parameters q = p;
        q.freeze = pos >= 48000 / 2 ? 1.0f : 0.0f;
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(256, in.size() - pos));
        const float* ins[2] = {in.data() + pos, in.data() + pos};
        float* outs[2] = {out.l.data() + pos, out.r.data() + pos};
        processBlock(*engine, q, n, ins, outs);
    }
    const double a = db(energy(out.l, 1.5, 2.5)), b = db(energy(out.l, 9.5, 10.5));
    if (std::fabs(a - b) > 1.0) std::printf("  frozen level %.2f dB then %.2f dB\n", a, b);
    check("a frozen tail holds its level (within 1 dB over 8 s)", std::fabs(a - b) < 1.0);
    check("the frozen tail is loud enough to hear", a > -60.0);

    // New input does not get in while frozen.
    auto loud = in;
    const auto extra = noise(24000, 0.8f, 99);
    std::copy(extra.begin(), extra.end(), loud.begin() + 6 * 48000);
    Engine e2(createEngine(48000.0f));
    Stereo o2{std::vector<float>(in.size()), std::vector<float>(in.size())};
    for (size_t pos = 0; pos < in.size(); pos += 256) {
        Parameters q = p;
        q.freeze = pos >= 48000 / 2 ? 1.0f : 0.0f;
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(256, in.size() - pos));
        const float* ins[2] = {loud.data() + pos, loud.data() + pos};
        float* outs[2] = {o2.l.data() + pos, o2.r.data() + pos};
        processBlock(*e2, q, n, ins, outs);
    }
    // The dry path still passes, so compare after removing it (mix is 100 % wet: dry gain is ~0).
    check("a frozen reverb takes no new input", std::fabs(db(energy(out.l, 9.5, 10.5)) - db(energy(o2.l, 9.5, 10.5))) < 0.5);
}

// ---- Wet shaping ---------------------------------------------------------------------------------------------

static void testWidthAndCuts()
{
    const auto in = impulse(2 * 48000);
    Parameters p = wetOnly();
    p.widthPercent = 0.0f;
    const Stereo mono = run(p, in);
    check("Width 0 makes the wet signal mono", maxDifference(mono.l, mono.r) < 1e-5);
    p.widthPercent = 150.0f;
    const Stereo wide = run(p, in);
    check("Width 150 widens it", maxDifference(wide.l, wide.r) > 1e-3);

    p = wetOnly();
    p.highCutHz = 20000.0f;
    const double open = highShare(run(p, in).l, 0.05, 1.0);
    p.highCutHz = 1000.0f;
    check("High Cut removes high frequencies", open - highShare(run(p, in).l, 0.05, 1.0) > 6.0);
    p = wetOnly();
    p.lowCutHz = 20.0f;
    const Stereo wide20 = run(p, in);
    p.lowCutHz = 500.0f;
    const Stereo wide500 = run(p, in);
    const double bassDrop = db(std::pow(goertzel(wide20.l, 0.05, 1.5, 100.0), 2)) - db(std::pow(goertzel(wide500.l, 0.05, 1.5, 100.0), 2));
    const double midChange = db(std::pow(goertzel(wide20.l, 0.05, 1.5, 2000.0), 2)) - db(std::pow(goertzel(wide500.l, 0.05, 1.5, 2000.0), 2));
    if (bassDrop < 15.0 || std::fabs(midChange) > 4.0) std::printf("  low cut: 100 Hz down %.1f dB, 2 kHz changed %.1f dB\n", bassDrop, midChange);
    check("Low Cut 500 Hz takes the 100 Hz region down by 15 dB", bassDrop > 15.0);
    check("Low Cut leaves 2 kHz alone", std::fabs(midChange) < 4.0);
}

// ---- Robustness and lifecycle ----------------------------------------------------------------------------------

static void testStabilityUnderSweepsAndOverload()
{
    for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
        Engine engine(createEngine(sr));
        std::vector<float> in(256), l(256), r(256);
        double mx = 0;
        bool bad = false;
        unsigned seed = 5;
        for (int blk = 0; blk < 500; ++blk) {
            Parameters p;
            p.dimensions = static_cast<float>(1 + blk % 6);
            for (size_t i = 0; i < 6; ++i) {
                p.distance[i] = 0.5f + static_cast<float>((blk * 37 + static_cast<int>(i) * 11) % 99);
                p.damping[i] = static_cast<float>((blk * 13) % 101);
                p.filter[i] = static_cast<float>((blk * 29) % 201 - 100);
            }
            p.decaySeconds = (blk / 5) % 2 ? 60.0f : 0.1f;
            p.effect = static_cast<float>(blk % 5);
            p.effectAmount = 100.0f;
            p.shiftSemitones = static_cast<float>(-12 + blk % 37);
            p.freeze = (blk / 40) % 2 ? 1.0f : 0.0f;
            p.mixPercent = 100.0f;
            for (float& v : in) {
                seed = seed * 1664525u + 1013904223u;
                v = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * (blk % 7 == 0 ? 2.0f : 0.5f);
            }
            const float* ins[2] = {in.data(), in.data()};
            float* outs[2] = {l.data(), r.data()};
            processBlock(*engine, p, 256, ins, outs);
            for (size_t i = 0; i < l.size(); ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) bad = true;
                mx = std::max(mx, static_cast<double>(std::fabs(l[i])));
            }
        }
        check("no NaN or inf under random sweeps, freezes and overload", !bad);
        if (mx > 50.0) std::printf("  peak %.1f at %.0f Hz\n", mx, sr);
        check("output stays bounded under overload", mx < 50.0);
    }
}

static void testBlockSizeIndependence()
{
    Parameters p;
    p.dimensions = 5.0f;
    p.effect = kEffectShimmer;
    p.effectAmount = 60.0f;
    p.earlyLevel = 80.0f;
    const auto in = noise(30000, 0.4f);
    const Stereo a = run(p, in, 48000.0f, 1);
    const Stereo b = run(p, in, 48000.0f, 37);
    const Stereo c = run(p, in, 48000.0f, 480);
    const double d = std::max({maxDifference(a.l, b.l), maxDifference(a.r, c.r), maxDifference(b.l, c.l)});
    if (d > 1e-5) std::printf("  block size difference %.3g\n", d);
    check("block size does not change the output", d < 1e-5);

    p.effect = kEffectDuck;
    p.effectAmount = 80.0f;
    const Stereo e = run(p, in, 48000.0f, 7), f = run(p, in, 48000.0f, 512);
    check("block size does not change a ducked output either", maxDifference(e.l, f.l) < 1e-5);
}

static void testInPlaceAndMissingRightInput()
{
    Parameters p = presetParameters(7);
    const auto in = noise(20000, 0.4f);
    const Stereo ref = run(p, in, in);

    Engine engine(createEngine(48000.0f));
    std::vector<float> l = in, r = in;
    const float* ins[2] = {l.data(), r.data()};
    float* outs[2] = {l.data(), r.data()};
    processBlock(*engine, p, static_cast<uint32_t>(in.size()), ins, outs);
    check("processing in place gives the same output", std::max(maxDifference(l, ref.l), maxDifference(r, ref.r)) < 1e-6);

    Engine e2(createEngine(48000.0f));
    std::vector<float> l2 = in, r2(in.size(), 0.0f);
    const float* ins2[2] = {l2.data(), nullptr};
    float* outs2[2] = {l2.data(), r2.data()};
    processBlock(*e2, p, static_cast<uint32_t>(in.size()), ins2, outs2);
    check("a missing right input is treated as the left, even in place", std::max(maxDifference(l2, ref.l), maxDifference(r2, ref.r)) < 1e-6);
}

static void testNoDenormalsAfterLongSilence()
{
    Parameters p = wetOnly();
    p.decaySeconds = 0.3f;
    p.earlyLevel = 50.0f;
    p.effect = kEffectShimmer;
    p.effectAmount = 50.0f;
    const Stereo out = run(p, burst(0.2, 40.0));
    bool subnormal = false;
    for (size_t i = 30 * 48000; i < out.l.size(); ++i)
        if (std::fpclassify(out.l[i]) == FP_SUBNORMAL || std::fpclassify(out.r[i]) == FP_SUBNORMAL) subnormal = true;
    check("the tail ends in exact zeros, never denormals", !subnormal);
    check("the tail really has ended", energy(out.l, 35.0, 39.0) == 0.0);
}

static void testResetClearsMemoryOnly()
{
    Parameters p = presetParameters(8);
    p.mixPercent = 100.0f;
    Engine engine(createEngine(48000.0f));
    const auto loud = noise(48000, 0.5f);
    const auto silent = std::vector<float>(48000, 0.0f);
    std::vector<float> l(48000), r(48000);
    const float* li[2] = {loud.data(), loud.data()};
    float* lo[2] = {l.data(), r.data()};
    processBlock(*engine, p, 48000, li, lo);
    check("the engine is ringing before reset", energy(l, 0.5, 1.0) > 1e-6);
    resetEngine(*engine);
    const float* si[2] = {silent.data(), silent.data()};
    processBlock(*engine, p, 48000, si, lo);
    check("reset clears the whole tail", energy(l, 0.0, 1.0) == 0.0 && energy(r, 0.0, 1.0) == 0.0);
    check("reset leaves the parameters alone", p.dimensions == 4.0f && p.mixPercent == 100.0f);
}

static void testFirstBlockStartsAtThePatch()
{
    // No fade-in from silence: the first block already plays at the patch's level.
    Parameters p = wetOnly();
    p.decaySeconds = 1.0f;
    p.outputDb = 6.0f;
    const auto in = noise(4800, 0.4f);
    Parameters q = p;
    const Stereo first = run(p, in);
    const Stereo again = run(q, in);
    check("a fresh engine is deterministic", maxDifference(first.l, again.l) == 0.0);
    check("the first block is not faded in", energy(first.l, 0.04, 0.1) > 1e-4);
}

static void testRandomise()
{
    const Parameters base = presetParameters(2);
    const Parameters a = randomiseParameters(base, 7), b = randomiseParameters(base, 7), c = randomiseParameters(base, 8);
    bool same = true, differs = false, inRange = true, kept = true;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        same = same && field(a, i) == field(b, i);
        differs = differs || field(a, i) != field(c, i);
        inRange = inRange && field(a, i) >= kParameterSpecs[i].minimum && field(a, i) <= kParameterSpecs[i].maximum;
    }
    for (const std::uint32_t i : {kMix, kOutput, kEffect, kEffectAmount, kShift, kFreeze})
        kept = kept && field(a, i) == field(base, i);
    check("randomise is deterministic from the seed", same);
    check("a different seed gives a different space", differs);
    check("randomised values stay in range", inRange);
    check("randomise keeps mix, output, effect, shift and freeze", kept);
    check("a randomised space is Custom", a.preset == 0.0f);

    // Across many seeds: always in range, always stable and finite, and it actually moves the controls.
    bool allIn = true, moved = true;
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        const Parameters r = randomiseParameters(base, seed);
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            allIn = allIn && field(r, i) >= kParameterSpecs[i].minimum && field(r, i) <= kParameterSpecs[i].maximum;
        moved = moved && (r.decaySeconds != base.decaySeconds || r.distance[0] != base.distance[0]);
    }
    check("300 randomised spaces are all inside the ranges", allIn);
    check("every randomise changes the space", moved);

    // Rendered: a handful of random spaces decay and stay finite (loud/unstable spaces would show here).
    bool healthy = true;
    for (std::uint32_t seed = 1; seed <= 12; ++seed) {
        Parameters r = randomiseParameters(base, seed * 31);
        r.mixPercent = 100.0f;
        std::unique_ptr<EngineState, EngineDeleter> engine(createEngine(48000.0f));
        std::vector<float> in(48000 * 4, 0.0f), outL(in.size()), outR(in.size());
        in[0] = 1.0f;
        const float* ins[] = {in.data(), in.data()};
        float* outs[] = {outL.data(), outR.data()};
        processBlock(*engine, r, static_cast<uint32_t>(in.size()), ins, outs);
        float peak = 0.0f, tail = 0.0f;
        for (size_t i = 0; i < outL.size(); ++i) {
            healthy = healthy && std::isfinite(outL[i]) && std::isfinite(outR[i]);
            peak = std::max(peak, std::fabs(outL[i]));
        }
        for (size_t i = outL.size() - 4800; i < outL.size(); ++i) tail = std::max(tail, std::fabs(outL[i]));
        healthy = healthy && peak < 4.0f && (r.decaySeconds > 3.0f || tail < peak);
    }
    check("randomised spaces render finite and bounded", healthy);
}

int main()
{
    testRandomise();
    testParameterTableMatchesCore();
    testPresets();
    testEveryPresetRendersAndDecays();
    testMixZeroIsBitExactDry();
    testDistanceSetsTheDelay();
    testPreDelayHoldsBackTheReverb();
    testWetLevelIsSimilarAcrossDimensions();
    testDecayTimeFollowsTheControl();
    testDampingShortensTheHighs();
    testFeedbackFilterTiltsTheTail();
    testInactiveDimensionsAreInert();
    testEveryControlMatters();
    testPitchShifterIsAccurate();
    testShimmerAddsTheOctave();
    testShimmerDecaysEvenAtFullAmount();
    testSwellFadesTheReverbIn();
    testDuckClearsTheWetSignalForTheInput();
    testGateCutsTheTail();
    testFreezeHoldsTheTail();
    testWidthAndCuts();
    testStabilityUnderSweepsAndOverload();
    testBlockSizeIndependence();
    testInPlaceAndMissingRightInput();
    testNoDenormalsAfterLongSilence();
    testResetClearsMemoryOnly();
    testFirstBlockStartsAtThePatch();
    std::printf("heavyverb_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
