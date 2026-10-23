#include "quartet_core.hpp"
#include "quartet_params.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace downspout::quartet;

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

static double peakFrequency(const std::vector<float>& x, float sr, size_t n = 1 << 16)
{
    const auto p = powerSpectrum(x, n);
    size_t best = 2;
    for (size_t i = 2; i < p.size() - 2; ++i) if (p[i] > p[best]) best = i;
    const double a = std::log(p[best - 1] + 1e-30), b = std::log(p[best] + 1e-30), c = std::log(p[best + 1] + 1e-30);
    return (static_cast<double>(best) + 0.5 * (a - c) / (a - 2.0 * b + c)) * sr / static_cast<double>(n);
}

// Power away from the harmonics of f0, DC counted as harmonic 0 (Hann leakage reaches a few bins).
static double inharmonicDb(const std::vector<float>& x, float sr, double f0, size_t n = 1 << 16)
{
    const auto p = powerSpectrum(x, n);
    std::vector<char> harmonic(p.size(), 0);
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

// Amplitude of the line nearest `hz`. The Hann main lobe's energy does not depend on where the line falls between bins
// (a peak bin would lose up to 15 % to scalloping), and sum |W|^2 = 3 N^2 / 8, so A = sqrt(32 / 3 x energy) / N.
static double lineAmplitude(const std::vector<float>& x, float sr, double hz, size_t n = 1 << 17)
{
    const auto p = powerSpectrum(x, n);
    const long bin = std::lround(hz * static_cast<double>(n) / sr);
    double energy = 0;
    for (long d = -3; d <= 3; ++d) energy += p[static_cast<size_t>(bin + d)];
    return std::sqrt(energy * 32.0 / 3.0) / static_cast<double>(n);
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

static double noteHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

static double rms(const std::vector<float>& v, size_t from)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
    return std::sqrt(s / static_cast<double>(v.size() - from));
}

// Operator A alone, a sine, steady: the reference tone for pitch and filter checks.
static Parameters solo()
{
    Parameters p;
    for (int o = 1; o < kOperators; ++o) p.op[static_cast<size_t>(o)].level = 0.0f;
    for (OperatorParams& o : p.op) { o.attackMs = 0.5f; o.decayMs = 1.0f; o.sustain = 1.0f; o.releaseMs = 5.0f; }
    p.feedback = 0.0f;
    p.filterType = 0.0f; p.cutoffHz = 20000.0f; p.resonance = 0.0f;
    p.velocity = 0.0f;
    return p;
}

// ── Waveforms, decimator, algorithm table ──────────────────────────────────

static double harmonicAmplitude(const float* table, int h)
{
    double sc = 0, ss = 0;
    for (int i = 0; i < kWaveSize; ++i) {
        const double ph = 2.0 * M_PI * h * i / kWaveSize;
        sc += table[i + 1] * std::cos(ph);
        ss += table[i + 1] * std::sin(ph);
    }
    return 2.0 * std::hypot(sc, ss) / kWaveSize;
}

static void testWaveforms()
{
    const float* sine = waveTable(Wave::Sine, 0);
    double worst = 0;
    for (int i = 0; i < kWaveSize; i += 7) worst = std::max(worst, std::fabs(sine[i + 1] - std::sin(2.0 * M_PI * i / kWaveSize)));
    check("the sine table is a sine", worst < 1e-6);

    const float* saw = waveTable(Wave::Saw, 0);
    bool sawOk = true;
    const double a1 = harmonicAmplitude(saw, 1);
    for (int h = 1; h <= 60; ++h) if (std::fabs(harmonicAmplitude(saw, h) * h / a1 - 1.0) > 0.01) sawOk = false;
    check("the saw's harmonics fall as 1/h", sawOk);
    const float* square = waveTable(Wave::Square, 0);
    bool sqOk = true;
    const double s1 = harmonicAmplitude(square, 1);
    for (int h = 1; h <= 60; h += 2) if (std::fabs(harmonicAmplitude(square, h) * h / s1 - 1.0) > 0.01) sqOk = false;
    for (int h = 2; h <= 60; h += 2) if (harmonicAmplitude(square, h) > 1e-3 * s1) sqOk = false;
    check("the square has only odd harmonics, falling as 1/h", sqOk);

    float peak = 0;
    for (int i = 1; i <= kWaveSize; ++i) peak = std::max({peak, std::fabs(saw[i]), std::fabs(square[i])});
    check("saw and square peak at 1", std::fabs(peak - 1.0f) < 1e-3f);

    bool limited = true;
    for (Wave w : {Wave::Saw, Wave::Square})
        for (int m = 0; m < kWaveMips; ++m) {
            const int limit = waveHarmonicLimit(m);
            for (int h = limit + 2; h < limit + 8 && h < 1000; ++h)
                if (harmonicAmplitude(waveTable(w, m), h) > 2e-4) limited = false;
        }
    check("no mip holds a harmonic above its limit", limited);
    check("the mip for a harmonic count never exceeds it", waveMipForHarmonics(1024.0) == 0 && waveHarmonicLimit(waveMipForHarmonics(300.0)) <= 300 &&
          waveMipForHarmonics(0.5) == kWaveMips - 1);
    check("a read is a sample at a sample point", std::fabs(readWave(Wave::Saw, 100.0 / kWaveSize, 1000.0) - waveTable(Wave::Saw, waveMipForHarmonics(1000.0))[101]) < 1e-6f);
}

static void testDecimator()
{
    const auto& h = decimatorTaps();
    double sum = 0;
    bool symmetric = true;
    for (int n = 0; n < kDecimatorTaps; ++n) {
        sum += h[static_cast<size_t>(n)];
        if (std::fabs(h[static_cast<size_t>(n)] - h[static_cast<size_t>(kDecimatorTaps - 1 - n)]) > 1e-9f) symmetric = false;
    }
    check("the decimator has unity DC gain", std::fabs(sum - 1.0) < 1e-6);
    check("the decimator is symmetric (linear phase)", symmetric);
    const int c = (kDecimatorTaps - 1) / 2;
    bool halfband = true;
    for (int k = 2; k <= c; k += 2) if (std::fabs(h[static_cast<size_t>(c + k)]) > 1e-6f) halfband = false;
    check("every second tap is zero (half-band)", halfband && std::fabs(h[static_cast<size_t>(c)] - 0.5f) < 0.01f);
    auto response = [&](double f) {   // f in cycles per oversampled sample
        Cx s(0.0, 0.0);
        for (int n = 0; n < kDecimatorTaps; ++n) s += static_cast<double>(h[static_cast<size_t>(n)]) * std::polar(1.0, -2.0 * M_PI * f * n);
        return 20.0 * std::log10(std::abs(s) + 1e-12);
    };
    double passband = 0;
    for (double f = 0.0; f <= 0.19; f += 0.005) passband = std::max(passband, std::fabs(response(f)));
    double stopband = -1000;
    for (double f = 0.32; f <= 0.5; f += 0.005) stopband = std::max(stopband, response(f));
    std::printf("  decimator: passband ripple to 0.19 fs2 (18.2 kHz at 48 kHz) %.3f dB; stopband from 0.32 fs2 %.1f dB\n", passband, stopband);
    check("the decimator is flat to 18 kHz at 48 kHz (within 0.05 dB)", passband < 0.05);
    check("and rejects everything above 0.32 of the oversampled rate by 60 dB", stopband < -60.0);
}

static void testAlgorithmTable()
{
    static constexpr int kCarriers[kAlgorithms] = {1, 1, 1, 1, 2, 1, 3, 2, 2, 3, 4};
    bool shape = true, carrierA = true;
    for (int a = 0; a < kAlgorithms; ++a) {
        const Algorithm& alg = algorithm(a);
        if (carrierCount(alg) != kCarriers[a]) shape = false;
        if (!(alg.carriers & 1u)) carrierA = false;
        for (int t = 0; t < kOperators; ++t)
            for (int m = 0; m <= t; ++m)
                if ((alg.modulators[static_cast<size_t>(t)] >> m) & 1u) shape = false;   // a modulator has a higher index than its target
    }
    check("every algorithm has the documented number of carriers and a legal routing", shape);
    check("operator A is a carrier in every algorithm", carrierA);
    const Algorithm& a1 = algorithm(0);
    check("algorithm 1 is a chain D -> C -> B -> A", operatorLayer(a1, 0) == 0 && operatorLayer(a1, 1) == 1 && operatorLayer(a1, 2) == 2 && operatorLayer(a1, 3) == 3);
    const Algorithm& a11 = algorithm(10);
    bool flat = true;
    for (int o = 0; o < kOperators; ++o) if (operatorLayer(a11, o) != 0) flat = false;
    check("algorithm 11 is all carriers", flat);
    check("out-of-range algorithm indices are clamped", &algorithm(-3) == &algorithm(0) && &algorithm(99) == &algorithm(10));
}

// ── Pitch and FM ────────────────────────────────────────────────────────────

static void testPitch()
{
    double worst = 0;
    for (int note : {36, 48, 60, 69, 81, 93, 100}) {
        const Render r = render(solo(), noteOn(note, 100), 1 << 17);
        worst = std::max(worst, std::fabs(1200.0 * std::log2(peakFrequency(r.l, kSr, 1 << 17) / noteHz(note))));
    }
    if (worst >= 0.5) std::printf("  worst pitch error %.3f cents\n", worst);
    check("every note is within half a cent", worst < 0.5);

    Parameters p = solo();
    p.op[0].ratio = 2.0f;
    check("ratio 2 doubles the frequency", std::fabs(peakFrequency(render(p, noteOn(60, 100), 1 << 17).l, kSr, 1 << 17) / noteHz(60) - 2.0) < 1e-3);
    p.op[0].ratio = 1.0f;
    p.op[0].fineCents = 50.0f;
    check("fine +50 cents raises the pitch half a semitone",
          std::fabs(1200.0 * std::log2(peakFrequency(render(p, noteOn(60, 100), 1 << 17).l, kSr, 1 << 17) / noteHz(60)) - 50.0) < 0.5);
    p = solo();
    p.op[0].fixedMode = 1.0f;
    p.op[0].fixedHz = 700.0f;
    const double lo = peakFrequency(render(p, noteOn(48, 100), 1 << 17).l, kSr, 1 << 17);
    const double hi = peakFrequency(render(p, noteOn(84, 100), 1 << 17).l, kSr, 1 << 17);
    check("a fixed-frequency operator ignores the note", std::fabs(lo - 700.0) < 0.2 && std::fabs(hi - 700.0) < 0.2);
    p = solo();
    p.bendRange = 12.0f;
    std::vector<MidiEvent> ev = noteOn(60, 100);
    MidiEvent b;
    b.frame = 10; b.type = MidiEvent::Type::PitchBend; b.bend = 8191;
    ev.push_back(b);
    check("a full pitch bend moves the pitch by the bend range", std::fabs(1200.0 * std::log2(peakFrequency(render(p, ev, 1 << 17).l, kSr, 1 << 17) / noteHz(60)) - 1200.0) < 2.0);
}

// Phase modulation by B at the same level as the depth: sidebands follow Bessel functions of the index.
static void testFmSidebandsAreBessel()
{
    // Carrier A at 8 x the note, modulator B at 1 x: lines at (8 +- k) f0 with amplitudes J_k(beta), beta = 2 pi depth level.
    Parameters p = solo();
    p.algorithm = 0.0f;                        // B modulates A (D, C at level 0)
    p.op[0].ratio = 8.0f;
    p.op[1].ratio = 1.0f;
    p.depth = 1.0f;
    p.op[1].level = static_cast<float>(0.5 / (2.0 * M_PI));   // beta = 0.5 rad
    const int note = 60;
    const double f0 = noteHz(note);
    const Render r = render(p, noteOn(note, 100), 1 << 17);
    const double j0 = lineAmplitude(r.l, kSr, 8 * f0), j1p = lineAmplitude(r.l, kSr, 9 * f0), j1m = lineAmplitude(r.l, kSr, 7 * f0);
    const double j2p = lineAmplitude(r.l, kSr, 10 * f0);
    // J0(0.5) = 0.9385, J1(0.5) = 0.2423, J2(0.5) = 0.0306
    std::printf("  FM beta 0.5: J1/J0 = %.4f (0.2582), J2/J0 = %.4f (0.0326)\n", j1p / j0, j2p / j0);
    check("the first sideband is J1/J0 of the carrier (index in radians = 2 pi x depth x level)", std::fabs(j1p / j0 - 0.2582) < 0.006);
    check("the sidebands are symmetric", std::fabs(j1p - j1m) / j1p < 0.02);
    check("the second sideband is J2/J0", std::fabs(j2p / j0 - 0.0326) < 0.003);
    // Doubling the depth doubles the index: J1/J0 of beta = 1.0 is 0.4401 / 0.7652.
    p.depth = 2.0f;
    const Render r2 = render(p, noteOn(note, 100), 1 << 17);
    const double q1 = lineAmplitude(r2.l, kSr, 9 * f0) / lineAmplitude(r2.l, kSr, 8 * f0);
    check("doubling FM Depth doubles the modulation index", std::fabs(q1 - 0.5751) < 0.012);
    // With no depth there is no modulation: a pure sine.
    p.depth = 0.0f;
    const Render r0 = render(p, noteOn(note, 100), 1 << 17);
    check("FM Depth 0 leaves the carrier unmodulated", lineAmplitude(r0.l, kSr, 9 * f0) < 1e-3 * lineAmplitude(r0.l, kSr, 8 * f0));
}

static void testAlgorithmsRouteSignal()
{
    for (int a = 0; a < kAlgorithms; ++a) {
        const Algorithm& alg = algorithm(a);
        // Only one operator sounds: if it is a carrier the output has its tone; if it is purely a modulator the output is silent.
        for (int o = 0; o < kOperators; ++o) {
            Parameters p = solo();
            p.algorithm = static_cast<float>(a);
            p.op[0].level = 0.0f;
            p.op[static_cast<size_t>(o)].level = 0.8f;
            const Render r = render(p, noteOn(60, 100), 1 << 14);
            const bool carrier = (alg.carriers >> o) & 1u;
            const double level = rms(r.l, 1 << 13);
            if (carrier != (level > 0.02)) std::printf("  algorithm %d operator %d: carrier %d level %.4f\n", a + 1, o, carrier, level);
            check("an operator is heard only if it is a carrier", carrier == (level > 0.02));
        }
        // Each declared modulator changes what its target produces.
        for (int t = 0; t < kOperators; ++t)
            for (int m = t + 1; m < kOperators; ++m) {
                if (!((alg.modulators[static_cast<size_t>(t)] >> m) & 1u)) continue;
                Parameters p;
                p.algorithm = static_cast<float>(a);
                p.feedback = 0.0f;
                p.op[0].ratio = 1.0f; p.op[1].ratio = 2.0f; p.op[2].ratio = 3.0f; p.op[3].ratio = 4.0f;
                for (OperatorParams& op : p.op) { op.level = 0.5f; op.sustain = 1.0f; op.decayMs = 1.0f; }
                const Render low = render(p, noteOn(60, 100), 1 << 14);
                p.op[static_cast<size_t>(m)].level = 0.9f;
                const Render high = render(p, noteOn(60, 100), 1 << 14);
                double d = 0;
                for (size_t i = 0; i < low.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(low.l[i] - high.l[i])));
                check("a declared modulator changes the output", d > 1e-3);
            }
    }
    // The output level does not jump by much between algorithms with the same carriers and no modulation.
    Parameters p = solo();
    p.op[0].level = 0.8f;
    const double a1 = rms(render(p, noteOn(60, 100), 1 << 14).l, 1 << 13);
    p.algorithm = 4.0f;  // two carriers, but only A has a level
    const double a5 = rms(render(p, noteOn(60, 100), 1 << 14).l, 1 << 13);
    check("carrier output is averaged over the carriers (1 carrier: x1, 2 carriers: x1/2)", std::fabs(a5 / a1 - 0.5) < 0.02);
}

