#include "monochord_core.hpp"
#include "monochord_params.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::monochord;

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

constexpr float kSr = 48000.0f;
using Cx = std::complex<double>;

static void fft(std::vector<Cx>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / static_cast<double>(len);
        const Cx wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Cx w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const Cx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static std::vector<double> powerSpectrum(const std::vector<float>& x, size_t from, size_t n)
{
    std::vector<Cx> a(n, Cx(0.0, 0.0));
    for (size_t i = 0; i < n && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n));
        a[i] = static_cast<double>(x[from + i]) * w;
    }
    fft(a);
    std::vector<double> p(n / 2);
    for (size_t i = 0; i < n / 2; ++i) p[i] = std::norm(a[i]);
    return p;
}

// The spectral peak within +-4 % of `expected`, by parabolic interpolation of the log spectrum.
static double peakNear(const std::vector<float>& x, size_t from, size_t n, float sr, double expected)
{
    const auto p = powerSpectrum(x, from, n);
    const size_t lo = static_cast<size_t>(expected * 0.96 * static_cast<double>(n) / sr);
    const size_t hi = static_cast<size_t>(expected * 1.04 * static_cast<double>(n) / sr) + 1;
    size_t best = lo + 1;
    for (size_t i = lo + 1; i < hi && i + 1 < p.size(); ++i) if (p[i] > p[best]) best = i;
    const double a = std::log(p[best - 1] + 1e-30), b = std::log(p[best] + 1e-30), c = std::log(p[best + 1] + 1e-30);
    return (static_cast<double>(best) + 0.5 * (a - c) / (a - 2.0 * b + c)) * sr / static_cast<double>(n);
}

// Energy of the Hann main lobe around `hz` (scallop-free), as an amplitude.
static double lineAmplitude(const std::vector<float>& x, size_t from, size_t n, float sr, double hz)
{
    const auto p = powerSpectrum(x, from, n);
    const long bin = std::lround(hz * static_cast<double>(n) / sr);
    double energy = 0;
    for (long d = -3; d <= 3; ++d) energy += p[static_cast<size_t>(bin + d)];
    return std::sqrt(energy * 32.0 / 3.0) / static_cast<double>(n);
}

static double dbOf(double x) { return 20.0 * std::log10(x + 1e-12); }
static double noteHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

static std::vector<MidiEvent> noteOn(int note, int vel, std::uint32_t frame = 0)
{
    MidiEvent e;
    e.frame = frame;
    e.type = MidiEvent::Type::NoteOn;
    e.data1 = static_cast<std::uint8_t>(note);
    e.data2 = static_cast<std::uint8_t>(vel);
    return {e};
}

static MidiEvent noteOffEvent(int note, std::uint32_t frame)
{
    MidiEvent e;
    e.frame = frame;
    e.type = MidiEvent::Type::NoteOff;
    e.data1 = static_cast<std::uint8_t>(note);
    return e;
}

static MidiEvent controller(int cc, int value, std::uint32_t frame)
{
    MidiEvent e;
    e.frame = frame;
    e.type = MidiEvent::Type::Controller;
    e.data1 = static_cast<std::uint8_t>(cc);
    e.data2 = static_cast<std::uint8_t>(value);
    return e;
}

struct Render {
    std::vector<float> l, r;
    EngineState state;
};

static Render render(const Parameters& p, const std::vector<MidiEvent>& events, size_t frames, float sr = kSr, size_t block = 256)
{
    Render res;
    res.l.assign(frames, 0.0f);
    res.r.assign(frames, 0.0f);
    size_t evi = 0;
    for (size_t pos = 0; pos < frames; pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min(block, frames - pos));
        std::vector<MidiEvent> local;
        while (evi < events.size() && events[evi].frame < pos + n) {
            MidiEvent e = events[evi++];
            e.frame = static_cast<uint32_t>(e.frame - pos);
            local.push_back(e);
        }
        processBlock(res.state, p, sr, n, local.data(), static_cast<uint32_t>(local.size()), res.l.data() + pos, res.r.data() + pos);
    }
    return res;
}

