#include "strata_core.hpp"
#include "strata_params.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::strata;

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

// Hann-windowed power spectrum of the last `n` samples of a signal (n a power of two).
static std::vector<double> powerSpectrum(const std::vector<float>& x, size_t n)
{
    std::vector<Cx> a(n);
    const size_t off = x.size() - n;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n));
        a[i] = static_cast<double>(x[off + i]) * w;
    }
    fft(a);
    std::vector<double> p(n / 2);
    for (size_t i = 0; i < n / 2; ++i) p[i] = std::norm(a[i]);
    return p;
}

// Peak frequency by parabolic interpolation of the log spectrum.
static double peakFrequency(const std::vector<float>& x, float sr, size_t n = 1 << 16)
{
    const auto p = powerSpectrum(x, n);
    size_t best = 2;
    for (size_t i = 2; i < p.size() - 2; ++i) if (p[i] > p[best]) best = i;
    const double a = std::log(p[best - 1] + 1e-30), b = std::log(p[best] + 1e-30), c = std::log(p[best + 1] + 1e-30);
    const double delta = 0.5 * (a - c) / (a - 2.0 * b + c);
    return (static_cast<double>(best) + delta) * sr / static_cast<double>(n);
}

// Fraction of the power that lies away from the harmonics of f0 (aliases and noise), in dB.
static double inharmonicDb(const std::vector<float>& x, float sr, double f0, size_t n = 1 << 16)
{
    const auto p = powerSpectrum(x, n);
    std::vector<char> harmonic(p.size(), 0);
    // DC counts as harmonic 0 (its window leakage reaches a few bins), and a Hann window leaks further than +-4 bins.
    for (size_t i = 0; i <= 16 && i < harmonic.size(); ++i) harmonic[i] = 1;
    for (double f = f0; f < sr * 0.5; f += f0) {
        const long bin = std::lround(f * static_cast<double>(n) / sr);
        for (long d = -16; d <= 16; ++d)
            if (bin + d >= 0 && bin + d < static_cast<long>(p.size())) harmonic[static_cast<size_t>(bin + d)] = 1;
    }
    double total = 0, other = 0;
    for (size_t i = 1; i < p.size(); ++i) {
        total += p[i];
        if (!harmonic[i]) other += p[i];
    }
    return 10.0 * std::log10(other / total + 1e-30);
}

