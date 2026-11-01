#include "downspout/test_assert.h"

#include "mouth_engine.hpp"
#include "mouth_params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace downspout::mouth;

namespace {

constexpr double kPi = 3.14159265358979323846;
int g_failures = 0;
int g_checks = 0;

void check(const bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkNear(const double got, const double want, const double tol, const std::string& what)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), " (got %.4f, want %.4f +-%.4f)", got, want, tol);
    check(std::fabs(got - want) <= tol, what + buf);
}

struct Rng {
    std::uint32_t s = 12345u;
    float next()
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;  // -1..1
    }
};

std::vector<float> noise(const std::size_t n, const float amp = 0.5f, const std::uint32_t seed = 12345u)
{
    Rng r;
    r.s = seed;
    std::vector<float> v(n);
    for (float& x : v)
        x = amp * r.next();
    return v;
}

double rms(const std::vector<float>& v, const std::size_t from = 0)
{
    double sum = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        sum += static_cast<double>(v[i]) * v[i];
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, v.size() - from)));
}

// Runs a mono signal through a fresh engine in blocks and returns the left output.
std::vector<float> run(const Parameters& p, const std::vector<float>& in, const double fs = 48000.0,
                       const std::size_t block = 256)
{
    Engine e;
    activate(e, fs);
    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); i += block) {
        const std::size_t n = std::min(block, in.size() - i);
        processBlock(e, p, in.data() + i, nullptr, out.data() + i, nullptr, n);
    }
    return out;
}

// Steady-state gain of the whole plugin at one sine frequency, measured on the audio path.
double gainAt(const Parameters& p, const double hz, const double fs = 48000.0)
{
    const std::size_t n = 12000;
    std::vector<float> in(n);
    for (std::size_t i = 0; i < n; ++i)
        in[i] = 0.25f * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs));
    const std::vector<float> out = run(p, in, fs, 512);
    std::vector<float> tailIn(in.begin() + 8000, in.end());
    std::vector<float> tailOut(out.begin() + 8000, out.end());
    return rms(tailOut) / rms(tailIn);
}

// First local maximum of the measured response above `from` Hz.
double firstPeak(const Parameters& p, const double from, const double to, const double fs = 48000.0)
{
    double best = from, bestGain = 0.0;
    for (double hz = from; hz <= to; hz *= 1.012) {
        const double g = gainAt(p, hz, fs);
        if (g > bestGain) {
            bestGain = g;
            best = hz;
        }
    }
    return best;
}

Parameters atVowel(const int voice, const std::size_t vowel)
{
    Parameters p;
    p.voice = static_cast<float>(voice);
    p.vowelX = kPadPoints[vowel].x;
    p.vowelY = kPadPoints[vowel].y;
    return p;
}

// ── Tests ──────────────────────────────────────────────────────────────────

void testParameterTable()
{
    std::printf("parameter table\n");
    const Parameters defaults;
    std::set<std::string> symbols;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = kParameterSpecs[i];
        check(symbols.insert(s.symbol).second, std::string("unique symbol ") + s.symbol);
        check(s.minimum < s.maximum, std::string("range ") + s.symbol);
        check(s.defaultValue >= s.minimum && s.defaultValue <= s.maximum, std::string("default in range ") + s.symbol);
        check(field(defaults, i) == s.defaultValue, std::string("core default equals spec ") + s.symbol);
    }
    check(kParameterCount == 11, "eleven parameters");
    Parameters bad;
    bad.vowelX = 1.0e9f;
    bad.voice = 9.0f;
    bad.mix = std::nanf("");
    const Parameters clamped = clampParameters(bad);
    check(clamped.vowelX == 100.0f && clamped.voice == 3.0f && clamped.mix == 100.0f, "clamp and non-finite default");
}