static double rms(const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; ++i) s += static_cast<double>(v[i]) * v[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

// A clean string: no body, no pickup, no damping, no stiffness; the bare waveguide.
static Parameters bare()
{
    Parameters p;
    p.bodyLevel = 0.0f; p.pickup = 0.0f; p.damping = 0.0f; p.stiffness = 0.0f;
    p.decaySec = 4.0f; p.velocity = 0.0f;
    return p;
}

// ── The loop is in tune ─────────────────────────────────────────────────────

static void testLoopPhaseDelayIsOnePeriod()
{
    double worst = 0;
    for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f})
        for (int note = 24; note <= 108; note += 3)
            for (float damping : {0.0f, 0.5f, 1.0f})
                for (float stiffness : {0.0f, 0.4f, 1.0f}) {
                    Parameters p;
                    p.damping = damping; p.stiffness = stiffness;
                    const double f0 = noteHz(note);
                    if (f0 > sr * 0.2) continue;
                    const LoopTuning t = tuneLoop(f0, p, sr);
                    worst = std::max(worst, std::fabs(loopPhaseDelay(t, f0, sr) - sr / f0));
                }
    std::printf("  worst loop phase delay error %.2e samples\n", worst);
    check("the loop's phase delay at the fundamental is exactly one period", worst < 1e-6);
    const LoopTuning t = tuneLoop(110.0, Parameters{}, 48000.0);
    check("the loop gain is below 1 (the string decays)", t.gain > 0.0 && t.gain < 1.0);
}

static void testPitchAccuracy()
{
    double worst = 0;
    for (int ex : {0, 2}) {
        for (int note : {28, 40, 52, 60, 69, 81, 93, 100}) {
            for (float damping : {0.0f, 0.8f}) {
                // Loop loss is per pass, so heavy damping on a high note leaves a string that is gone in milliseconds: there is no
                // pitch to measure (the measured top note with damping 0.8 is a few cents off because the peak is noise).
                if (damping > 0.5f && note > 81) continue;
                for (float stiffness : {0.0f, 0.6f}) {
                    Parameters p = bare();
                    p.exciter = static_cast<float>(ex);
                    p.damping = damping; p.stiffness = stiffness;
                    p.decaySec = 6.0f;
                    const Render r = render(p, noteOn(note, 100), 1 << 17);
                    const double f = peakNear(r.l, 2000, 1 << 16, kSr, noteHz(note));
                    const double cents = std::fabs(1200.0 * std::log2(f / noteHz(note)));
                    if (cents >= 1.0) std::printf("  off by %.2f cents: exciter %d note %d damping %.1f stiffness %.1f\n", cents, ex, note, static_cast<double>(damping), static_cast<double>(stiffness));
                    worst = std::max(worst, cents);
                }
            }
        }
    }
    std::printf("  worst pitch error %.3f cents\n", worst);
    check("every plucked or struck note is within 1 cent, whatever the damping and stiffness", worst < 1.0);
    for (float sr : {44100.0f, 96000.0f}) {
        Parameters p = bare();
        p.damping = 0.5f; p.stiffness = 0.3f;
        const Render r = render(p, noteOn(57, 100), 1 << 17, sr);
        const double f = peakNear(r.l, 2000, 1 << 16, sr, noteHz(57));
        check("tuning holds at other sample rates", std::fabs(1200.0 * std::log2(f / noteHz(57))) < 1.0);
    }
    // Pitch bend: the loop is retuned at control rate.
    Parameters p = bare();
    p.bendRange = 12.0f;
    std::vector<MidiEvent> ev = noteOn(57, 100);
    MidiEvent b;
    b.frame = 10; b.type = MidiEvent::Type::PitchBend; b.bend = 8191;
    ev.push_back(b);
    const Render r = render(p, ev, 1 << 17);
    check("a full pitch bend raises the pitch by the bend range (within 10 cents)",
          std::fabs(1200.0 * std::log2(peakNear(r.l, 4000, 1 << 16, kSr, noteHz(57) * 2.0) / (noteHz(57) * 2.0))) < 10.0);
}