static std::vector<MidiEvent> noteOn(int note, int vel, std::uint32_t frame = 0)
{
    MidiEvent e;
    e.frame = frame;
    e.type = MidiEvent::Type::NoteOn;
    e.data1 = static_cast<std::uint8_t>(note);
    e.data2 = static_cast<std::uint8_t>(vel);
    return {e};
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

static MidiEvent noteOffEvent(int note, std::uint32_t frame)
{
    MidiEvent e;
    e.frame = frame;
    e.type = MidiEvent::Type::NoteOff;
    e.data1 = static_cast<std::uint8_t>(note);
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

// A clean single-voice patch: pure sine, one oscillator, no extras, a held note at unity.
static Parameters clean()
{
    Parameters p;
    p.osc[0].table = 0.0f; p.osc[0].position = 0.0f; p.osc[0].level = 0.8f;
    p.osc[1].level = 0.0f;
    p.subLevel = 0.0f;
    p.unisonVoices = 1.0f; p.unisonAmount = 0.0f;
    p.filterType = 0.0f; p.cutoffHz = 20000.0f; p.resonance = 0.0f;
    p.ampAttackMs = 0.5f; p.ampDecayMs = 1.0f; p.ampSustain = 1.0f; p.ampReleaseMs = 5.0f;
    p.velocity = 0.0f;
    return p;
}

static double rms(const std::vector<float>& v, size_t from)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
    return std::sqrt(s / static_cast<double>(v.size() - from));
}

static double noteHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// ── Tables ─────────────────────────────────────────────────────────────────

static void testTableDefinitions()
{
    const Bank& bk = bank();
    const auto sine = bk.spectrum(0, 0, 0);
    double others = 0;
    for (size_t h = 2; h < sine.size(); ++h) others = std::max(others, sine[h]);
    check("Basic frame 0 is a pure sine", others < 1e-5 && std::fabs(sine[1] - 0.8) < 2e-3);

    const auto square = bk.spectrum(0, 7, 0);
    bool oneOverH = true, noEven = true;
    for (int h = 1; h < 60; h += 2) if (std::fabs(square[static_cast<size_t>(h)] * h / square[1] - 1.0) > 0.02) oneOverH = false;
    for (int h = 2; h < 60; h += 2) if (square[static_cast<size_t>(h)] > 1e-3 * square[1]) noEven = false;
    check("Basic frame 7 is a square: odd harmonics fall as 1/h", oneOverH);
    check("... and it has no even harmonics", noEven);

    const auto saw = bk.spectrum(0, 4, 0);  // part-way between triangle and saw: both parities present, falling
    check("a mid Basic frame has falling harmonics", saw[1] > saw[2] && saw[2] > saw[4]);
}

static void testEveryFrameIsNormalised()
{
    const Bank& bk = bank();
    bool ok = true;
    for (int c = 0; c < kCategories; ++c)
        for (int f = 0; f < kFrames; ++f) {
            const float* d = bk.frame(c, f, 0);
            float peak = 0;
            for (int i = 1; i <= kTableSize; ++i) peak = std::max(peak, std::fabs(d[i]));
            if (std::fabs(peak - 0.8f) > 2e-3f) { ok = false; std::printf("  cat %d frame %d peak %.4f\n", c, f, static_cast<double>(peak)); }
        }
    check("every frame peaks at 0.8, so the level does not move with the position", ok);
}

static void testMipsAreBandLimited()
{
    const Bank& bk = bank();
    bool ok = true;
    for (int c = 0; c < kCategories; ++c)
        for (int f : {0, 3, 7})
            for (int m = 0; m < kMips; ++m) {
                const auto s = bk.spectrum(c, f, m);
                const int limit = Bank::harmonicLimit(m);
                for (size_t h = static_cast<size_t>(limit) + 2; h < s.size(); ++h)
                    if (s[h] > 2e-5) { ok = false; }
            }
    check("no mip holds a harmonic above its limit", ok);
    check("harmonic limits halve every two mips", Bank::harmonicLimit(0) == 1024 && Bank::harmonicLimit(2) == 512 && Bank::harmonicLimit(20) == 1);
    check("the mip for a harmonic count never exceeds it", Bank::mipForHarmonics(1024.0) == 0 && Bank::harmonicLimit(Bank::mipForHarmonics(700.0)) <= 700 &&
          Bank::mipForHarmonics(0.5) == kMips - 1);
    bool never = true;
    for (double f = 8.0; f < 20000.0; f *= 1.01) {
        const int m = Bank::mipForHarmonics((kSr * 0.5) / f);
        if (m < kMips - 1 && Bank::harmonicLimit(m) * f > kSr * 0.5 + 1e-6) never = false;
    }
    check("a read at any pitch from 8 Hz to 20 kHz keeps every harmonic under Nyquist", never);
}

static void testReadsAreExactAndContinuous()
{
    const Bank& bk = bank();
    bool exact = true;
    for (int c = 0; c < kCategories; ++c) {
        const float* d = bk.frame(c, 3, 4);
        for (int i : {0, 17, 511, 1024, 2047})
            if (std::fabs(bk.read(c, 3.0 / 7.0, 4, static_cast<double>(i) / kTableSize) - d[i + 1]) > 1e-6f) exact = false;
    }
    check("a read on a sample point returns that sample", exact);
    double worstPhase = 0, worstPos = 0;
    float last = bk.read(0, 0.4, 6, 0.0);
    for (int i = 1; i <= 8192; ++i) {
        const float v = bk.read(0, 0.4, 6, static_cast<double>(i) / 8192.0);
        worstPhase = std::max(worstPhase, static_cast<double>(std::fabs(v - last)));
        last = v;
    }
    last = bk.read(2, 0.0, 3, 0.31);
    for (int i = 1; i <= 4000; ++i) {
        const float v = bk.read(2, static_cast<double>(i) / 4000.0, 3, 0.31);
        worstPos = std::max(worstPos, static_cast<double>(std::fabs(v - last)));
        last = v;
    }
    check("a read is continuous along the phase", worstPhase < 0.02);
    check("a read is continuous across the position", worstPos < 0.01);
}

// ── Oscillator ─────────────────────────────────────────────────────────────

static void testPitchAccuracy()
{
    double worst = 0;
    for (int note : {36, 48, 60, 69, 81, 93, 100}) {
        const Render r = render(clean(), noteOn(note, 100), 1 << 17);
        const double f = peakFrequency(r.l, kSr, 1 << 17);
        const double cents = 1200.0 * std::log2(f / noteHz(note));
        worst = std::max(worst, std::fabs(cents));
    }
    if (worst >= 0.5) std::printf("  worst pitch error %.3f cents\n", worst);
    check("every note is within half a cent", worst < 0.5);

    Parameters p = clean();
    p.osc[0].transpose = 12.0f;
    check("transpose +12 doubles the frequency", std::fabs(peakFrequency(render(p, noteOn(60, 100), 1 << 17).l, kSr, 1 << 17) / noteHz(60) - 2.0) < 1e-3);
    p.osc[0].transpose = 0.0f;
    p.osc[0].fineCents = 50.0f;
    check("fine +50 cents raises the pitch half a semitone",
          std::fabs(1200.0 * std::log2(peakFrequency(render(p, noteOn(60, 100), 1 << 17).l, kSr, 1 << 17) / noteHz(60)) - 50.0) < 0.5);

    // Pitch bend.
    p = clean();
    p.bendRange = 12.0f;
    std::vector<MidiEvent> ev = noteOn(60, 100);
    MidiEvent b;
    b.frame = 10; b.type = MidiEvent::Type::PitchBend; b.bend = 8191;
    ev.push_back(b);
    const double up = 1200.0 * std::log2(peakFrequency(render(p, ev, 1 << 17).l, kSr, 1 << 17) / noteHz(60));
    check("full pitch bend moves the pitch by the bend range", std::fabs(up - 1200.0) < 2.0);
}

// The reason for the mip chain: a square at high pitch, read naively, aliases heavily.
static std::vector<float> naiveSquare(double freq, size_t n)
{
    const Bank& bk = bank();
    const float* d = bk.frame(0, 7, 0);
    std::vector<float> out(n);
    double ph = 0;
    for (size_t i = 0; i < n; ++i) {
        ph += freq / kSr;
        ph -= std::floor(ph);
        const double x = ph * kTableSize;
        const int k = static_cast<int>(x);
        const float t = static_cast<float>(x - k);
        out[i] = d[k + 1] + t * (d[k + 2] - d[k + 1]);
    }
    return out;
}

static void testAliasingIsAbsent()
{
    Parameters p = clean();
    p.osc[0].position = 1.0f;  // the square frame: 1/h harmonics out to the top
    double worst = -300.0;
    for (int note : {60, 72, 84, 96, 105}) {
        const Render r = render(p, noteOn(note, 100), 1 << 17);
        const double db = inharmonicDb(r.l, kSr, noteHz(note), 1 << 17);
        const double naive = inharmonicDb(naiveSquare(noteHz(note), 1 << 17), kSr, noteHz(note), 1 << 17);
        std::printf("  note %3d (%.0f Hz): inharmonic power %.1f dB; the same table read naively %.1f dB\n", note, noteHz(note), db, naive);
        worst = std::max(worst, db);
        if (note >= 84) check("a naive read of the same table is at least 20 dB worse (the metric sees aliasing)", naive > db + 20.0);
    }
    check("band-limited square: inharmonic power below -70 dB at every pitch", worst < -70.0);
}

static void testWarpModes()
{
    Parameters base = clean();
    base.osc[0].position = 0.5f;
    const Render ref = render(base, noteOn(60, 100), 1 << 15);
    for (int mode : {1, 2}) {
        Parameters p = base;
        p.osc[0].warpMode = static_cast<float>(mode);
        p.osc[0].warpAmount = 0.0f;
        const Render r = render(p, noteOn(60, 100), 1 << 15);
        double d = 0;
        for (size_t i = 0; i < r.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(r.l[i] - ref.l[i])));
        check(mode == 1 ? "Bend at amount 0 changes nothing" : "Sync at amount 0 changes nothing", d == 0.0);
    }
    for (int mode : {1, 2, 3}) {
        Parameters p = base;
        p.osc[0].warpMode = static_cast<float>(mode);
        p.osc[0].warpAmount = 0.7f;
        const Render r = render(p, noteOn(60, 100), 1 << 17);
        double d = 0;
        for (size_t i = 0; i < r.l.size() && i < ref.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(r.l[i] - ref.l[i])));
        check("a warp changes the sound", d > 0.01);
        // The cycle still repeats at the note's period, so the power sits on its harmonics.
        const double f0 = noteHz(60);
        const double inh = inharmonicDb(r.l, kSr, f0, 1 << 17);
        std::printf("  warp %d at 0.7: inharmonic power %.1f dB\n", mode, inh);
        // Warping a band-limited table creates content past Nyquist that cannot be removed afterwards; these are the
        // measured levels at note 60, amount 0.7 (Bend -42.6, Sync -38.5, Fold with ADAA -33.1 dB), with a few dB of margin.
        const double limit = mode == 1 ? -39.0 : (mode == 2 ? -35.0 : -30.0);
        check("a warp keeps the note's period: little power off the harmonics", inh < limit);
        check("a warp stays finite and below 2", [&] {
            for (float v : r.l) if (!std::isfinite(v) || std::fabs(v) > 2.0f) return false;
            return true;
        }());
    }
}

