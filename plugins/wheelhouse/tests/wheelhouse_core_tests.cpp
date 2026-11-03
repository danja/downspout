#include "downspout/test_assert.h"
#include "wheelhouse_core.hpp"
#include "wheelhouse_params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace downspout::wheelhouse;

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
constexpr double kPi = 3.14159265358979323846;

struct Timed { std::uint32_t frame; MidiEvent ev; };

static Timed noteOn(std::uint32_t frame, int key, int channel = 0) { return {frame, {0, static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(key), 100}}; }
static Timed noteOff(std::uint32_t frame, int key, int channel = 0) { return {frame, {0, static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(key), 0}}; }
static Timed controller(std::uint32_t frame, int number, int value, int channel = 0)
{
    return {frame, {0, static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)}};
}

struct Out {
    std::vector<float> l, r;
    EngineState state;
};

static Out render(const Parameters& p, const std::vector<Timed>& events, float seconds, std::uint32_t block = 256)
{
    Out o;
    const auto total = static_cast<std::size_t>(seconds * kSr);
    o.l.assign(total, 0.0f);
    o.r.assign(total, 0.0f);
    for (std::size_t pos = 0; pos < total; pos += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, total - pos));
        std::vector<MidiEvent> here;
        for (const auto& te : events)
            if (te.frame >= pos && te.frame < pos + n) { MidiEvent e = te.ev; e.frame = static_cast<std::uint32_t>(te.frame - pos); here.push_back(e); }
        processBlock(o.state, p, kSr, n, here.data(), static_cast<std::uint32_t>(here.size()), o.l.data() + pos, o.r.data() + pos);
    }
    return o;
}

// A patch that makes no sound of its own and leaves everything else neutral, so one control at a time can be isolated.
static Parameters bare()
{
    Parameters p;
    p.upper.fill(0.0f); p.lower.fill(0.0f); p.pedal.fill(0.0f);
    p.percOn = 0.0f; p.vibrato = 0.0f; p.click = 0.0f; p.drive = 0.0f; p.tone = 1.0f; p.volume = 1.0f; p.width = 0.0f;
    return p;
}

static double freqOfMidi(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// Amplitude of the component at `f` over n samples from `start`, measured with a Hann window evaluated at exactly f.
static double toneAmp(const std::vector<float>& x, std::size_t start, std::size_t n, double f)
{
    // Both the window and the probe tone are advanced by a complex rotation per sample (double precision holds over 10^5 steps).
    double re = 0.0, im = 0.0, wsum = 0.0;
    const double dp = 2.0 * kPi * f / static_cast<double>(kSr);
    const double dw = 2.0 * kPi / static_cast<double>(n);
    const double rc = std::cos(dp), rs = std::sin(dp), wc = std::cos(dw), ws = std::sin(dw);
    double pc = std::cos(dp * static_cast<double>(start)), ps = std::sin(dp * static_cast<double>(start));
    double hc = 1.0, hs = 0.0;   // cos and sin of the window phase
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * hc;
        re += x[start + i] * w * pc;
        im -= x[start + i] * w * ps;
        wsum += w;
        const double npc = pc * rc - ps * rs;
        ps = ps * rc + pc * rs;
        pc = npc;
        const double nhc = hc * wc - hs * ws;
        hs = hs * wc + hc * ws;
        hc = nhc;
    }
    return 2.0 * std::hypot(re, im) / wsum;
}

static double peak(const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0) to = x.size();
    double m = 0.0;
    for (std::size_t i = from; i < to; ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
    return m;
}

static double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

static bool allZero(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i < to; ++i) if (x[i] != 0.0f) return false;
    return true;
}

// Window used for steady-state measurements: 0.4 s starting at 0.4 s.
static double steady(const Out& o, double f) { return toneAmp(o.l, 19200, 19200, f); }