static void testFeedback()
{
    Parameters p;
    p.algorithm = 10.0f;   // all carriers, so D is heard and its feedback is audible
    p.op[3].level = 0.8f; p.op[3].ratio = 1.0f;
    for (int o = 0; o < 3; ++o) p.op[static_cast<size_t>(o)].level = 0.0f;
    for (OperatorParams& op : p.op) { op.sustain = 1.0f; op.decayMs = 1.0f; }
    p.feedback = 0.0f;
    const Render clean = render(p, noteOn(60, 100), 1 << 17);
    p.feedback = 1.0f;
    const Render fb = render(p, noteOn(60, 100), 1 << 17);
    const double f0 = noteHz(60);
    check("feedback adds harmonics to operator D", lineAmplitude(fb.l, kSr, 3 * f0) > 20.0 * lineAmplitude(clean.l, kSr, 3 * f0));
    check("... and keeps its pitch", std::fabs(peakFrequency(fb.l, kSr, 1 << 17) / f0 - 1.0) < 0.002);
    check("feedback stays finite and bounded", [&] { for (float v : fb.l) if (!std::isfinite(v) || std::fabs(v) > 2.0f) return false; return true; }());
}

// ── Aliasing and oversampling ───────────────────────────────────────────────

static void testWaveformsDoNotAlias()
{
    double worst = -300;
    for (Wave w : {Wave::Saw, Wave::Square}) {
        for (int note : {60, 84, 96, 105}) {
            Parameters p = solo();
            p.op[0].wave = static_cast<float>(w);
            p.quality = 0.0f;
            const Render r = render(p, noteOn(note, 100), 1 << 17);
            const double db = inharmonicDb(r.l, kSr, noteHz(note), 1 << 17);
            worst = std::max(worst, db);
        }
    }
    std::printf("  saw and square operators: worst inharmonic power %.1f dB (1x)\n", worst);
    check("a saw or square operator has no aliasing (below -65 dB, 1x, notes 60-105)", worst < -65.0);
}