// ── Unison ─────────────────────────────────────────────────────────────────

static double toneAmplitude(const std::vector<float>& x, double hz, float sr)
{
    double sc = 0, ss = 0;
    const size_t from = x.size() / 2;
    for (size_t i = from; i < x.size(); ++i) {
        const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / sr;
        sc += x[i] * std::cos(ph);
        ss += x[i] * std::sin(ph);
    }
    return 2.0 * std::hypot(sc, ss) / static_cast<double>(x.size() - from);
}

static void testNoDcOffset()
{
    Parameters p = clean();
    p.osc[0].position = 0.5f;
    for (int mode : {1, 2, 3}) {
        p.osc[0].warpMode = static_cast<float>(mode);
        p.osc[0].warpAmount = 0.7f;
        const Render r = render(p, noteOn(60, 100), 1 << 17);
        double mean = 0, power = 0;
        for (size_t i = 1 << 16; i < r.l.size(); ++i) { mean += r.l[i]; power += static_cast<double>(r.l[i]) * r.l[i]; }
        mean /= static_cast<double>(r.l.size() - (1 << 16));
        power = std::sqrt(power / static_cast<double>(r.l.size() - (1 << 16)));
        check("a warp leaves no DC offset at the output", std::fabs(mean) < 0.002 * (power + 1e-9) + 1e-5);
    }
}