// Wheels: the footage offsets and foldback, against a hand-written table (MIDI notes of the wheel each footage plays).
static void testWheelMapping()
{
    const int low[9] = {24, 31, 36, 48, 55, 60, 64, 67, 72};        // key 36 (C2): 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'
    const int high[9] = {84, 91, 96, 108, 103, 108, 112, 103, 108}; // key 96 (C7), folded back below the top wheel (114)
    bool ok = true;
    for (int f = 0; f < kDrawbarCount; ++f) {
        if (wheelFor(36, kFootageSemitones[static_cast<std::size_t>(f)]) + 24 != low[f]) ok = false;
        if (wheelFor(96, kFootageSemitones[static_cast<std::size_t>(f)]) + 24 != high[f]) ok = false;
    }
    check("footage wheels at the bottom and top of the manual", ok);
    check("pedal footages", wheelFor(36, kPedalSemitones[0]) + 24 == 24 && wheelFor(36, kPedalSemitones[1]) + 24 == 36 && wheelFor(67, kPedalSemitones[1]) + 24 == 67);
    check("wheel 0 is C1, wheel 45 is A440", std::fabs(wheelFrequency(0) - 32.7032) < 0.001 && std::fabs(wheelFrequency(45) - 440.0) < 1e-9);
    check("the top wheel", kLastWheelNote == 114 && wheelFor(114, 0) == 90 && wheelFor(115, 0) == 79);
    bool inRange = true;
    for (int key = kLowestKey; key <= kHighestKey; ++key)
        for (int f = 0; f < kDrawbarCount; ++f) {
            const int w = wheelFor(key, kFootageSemitones[static_cast<std::size_t>(f)]);
            if (w < 0 || w >= kWheelCount) inRange = false;
        }
    check("every key and footage maps to one of the 91 wheels", inRange);
    check("key ranges", keyInRange(kUpper, 36) && keyInRange(kUpper, 96) && !keyInRange(kUpper, 35) && !keyInRange(kLower, 97)
          && keyInRange(kPedal, 67) && !keyInRange(kPedal, 68));
    check("drawbar amplitudes: off, 3 dB steps, 1.0 at 8", drawbarAmplitude(0) == 0.0f && drawbarAmplitude(8) == 1.0f
          && std::fabs(drawbarAmplitude(5) - std::pow(10.0f, -9.0f / 20.0f)) < 1e-6f);
}

// Each footage sounds at its own pitch, and only there.
static void testFootagePitches()
{
    for (int f = 0; f < kDrawbarCount; ++f) {
        Parameters p = bare();
        p.upper[static_cast<std::size_t>(f)] = 8.0f;
        const Out o = render(p, {noteOn(0, 48)}, 1.0f);
        const double want = freqOfMidi(48 + kFootageSemitones[static_cast<std::size_t>(f)]);
        const double a = steady(o, want);
        bool alone = a > 0.001;
        for (int g = 0; g < kDrawbarCount; ++g)
            if (g != f && std::fabs(freqOfMidi(48 + kFootageSemitones[static_cast<std::size_t>(g)]) - want) > 20.0)
                if (steady(o, freqOfMidi(48 + kFootageSemitones[static_cast<std::size_t>(g)])) > a * 0.01) alone = false;
        check("a footage sounds at its own pitch only", alone);
    }
}

// Every drawbar position across its range: 3 dB per step, and 0 is silent.
static void testDrawbarSteps()
{
    Parameters ref = bare();
    ref.upper[2] = 8.0f;
    const double full = steady(render(ref, {noteOn(0, 48)}, 1.0f), freqOfMidi(48));
    check("a full 8' drawbar is audible", full > 0.01);
    bool ok = true;
    for (int pos = 1; pos <= 8; ++pos) {
        Parameters p = bare();
        p.upper[2] = static_cast<float>(pos);
        const double a = steady(render(p, {noteOn(0, 48)}, 1.0f), freqOfMidi(48));
        const double want = std::pow(10.0, (pos - 8) * 3.0 / 20.0);
        if (std::fabs(a / full - want) > 0.02 * want) ok = false;
    }
    check("every drawbar position is 3 dB below the next", ok);
    Parameters off = bare();
    check("drawbar 0 is silent", allZero(render(off, {noteOn(0, 48)}, 0.5f).l, 0, 24000));

    // The top of the keyboard, where footages fold back: each plays its folded wheel at the same level as an 8' would.
    const int folded[9] = {84, 91, 96, 108, 103, 108, 112, 103, 108};
    bool foldOk = true;
    Parameters r96 = bare(); r96.upper[2] = 8.0f;
    const double ref96 = steady(render(r96, {noteOn(0, 96)}, 1.0f), freqOfMidi(96));
    for (int f = 0; f < kDrawbarCount; ++f) {
        Parameters p = bare();
        p.upper[static_cast<std::size_t>(f)] = 8.0f;
        const double a = steady(render(p, {noteOn(0, 96)}, 1.0f), freqOfMidi(folded[f]));
        if (std::fabs(a / ref96 - 1.0) > 0.03) foldOk = false;
    }
    check("footages above the top wheel fold back by octaves", foldOk);
}