// ── Decay, damping, stiffness ───────────────────────────────────────────────

static double decayDbPerSecond(const std::vector<float>& x, double hz, float sr, size_t t1, size_t t2)
{
    const size_t n = 1 << 13;
    const double a1 = lineAmplitude(x, t1, n, sr, hz), a2 = lineAmplitude(x, t2, n, sr, hz);
    return (dbOf(a1) - dbOf(a2)) / (static_cast<double>(t2 - t1) / sr);
}

static void testDecayTimeMatchesTheControl()
{
    for (float decay : {1.0f, 3.0f, 8.0f}) {
        for (int note : {48, 72}) {
            Parameters p = bare();
            p.decaySec = decay;
            const Render r = render(p, noteOn(note, 100), 1 << 18);
            const double rate = decayDbPerSecond(r.l, noteHz(note), kSr, 6000, 6000 + static_cast<size_t>(kSr * decay * 0.5));
            const double t60 = 60.0 / rate;
            if (std::fabs(t60 / decay - 1.0) >= 0.12) std::printf("  decay %.1f note %d: measured T60 %.2f s\n", static_cast<double>(decay), note, t60);
            check("the fundamental falls 60 dB in the Decay time (within 12 %)", std::fabs(t60 / decay - 1.0) < 0.12);
        }
    }
}

static void testDampingKillsHighPartialsFirst()
{
    Parameters p = bare();
    p.decaySec = 6.0f;
    const int note = 45;   // 110 Hz: the 8th partial is 880 Hz, well inside the passband
    const double f0 = noteHz(note);
    p.damping = 0.0f;
    Render r = render(p, noteOn(note, 100), 1 << 18);
    const double flatRatio = decayDbPerSecond(r.l, 8 * f0, kSr, 4000, 28000) / decayDbPerSecond(r.l, f0, kSr, 4000, 28000);
    p.damping = 0.7f;
    r = render(p, noteOn(note, 100), 1 << 18);
    const double dampedRatio = decayDbPerSecond(r.l, 8 * f0, kSr, 4000, 28000) / decayDbPerSecond(r.l, f0, kSr, 4000, 28000);
    std::printf("  decay rate of partial 8 over the fundamental: %.2f with no damping, %.2f at 0.7\n", flatRatio, dampedRatio);
    check("with no damping every partial falls at the same rate", std::fabs(flatRatio - 1.0) < 0.25);
    check("damping makes the high partials fall much faster than the fundamental", dampedRatio > 2.0);
}

static void testStiffnessSharpensThePartials()
{
    const int note = 40;   // 82 Hz
    const double f0 = noteHz(note);
    Parameters p = bare();
    p.decaySec = 8.0f;
    p.position = 0.12f;
    p.hardness = 1.0f;
    const Render plain = render(p, noteOn(note, 100), 1 << 18);
    const double harmonic = peakNear(plain.l, 2000, 1 << 17, kSr, 6.0 * f0);
    for (float s : {0.5f, 0.6f, 0.7f, 0.8f, 0.9f}) {
        p.stiffness = s;
        const Render probe = render(p, noteOn(note, 100), 1 << 18);
        std::printf("  stiffness %.1f: partial 6 at %+.2f cents\n", static_cast<double>(s), 1200.0 * std::log2(peakNear(probe.l, 2000, 1 << 17, kSr, 6.0 * f0) / (6.0 * f0)));
    }
    p.stiffness = 0.7f;
    const Render stiff = render(p, noteOn(note, 100), 1 << 18);
    const double sharp = peakNear(stiff.l, 2000, 1 << 17, kSr, 6.0 * f0);
    const double plainCents = 1200.0 * std::log2(harmonic / (6.0 * f0));
    const double stiffCents = 1200.0 * std::log2(sharp / (6.0 * f0));
    std::printf("  partial 6: %+.2f cents with no stiffness, %+.2f cents at 0.7\n", plainCents, stiffCents);
    check("with no stiffness the partials are harmonic (within 1 cent)", std::fabs(plainCents) < 1.0);
    check("stiffness sharpens the upper partials (more than 5 cents, less than a semitone)", stiffCents > 5.0 && stiffCents < 100.0);
    check("... and the fundamental stays in tune", std::fabs(1200.0 * std::log2(peakNear(stiff.l, 2000, 1 << 17, kSr, f0) / f0)) < 1.0);
}