static void testUnison()
{
    // At Amount 0 every unison voice is the same note in step: the fundamental must stay the same size.
    // (Starting the voices at evenly spaced phases would cancel it.)
    Parameters one = clean();
    const double ref = toneAmplitude(render(one, noteOn(57, 100), 1 << 16).l, noteHz(57), kSr);
    for (int n : {2, 4, 8}) {
        Parameters p = one;
        p.unisonVoices = static_cast<float>(n);
        p.unisonAmount = 0.0f;
        const double a = toneAmplitude(render(p, noteOn(57, 100), 1 << 16).l, noteHz(57), kSr);
        if (std::fabs(20.0 * std::log10(a / ref)) >= 1.0) std::printf("  %d voices at amount 0: %.2f dB\n", n, 20.0 * std::log10(a / ref));
        check("unison at amount 0 keeps the fundamental the same size", std::fabs(20.0 * std::log10(a / ref)) < 1.0);
    }
    // At full amount the level stays within 2 dB of one voice.
    const double r1 = rms(render(one, noteOn(57, 100), 1 << 17).l, 1 << 15);
    for (int n : {2, 4, 8}) {
        Parameters p = one;
        p.unisonVoices = static_cast<float>(n);
        p.unisonAmount = 1.0f;
        const double r = rms(render(p, noteOn(57, 100), 1 << 17).l, 1 << 15);
        check("unison at full amount keeps the level within 2 dB of one voice", std::fabs(20.0 * std::log10(r / r1)) < 2.0);
    }
    // The amount widens the stereo image.
    Parameters p = one;
    p.unisonVoices = 4.0f;
    p.unisonAmount = 0.0f;
    const Render narrow = render(p, noteOn(60, 100), 1 << 14);
    p.unisonAmount = 1.0f;
    const Render wide = render(p, noteOn(60, 100), 1 << 14);
    auto side = [](const Render& r) { double s = 0, m = 0; for (size_t i = 0; i < r.l.size(); ++i) { s += std::pow(r.l[i] - r.r[i], 2.0); m += std::pow(r.l[i] + r.r[i], 2.0); } return s / (m + 1e-12); };
    check("a mono signal at amount 0", side(narrow) < 1e-6);
    check("unison amount widens the image", side(wide) > 0.01);
    // The three modes differ.
    Render modes[3];
    for (int m = 0; m < 3; ++m) {
        Parameters q = one;
        q.unisonVoices = 4.0f; q.unisonAmount = 0.6f; q.unisonMode = static_cast<float>(m);
        modes[m] = render(q, noteOn(60, 100), 1 << 14);
    }
    double d01 = 0, d02 = 0;
    for (size_t i = 0; i < modes[0].l.size(); ++i) { d01 = std::max(d01, static_cast<double>(std::fabs(modes[0].l[i] - modes[1].l[i]))); d02 = std::max(d02, static_cast<double>(std::fabs(modes[0].l[i] - modes[2].l[i]))); }
    check("the unison modes sound different", d01 > 0.01 && d02 > 0.01);
}

// ── Filter, envelope, LFO ──────────────────────────────────────────────────

static void testFilterMatchesItsResponse()
{
    // The tone's size at unity is known: table peak 0.8 x level 0.8 x master 0.6 x the sine's RMS.
    // A single centred voice is equal-power panned: each channel carries 1/sqrt 2 of the mono signal.
    const double unity = 0.8 * 0.8 * 0.6 / std::sqrt(2.0) / std::sqrt(2.0);
    for (int type = 0; type < 5; ++type) {
        double worst = 0;
        for (int note : {48, 60, 72, 84, 96}) {
            Parameters p = clean();
            p.filterType = static_cast<float>(type);
            p.cutoffHz = 1000.0f;
            p.resonance = 0.3f;
            const Render r = render(p, noteOn(note, 100), 1 << 15);
            const double measured = 20.0 * std::log10(rms(r.l, 1 << 14) / unity);
            const double expected = filterResponseDb(static_cast<FilterType>(type), 1000.0, 0.3, noteHz(note), kSr);
            worst = std::max(worst, std::fabs(measured - expected));
        }
        if (worst >= 0.3) std::printf("  filter type %d: worst %.3f dB\n", type, worst);
        check("the filter's measured response matches the plotted model", worst < 0.3);
    }
    check("a 12 dB low-pass is -3 dB at its cutoff with no resonance", std::fabs(filterResponseDb(FilterType::LowPass12, 1000.0, 0.0, 1000.0, kSr) + 3.01) < 0.02);
    check("a 24 dB low-pass is -6 dB at its cutoff", std::fabs(filterResponseDb(FilterType::LowPass24, 1000.0, 0.0, 1000.0, kSr) + 6.02) < 0.02);
    check("resonance raises the peak", filterResponseDb(FilterType::LowPass12, 1000.0, 0.9, 1000.0, kSr) > 10.0);
    // Key tracking and the envelope move the cutoff.
    Parameters p = clean();
    p.cutoffHz = 500.0f;
    p.keyTrack = 1.0f;
    const double tracked = rms(render(p, noteOn(84, 100), 1 << 15).l, 1 << 14);
    p.keyTrack = 0.0f;
    const double fixed = rms(render(p, noteOn(84, 100), 1 << 15).l, 1 << 14);
    check("key tracking opens the filter for a high note", tracked > fixed * 1.5);
}

static void testEnvelopeShape()
{
    const double sr = 48000.0;
    Envelope e;
    e.stage = Envelope::Stage::Attack;
    auto step = [&](bool gate) { return envelopeStep(e, gate, 100.0, 200.0, 0.4, 300.0, sr); };
    size_t reach = 0;
    while (e.stage == Envelope::Stage::Attack && reach < 100000) { (void)step(true); ++reach; }
    check("the attack is linear and takes the set time", std::fabs(static_cast<double>(reach) / sr * 1000.0 - 100.0) < 0.1);
    size_t decay = 0;
    const double start = e.value;
    while (e.value - 0.4 > (start - 0.4) * std::exp(-1.0) && decay < 200000) { (void)step(true); ++decay; }
    check("the decay falls 63 % of the way in its time constant (200 ms)", std::fabs(static_cast<double>(decay) / sr * 1000.0 - 200.0) < 3.0);
    for (int i = 0; i < 500000; ++i) (void)step(true);
    check("the envelope holds the sustain level", std::fabs(e.value - 0.4) < 1e-4);
    const double top = e.value;
    size_t rel = 0;
    while (e.value > top * std::exp(-1.0) && rel < 200000) { (void)step(false); ++rel; }
    check("the release falls 63 % of the way in its time constant (300 ms)", std::fabs(static_cast<double>(rel) / sr * 1000.0 - 300.0) < 3.0);
    for (int i = 0; i < 2000000 && e.stage != Envelope::Stage::Idle; ++i) (void)step(false);
    check("the release ends in the idle stage at zero", e.stage == Envelope::Stage::Idle && e.value == 0.0);
}