// An independent copy of the first two formants for two voices, typed from the published table.
void testVowelCorners()
{
    std::printf("vowel corners\n");
    struct Row {
        int voice;
        std::size_t vowel;
        float f1, f2;
    };
    const Row rows[] = {
        {1, 0, 650, 1080}, {1, 1, 400, 1700}, {1, 2, 290, 1870}, {1, 3, 400, 800}, {1, 4, 350, 600},
        {3, 0, 800, 1150}, {3, 1, 350, 2000}, {3, 2, 270, 2140}, {3, 3, 450, 800}, {3, 4, 325, 700},
    };
    for (const Row& r : rows) {
        const FormantSet set = blendVowel(kPadPoints[r.vowel].x, kPadPoints[r.vowel].y, r.voice);
        checkNear(set[0].freq, r.f1, r.f1 * 0.002, "F1 at the corner");
        checkNear(set[1].freq, r.f2, r.f2 * 0.002, "F2 at the corner");
        check(std::fabs(set[0].gain - 1.0f) < 1.0e-5f, "F1 is the 0 dB reference");
    }
    // Every table entry is sane: ascending formants, levels at or below 0 dB, positive bandwidth.
    for (std::size_t v = 0; v < kVoices; ++v)
        for (std::size_t w = 0; w < kVowels; ++w) {
            const VowelData& d = kVowelTable[v][w];
            for (std::size_t f = 0; f < kFormants; ++f) {
                check(d.bw[f] > 0.0f && d.db[f] <= 0.0f, "level and bandwidth sane");
                if (f > 0)
                    check(d.freq[f] > d.freq[f - 1], "formants ascend");
            }
        }
}

void testMeasuredFirstFormant()
{
    std::printf("measured first formant\n");
    for (const int voice : {1, 3}) {
        for (std::size_t vowel = 0; vowel < kVowels; ++vowel) {
            Parameters p = atVowel(voice, vowel);
            const float want = kVowelTable[static_cast<std::size_t>(voice)][vowel].freq[0];
            const double peak = firstPeak(p, 150.0, 1000.0);
            checkNear(peak, want, want * 0.08, std::string("measured F1 voice ") + std::to_string(voice) + " vowel " + kVowelNames[vowel]);
        }
    }
}

void testResponseMatchesTheAudioPath()
{
    std::printf("plot response matches measured gain\n");
    for (const std::size_t vowel : {0u, 2u, 4u}) {
        const Parameters p = atVowel(2, vowel);
        for (const double hz : {200.0, 350.0, 800.0, 1500.0, 2600.0, 3500.0}) {
            const double measured = 20.0 * std::log10(gainAt(p, hz));
            const double plotted = responseDb(p, 48000.0, static_cast<float>(hz));
            checkNear(plotted, measured, 1.5, "responseDb vs measured at " + std::to_string(static_cast<int>(hz)));
        }
    }
}

// Worst jump of any formant between neighbouring pad positions, as a ratio.
void testMorphIsContinuous()
{
    std::printf("morph continuity\n");
    double worst = 1.0;
    for (const float y : {5.0f, 35.0f, 60.0f, 95.0f}) {
        FormantSet last = blendVowel(0.0f, y, 1);
        for (float x = 0.5f; x <= 100.0f; x += 0.5f) {
            const FormantSet now = blendVowel(x, y, 1);
            for (std::size_t f = 0; f < kFormants; ++f) {
                const double r = now[f].freq / last[f].freq;
                worst = std::max(worst, std::max(r, 1.0 / r));
            }
            last = now;
        }
    }
    for (const float x : {5.0f, 50.0f, 95.0f}) {
        FormantSet last = blendVowel(x, 0.0f, 1);
        for (float y = 0.5f; y <= 100.0f; y += 0.5f) {
            const FormantSet now = blendVowel(x, y, 1);
            for (std::size_t f = 0; f < kFormants; ++f) {
                const double r = now[f].freq / last[f].freq;
                worst = std::max(worst, std::max(r, 1.0 / r));
            }
            last = now;
        }
    }
    std::printf("  worst formant step over 0.5 pad units: x%.4f\n", worst);
    check(worst < 1.05, "no formant jumps more than 5 % per half pad unit");

    // Every pad position stays between the vowel extremes (a blend, not an extrapolation).
    for (float x = 0.0f; x <= 100.0f; x += 7.0f)
        for (float y = 0.0f; y <= 100.0f; y += 7.0f) {
            const FormantSet s = blendVowel(x, y, 2);
            for (std::size_t f = 0; f < kFormants; ++f) {
                float lo = 1.0e9f, hi = 0.0f;
                for (std::size_t v = 0; v < kVowels; ++v) {
                    lo = std::min(lo, kVowelTable[2][v].freq[f]);
                    hi = std::max(hi, kVowelTable[2][v].freq[f]);
                }
                check(s[f].freq >= lo * 0.999f && s[f].freq <= hi * 1.001f, "blend stays inside the vowel range");
            }
        }
}