// ── Excitation ──────────────────────────────────────────────────────────────

static void testExcitationShapes()
{
    Parameters p;
    std::vector<float> s(400);
    bool zeroMean = true, peakOne = true;
    for (int ex : {0, 2})
        for (float hardness : {0.0f, 0.5f, 1.0f})
            for (float position : {0.05f, 0.2f, 0.5f}) {
                p.hardness = hardness; p.position = position;
                excitation(static_cast<Exciter>(ex), 400, p, s.data());
                double mean = 0, peak = 0;
                for (float v : s) { mean += v; peak = std::max(peak, static_cast<double>(std::fabs(v))); }
                if (std::fabs(mean / 400.0) > 1e-6) zeroMean = false;
                if (std::fabs(peak - 1.0) > 1e-5) peakOne = false;
            }
    check("a plucked or struck string starts with no net displacement (so no DC is trapped in the loop)", zeroMean);
    check("the excitation peaks at 1", peakOne);
    p.hardness = 1.0f; p.position = 0.25f;
    excitation(Exciter::Pluck, 400, p, s.data());
    const int apex = static_cast<int>(std::max_element(s.begin(), s.end()) - s.begin());
    check("a pluck's apex sits at the pluck position", std::abs(apex - 100) <= 2);
    excitation(Exciter::Bow, 400, p, s.data());
    bool zeros = true;
    for (float v : s) if (v != 0.0f) zeros = false;
    check("a bow writes nothing at note-on", zeros);
    // A softer hammer is a wider pulse.
    auto width = [&](float hardness) {
        Parameters q;
        q.hardness = hardness; q.position = 0.5f;
        excitation(Exciter::Hammer, 400, q, s.data());
        int n = 0;
        for (float v : s) if (v > 0.5f) ++n;
        return n;
    };
    check("a softer hammer is a wider pulse", width(0.0f) > 2 * width(1.0f));
}

static void testPickPositionNulls()
{
    // A string plucked a fraction p along its length has no k-th harmonic when k p is a whole number.
    Parameters p = bare();
    p.hardness = 1.0f;
    p.decaySec = 8.0f;
    const int note = 52;
    const double f0 = noteHz(note);
    p.position = 0.2f;
    Render r = render(p, noteOn(note, 100), 1 << 17);
    const double a1 = lineAmplitude(r.l, 3000, 1 << 15, kSr, f0), a5 = lineAmplitude(r.l, 3000, 1 << 15, kSr, 5 * f0), a4 = lineAmplitude(r.l, 3000, 1 << 15, kSr, 4 * f0);
    std::printf("  pluck at 0.2: harmonic 5 is %.1f dB, harmonic 4 %.1f dB below the fundamental\n", dbOf(a5 / a1), dbOf(a4 / a1));
    check("a pluck a fifth of the way along has almost no fifth harmonic (below -30 dB)", dbOf(a5 / a1) < -30.0);
    // theory: harmonic k of a triangle apex at p goes as sin(k pi p) / k^2, so the fourth is 24.1 dB down
    check("... while the fourth is where theory puts it (-24 dB, within 3 dB)", std::fabs(dbOf(a4 / a1) + 24.1) < 3.0);
    p.position = 0.5f;
    r = render(p, noteOn(note, 100), 1 << 17);
    check("a pluck at the middle has almost no second harmonic (below -30 dB)", dbOf(lineAmplitude(r.l, 3000, 1 << 15, kSr, 2 * f0) / lineAmplitude(r.l, 3000, 1 << 15, kSr, f0)) < -30.0);
}