static void testLfo()
{
    const double sr = 48000.0;
    EngineState st;
    std::vector<double> sine;
    for (int i = 0; i < 480000; ++i) sine.push_back(lfoStep(st, LfoShape::Sine, 4.0, sr));
    int crossings = 0;
    for (size_t i = 1; i < sine.size(); ++i) if (sine[i - 1] < 0.0 && sine[i] >= 0.0) ++crossings;
    check("a 4 Hz LFO crosses zero upward forty times in ten seconds", std::abs(crossings - 40) <= 1);
    for (LfoShape s : {LfoShape::Sine, LfoShape::Triangle, LfoShape::Saw, LfoShape::Square}) {
        EngineState q;
        double lo = 9, hi = -9;
        for (int i = 0; i < 96000; ++i) { const double v = lfoStep(q, s, 3.0, sr); lo = std::min(lo, v); hi = std::max(hi, v); }
        check("each LFO shape spans -1 to +1", lo < -0.99 && hi > 0.99 && lo >= -1.0001 && hi <= 1.0001);
    }
    EngineState q;
    std::vector<double> sh;
    for (int i = 0; i < 48000 * 3; ++i) sh.push_back(lfoStep(q, LfoShape::SampleHold, 2.0, sr));
    int changes = 0;
    bool bounded = true;
    for (size_t i = 1; i < sh.size(); ++i) { if (sh[i] != sh[i - 1]) ++changes; if (std::fabs(sh[i]) > 1.0) bounded = false; }
    check("sample-and-hold changes once per cycle and stays in range", changes >= 4 && changes <= 7 && bounded);
}

static void testModulationRoutes()
{
    Parameters p = clean();
    p.osc[0].position = 0.5f;
    p.osc[0].table = 2.0f;  // formant frames differ a lot from one position to the next
    const Render ref = render(p, noteOn(60, 100), 1 << 15);
    auto differs = [&](const Parameters& q) {
        const Render r = render(q, noteOn(60, 100), 1 << 15);
        double d = 0;
        for (size_t i = 0; i < r.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(r.l[i] - ref.l[i])));
        return d > 1e-3;
    };
    Parameters q = p; q.lfoPosition = 1.0f; q.lfoRateHz = 5.0f;  check("LFO to position moves the sound", differs(q));
    q = p; q.lfoPitch = 1.0f; q.lfoRateHz = 5.0f;                 check("LFO to pitch moves the sound", differs(q));
    q = p; q.lfoCutoff = 1.0f; q.cutoffHz = 800.0f;
    {
        Parameters base = p; base.cutoffHz = 800.0f;
        const Render a = render(base, noteOn(60, 100), 1 << 15), b = render(q, noteOn(60, 100), 1 << 15);
        double d = 0;
        for (size_t i = 0; i < a.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        check("LFO to cutoff moves the sound", d > 1e-3);
    }
    q = p; q.envPosition = 1.0f; q.fltDecayMs = 200.0f;           check("the filter envelope to position moves the sound", differs(q));
}

// ── Voices and MIDI ─────────────────────────────────────────────────────────

static void testVoiceAllocationAndStealing()
{
    Parameters p = clean();
    p.ampSustain = 1.0f;
    std::vector<MidiEvent> ev;
    for (int i = 0; i < 8; ++i) ev.push_back(noteOn(48 + i, 100, static_cast<std::uint32_t>(i * 10))[0]);
    Render r = render(p, ev, 2000);
    check("eight notes use eight voices", activeVoices(r.state) == 8);
    ev.push_back(noteOn(72, 100, 1500)[0]);
    r = render(p, ev, 2000);
    check("a ninth note steals a voice rather than adding one", activeVoices(r.state) == 8);
    bool finite = true;
    for (float v : r.l) if (!std::isfinite(v)) finite = false;
    check("stealing stays finite", finite);
    // Note off releases; the voice then ends.
    ev.clear();
    ev.push_back(noteOn(60, 100, 0)[0]);
    ev.push_back(noteOffEvent(60, 1000));
    r = render(p, ev, 10000);
    check("a released voice ends after its release", activeVoices(r.state) == 0);
    double tail = 0;
    for (size_t i = 8000; i < 10000; ++i) tail = std::max(tail, static_cast<double>(std::fabs(r.l[i])));
    check("... and is silent (the DC blocker's tail is below 1e-4)", tail < 1e-4);
}