// Divisions: each answers its own MIDI channel and drawbars.
static void testDivisions()
{
    Parameters p = bare();
    p.upper[2] = 8.0f;     // upper 8'  : f
    p.lower[3] = 8.0f;     // lower 4'  : 2f
    p.pedal[0] = 8.0f;     // pedal 16' : f/2
    const double f = freqOfMidi(48);
    const Out up = render(p, {noteOn(0, 48, 0)}, 1.0f);
    check("upper channel plays the upper drawbars", steady(up, f) > 0.01 && steady(up, 2 * f) < 1e-4 && steady(up, f / 2) < 1e-4);
    const Out lo = render(p, {noteOn(0, 48, 1)}, 1.0f);
    check("lower channel plays the lower drawbars", steady(lo, 2 * f) > 0.01 && steady(lo, f) < 1e-4);
    const Out pd = render(p, {noteOn(0, 48, 2)}, 1.0f);
    check("pedal channel plays the pedal drawbars", steady(pd, f / 2) > 0.01 && steady(pd, f) < 1e-4);
    check("another channel plays nothing", allZero(render(p, {noteOn(0, 48, 5)}, 0.5f).l, 0, 24000));
    Parameters off = p; off.upperChannel = 0.0f;
    check("channel 0 turns a division off", allZero(render(off, {noteOn(0, 48, 0)}, 0.5f).l, 0, 24000));
    Parameters same = p; same.lowerChannel = 1.0f;
    const Out both = render(same, {noteOn(0, 48, 0)}, 1.0f);
    check("two divisions on one channel both play", steady(both, f) > 0.01 && steady(both, 2 * f) > 0.01);

    // Keys outside a division's range are ignored; the edges play.
    check("key below the manual is ignored", allZero(render(p, {noteOn(0, 35, 0)}, 0.5f).l, 0, 24000));
    check("key above the manual is ignored", allZero(render(p, {noteOn(0, 97, 0)}, 0.5f).l, 0, 24000));
    check("the lowest and highest keys play", peak(render(p, {noteOn(0, 36, 0)}, 0.5f).l) > 0.001 && peak(render(p, {noteOn(0, 96, 0)}, 0.5f).l) > 0.001);
    check("pedal range ends at G4", peak(render(p, {noteOn(0, 67, 2)}, 0.5f).l) > 0.001 && allZero(render(p, {noteOn(0, 68, 2)}, 0.5f).l, 0, 24000));
}

// Releasing, all-notes-off and the swell and volume controls.
static void testReleaseSwellVolume()
{
    Parameters p = bare();
    p.upper[2] = 8.0f;
    const Out o = render(p, {noteOn(0, 48), noteOff(24000, 48)}, 1.0f);
    check("sound before the release", peak(o.l, 12000, 24000) > 0.01);
    check("silent (exactly) 150 ms after the release", allZero(o.l, 24000 + 7200, o.l.size()));
    const Out cc = render(p, {noteOn(0, 48), noteOn(0, 52), controller(24000, 123, 0)}, 1.0f);
    check("all-notes-off clears the keys", allZero(cc.l, 24000 + 7200, cc.l.size()));
    const Out other = render(p, {noteOn(0, 48), controller(24000, 123, 0, 4)}, 1.0f);
    check("all-notes-off on another channel does not", peak(other.l, 40000, 48000) > 0.01);
    const Out twice = render(p, {noteOn(0, 48), noteOn(100, 48), noteOff(24000, 48)}, 1.0f);
    check("a repeated note-on is one key", allZero(twice.l, 24000 + 7200, twice.l.size()));

    const double full = steady(render(p, {noteOn(0, 48)}, 1.0f), freqOfMidi(48));
    const double half = steady(render(p, {controller(0, 11, 64), noteOn(0, 48)}, 1.0f), freqOfMidi(48));
    check("CC 11 swell is squared: 64 is -11.9 dB", std::fabs(half / full - std::pow(64.0 / 127.0, 2.0)) < 0.02);
    check("CC 11 at 0 is silent", allZero(render(p, {controller(0, 11, 0), noteOn(0, 48)}, 0.5f).l, 0, 24000));
    check("swell works from any channel", std::fabs(steady(render(p, {controller(0, 11, 64, 9), noteOn(0, 48)}, 1.0f), freqOfMidi(48)) / full - 0.2539) < 0.02);
    Parameters v = p; v.volume = 0.35f;
    check("Volume scales the output", std::fabs(steady(render(v, {noteOn(0, 48)}, 1.0f), freqOfMidi(48)) / full - 0.35) < 0.01);
    Parameters v0 = p; v0.volume = 0.0f;
    check("Volume 0 is silent", allZero(render(v0, {noteOn(0, 48)}, 0.5f).l, 0, 24000));
    Parameters st = p; st.width = 1.0f;
    check("L and R are identical without a scanner", render(st, {noteOn(0, 48)}, 0.3f).l == render(st, {noteOn(0, 48)}, 0.3f).r);
}