static void testHammerBrightnessAndPickup()
{
    const int note = 48;
    const double f0 = noteHz(note);
    Parameters p = bare();
    p.exciter = 2.0f;
    p.position = 0.3f;
    p.hardness = 1.0f;
    Render r = render(p, noteOn(note, 100), 1 << 17);
    const double hard = dbOf(lineAmplitude(r.l, 3000, 1 << 15, kSr, 9 * f0) / lineAmplitude(r.l, 3000, 1 << 15, kSr, f0));
    p.hardness = 0.0f;
    r = render(p, noteOn(note, 100), 1 << 17);
    const double soft = dbOf(lineAmplitude(r.l, 3000, 1 << 15, kSr, 9 * f0) / lineAmplitude(r.l, 3000, 1 << 15, kSr, f0));
    check("a hard hammer is brighter than a soft one (the ninth harmonic is more than 6 dB higher)", hard > soft + 6.0);
    // The pickup comb: a pickup a quarter of the way along cancels every fourth harmonic.
    p = bare();
    p.hardness = 1.0f;
    p.position = 0.13f;
    p.decaySec = 8.0f;
    const double without = lineAmplitude(render(p, noteOn(note, 100), 1 << 17).l, 3000, 1 << 15, kSr, 4 * f0);
    p.pickup = 0.25f;
    const Render picked = render(p, noteOn(note, 100), 1 << 17);
    const double with = lineAmplitude(picked.l, 3000, 1 << 15, kSr, 4 * f0);
    check("a pickup a quarter of the way along removes the fourth harmonic (by more than 25 dB)", dbOf(with / without) < -25.0);
    check("... and still lets the fundamental through", lineAmplitude(picked.l, 3000, 1 << 15, kSr, f0) > 0.01);
}

// ── Bow ──────────────────────────────────────────────────────────────────────

static void testBow()
{
    Parameters p = bare();
    p.exciter = 1.0f;
    p.decaySec = 0.3f;          // a plucked string would be gone in a second
    p.releaseMs = 50.0f;
    const int note = 57;
    std::vector<MidiEvent> ev = noteOn(note, 100);
    ev.push_back(noteOffEvent(note, 144000));
    const Render r = render(p, ev, 192000);
    const double early = rms(r.l, 24000, 48000), late = rms(r.l, 96000, 144000);
    std::printf("  bow: rms %.4f at 0.5-1 s, %.4f at 2-3 s; pitch %+.1f cents\n", early, late,
                1200.0 * std::log2(peakNear(r.l, 60000, 1 << 16, kSr, noteHz(note)) / noteHz(note)));
    check("a bowed string keeps sounding while the key is held, long after a pluck would have died", late > 0.01 && late > 0.5 * early);
    check("a bowed note is in tune (within 15 cents)", std::fabs(1200.0 * std::log2(peakNear(r.l, 60000, 1 << 16, kSr, noteHz(note)) / noteHz(note))) < 15.0);
    check("the bow stops when the key is released", rms(r.l, 144000 + 24000, 192000) < 1e-3);
    bool bounded = true;
    for (float v : r.l) if (!std::isfinite(v) || std::fabs(v) > 3.0f) bounded = false;
    check("a bowed string stays bounded", bounded);
    double mean = 0;
    for (size_t i = 72000; i < 144000; ++i) mean += r.l[i];
    mean /= 72000.0;
    check("a bowed string leaves no DC at the output", std::fabs(mean) < 0.01 * late + 1e-5);
    // Loudness follows the velocity (the bow's own oscillation is self-limiting).
    Parameters v = p;
    v.velocity = 1.0f;
    const double bowLoud = rms(render(v, noteOn(note, 127), 96000).l, 48000, 96000);
    const double bowSoft = rms(render(v, noteOn(note, 64), 96000).l, 48000, 96000);
    check("a bowed note's level follows the velocity", std::fabs(dbOf(bowSoft / bowLoud) - dbOf(64.0 / 127.0)) < 0.5);
    bool allOscillate = true;
    for (float pressure : {0.1f, 0.5f, 1.0f})
        for (int vel : {20, 80, 127}) {
            Parameters q = p;
            q.bowPressure = pressure;
            if (rms(render(q, noteOn(note, vel), 96000).l, 72000, 96000) < 0.005) allOscillate = false;
        }
    check("the bow sounds at every pressure and velocity (it never settles to a constant offset)", allOscillate);
    // More pressure, a different sound.
    Parameters q = p;
    q.bowPressure = 1.0f;
    const Render r2 = render(q, noteOn(note, 100), 96000);
    double d = 0;
    for (size_t i = 24000; i < 96000; ++i) d = std::max(d, static_cast<double>(std::fabs(r.l[i] - r2.l[i])));
    check("bow pressure changes the sound", d > 1e-3);
}