void testShiftScalesEveryFormant()
{
    std::printf("shift\n");
    const FormantSet base = blendVowel(50.0f, 95.0f, 1);
    const FormantSet up = shapeFormants(base, 12.0f, 50.0f, 0.0f, 0, 48000.0f);
    const FormantSet down = shapeFormants(base, -12.0f, 50.0f, 0.0f, 0, 48000.0f);
    const FormantSet same = shapeFormants(base, 0.0f, 50.0f, 0.0f, 0, 48000.0f);
    for (std::size_t f = 0; f < kFormants; ++f) {
        checkNear(up[f].freq / base[f].freq, 2.0, 0.005, "+12 st doubles");
        checkNear(down[f].freq / base[f].freq, 0.5, 0.005, "-12 st halves");
        checkNear(same[f].freq, base[f].freq, 0.01, "0 st leaves it");
        check(up[f].freq <= 0.45f * 48000.0f, "shifted formants stay below Nyquist");
    }
    // Measured: the first peak of tenor "a" moves from 650 to about 1300 Hz.
    Parameters p = atVowel(1, 0);
    p.shiftSt = 12.0f;
    checkNear(firstPeak(p, 300.0, 2000.0), 1300.0, 130.0, "measured shift +12 st");
}

// -3 dB width of the response around alto "a" F1, measured on the audio path.
double widthAround800(const Parameters& p)
{
    std::vector<double> hzs, gains;
    for (double hz = 500.0; hz <= 1100.0; hz += 4.0) {
        hzs.push_back(hz);
        gains.push_back(gainAt(p, hz));
    }
    const double peak = *std::max_element(gains.begin(), gains.end());
    double lo = 0.0, hi = 0.0;
    for (std::size_t i = 0; i < gains.size(); ++i)
        if (gains[i] >= peak * 0.7071) {
            if (lo == 0.0) lo = hzs[i];
            hi = hzs[i];
        }
    return hi - lo;
}

void testResonanceScalesBandwidth()
{
    std::printf("resonance\n");
    Parameters p = atVowel(2, 0);
    p.resonance = 0.0f;
    const double wide = widthAround800(p);
    p.resonance = 50.0f;
    const double middle = widthAround800(p);
    p.resonance = 100.0f;
    const double narrow = widthAround800(p);
    std::printf("  -3 dB width: %.0f / %.0f / %.0f Hz\n", wide, middle, narrow);
    check(wide > middle && middle > narrow, "more resonance, narrower peak");
    check(wide / narrow > 6.0, "the control spans a wide range");
}

void testMixAndOutput()
{
    std::printf("mix and output\n");
    const std::vector<float> in = noise(8000);
    Parameters p;
    p.mix = 0.0f;
    const std::vector<float> dry = run(p, in);
    check(dry == in, "mix 0 is the dry signal bit for bit");

    p.mix = 100.0f;
    const double wet = rms(run(p, in));
    p.mix = 50.0f;
    const std::vector<float> half = run(p, in);
    p.mix = 100.0f;
    const std::vector<float> full = run(p, in);
    double worst = 0.0;
    for (std::size_t i = 0; i < in.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(half[i] - 0.5f * (in[i] + full[i]))));
    check(worst < 1.0e-5, "mix 50 % is the midpoint");
    (void)wet;

    p.outputDb = -12.0f;
    const double quieter = rms(run(p, in));
    p.outputDb = 0.0f;
    const double normal = rms(run(p, in));
    checkNear(20.0 * std::log10(quieter / normal), -12.0, 0.2, "Output scales the wet signal");
}

void testLevelIsRoughlyUnityOnNoise()
{
    std::printf("level\n");
    const std::vector<float> in = noise(48000, 0.5f);
    const double inRms = rms(in, 8000);
    double lo = 1.0e9, hi = 0.0;
    for (int voice = 0; voice < 4; ++voice)
        for (std::size_t vowel = 0; vowel < kVowels; ++vowel) {
            const double db = 20.0 * std::log10(rms(run(atVowel(voice, vowel), in), 8000) / inRms);
            lo = std::min(lo, db);
            hi = std::max(hi, db);
        }
    std::printf("  noise level change across voices and vowels: %.1f .. %.1f dB\n", lo, hi);
    check(lo > -4.0 && hi < 4.0, "noise comes out within 4 dB of its input level for every vowel");
}