// Percussion: the second or third harmonic with a decay, fired once per legato run, and the 1' drawbar is silenced.
static void testPercussion()
{
    Parameters p = bare();
    p.percOn = 1.0f; p.percHarmonic = 1.0f; p.percDecay = 0.0f; p.percLevel = 1.0f;
    const double third = freqOfMidi(48 + 19), second = freqOfMidi(48 + 12);
    const Out fast = render(p, {noteOn(0, 48)}, 1.5f);
    const double early = toneAmp(fast.l, 960, 2400, third), late = toneAmp(fast.l, 48000, 2400, third);
    check("percussion sounds the third harmonic", early > 0.005 && toneAmp(fast.l, 960, 2400, second) < early * 0.05);
    check("percussion decays", late < early * 0.01);
    // Decay time: -60 dB over 0.35 s (fast), 1.5 s (slow): the level at time t is 10^(-3 t / T).
    check("fast decay follows -60 dB in 0.35 s", std::fabs(toneAmp(fast.l, 6000, 2400, third) / toneAmp(fast.l, 960, 2400, third)
          - std::pow(10.0, -3.0 * ((6000 - 960) / 48000.0) / 0.35)) < 0.03);
    Parameters slow = p; slow.percDecay = 1.0f;
    const Out s = render(slow, {noteOn(0, 48)}, 1.5f);
    check("slow decay lasts longer", toneAmp(s.l, 48000, 2400, third) > late * 20.0);
    Parameters sec = p; sec.percHarmonic = 0.0f;
    const Out so = render(sec, {noteOn(0, 48)}, 0.5f);
    check("percussion can sound the second harmonic", toneAmp(so.l, 960, 2400, second) > 0.005 && toneAmp(so.l, 960, 2400, third) < 0.0005);
    Parameters soft = p; soft.percLevel = 0.0f;
    check("soft percussion is half the level", std::fabs(toneAmp(render(soft, {noteOn(0, 48)}, 0.5f).l, 960, 2400, third) / early - 0.5) < 0.02);
    Parameters offP = p; offP.percOn = 0.0f;
    check("percussion off is silent", allZero(render(offP, {noteOn(0, 48)}, 0.5f).l, 0, 24000));

    // Single trigger: a key pressed while another is held does not fire it again.
    const Out legato = render(p, {noteOn(0, 48), noteOn(96000, 52)}, 2.5f);
    const Out detached = render(p, {noteOn(0, 48), noteOff(48000, 48), noteOn(96000, 52)}, 2.5f);
    const double third52 = freqOfMidi(52 + 19);
    check("a legato key does not retrigger percussion", toneAmp(legato.l, 96000 + 960, 2400, third52) < early * 0.01);
    check("a detached key does", toneAmp(detached.l, 96000 + 960, 2400, third52) > early * 0.5);

    // The 1' drawbar is cut while percussion is on.
    Parameters one = bare(); one.upper[8] = 8.0f;
    const double f1 = freqOfMidi(48 + 36);
    check("1' sounds without percussion", steady(render(one, {noteOn(0, 48)}, 1.0f), f1) > 0.01);
    one.percOn = 1.0f;
    check("percussion silences the 1' drawbar", steady(render(one, {noteOn(0, 48)}, 1.0f), f1) < 1e-4);
    // Percussion belongs to the upper manual alone.
    Parameters lower = p; lower.lower[2] = 8.0f;
    const Out lo = render(lower, {noteOn(0, 48, 1)}, 0.5f);
    check("the lower manual has no percussion", toneAmp(lo.l, 960, 2400, third) < 0.0005);
}