// ── Voices and MIDI ─────────────────────────────────────────────────────────

static void testVoicesAndMidi()
{
    Parameters p = bare();
    std::vector<MidiEvent> ev;
    for (int i = 0; i < 8; ++i) ev.push_back(noteOn(48 + i, 100, static_cast<std::uint32_t>(i * 10))[0]);
    Render r = render(p, ev, 4000);
    check("eight notes use eight voices", activeVoices(r.state) == 8);
    ev.push_back(noteOn(72, 100, 3000)[0]);
    r = render(p, ev, 4000);
    check("a ninth note steals a voice rather than adding one", activeVoices(r.state) == 8);

    p.releaseMs = 20.0f;
    ev = {noteOn(60, 100, 0)[0], noteOffEvent(60, 2000)};
    r = render(p, ev, 24000);
    check("a released string is damped and its voice ends", activeVoices(r.state) == 0);
    check("... and is silent", rms(r.l, 20000, 24000) < 1e-4);

    ev = {controller(64, 127, 0), noteOn(60, 100, 10)[0], noteOffEvent(60, 2000)};
    p.releaseMs = 20.0f;
    r = render(p, ev, 24000);
    check("the sustain pedal keeps a released string undamped", activeVoices(r.state) == 1 && rms(r.l, 20000, 24000) > 0.01);
    ev.push_back(controller(64, 0, 12000));
    r = render(p, ev, 24000);
    check("releasing the pedal damps it", activeVoices(r.state) == 0);
    ev = {noteOn(60, 100, 0)[0], controller(123, 0, 3000)};
    r = render(p, ev, 24000);
    check("all-notes-off damps held notes", activeVoices(r.state) == 0);
    ev = {noteOn(60, 100, 0)[0], controller(120, 0, 3000)};
    p.releaseMs = 2000.0f;
    r = render(p, ev, 6000);
    double after = 0;
    for (size_t i = 3010; i < 6000; ++i) after = std::max(after, static_cast<double>(std::fabs(r.l[i])));
    check("all-sound-off silences at once", after == 0.0 && activeVoices(r.state) == 0);

    p = bare();
    p.velocity = 1.0f;
    const double loud = rms(render(p, noteOn(60, 127), 24000).l, 2000, 12000);
    const double soft = rms(render(p, noteOn(60, 64), 24000).l, 2000, 12000);
    check("velocity 1.0 scales the level with the key velocity", std::fabs(dbOf(soft / loud) - dbOf(64.0 / 127.0)) < 0.2);
    p.velocity = 0.0f;
    check("velocity 0 plays every note at full level", std::fabs(rms(render(p, noteOn(60, 127), 24000).l, 2000, 12000) - rms(render(p, noteOn(60, 30), 24000).l, 2000, 12000)) < 1e-6);

    p = bare();
    const Render early = render(p, noteOn(60, 100, 100), 400, kSr, 400);
    bool silent = true;
    for (size_t i = 0; i < 100; ++i) if (early.l[i] != 0.0f) silent = false;
    check("nothing sounds before the note's frame", silent);
    p.pickup = 0.2f; p.bodyLevel = 0.4f; p.damping = 0.4f; p.stiffness = 0.3f; p.exciter = 1.0f;
    ev = {noteOn(55, 100, 37)[0], noteOn(62, 90, 513)[0], noteOffEvent(55, 6000)};
    const Render a = render(p, ev, 12000, kSr, 16);
    const Render b = render(p, ev, 12000, kSr, 1000);
    double d = 0;
    for (size_t i = 0; i < a.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
    check("block size does not change the output", d < 1e-6);
    bool mono = true;
    for (size_t i = 0; i < a.l.size(); ++i) if (a.l[i] != a.r[i]) mono = false;
    check("the output is mono (both channels equal)", mono);
}

static void testDeadStringsFreeTheirVoices()
{
    Parameters p = bare();
    p.decaySec = 0.1f;
    const Render r = render(p, noteOn(60, 100), 48000 * 6);
    check("a held note whose string has died frees its voice", activeVoices(r.state) == 0);
}

static void testEveryControlMatters()
{
    Parameters base;
    base.exciter = 0.0f; base.pickup = 0.25f; base.bodyLevel = 0.4f;
    base.releaseMs = 300.0f;
    std::vector<MidiEvent> ev = noteOn(55, 100);
    MidiEvent b;
    b.frame = 3000; b.type = MidiEvent::Type::PitchBend; b.bend = 4096;
    ev.push_back(b);
    ev.push_back(noteOffEvent(55, 10000));
    const Render ref = render(base, ev, 20000);
    Parameters bowBase = base;
    bowBase.exciter = 1.0f;
    const Render bowRef = render(bowBase, ev, 20000);
    int dead = 0;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = kParameterSpecs[i];
        const bool bowOnly = i == kBowPressure;
        Parameters q = bowOnly ? bowBase : base;
        const float v = field(q, i);
        field(q, i) = v < (s.minimum + s.maximum) * 0.5f ? s.minimum + (s.maximum - s.minimum) * 0.75f : s.minimum + (s.maximum - s.minimum) * 0.2f;
        const Render r = render(q, ev, 20000);
        const Render& cmp = bowOnly ? bowRef : ref;
        double d = 0;
        for (size_t k = 0; k < r.l.size(); ++k) d = std::max(d, static_cast<double>(std::fabs(r.l[k] - cmp.l[k])));
        if (d < 1e-4) { ++dead; std::printf("  no effect: %s\n", s.name); }
    }
    check("every control changes the sound", dead == 0);
}