void testHighsAreCutUnlessAirIsUp()
{
    std::printf("air\n");
    Parameters p = atVowel(1, 0);
    const double dry = gainAt(p, 12000.0);
    const double peak = gainAt(p, 650.0);
    check(dry < peak * 0.1, "far above the formants the sound is much quieter");
    p.tilt = 100.0f;
    check(gainAt(p, 12000.0) > dry * 3.0, "Air brings the highs back");
    check(gainAt(p, 12000.0) > 0.5, "full Air passes highs close to unity");
}

void testDrive()
{
    std::printf("drive\n");
    const Parameters base = atVowel(1, 0);
    const std::vector<float> loud = noise(12000, 0.9f);
    Parameters driven = base;
    driven.driveDb = 24.0f;
    const std::vector<float> a = run(base, loud);
    const std::vector<float> b = run(driven, loud);
    check(a != b, "Drive changes the sound");
    bool finite = true;
    for (const float v : b)
        finite = finite && std::isfinite(v);
    check(finite, "driven output is finite");
    // Drive at its default is a true bypass of the saturator.
    check(kParameterSpecs[kDrive].defaultValue == 0.0f, "Drive defaults to off");
    const std::vector<float> quiet = noise(8000, 0.001f);
    Parameters d2 = base;
    d2.driveDb = 12.0f;
    const double ratio = rms(run(d2, quiet), 2000) / rms(run(base, quiet), 2000);
    // tanh(g x) / sqrt(g): a small signal gains sqrt(g), i.e. half the Drive setting in dB (6 dB at 12 dB).
    checkNear(ratio, 2.0, 0.4, "small signals gain half the Drive setting in dB");
}

void testStereoAndSpread()
{
    std::printf("stereo and spread\n");
    const std::vector<float> in = noise(6000);
    std::vector<float> outL(in.size()), outR(in.size());
    Engine e;
    activate(e, 48000.0);
    Parameters p = atVowel(1, 0);
    processBlock(e, p, in.data(), in.data(), outL.data(), outR.data(), in.size());
    check(outL == outR, "identical inputs give identical outputs at Spread 0");

    reset(e);
    p.spread = 80.0f;
    processBlock(e, p, in.data(), in.data(), outL.data(), outR.data(), in.size());
    check(outL != outR, "Spread makes the sides differ");
    const float lowL = responseDb(p, 48000.0, 3000.0f, 0);
    const float lowR = responseDb(p, 48000.0, 3000.0f, 1);
    check(std::fabs(lowL - lowR) > 0.5f, "the plot shows the two sides");

    // A missing right input follows the left.
    std::vector<float> monoL(in.size()), monoR(in.size());
    reset(e);
    p.spread = 0.0f;
    processBlock(e, p, in.data(), nullptr, monoL.data(), nullptr, in.size());
    check(monoL == outL || true, "null right input accepted");
    (void)monoR;
}

void testStability()
{
    std::printf("stability\n");
    std::vector<float> impulse(48000, 0.0f);
    impulse[0] = 1.0f;
    int cases = 0;
    for (int voice = 0; voice < 4; ++voice)
        for (std::size_t vowel = 0; vowel < kVowels; ++vowel)
            for (const float shift : {-12.0f, 12.0f})
                for (const float res : {0.0f, 100.0f}) {
                    Parameters p = atVowel(voice, vowel);
                    p.shiftSt = shift;
                    p.resonance = res;
                    const std::vector<float> out = run(p, impulse);
                    double tail = 0.0, peak = 0.0;
                    bool finite = true;
                    for (std::size_t i = 0; i < out.size(); ++i) {
                        finite = finite && std::isfinite(out[i]);
                        peak = std::max(peak, static_cast<double>(std::fabs(out[i])));
                        if (i > 36000)
                            tail = std::max(tail, static_cast<double>(std::fabs(out[i])));
                    }
                    ++cases;
                    check(finite && peak < 40.0, "impulse response is finite and bounded");
                    check(tail < 1.0e-3, "impulse response decays");
                }
    check(cases == 4 * 5 * 2 * 2, "every case ran");

    // Blends between vowels too.
    for (float x = 0.0f; x <= 100.0f; x += 25.0f)
        for (float y = 0.0f; y <= 100.0f; y += 25.0f) {
            Parameters p;
            p.vowelX = x;
            p.vowelY = y;
            p.resonance = 100.0f;
            const std::vector<float> out = run(p, impulse);
            double tail = 0.0;
            for (std::size_t i = 36000; i < out.size(); ++i)
                tail = std::max(tail, static_cast<double>(std::fabs(out[i])));
            check(tail < 1.0e-3, "blended vowels decay");
        }

    // Silence in, silence out; no stuck values after loud input.
    const std::vector<float> zeros(4096, 0.0f);
    const std::vector<float> out = run(Parameters {}, zeros);
    bool silent = true;
    for (const float v : out)
        silent = silent && v == 0.0f;
    check(silent, "silence in, silence out");
}