// Scanner vibrato/chorus: sidebands of a steady tone at +-rate, against the FM expectation.
static void testScanner()
{
    const double f = freqOfMidi(60);
    Parameters base = bare();
    base.upper[2] = 8.0f;
    const auto sideRatio = [&](const Parameters& p, double rate) {
        const Out o = render(p, {noteOn(0, 60)}, 2.0f);
        return toneAmp(o.l, 24000, 48000, f + rate) / toneAmp(o.l, 24000, 48000, f);
    };
    const double off = sideRatio(base, 6.9);
    check("no sidebands with the scanner off", off < 1e-3);
    double ratios[3];
    for (int m = 1; m <= 3; ++m) {
        Parameters p = base; p.vibrato = static_cast<float>(m); p.vibUpper = 1.0f;
        ratios[m - 1] = sideRatio(p, 6.9);
    }
    // Triangle delay swing +-D: phase deviation 2 pi f D, first harmonic of a triangle 8/pi^2; sideband/carrier = J1(b) / J0(b).
    const auto expected = [&](double depthMs) {
        const double b = 8.0 / (kPi * kPi) * 2.0 * kPi * f * depthMs * 0.001;
        double j0 = 0.0, j1 = 0.0;
        for (int k = 0; k < 20; ++k) {
            const double term = std::pow(-1.0, k) / (std::tgamma(k + 1.0) * std::tgamma(k + 1.0)) * std::pow(b / 2.0, 2 * k);
            j0 += term;
            j1 += std::pow(-1.0, k) / (std::tgamma(k + 1.0) * std::tgamma(k + 2.0)) * std::pow(b / 2.0, 2 * k + 1);
        }
        return j1 / j0;
    };
    for (int m = 0; m < 3; ++m)
        check("vibrato depth matches the FM sidebands", std::fabs(ratios[m] / expected(kScannerDepthMs[static_cast<std::size_t>(m)]) - 1.0) < 0.15);
    check("V1 < V2 < V3", ratios[0] < ratios[1] && ratios[1] < ratios[2]);

    Parameters c3 = base; c3.vibrato = 6.0f;
    const double chorus = sideRatio(c3, 6.9);
    check("chorus has sidebands, fewer than the same vibrato", chorus > 0.03 && chorus < ratios[2]);
    Parameters v1c = base; v1c.vibrato = 4.0f;
    check("C1 is the V1 depth", sideRatio(v1c, 6.9) < chorus);

    Parameters rate5 = base; rate5.vibrato = 3.0f; rate5.scannerRate = 5.0f;
    check("Scanner Rate moves the sidebands", sideRatio(rate5, 5.0) > 5.0 * sideRatio(rate5, 8.0) && sideRatio(rate5, 5.0) > 0.1);

    Parameters notRouted = base; notRouted.vibrato = 3.0f; notRouted.vibUpper = 0.0f;
    check("Vibrato Upper off bypasses the scanner", sideRatio(notRouted, 6.9) < 1e-3);
    Parameters lower = bare(); lower.lower[2] = 8.0f; lower.vibrato = 3.0f; lower.vibLower = 1.0f;
    const Out lo = render(lower, {noteOn(0, 60, 1)}, 2.0f);
    check("Vibrato Lower scans the lower manual", toneAmp(lo.l, 24000, 48000, f + 6.9) / toneAmp(lo.l, 24000, 48000, f) > 0.1);
    lower.vibLower = 0.0f;
    const Out lo2 = render(lower, {noteOn(0, 60, 1)}, 2.0f);
    check("the lower manual is not scanned by default", toneAmp(lo2.l, 24000, 48000, f + 6.9) / toneAmp(lo2.l, 24000, 48000, f) < 1e-3);
    Parameters pedal = bare(); pedal.pedal[1] = 8.0f; pedal.vibrato = 3.0f; pedal.vibUpper = 1.0f; pedal.vibLower = 1.0f;
    const Out pd = render(pedal, {noteOn(0, 60, 2)}, 2.0f);
    check("the pedals go round the scanner", toneAmp(pd.l, 24000, 48000, f + 6.9) / toneAmp(pd.l, 24000, 48000, f) < 1e-3);

    Parameters w0 = base; w0.vibrato = 3.0f; w0.width = 0.0f;
    const Out a = render(w0, {noteOn(0, 60)}, 0.5f);
    check("Width 0: left and right are identical", a.l == a.r);
    Parameters w1 = w0; w1.width = 1.0f;
    const Out b = render(w1, {noteOn(0, 60)}, 0.5f);
    check("Width 1: left and right differ", b.l != b.r && std::fabs(rms(b.l, 12000, 24000) - rms(b.r, 12000, 24000)) < 0.1 * rms(b.l, 12000, 24000));
}