static void testStealingFadesTheOldVoice()
{
    Parameters p = clean();
    p.ampSustain = 1.0f;
    p.ampAttackMs = 0.5f;
    std::vector<MidiEvent> ev;
    for (int i = 0; i < 8; ++i) ev.push_back(noteOn(40 + 3 * i, 127, static_cast<std::uint32_t>(i * 10))[0]);
    ev.push_back(noteOn(81, 127, 6000)[0]);
    const Render r = render(p, ev, 12000);
    auto maxStep = [&](size_t a, size_t b) {
        double m = 0.0;
        for (size_t i = a; i < b; ++i) m = std::max(m, static_cast<double>(std::fabs(r.l[i] - r.l[i - 1])));
        return m;
    };
    const double steady = maxStep(4000, 5900);
    const double around = maxStep(6000, 6400);
    check("stealing a voice adds no step bigger than the steady waveform's", around < 1.5 * steady);
    check("... and the new note sounds once the old one has faded", activeVoices(r.state) == 8 && rms(r.l, 11000) > 0.0);
    // Released before it sounded: the stolen voice just ends.
    ev.push_back(noteOffEvent(81, 6001));
    const Render q = render(p, ev, 12000);
    check("a note released while the voice it stole fades never sounds", activeVoices(q.state) == 7);
}

// ── Play mode and glide ─────────────────────────────────────────────────────

static double cents(double hz, int note) { return 1200.0 * std::log2(hz / noteHz(note)); }

static double pitchAt(const std::vector<float>& x, size_t from, size_t n = 8192)
{
    return peakFrequency(std::vector<float>(x.begin() + static_cast<long>(from), x.begin() + static_cast<long>(from + n)), kSr, n);
}

static void testGlideSlidesFromThePreviousNote()
{
    Parameters p = clean();
    p.glideMs = 400.0f;
    std::vector<MidiEvent> ev = noteOn(60, 100, 0);
    ev.push_back(noteOffEvent(60, 20000));
    ev.push_back(noteOn(72, 100, 24000)[0]);
    const Render r = render(p, ev, 24000 + 65536);
    const double early = pitchAt(r.l, 24000 + 100);
    const double late = pitchAt(r.l, 24000 + 40000);
    check("with Glide the second note starts well below its pitch", cents(early, 72) < -300.0);
    check("... and arrives on it", std::fabs(cents(late, 72)) < 5.0);
    p.glideMs = 0.0f;
    const Render q = render(p, ev, 24000 + 65536);
    check("Glide 0 plays every note at its pitch from the start", std::fabs(cents(pitchAt(q.l, 24000 + 100), 72)) < 5.0);
}

static void testMonoAndLegato()
{
    Parameters p = clean();
    p.playMode = 1.0f;   // Mono
    std::vector<MidiEvent> ev = noteOn(60, 100, 0);
    ev.push_back(noteOn(64, 100, 4000)[0]);
    ev.push_back(noteOn(67, 100, 8000)[0]);
    Render r = render(p, ev, 20000);
    check("Mono plays one voice for three held notes", activeVoices(r.state) == 1);
    check("... at the newest note", std::fabs(cents(pitchAt(r.l, 12000), 67)) < 5.0);
    ev.push_back(noteOffEvent(67, 14000));
    r = render(p, ev, 30000);
    check("releasing the top note returns to the one still held", std::fabs(cents(pitchAt(r.l, 20000), 64)) < 5.0 && activeVoices(r.state) == 1);
    ev.push_back(noteOffEvent(64, 22000));
    ev.push_back(noteOffEvent(60, 22001));
    r = render(p, ev, 40000);
    check("releasing every key lets the voice end", activeVoices(r.state) == 0);
    p.playMode = 0.0f;
    check("Poly gives each held note a voice", activeVoices(render(p, ev, 12000).state) == 3);

    // Legato keeps the envelopes running across an overlapping note; Mono restarts them.
    Parameters e = clean();
    e.ampSustain = 0.4f; e.ampDecayMs = 100.0f; e.ampAttackMs = 5.0f;
    std::vector<MidiEvent> two = noteOn(60, 100, 0);
    two.push_back(noteOn(64, 100, 24000)[0]);
    e.playMode = 1.0f;
    const Render m = render(e, two, 24000 + 2400);
    e.playMode = 2.0f;
    const Render l = render(e, two, 24000 + 2400);
    auto window = [](const Render& x) {
        double s = 0.0;
        for (size_t i = 24000 + 100; i < 24000 + 1500; ++i) s += static_cast<double>(x.l[i]) * x.l[i];
        return std::sqrt(s / 1400.0);
    };
    check("Mono restarts the envelope on a new note and Legato does not", window(m) > 1.4 * window(l));
}

static void testSustainPedalAndPanic()
{
    Parameters p = clean();
    std::vector<MidiEvent> ev = {controller(64, 127, 0), noteOn(60, 100, 10)[0], noteOffEvent(60, 2000)};
    Render r = render(p, ev, 12000);
    check("the sustain pedal holds a released note", activeVoices(r.state) == 1 && rms(r.l, 11000) > 0.05);
    ev.push_back(controller(64, 0, 6000));
    r = render(p, ev, 12000);
    check("releasing the pedal lets it go", activeVoices(r.state) == 0);
    ev = {noteOn(60, 100, 0)[0], controller(123, 0, 3000)};
    r = render(p, ev, 12000);
    check("all-notes-off releases held notes", activeVoices(r.state) == 0);
    ev = {noteOn(60, 100, 0)[0], controller(120, 0, 3000)};
    p.ampReleaseMs = 4000.0f;
    r = render(p, ev, 6000);
    double after = 0;
    for (size_t i = 3010; i < 6000; ++i) after = std::max(after, static_cast<double>(std::fabs(r.l[i])));
    check("all-sound-off silences at once", after == 0.0 && activeVoices(r.state) == 0);
}