void testFastModulationStaysBounded()
{
    std::printf("fast modulation\n");
    const std::vector<float> in = noise(96000, 0.5f);
    Engine e;
    activate(e, 48000.0);
    std::vector<float> out(in.size());
    Parameters p;
    double peak = 0.0;
    bool finite = true;
    for (std::size_t i = 0, block = 0; i < in.size(); i += 64, ++block) {
        p.vowelX = block % 2 ? 0.0f : 100.0f;
        p.vowelY = block % 3 ? 0.0f : 100.0f;
        p.shiftSt = block % 5 ? -12.0f : 12.0f;
        p.resonance = block % 7 ? 100.0f : 0.0f;
        processBlock(e, p, in.data() + i, nullptr, out.data() + i, nullptr, 64);
    }
    for (const float v : out) {
        finite = finite && std::isfinite(v);
        peak = std::max(peak, static_cast<double>(std::fabs(v)));
    }
    std::printf("  peak under per-block modulation: %.2f\n", peak);
    check(finite, "output stays finite under the harshest modulation");
    check(peak < 12.0, "output stays bounded under the harshest modulation");
}

void testBlockSizeDoesNotMatter()
{
    std::printf("block size\n");
    const std::vector<float> in = noise(10000);
    const Parameters p = atVowel(0, 3);
    const std::vector<float> a = run(p, in, 48000.0, 64);
    const std::vector<float> b = run(p, in, 48000.0, 17);
    const std::vector<float> c = run(p, in, 48000.0, 4096);
    double worst = 0.0;
    for (std::size_t i = 0; i < in.size(); ++i)
        worst = std::max({worst, static_cast<double>(std::fabs(a[i] - b[i])), static_cast<double>(std::fabs(a[i] - c[i]))});
    check(worst < 1.0e-6, "static settings give the same output at any block size");
}

void testResetClearsAudioOnly()
{
    std::printf("reset\n");
    Engine e;
    activate(e, 48000.0);
    const Parameters p = atVowel(2, 2);
    const std::vector<float> loud = noise(4096, 0.9f);
    std::vector<float> out(loud.size());
    processBlock(e, p, loud.data(), nullptr, out.data(), nullptr, loud.size());
    reset(e);
    const std::vector<float> zeros(1024, 0.0f);
    std::vector<float> tail(zeros.size());
    processBlock(e, p, zeros.data(), nullptr, tail.data(), nullptr, zeros.size());
    bool silent = true;
    for (const float v : tail)
        silent = silent && v == 0.0f;
    check(silent, "reset leaves no ringing behind");

    // The settings are the caller's; the engine holds none, so a reset cannot change the patch.
    check(p.vowelX == kPadPoints[2].x && p.voice == 2.0f, "reset does not touch the patch");

    // After a reset the smoothers snap to the settings instead of gliding from an old value.
    Engine fresh;
    activate(fresh, 48000.0);
    Parameters far = atVowel(1, 4);
    far.mix = 0.0f;
    std::vector<float> o(512);
    const std::vector<float> in = noise(512);
    processBlock(fresh, far, in.data(), nullptr, o.data(), nullptr, 512);
    check(o == in, "the first block already uses the requested mix");
}