// Key click: a short burst on every key movement, none when off, linear in the amount.
static void testKeyClick()
{
    Parameters p = bare();
    check("no click, no drawbars: silent", allZero(render(p, {noteOn(0, 48), noteOff(24000, 48)}, 1.0f).l, 0, 48000));
    p.click = 0.5f;
    const Out o = render(p, {noteOn(1000, 48), noteOff(24000, 48)}, 1.0f);
    check("key-on click", peak(o.l, 1000, 1000 + 480) > 1e-3);
    check("the click is over in 30 ms", allZero(o.l, 1000 + 1440 + 600, 24000));
    check("key-off click", peak(o.l, 24000, 24000 + 480) > 1e-3 && allZero(o.l, 24000 + 3000, o.l.size()));
    Parameters q = p; q.click = 1.0f;
    const Out o2 = render(q, {noteOn(1000, 48)}, 0.1f);
    check("click is linear in its amount", std::fabs(peak(o2.l, 1000, 1480) / peak(o.l, 1000, 1480) - 2.0) < 0.02);
    check("no click before the key", allZero(o.l, 0, 1000));
}

// Overdrive: a linear signal path at 0, harmonics above it, bounded output.
static void testDrive()
{
    Parameters p = bare();
    p.upper.fill(8.0f);
    p.volume = 1.0f;
    const double f = freqOfMidi(36);
    const auto thd = [&](float drive) {
        Parameters q = p; q.drive = drive;
        const Out o = render(q, {noteOn(0, 36)}, 1.0f);
        // Every footage is in the signal, so measure at 3.5 f: no footage plays there (they sit at 0.5, 0.75, 1, 2, 3, 4, 5, 6, 8 f),
        // so any energy at it is distortion.
        return toneAmp(o.l, 19200, 19200, f * 3.5);
    };
    const double clean = thd(0.0f), driven = thd(1.0f);
    check("the clean path adds no harmonics", clean < 1e-5);
    check("Drive adds harmonics", driven > 50.0 * std::max(clean, 1e-6) && driven > 1e-4);
    Parameters hot = p; hot.drive = 1.0f;
    const Out o = render(hot, {noteOn(0, 36), noteOn(0, 40), noteOn(0, 43), noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)}, 1.0f);
    check("driven output is bounded by 1 / k", peak(o.l) <= 1.0 / (1.0 + 7.0) + 1e-6);
}

// Tone: a one-pole low-pass at 1.5 kHz when fully down; transparent when open.
static void testTone()
{
    const double c = 1.0 - std::exp(-2.0 * kPi * 1500.0 / kSr);
    const auto mag = [&](double f) {
        const double w = 2.0 * kPi * f / kSr;
        return c / std::sqrt(1.0 - 2.0 * (1.0 - c) * std::cos(w) + (1.0 - c) * (1.0 - c));
    };
    for (const int key : {96, 84, 60, 36}) {
        Parameters open = bare(); open.upper[2] = 8.0f;
        Parameters dull = open; dull.tone = 0.0f;
        const double f = freqOfMidi(key);
        const double ratio = steady(render(dull, {noteOn(0, key)}, 1.0f), f) / steady(render(open, {noteOn(0, key)}, 1.0f), f);
        check("Tone 0 is a 1.5 kHz one-pole low-pass", std::fabs(ratio / mag(f) - 1.0) < 0.03);
    }
}