static void testOversamplingReducesAliasing()
{
    // A strong two-stage FM patch at a high note: its sidebands run well past Nyquist.
    Parameters p;
    p.algorithm = 0.0f;
    p.feedback = 0.0f;
    p.depth = 1.0f;
    p.op[0].ratio = 1.0f; p.op[1].ratio = 1.0f; p.op[2].ratio = 2.0f; p.op[3].ratio = 3.0f;
    p.op[0].level = 0.8f; p.op[1].level = 0.5f; p.op[2].level = 0.4f; p.op[3].level = 0.3f;
    for (OperatorParams& o : p.op) { o.sustain = 1.0f; o.decayMs = 1.0f; o.attackMs = 0.5f; }
    p.cutoffHz = 20000.0f; p.resonance = 0.0f; p.filterType = 0.0f; p.velocity = 0.0f;
    double worst1 = -300, worst2 = -300;
    for (int note : {72, 84, 96}) {
        p.quality = 0.0f;
        const double a = inharmonicDb(render(p, noteOn(note, 100), 1 << 17).l, kSr, noteHz(note), 1 << 17);
        p.quality = 1.0f;
        const double b = inharmonicDb(render(p, noteOn(note, 100), 1 << 17).l, kSr, noteHz(note), 1 << 17);
        std::printf("  strong 3-stage FM, note %d: inharmonic %.1f dB at 1x, %.1f dB at 2x\n", note, a, b);
        worst1 = std::max(worst1, a);
        worst2 = std::max(worst2, b);
        // The sidebands of this patch (three stages, about 6 rad each) reach far past 2x the Nyquist rate at the top pitches,
        // so the gain shrinks as the note rises: measured 36, 17 and 6.5 dB at notes 72, 84 and 96.
        check("2x oversampling lowers the aliasing", b < a - (note <= 84 ? 10.0 : 5.0));
    }
    check("the 2x patch is cleaner than -50 dB at note 72", worst2 > -300.0 && inharmonicDb(render([&] { Parameters q = p; q.quality = 1.0f; return q; }(), noteOn(72, 100), 1 << 17).l, kSr, noteHz(72), 1 << 17) < -50.0);
    (void)worst1;
    // The decimator costs the passband nothing: the same sine at 1x and 2x.
    Parameters s = solo();
    for (int note : {60, 96, 110}) {
        s.quality = 0.0f;
        const double a1 = rms(render(s, noteOn(note, 100), 1 << 15).l, 1 << 14);
        s.quality = 1.0f;
        const double a2 = rms(render(s, noteOn(note, 100), 1 << 15).l, 1 << 14);
        check("2x and 1x give the same level for a sine (within 0.1 dB)", std::fabs(20.0 * std::log10(a2 / a1)) < 0.1);
    }
}