static void testStabilityAndBounds()
{
    for (float sr : {44100.0f, 96000.0f, 192000.0f}) {
        EngineState st;
        std::vector<float> l(256), r(256);
        bool bad = false;
        double mx = 0;
        for (int blk = 0; blk < 400; ++blk) {
            Parameters p;
            p.exciter = static_cast<float>(blk % 3);
            p.position = 0.05f + 0.45f * static_cast<float>((blk * 7) % 101) / 100.0f;
            p.hardness = static_cast<float>((blk * 11) % 101) / 100.0f;
            p.bowPressure = static_cast<float>((blk * 13) % 101) / 100.0f;
            p.decaySec = 0.1f * std::pow(200.0f, static_cast<float>((blk * 17) % 100) / 99.0f);
            p.damping = static_cast<float>((blk * 19) % 101) / 100.0f;
            p.stiffness = static_cast<float>((blk * 23) % 101) / 100.0f;
            p.bodySize = 0.5f + 1.5f * static_cast<float>((blk * 29) % 101) / 100.0f;
            p.bodyLevel = static_cast<float>((blk * 31) % 101) / 100.0f;
            p.pickup = 0.5f * static_cast<float>((blk * 37) % 101) / 100.0f;
            p.releaseMs = 5.0f + static_cast<float>((blk * 41) % 500);
            p.master = 1.0f;
            std::vector<MidiEvent> ev;
            for (int k = 0; k < 3; ++k) {
                MidiEvent e;
                e.frame = static_cast<std::uint32_t>(k * 60);
                e.type = (blk + k) % 4 == 0 ? MidiEvent::Type::NoteOff : MidiEvent::Type::NoteOn;
                e.data1 = static_cast<std::uint8_t>(15 + (blk * 7 + k * 13) % 100);
                e.data2 = static_cast<std::uint8_t>(40 + (blk * 3) % 87);
                ev.push_back(e);
            }
            processBlock(st, p, sr, 256, ev.data(), static_cast<std::uint32_t>(ev.size()), l.data(), r.data());
            for (size_t i = 0; i < l.size(); ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) bad = true;
                mx = std::max(mx, static_cast<double>(std::fabs(l[i])));
            }
        }
        check("no NaN or inf under random parameters and notes (15 to 114, down to 20 Hz)", !bad);
        check("output stays bounded (below 40)", mx < 40.0);
    }
    Parameters nan;
    nan.decaySec = std::nanf("");
    nan.position = std::nanf("");
    const Parameters c = clampParameters(nan);
    check("NaN parameters are clamped to finite values", std::isfinite(c.decaySec) && std::isfinite(c.position));
}