// The level: a typical registration sits in a sane band, and a handful of keys with every drawbar out cannot overload.
static void testLevels()
{
    Parameters jazz;
    jazz.upper = {8, 8, 8, 0, 0, 0, 0, 0, 0};
    const Out o = render(jazz, {noteOn(0, 48), noteOn(0, 52), noteOn(0, 55), noteOn(0, 60)}, 1.0f);
    const double pk = peak(o.l, 12000, 48000), r = rms(o.l, 12000, 48000);
    std::printf("  level probe: jazz chord peak %.3f rms %.3f\n", pk, r);
    check("Jazz chord level sits in a sane band", pk > 0.1 && pk < 0.5 && r > 0.04);
    Parameters full = jazz; full.upper.fill(8.0f);
    const Out f = render(full, {noteOn(0, 36), noteOn(0, 40), noteOn(0, 43), noteOn(0, 48), noteOn(0, 52), noteOn(0, 55), noteOn(0, 60)}, 1.0f);
    std::printf("  level probe: all drawbars, seven keys peak %.3f\n", peak(f.l, 12000, 48000));
    check("every drawbar and seven keys stay below full scale", peak(f.l) < 1.0);
    // All 61 keys on all three manuals at full drawbars: finite and bounded.
    Parameters all = full; all.lower.fill(8.0f); all.pedal.fill(8.0f); all.percOn = 1.0f; all.drive = 1.0f; all.vibrato = 6.0f; all.vibLower = 1.0f;
    std::vector<Timed> ev;
    for (int k = 36; k <= 96; ++k) { ev.push_back(noteOn(0, k, 0)); ev.push_back(noteOn(0, k, 1)); if (k <= 67) ev.push_back(noteOn(0, k, 2)); }
    const Out big = render(all, ev, 1.0f);
    bool finite = true;
    for (float v : big.l) if (!std::isfinite(v)) finite = false;
    check("all keys, all drawbars: finite and bounded", finite && peak(big.l) <= 1.0 / 8.0 + 1e-6);
}

// Determinism and block-size independence: the render is a function of the events and the sample position only.
static void testDeterminism()
{
    Parameters p;
    p.percOn = 1.0f; p.vibrato = 5.0f; p.vibLower = 1.0f; p.click = 0.4f; p.drive = 0.4f; p.tone = 0.6f; p.width = 0.8f;
    const std::vector<Timed> ev = {noteOn(100, 48), noteOn(3000, 55), noteOn(5000, 60, 1), controller(7000, 11, 90), noteOff(9000, 48), noteOn(12000, 72, 2)};
    const Out ref = render(p, ev, 0.6f, 256);
    for (const std::uint32_t block : {1u, 7u, 64u, 333u, 4096u}) {
        const Out o = render(p, ev, 0.6f, block);
        check("the render does not depend on the block size", o.l == ref.l && o.r == ref.r);
    }
    check("the render repeats exactly", render(p, ev, 0.6f, 256).l == ref.l);
    check("the render is not silent", peak(ref.l) > 0.01);
}

// Pulling a drawbar while a key is held takes effect on the held key, and a parameter change keeps the key down.
static void testLiveChanges()
{
    Parameters p = bare();
    p.upper[2] = 8.0f;
    Out o;
    o.l.assign(48000, 0.0f); o.r.assign(48000, 0.0f);
    MidiEvent on; on.status = 0x90; on.data1 = 48; on.data2 = 100;
    processBlock(o.state, p, kSr, 24000, &on, 1, o.l.data(), o.r.data());
    p.upper[2] = 4.0f;
    processBlock(o.state, p, kSr, 24000, nullptr, 0, o.l.data() + 24000, o.r.data() + 24000);
    const double before = toneAmp(o.l, 12000, 8000, freqOfMidi(48)), after = toneAmp(o.l, 40000, 8000, freqOfMidi(48));
    check("a drawbar moved on a held key takes effect", std::fabs(after / before - std::pow(10.0, -12.0 / 20.0)) < 0.03);
    check("the key stays down across the change", after > 0.001);
}