static void testNoDcOffset()
{
    Parameters p;
    p.algorithm = 3.0f;
    p.feedback = 0.9f;
    for (OperatorParams& o : p.op) { o.wave = 1.0f; o.sustain = 1.0f; o.decayMs = 1.0f; }
    const Render r = render(p, noteOn(55, 100), 1 << 17);
    double mean = 0;
    for (size_t i = 1 << 16; i < r.l.size(); ++i) mean += r.l[i];
    mean /= static_cast<double>(r.l.size() - (1 << 16));
    check("saw operators with feedback leave no DC at the output", std::fabs(mean) < 0.003 * rms(r.l, 1 << 16) + 1e-5);
}

// ── Pitch envelope, filter, envelopes ───────────────────────────────────────

static double zeroCrossingFrequency(const std::vector<float>& x, size_t from, size_t to, float sr)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (crossings == 0) first = i;
            last = i;
            ++crossings;
        }
    return crossings < 2 ? 0.0 : (crossings - 1) * static_cast<double>(sr) / static_cast<double>(last - first);
}

static void testPitchEnvelope()
{
    Parameters p = solo();
    p.pitchEnvOn = 1.0f;
    p.pitchEnvSemitones = 12.0f;
    p.pitchAttackMs = 0.5f; p.pitchDecayMs = 100.0f; p.pitchSustain = 0.0f;
    const Render r = render(p, noteOn(60, 100), 48000);
    const double early = zeroCrossingFrequency(r.l, 100, 700, kSr);   // about 2 ms - 15 ms
    const double late = zeroCrossingFrequency(r.l, 40000, 47000, kSr);
    check("the pitch envelope starts about an octave up", early > noteHz(60) * 1.7);
    check("... and falls back to the note", std::fabs(late / noteHz(60) - 1.0) < 0.01);
    p.pitchEnvOn = 0.0f;
    const Render off = render(p, noteOn(60, 100), 48000);
    check("with the pitch envelope off the pitch is steady", std::fabs(zeroCrossingFrequency(off.l, 100, 700, kSr) / noteHz(60) - 1.0) < 0.03);
}