static void testVelocity()
{
    Parameters p = clean();
    p.velocity = 1.0f;
    const double loud = rms(render(p, noteOn(60, 127), 1 << 14).l, 1 << 13);
    const double soft = rms(render(p, noteOn(60, 64), 1 << 14).l, 1 << 13);
    check("velocity 1.0 scales the level with the key velocity", std::fabs(20.0 * std::log10(soft / loud) - 20.0 * std::log10(64.0 / 127.0)) < 0.2);
    p.velocity = 0.0f;
    const double a = rms(render(p, noteOn(60, 127), 1 << 14).l, 1 << 13);
    const double b = rms(render(p, noteOn(60, 30), 1 << 14).l, 1 << 13);
    check("velocity 0 plays every note at full level", std::fabs(a - b) < 1e-6);
}

static void testEventsAreSampleAccurate()
{
    Parameters p = clean();
    const Render r = render(p, noteOn(60, 100, 100), 400, kSr, 400);
    bool silent = true;
    for (size_t i = 0; i < 100; ++i) if (r.l[i] != 0.0f) silent = false;
    check("nothing sounds before the note's frame", silent);
    check("the note sounds from its frame", rms(std::vector<float>(r.l.begin() + 200, r.l.end()), 0) > 0.01);
    // Any block split gives the same samples.
    p.unisonVoices = 3.0f; p.unisonAmount = 0.5f; p.unisonMode = 2.0f;
    p.lfoPitch = 0.3f; p.lfoShape = 4.0f;
    std::vector<MidiEvent> ev = {noteOn(60, 100, 37)[0], noteOn(67, 90, 513)[0], noteOffEvent(60, 2000)};
    const Render a = render(p, ev, 6000, kSr, 16);
    const Render b = render(p, ev, 6000, kSr, 1000);
    double d = 0;
    for (size_t i = 0; i < a.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
    check("block size does not change the output", d < 1e-6);
}

static void testEveryControlMatters()
{
    // A baseline that exercises everything: both oscillators warped, unison, a filter envelope, an LFO routed everywhere.
    Parameters base;
    base.osc[0].warpMode = 2.0f; base.osc[0].warpAmount = 0.4f; base.osc[0].position = 0.5f;
    base.osc[1].warpMode = 1.0f; base.osc[1].warpAmount = 0.4f; base.osc[1].position = 0.5f; base.osc[1].level = 0.8f;
    base.unisonVoices = 3.0f; base.unisonAmount = 0.5f;
    base.cutoffHz = 1500.0f; base.envAmount = 0.3f; base.envPosition = 0.3f; base.keyTrack = 0.4f;
    base.lfoPosition = 0.5f; base.lfoPitch = 0.3f; base.lfoCutoff = 0.5f; base.lfoRateHz = 3.0f;
    base.subLevel = 0.4f; base.velocity = 0.7f;
    base.ampReleaseMs = 400.0f; base.fltReleaseMs = 400.0f;
    std::vector<MidiEvent> ev = noteOn(66, 90);
    MidiEvent b;
    b.frame = 2000; b.type = MidiEvent::Type::PitchBend; b.bend = 4096;
    ev.push_back(b);
    ev.push_back(noteOn(70, 90, 3000)[0]);   // an overlapping note, so Play Mode and Glide have something to do
    ev.push_back(noteOffEvent(66, 6000));
    ev.push_back(noteOffEvent(70, 7000));
    const Render ref = render(base, ev, 12000);
    int silentControls = 0;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = kParameterSpecs[i];
        Parameters q = base;
        // A value well away from the baseline, inside the range.
        const float v = field(base, i);
        field(q, i) = v < (s.minimum + s.maximum) * 0.5f ? s.minimum + (s.maximum - s.minimum) * 0.75f : s.minimum + (s.maximum - s.minimum) * 0.2f;
        const Render r = render(q, ev, 12000);
        double d = 0;
        for (size_t k = 0; k < r.l.size(); ++k) d = std::max(d, static_cast<double>(std::fabs(r.l[k] - ref.l[k])));
        if (d < 1e-4) { ++silentControls; std::printf("  no effect: %s\n", s.name); }
    }
    check("every control changes the sound", silentControls == 0);
}