static void testDenormalsAndReset()
{
    Parameters p;
    p.decaySec = 0.2f;
    p.releaseMs = 30.0f;
    std::vector<MidiEvent> ev = {noteOn(60, 100, 0)[0], noteOffEvent(60, 2000)};
    Render r = render(p, ev, 48000 * 3);
    bool denormal = false;
    auto test = [&](double v) { if (v != 0.0 && std::fabs(v) < 2.2250738585072014e-308) denormal = true; };
    for (const Voice& v : r.state.voices) { test(v.lowpassState); test(v.fracX1); test(v.fracY1); for (double z : v.stiffX1) test(z); for (double z : v.stiffY1) test(z); test(v.body[0].z1); test(v.body[1].z2); }
    test(r.state.dcX); test(r.state.dcY);
    check("loop, body and DC memory are not left denormal", !denormal);

    Parameters q = bare();
    Render s = render(q, noteOn(60, 100), 4000);
    check("a voice is sounding", activeVoices(s.state) == 1);
    resetState(s.state);
    check("reset silences every voice", activeVoices(s.state) == 0 && s.state.bend[0] == 0.0f && !s.state.primed);
    check("reset leaves the parameters alone", q.decaySec == 4.0f);
}

static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    check("fourteen parameters", kParameterCount == 14);
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
    }
    bool unique = true;
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        for (std::uint32_t j = i + 1; j < kParameterCount; ++j)
            if (std::string(kParameterSpecs[i].symbol) == kParameterSpecs[j].symbol) unique = false;
    check("symbols are unique", unique);
}

// ── Per-channel notes and bend (Retune sends one note per channel, each with its own bend) ──

static void testBendAndNoteOffArePerChannel()
{
    Parameters p = bare();
    p.bendRange = 12.0f;
    auto on = [](int note, int channel, std::uint32_t frame = 0) {
        MidiEvent e = noteOn(note, 100, frame)[0];
        e.channel = static_cast<std::uint8_t>(channel);
        return e;
    };
    MidiEvent bendOther;
    bendOther.frame = 10; bendOther.type = MidiEvent::Type::PitchBend; bendOther.bend = 8191; bendOther.channel = 1;

    const Render a = render(p, {on(60, 0)}, 1 << 15);
    const Render b = render(p, {on(60, 0), bendOther}, 1 << 15);
    double d = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
    check("a bend on channel 2 leaves a note on channel 1 untouched", d == 0.0);

    const Render c = render(p, {on(60, 1), bendOther}, 1 << 15);
    d = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - c.l[i])));
    check("... but moves a note on channel 2", d > 1e-3);

    // The same note on two channels is two voices; a note-off releases only its own channel.
    p.releaseMs = 20.0f;
    MidiEvent off = noteOffEvent(60, 500);
    off.channel = 1;
    const Render e = render(p, {on(60, 0), on(60, 1), off}, 1 << 15);
    check("the same note on two channels is held independently", activeVoices(e.state) == 1);
}

int main()
{
    testLoopPhaseDelayIsOnePeriod();
    testPitchAccuracy();
    testDecayTimeMatchesTheControl();
    testDampingKillsHighPartialsFirst();
    testStiffnessSharpensThePartials();
    testExcitationShapes();
    testPickPositionNulls();
    testHammerBrightnessAndPickup();
    testBow();
    testVoicesAndMidi();
    testDeadStringsFreeTheirVoices();
    testEveryControlMatters();
    testStabilityAndBounds();
    testDenormalsAndReset();
    testParameterTableMatchesCore();
    testBendAndNoteOffArePerChannel();
    std::printf("monochord_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