// The real-time cost: every key down on every manual renders much faster than real time.
static void testCost()
{
    Parameters all;
    all.upper.fill(8.0f); all.lower.fill(8.0f); all.pedal.fill(8.0f); all.percOn = 1.0f; all.vibrato = 6.0f; all.vibLower = 1.0f;
    std::vector<Timed> ev;
    for (int k = 36; k <= 96; ++k) { ev.push_back(noteOn(0, k, 0)); ev.push_back(noteOn(0, k, 1)); }
    const auto t0 = std::chrono::steady_clock::now();
    const Out o = render(all, ev, 10.0f, 512);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  cost probe: 10 s of 122 held keys rendered in %.2f s\n", secs);
    // The time bound is meaningless under a sanitizer build (several times slower), so it is skipped there; the output is still checked.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    const bool fastEnough = true;
#else
    const bool fastEnough = secs < 5.0;
#endif
    check("122 held keys render faster than real time", fastEnough && peak(o.l) > 0.01);
}

// Idle behaviour and reset: nothing keeps ringing, nothing denormal.
static void testIdleAndReset()
{
    Parameters p;
    p.click = 1.0f; p.drive = 1.0f; p.tone = 0.2f; p.vibrato = 3.0f;
    const Out o = render(p, {noteOn(0, 48), noteOff(12000, 48)}, 3.0f);
    check("a long idle is exactly silent", allZero(o.l, 24000, o.l.size()) && allZero(o.r, 24000, o.r.size()));
    Out r = render(p, {noteOn(0, 48)}, 0.3f);
    resetState(r.state);
    std::vector<float> l(4800), rr(4800);
    processBlock(r.state, p, kSr, 4800, nullptr, 0, l.data(), rr.data());
    check("reset releases every key", allZero(l, 2400, 4800));
    std::vector<float> z(16, 1.0f);
    processBlock(r.state, p, 0.0f, 16, nullptr, 0, z.data(), z.data());
    check("a zero sample rate gives silence", allZero(z, 0, 16));
}

static void testParameters()
{
    Parameters wild;
    wild.upper[0] = 12.0f; wild.upper[1] = -3.0f; wild.upper[2] = 4.4f; wild.pedal[1] = 99.0f;
    wild.percOn = 7.0f; wild.vibrato = 40.0f; wild.scannerRate = 100.0f; wild.click = -2.0f; wild.drive = 5.0f;
    wild.upperChannel = 33.0f; wild.lowerChannel = -1.0f;
    const Parameters c = clampParameters(wild);
    check("clamp drawbars", c.upper[0] == 8.0f && c.upper[1] == 0.0f && c.upper[2] == 4.0f && c.pedal[1] == 8.0f);
    check("clamp switches and modes", c.percOn == 1.0f && c.vibrato == 6.0f && c.scannerRate == kMaxScannerRate);
    check("clamp amounts and channels", c.click == 0.0f && c.drive == 1.0f && c.upperChannel == 16.0f && c.lowerChannel == 0.0f);

    const Parameters d;
    bool ok = true;
    std::set<std::string> symbols;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto& s = specs()[i];
        if (field(d, i) != s.defaultValue || s.defaultValue < s.minimum || s.defaultValue > s.maximum) ok = false;
        symbols.insert(s.symbol);
        Parameters t;
        setField(t, i, s.maximum);
        if (field(t, i) != s.maximum) ok = false;
    }
    check("table defaults match the core and sit in range; setField round-trips", ok);
    check("every parameter has its own symbol", symbols.size() == kParameterCount);
    check("parameter ids are pinned", kUpperDrawbar0 == 0 && kLowerDrawbar0 == 9 && kPedalDrawbar0 == 18 && kPercOn == 20 && kVibrato == 24
          && kScannerRate == 27 && kVolume == 32 && kPedalChannel == 35 && kParameterCount == 36);
    check("default registration is 888000000 on the upper manual", d.upper[0] == 8 && d.upper[1] == 8 && d.upper[2] == 8 && d.upper[3] == 0);
    check("symbols are named after the footage", std::string(specs()[kUpperDrawbar0 + 4].symbol) == "upper_2_2_3"
          && std::string(specs()[kLowerDrawbar0 + 8].name) == "Lower 1'");
}

int main()
{
    testWheelMapping();
    testFootagePitches();
    testDrawbarSteps();
    testDivisions();
    testReleaseSwellVolume();
    testPercussion();
    testScanner();
    testKeyClick();
    testDrive();
    testTone();
    testLevels();
    testDeterminism();
    testLiveChanges();
    testCost();
    testIdleAndReset();
    testParameters();
    std::printf("wheelhouse core tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