static void testStabilityAndBounds()
{
    for (float sr : {44100.0f, 96000.0f, 192000.0f}) {
        EngineState st;
        std::vector<float> l(256), r(256);
        bool bad = false;
        double mx = 0;
        for (int blk = 0; blk < 300; ++blk) {
            Parameters p;
            for (int o = 0; o < 2; ++o) {
                OscParams& op = p.osc[static_cast<size_t>(o)];
                op.table = static_cast<float>((blk + o) % 5);
                op.position = static_cast<float>((blk * 7 + o * 3) % 101) / 100.0f;
                op.warpMode = static_cast<float>((blk / 2 + o) % 4);
                op.warpAmount = static_cast<float>((blk * 13) % 101) / 100.0f;
                op.transpose = static_cast<float>((blk * 5) % 49 - 24);
                op.fineCents = static_cast<float>((blk * 3) % 101 - 50);
                op.level = static_cast<float>((blk * 11 + o) % 101) / 100.0f;
            }
            p.subLevel = static_cast<float>((blk * 17) % 101) / 100.0f;
            p.unisonMode = static_cast<float>(blk % 3);
            p.unisonVoices = static_cast<float>(1 + (blk * 3) % 8);
            p.unisonAmount = static_cast<float>((blk * 19) % 101) / 100.0f;
            p.filterType = static_cast<float>(blk % 5);
            p.cutoffHz = 20.0f * std::pow(1000.0f, static_cast<float>((blk * 23) % 100) / 99.0f);
            p.resonance = static_cast<float>((blk * 29) % 101) / 100.0f;
            p.keyTrack = static_cast<float>(blk % 2);
            p.envAmount = static_cast<float>((blk * 31) % 201 - 100) / 100.0f;
            p.lfoShape = static_cast<float>(blk % 5);
            p.lfoPosition = p.lfoPitch = p.lfoCutoff = static_cast<float>((blk * 37) % 101) / 100.0f;
            p.lfoRateHz = 0.05f + static_cast<float>((blk * 41) % 200) * 0.1f;
            p.ampAttackMs = 0.5f + static_cast<float>(blk % 7);
            p.ampReleaseMs = 1.0f + static_cast<float>((blk * 43) % 300);
            p.master = 1.0f;
            std::vector<MidiEvent> ev;
            for (int k = 0; k < 3; ++k) {
                MidiEvent e;
                e.frame = static_cast<std::uint32_t>(k * 60);
                e.type = (blk + k) % 4 == 0 ? MidiEvent::Type::NoteOff : MidiEvent::Type::NoteOn;
                e.data1 = static_cast<std::uint8_t>(24 + (blk * 7 + k * 13) % 90);
                e.data2 = static_cast<std::uint8_t>(40 + (blk * 3) % 87);
                ev.push_back(e);
            }
            processBlock(st, p, sr, 256, ev.data(), static_cast<std::uint32_t>(ev.size()), l.data(), r.data());
            for (size_t i = 0; i < l.size(); ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) bad = true;
                mx = std::max(mx, static_cast<double>(std::max(std::fabs(l[i]), std::fabs(r[i]))));
            }
        }
        check("no NaN or inf under random parameters and notes", !bad);
        check("output stays bounded (below 40)", mx < 40.0);
    }
    Parameters nan;
    nan.cutoffHz = std::nanf("");
    nan.osc[0].position = std::nanf("");
    const Parameters c = clampParameters(nan);
    check("NaN parameters are clamped to finite values", std::isfinite(c.cutoffHz) && std::isfinite(c.osc[0].position));
}

static void testSilentDecayLeavesNoDenormals()
{
    Parameters p;
    p.resonance = 0.9f;
    p.ampReleaseMs = 20.0f;
    std::vector<MidiEvent> ev = {noteOn(60, 100, 0)[0], noteOffEvent(60, 2000)};
    const Render r = render(p, ev, 48000 * 3);
    bool denormal = false;
    auto test = [&](double v) { if (v != 0.0 && std::fabs(v) < 2.2250738585072014e-308) denormal = true; };
    for (const Voice& v : r.state.voices)
        for (int c = 0; c < 2; ++c) { test(v.filter1[static_cast<size_t>(c)].ic1); test(v.filter1[static_cast<size_t>(c)].ic2); test(v.filter2[static_cast<size_t>(c)].ic1); test(v.filter2[static_cast<size_t>(c)].ic2); }
    check("filter memory is not left denormal", !denormal);
}

static void testResetClearsVoicesOnly()
{
    Parameters p = clean();
    Render r = render(p, noteOn(60, 100), 4000);
    check("a voice is sounding", activeVoices(r.state) == 1);
    resetState(r.state);
    check("reset silences every voice", activeVoices(r.state) == 0 && r.state.bend[0] == 0.0f && !r.state.primed);
    check("reset leaves the parameters alone", p.osc[0].level == 0.8f);
}

static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    check("forty-three parameters", kParameterCount == 43);
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
    Parameters p = clean();
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
    p.ampReleaseMs = 20.0f;
    p.ampSustain = 1.0f;
    MidiEvent off = noteOffEvent(60, 500);
    off.channel = 1;
    const Render e = render(p, {on(60, 0), on(60, 1), off}, 1 << 15);
    check("the same note on two channels is held independently", activeVoices(e.state) == 1);
}

int main()
{
    testTableDefinitions();
    testEveryFrameIsNormalised();
    testMipsAreBandLimited();
    testReadsAreExactAndContinuous();
    testPitchAccuracy();
    testAliasingIsAbsent();
    testWarpModes();
    testNoDcOffset();
    testUnison();
    testFilterMatchesItsResponse();
    testEnvelopeShape();
    testLfo();
    testModulationRoutes();
    testVoiceAllocationAndStealing();
    testStealingFadesTheOldVoice();
    testGlideSlidesFromThePreviousNote();
    testMonoAndLegato();
    testSustainPedalAndPanic();
    testVelocity();
    testEventsAreSampleAccurate();
    testEveryControlMatters();
    testStabilityAndBounds();
    testSilentDecayLeavesNoDenormals();
    testResetClearsVoicesOnly();
    testParameterTableMatchesCore();
    testBendAndNoteOffArePerChannel();
    std::printf("strata_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