static void testFilterMatchesItsResponse()
{
    const double unity = 0.8 * 0.6 / std::sqrt(2.0);   // a sine at level 0.8, master 0.6
    for (int type = 0; type < 5; ++type) {
        double worst = 0;
        for (int note : {48, 60, 72, 84, 96}) {
            Parameters p = solo();
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
    Parameters p = solo();
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

// ── Voices and MIDI ─────────────────────────────────────────────────────────

static void testVoicesAndMidi()
{
    Parameters p = solo();
    std::vector<MidiEvent> ev;
    for (int i = 0; i < 8; ++i) ev.push_back(noteOn(48 + i, 100, static_cast<std::uint32_t>(i * 10))[0]);
    Render r = render(p, ev, 2000);
    check("eight notes use eight voices", activeVoices(r.state) == 8);
    ev.push_back(noteOn(72, 100, 1500)[0]);
    r = render(p, ev, 2000);
    check("a ninth note steals a voice rather than adding one", activeVoices(r.state) == 8);
    ev = {noteOn(60, 100, 0)[0], noteOffEvent(60, 1000)};
    r = render(p, ev, 10000);
    check("a released voice ends after its release", activeVoices(r.state) == 0);
    double tail = 0;
    for (size_t i = 8000; i < 10000; ++i) tail = std::max(tail, static_cast<double>(std::fabs(r.l[i])));
    check("... and is silent (the DC blocker's tail is below 1e-4)", tail < 1e-4);

    ev = {controller(64, 127, 0), noteOn(60, 100, 10)[0], noteOffEvent(60, 2000)};
    r = render(p, ev, 12000);
    check("the sustain pedal holds a released note", activeVoices(r.state) == 1 && rms(r.l, 11000) > 0.05);
    ev.push_back(controller(64, 0, 6000));
    r = render(p, ev, 12000);
    check("releasing the pedal lets it go", activeVoices(r.state) == 0);
    ev = {noteOn(60, 100, 0)[0], controller(123, 0, 3000)};
    r = render(p, ev, 12000);
    check("all-notes-off releases held notes", activeVoices(r.state) == 0);
    ev = {noteOn(60, 100, 0)[0], controller(120, 0, 3000)};
    for (OperatorParams& o : p.op) o.releaseMs = 4000.0f;
    r = render(p, ev, 6000);
    double after = 0;
    for (size_t i = 3010; i < 6000; ++i) after = std::max(after, static_cast<double>(std::fabs(r.l[i])));
    check("all-sound-off silences at once", after == 0.0 && activeVoices(r.state) == 0);

    p = solo();
    p.velocity = 1.0f;
    const double loud = rms(render(p, noteOn(60, 127), 1 << 14).l, 1 << 13);
    const double soft = rms(render(p, noteOn(60, 64), 1 << 14).l, 1 << 13);
    check("velocity 1.0 scales the level with the key velocity", std::fabs(20.0 * std::log10(soft / loud) - 20.0 * std::log10(64.0 / 127.0)) < 0.2);
    p.velocity = 0.0f;
    check("velocity 0 plays every note at full level", std::fabs(rms(render(p, noteOn(60, 127), 1 << 14).l, 1 << 13) - rms(render(p, noteOn(60, 30), 1 << 14).l, 1 << 13)) < 1e-6);

    p = solo();
    const Render early = render(p, noteOn(60, 100, 100), 400, kSr, 400);
    bool silent = true;
    for (size_t i = 0; i < 100; ++i) if (early.l[i] != 0.0f) silent = false;
    check("nothing sounds before the note's frame", silent);
    p.algorithm = 2.0f; p.op[1].level = 0.4f; p.op[2].level = 0.3f; p.feedback = 0.4f; p.pitchEnvOn = 1.0f; p.quality = 1.0f;
    ev = {noteOn(60, 100, 37)[0], noteOn(67, 90, 513)[0], noteOffEvent(60, 2000)};
    const Render a = render(p, ev, 6000, kSr, 16);
    const Render b = render(p, ev, 6000, kSr, 1000);
    double d = 0;
    for (size_t i = 0; i < a.l.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
    check("block size does not change the output", d < 1e-6);
    bool mono = true;
    for (size_t i = 0; i < a.l.size(); ++i) if (a.l[i] != a.r[i]) mono = false;
    check("the output is mono (both channels equal)", mono);
}

static void testEveryControlMatters()
{
    Parameters base;
    base.algorithm = 3.0f;       // D -> (B + C) -> A: every operator is in the signal path
    base.cutoffHz = 1500.0f; base.resonance = 0.3f; base.keyTrack = 0.4f;
    base.pitchEnvOn = 1.0f; base.pitchEnvSemitones = 7.0f; base.pitchDecayMs = 150.0f; base.pitchSustain = 0.2f;
    for (OperatorParams& o : base.op) { o.releaseMs = 400.0f; o.level = std::max(o.level, 0.4f); }
    base.quality = 1.0f;
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
        const ParameterSpec& s = specs()[i];
        Parameters start = base;
        Parameters q;
        const Render* compareTo = &ref;
        Render fixedRef;
        // A fixed frequency only matters in fixed mode, so test it against a baseline that is in fixed mode.
        if (i < kGlobalBase && i % kPerOperator == kFixedHz) {
            start.op[i / kPerOperator].fixedMode = 1.0f;
            fixedRef = render(start, ev, 12000);
            compareTo = &fixedRef;
        }
        q = start;
        const float v = field(start, i);
        field(q, i) = v < (s.minimum + s.maximum) * 0.5f ? s.minimum + (s.maximum - s.minimum) * 0.75f : s.minimum + (s.maximum - s.minimum) * 0.2f;
        const Render r = render(q, ev, 12000);
        double d = 0;
        for (size_t k = 0; k < r.l.size(); ++k) d = std::max(d, static_cast<double>(std::fabs(r.l[k] - compareTo->l[k])));
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
            for (int o = 0; o < kOperators; ++o) {
                OperatorParams& op = p.op[static_cast<size_t>(o)];
                op.wave = static_cast<float>((blk + o) % 3);
                op.fixedMode = static_cast<float>((blk / 3 + o) % 2);
                op.ratio = 0.25f * std::pow(128.0f, static_cast<float>((blk * 7 + o * 11) % 100) / 99.0f);
                op.fineCents = static_cast<float>((blk * 3 + o) % 101 - 50);
                op.fixedHz = 10.0f * std::pow(2000.0f, static_cast<float>((blk * 13 + o) % 100) / 99.0f);
                op.level = static_cast<float>((blk * 17 + o * 5) % 101) / 100.0f;
                op.attackMs = 0.5f + static_cast<float>((blk + o) % 5);
                op.releaseMs = 1.0f + static_cast<float>((blk * 29 + o) % 300);
            }
            p.algorithm = static_cast<float>(blk % kAlgorithms);
            p.feedback = static_cast<float>((blk * 19) % 101) / 100.0f;
            p.depth = 4.0f * static_cast<float>((blk * 23) % 101) / 100.0f;
            p.pitchEnvOn = static_cast<float>(blk % 2);
            p.pitchEnvSemitones = static_cast<float>((blk * 31) % 97 - 48);
            p.filterType = static_cast<float>(blk % 5);
            p.cutoffHz = 20.0f * std::pow(1000.0f, static_cast<float>((blk * 37) % 100) / 99.0f);
            p.resonance = static_cast<float>((blk * 41) % 101) / 100.0f;
            p.master = 1.0f;
            p.quality = static_cast<float>(blk % 2);
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
                mx = std::max(mx, static_cast<double>(std::fabs(l[i])));
            }
        }
        check("no NaN or inf under random parameters and notes", !bad);
        check("output stays bounded (below 40)", mx < 40.0);
    }
    Parameters nan;
    nan.cutoffHz = std::nanf("");
    nan.op[0].ratio = std::nanf("");
    const Parameters c = clampParameters(nan);
    check("NaN parameters are clamped to finite values", std::isfinite(c.cutoffHz) && std::isfinite(c.op[0].ratio));
}

static void testDenormalsAndReset()
{
    Parameters p;
    p.resonance = 0.9f;
    for (OperatorParams& o : p.op) o.releaseMs = 20.0f;
    p.feedback = 0.8f;
    std::vector<MidiEvent> ev = {noteOn(60, 100, 0)[0], noteOffEvent(60, 2000)};
    Render r = render(p, ev, 48000 * 3);
    bool denormal = false;
    auto test = [&](double v) { if (v != 0.0 && std::fabs(v) < 2.2250738585072014e-308) denormal = true; };
    for (const Voice& v : r.state.voices) { test(v.filter1.ic1); test(v.filter1.ic2); test(v.filter2.ic1); test(v.filter2.ic2); test(v.feedbackY1); test(v.feedbackY2); }
    test(r.state.dcX); test(r.state.dcY);
    check("filter, feedback and DC memory are not left denormal", !denormal);

    Parameters q = solo();
    Render s = render(q, noteOn(60, 100), 4000);
    check("a voice is sounding", activeVoices(s.state) == 1);
    resetState(s.state);
    check("reset silences every voice", activeVoices(s.state) == 0 && s.state.bend[0] == 0.0f && !s.state.primed);
    check("reset leaves the parameters alone", q.op[0].level == 0.8f);
}

static void testParameterTableMatchesCore()
{
    const Parameters defaults;
    Parameters probe;
    check("fifty-nine parameters", kParameterCount == 59);
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = specs()[i];
        check("symbol and name are set", s.symbol[0] != '\0' && s.name[0] != '\0');
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
            if (std::string(specs()[i].symbol) == specs()[j].symbol) unique = false;
    check("symbols are unique", unique);
}

// ── Play mode and glide ─────────────────────────────────────────────────────

static double cents(double hz, int note) { return 1200.0 * std::log2(hz / noteHz(note)); }

static double pitchAt(const std::vector<float>& x, size_t from, size_t n = 8192)
{
    return peakFrequency(std::vector<float>(x.begin() + static_cast<long>(from), x.begin() + static_cast<long>(from + n)), kSr, n);
}

static void testGlideSlidesFromThePreviousNote()
{
    Parameters p = solo();
    p.glideMs = 400.0f;
    std::vector<MidiEvent> ev = noteOn(60, 100, 0);
    ev.push_back(noteOffEvent(60, 20000));
    ev.push_back(noteOn(72, 100, 24000)[0]);
    const Render r = render(p, ev, 24000 + 65536);
    check("with Glide the second note starts well below its pitch", cents(pitchAt(r.l, 24000 + 100), 72) < -300.0);
    check("... and arrives on it", std::fabs(cents(pitchAt(r.l, 24000 + 40000), 72)) < 5.0);
    p.glideMs = 0.0f;
    const Render q = render(p, ev, 24000 + 65536);
    check("Glide 0 plays every note at its pitch from the start", std::fabs(cents(pitchAt(q.l, 24000 + 100), 72)) < 5.0);
}

static void testMonoAndLegato()
{
    Parameters p = solo();
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
    Parameters e = solo();
    for (auto& o : e.op) { o.sustain = 0.4f; o.decayMs = 100.0f; o.attackMs = 5.0f; }
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
    check("Mono restarts the envelope on a new note and Legato does not", window(m) > 1.2 * window(l));
}

static void testStealingFadesTheOldVoice()
{
    Parameters p = solo();
    for (auto& o : p.op) { o.sustain = 1.0f; o.attackMs = 0.5f; }
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
    check("... and the new note sounds once the old one has faded", activeVoices(r.state) == 8);
    ev.push_back(noteOffEvent(81, 6001));
    const Render q = render(p, ev, 12000);
    check("a note released while the voice it stole fades never sounds", activeVoices(q.state) == 7);
}

// ── Per-channel notes and bend (Retune sends one note per channel, each with its own bend) ──

static void testBendAndNoteOffArePerChannel()
{
    Parameters p = solo();
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
    for (auto& o : p.op) o.releaseMs = 20.0f;
    MidiEvent off = noteOffEvent(60, 500);
    off.channel = 1;
    const Render e = render(p, {on(60, 0), on(60, 1), off}, 1 << 15);
    check("the same note on two channels is held independently", activeVoices(e.state) == 1);
}

int main()
{
    testWaveforms();
    testDecimator();
    testAlgorithmTable();
    testPitch();
    testFmSidebandsAreBessel();
    testAlgorithmsRouteSignal();
    testFeedback();
    testWaveformsDoNotAlias();
    testOversamplingReducesAliasing();
    testNoDcOffset();
    testPitchEnvelope();
    testFilterMatchesItsResponse();
    testEnvelopeShape();
    testVoicesAndMidi();
    testEveryControlMatters();
    testStabilityAndBounds();
    testDenormalsAndReset();
    testParameterTableMatchesCore();
    testGlideSlidesFromThePreviousNote();
    testMonoAndLegato();
    testStealingFadesTheOldVoice();
    testBendAndNoteOffArePerChannel();
    std::printf("quartet_core_tests: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