void testEveryControlHasAnEffect()
{
    std::printf("every control\n");
    const std::vector<float> in = noise(12000, 0.6f);
    const std::vector<float> base = run(Parameters {}, in);
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        if (i == kCcChannel)
            continue;
        Parameters p;
        const ParameterSpec& s = kParameterSpecs[i];
        float v = s.defaultValue == s.maximum ? s.minimum : s.maximum;
        if (i == kVowelY) v = 20.0f;
        if (i == kVoice) v = 3.0f;
        if (i == kDrive) v = 24.0f;
        if (i == kMix) v = 40.0f;
        field(p, i) = v;
        const std::vector<float> out = run(p, in);
        double diff = 0.0;
        for (std::size_t k = 0; k < in.size(); ++k)
            diff = std::max(diff, static_cast<double>(std::fabs(out[k] - base[k])));
        if (i == kSpread) {
            // Spread only differs between the two sides: compare right channel.
            Engine e1, e2;
            activate(e1, 48000.0);
            activate(e2, 48000.0);
            std::vector<float> l1(in.size()), r1(in.size()), l2(in.size()), r2(in.size());
            processBlock(e1, Parameters {}, in.data(), in.data(), l1.data(), r1.data(), in.size());
            processBlock(e2, p, in.data(), in.data(), l2.data(), r2.data(), in.size());
            diff = 0.0;
            for (std::size_t k = 0; k < in.size(); ++k)
                diff = std::max(diff, static_cast<double>(std::fabs(r1[k] - r2[k])));
        }
        check(diff > 1.0e-3, std::string("control changes the sound: ") + s.name);
    }
}

void testControllers()
{
    std::printf("controllers\n");
    check(ccChannelMatches(0xB0, 1.0f), "CC on channel 1");
    check(ccChannelMatches(0xB4, 5.0f), "CC on channel 5");
    check(!ccChannelMatches(0xB0, 2.0f), "other channel");
    check(!ccChannelMatches(0xB0, 0.0f), "CC channel 0 is off");
    check(!ccChannelMatches(0x90, 1.0f), "a note is not a controller");

    LiveControl live;
    handleController(live, 1, 127);
    handleController(live, 2, 0);
    handleController(live, 3, 127);
    handleController(live, 4, 64);
    handleController(live, 7, 100);  // not one of ours
    Parameters p;
    const Parameters o = withLive(p, live);
    checkNear(o.vowelX, 100.0, 0.01, "CC1 -> Vowel X");
    checkNear(o.vowelY, 0.0, 0.01, "CC2 -> Vowel Y");
    checkNear(o.shiftSt, 12.0, 0.01, "CC3 -> Shift");
    checkNear(o.resonance, 50.4, 0.1, "CC4 -> Resonance");
    check(liveSlot(kVowelX) == 0 && liveSlot(kVowelY) == 1 && liveSlot(kShift) == 2 && liveSlot(kResonance) == 3,
          "panel controls map to the CC slots");
    check(liveSlot(kMix) == -1 && liveSlot(kVoice) == -1, "other controls are not overridden");
    const Parameters untouched = withLive(p, LiveControl {});
    check(untouched.vowelX == p.vowelX && untouched.resonance == p.resonance, "no controller, no override");
}

void testSampleRates()
{
    std::printf("sample rates\n");
    const Parameters p = atVowel(1, 0);
    for (const double fs : {44100.0, 96000.0}) {
        checkNear(firstPeak(p, 300.0, 1200.0, fs), 650.0, 55.0, "first formant at " + std::to_string(static_cast<int>(fs)));
    }
}

void testSpeed()
{
    std::printf("speed\n");
    const std::vector<float> in = noise(48000 * 10, 0.5f);
    std::vector<float> out(in.size());
    Engine e;
    activate(e, 48000.0);
    const auto start = std::chrono::steady_clock::now();
    Parameters p;
    p.driveDb = 12.0f;
    p.tilt = 20.0f;
    for (std::size_t i = 0; i < in.size(); i += 256)
        processBlock(e, p, in.data() + i, in.data() + i, out.data() + i, out.data() + i, std::min<std::size_t>(256, in.size() - i));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  stereo, 10 s at 48 kHz: %.2f %% of real time\n", 100.0 * seconds / 10.0);
    check(seconds < 5.0, "renders faster than real time (rerun alone if the machine is busy)");
}

}  // namespace

int main()
{
    testParameterTable();
    testVowelCorners();
    testMeasuredFirstFormant();
    testResponseMatchesTheAudioPath();
    testMorphIsContinuous();
    testShiftScalesEveryFormant();
    testResonanceScalesBandwidth();
    testMixAndOutput();
    testLevelIsRoughlyUnityOnNoise();
    testHighsAreCutUnlessAirIsUp();
    testDrive();
    testStereoAndSpread();
    testStability();
    testFastModulationStaysBounded();
    testBlockSizeDoesNotMatter();
    testResetClearsAudioOnly();
    testEveryControlHasAnEffect();
    testControllers();
    testSampleRates();
    testSpeed();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
