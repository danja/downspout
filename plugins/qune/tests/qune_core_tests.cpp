#include "downspout/test_assert.h"

#include "qune_engine.hpp"
#include "qune_params.hpp"
#include "qune_pitch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace downspout::qune;

namespace {

constexpr double kPi = 3.14159265358979323846;

// ── Signals ────────────────────────────────────────────────────────────────

double formantMagnitude(const double f, const double centre, const double bandwidth)
{
    const double x = (f * f - centre * centre) / (f * bandwidth);
    return 1.0 / std::sqrt(1.0 + x * x);
}

// Spectral envelope of the test vowel: three formants over a -12 dB/oct source.
double vowelEnvelope(const double f)
{
    return (formantMagnitude(f, 700.0, 110.0) + 0.7 * formantMagnitude(f, 1200.0, 130.0)
            + 0.4 * formantMagnitude(f, 2600.0, 170.0))
           / (1.0 + (f / 250.0) * (f / 250.0));
}

// Additive, so the period is exact: a sampled impulse train jitters by up to a
// sample and would test the detector against noise the signal should not have.
std::vector<float> vowel(const double f0, const double sampleRate, const int frames, const double gain = 1.0)
{
    std::vector<float> out(static_cast<std::size_t>(frames), 0.0f);
    double total = 0.0;
    std::vector<double> amps;
    for (int h = 1; h * f0 < 0.45 * sampleRate && h * f0 < 6000.0; ++h)
    {
        amps.push_back(vowelEnvelope(h * f0));
        total += amps.back();
    }
    for (std::size_t h = 0; h < amps.size(); ++h)
    {
        const double w = 2.0 * kPi * f0 * static_cast<double>(h + 1) / sampleRate;
        const double a = 0.5 * gain * amps[h] / total;
        for (int i = 0; i < frames; ++i)
            out[static_cast<std::size_t>(i)] += static_cast<float>(a * std::cos(w * i));
    }
    return out;
}

std::vector<float> noise(const int frames, const float gain, std::uint32_t seed = 12345u)
{
    std::vector<float> out(static_cast<std::size_t>(frames));
    for (float& v : out)
    {
        seed = seed * 1664525u + 1013904223u;
        v = gain * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

// Concatenate vowels at different pitches with phase continuity not required.
std::vector<float> sequence(const std::vector<std::pair<double, double>>& notes, const double sampleRate)
{
    std::vector<float> out;
    for (const auto& [hz, seconds] : notes)
    {
        const auto part = vowel(hz, sampleRate, static_cast<int>(seconds * sampleRate));
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

double cents(const double a, const double b)
{
    return 1200.0 * std::log2(a / b);
}

// ── Measurement, independent of PitchTracker ───────────────────────────────

double rms(const float* x, const int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / std::max(n, 1));
}

// Normalised autocorrelation over a long window, searched only inside the
// plausible band so an octave error cannot be reported as the answer.
double measureHz(const float* x, const int n, const double sampleRate, const double lowHz, const double highHz)
{
    const int minLag = static_cast<int>(sampleRate / highHz);
    const int maxLag = static_cast<int>(sampleRate / lowHz) + 1;
    const int window = n - maxLag - 2;
    if (window < 4 * maxLag)
        return 0.0;

    std::vector<double> score(static_cast<std::size_t>(maxLag + 2), 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double corr = 0.0, a = 0.0, b = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double u = x[i];
            const double v = x[i + lag];
            corr += u * v;
            a += u * u;
            b += v * v;
        }
        score[static_cast<std::size_t>(lag)] = (a + b > 0.0) ? 2.0 * corr / (a + b) : 0.0;
    }
    int best = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (score[static_cast<std::size_t>(lag)] > score[static_cast<std::size_t>(best)])
            best = lag;
    const double a = score[static_cast<std::size_t>(best - 1)];
    const double b = score[static_cast<std::size_t>(best)];
    const double c = score[static_cast<std::size_t>(best + 1)];
    const double curvature = a - 2.0 * b + c;
    const double offset = (std::fabs(curvature) > 1.0e-12) ? 0.5 * (a - c) / curvature : 0.0;
    return sampleRate / (static_cast<double>(best) + offset);
}

double goertzel(const float* x, const int n, const double hz, const double sampleRate)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double s = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / n * 2.0;
}

// ── Rig ────────────────────────────────────────────────────────────────────

struct Rig {
    explicit Rig(const double sampleRate = 48000.0)
        : sr(sampleRate)
    {
        activate(state, sampleRate);
    }

    void note(const int n, const int velocity = 100, const int channel = 0)
    {
        const std::uint8_t data[3] = {static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(n),
                                      static_cast<std::uint8_t>(velocity)};
        midiEvent(state, p, data, 3);
    }
    void noteOff(const int n, const int channel = 0)
    {
        const std::uint8_t data[3] = {static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(n), 0};
        midiEvent(state, p, data, 3);
    }
    void controller(const int cc, const int value, const int channel = 0)
    {
        const std::uint8_t data[3] = {static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(cc),
                                      static_cast<std::uint8_t>(value)};
        midiEvent(state, p, data, 3);
    }
    void bend(const int value14, const int channel = 0)
    {
        const std::uint8_t data[3] = {static_cast<std::uint8_t>(0xE0 | channel),
                                      static_cast<std::uint8_t>(value14 & 0x7F),
                                      static_cast<std::uint8_t>((value14 >> 7) & 0x7F)};
        midiEvent(state, p, data, 3);
    }

    // Runs `left`/`right` through the engine in host-sized blocks.
    void run(const std::vector<float>& left, const std::vector<float>& right, std::vector<float>& outLeft,
             std::vector<float>& outRight, const std::uint32_t block = 480)
    {
        const std::size_t total = left.size();
        outLeft.assign(total, 0.0f);
        outRight.assign(total, 0.0f);
        for (std::size_t i = 0; i < total; i += block)
        {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, total - i));
            processBlock(state, p, &left[i], &right[i], &outLeft[i], &outRight[i], n);
            for (std::size_t e = 0; e < midiOutCount(state); ++e)
            {
                const MidiOutEvent& event = midiOutEvents(state)[e];
                sent.push_back({i + event.frame, event.data[0], event.data[1], event.data[2]});
            }
        }
    }

    struct Sent {
        std::size_t frame;
        int status;
        int note;
        int velocity;
    };
    std::vector<Sent> sent;

    std::vector<float> run(const std::vector<float>& mono, const std::uint32_t block = 480)
    {
        std::vector<float> l, r;
        run(mono, mono, l, r, block);
        return l;
    }

    double sr;
    EngineState state;
    Parameters p;
};

int g_failures = 0;
int g_checks = 0;

void check(const bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkNear(const double actual, const double expected, const double tolerance, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    if (!ok)
        std::printf("  (%s: got %.4f, want %.4f +/- %.4f)\n", what.c_str(), actual, expected, tolerance);
    check(ok, what);
}

const char* const kQualityLabel[] = {"fast", "good", "best"};

// The last `seconds` of a rendered signal.
const float* tail(const std::vector<float>& x, const double sampleRate, const double seconds, int& count)
{
    count = static_cast<int>(seconds * sampleRate);
    return x.data() + (x.size() - static_cast<std::size_t>(count));
}

// ── Scales ─────────────────────────────────────────────────────────────────

void testScaleOrdinalsAreStable()
{
    std::printf("scale ordinals\n");
    check(static_cast<int>(ScaleId::chromatic) == 0, "chromatic == 0");
    check(static_cast<int>(ScaleId::major) == 1, "major == 1");
    check(static_cast<int>(ScaleId::minor) == 3, "minor == 3");
    check(static_cast<int>(ScaleId::bebopMinor) == 23, "bebopMinor == 23");
    check(static_cast<int>(ScaleId::count) == 24, "count == 24");
    check(kScaleNames.size() == 24, "24 scale names");
    check(kScaleDefs.size() == 24, "24 scale definitions");
}

void testScaleSetsMatchIndependentReference()
{
    std::printf("scale pitch content\n");
    // Held here independently of kScaleDefs; docs/scales.md is the source.
    const std::array<std::vector<int>, 24> expected = {{
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},  // chromatic
        {0, 2, 4, 5, 7, 9, 11},                  // major
        {0, 2, 4, 5, 7, 9, 11},                  // ionian
        {0, 2, 3, 5, 7, 8, 10},                  // minor
        {0, 2, 3, 5, 7, 8, 11},                  // harmonic minor
        {0, 2, 3, 5, 7, 9, 11},                  // melodic minor
        {0, 2, 3, 5, 7, 9, 10},                  // dorian
        {0, 1, 3, 5, 7, 8, 10},                  // phrygian
        {0, 2, 4, 6, 7, 9, 11},                  // lydian
        {0, 2, 4, 5, 7, 9, 10},                  // mixolydian
        {0, 1, 3, 5, 6, 8, 10},                  // locrian
        {0, 1, 4, 5, 7, 8, 10},                  // phrygian dominant
        {0, 1, 3, 5, 7, 9, 11},                  // neapolitan major
        {0, 1, 3, 5, 7, 8, 11},                  // neapolitan minor
        {0, 2, 4, 7, 9},                         // pentatonic major
        {0, 3, 5, 7, 10},                        // pentatonic minor
        {0, 3, 5, 6, 7, 10},                     // blues
        {0, 2, 4, 6, 8, 10},                     // whole tone
        {0, 1, 3, 4, 6, 8, 10},                  // altered
        {0, 1, 3, 4, 6, 7, 9, 10},               // half-whole diminished
        {0, 2, 3, 5, 6, 8, 9, 11},               // whole-half diminished
        {0, 2, 4, 5, 7, 9, 10, 11},              // bebop dominant
        {0, 2, 4, 5, 7, 8, 9, 11},               // bebop major
        {0, 2, 3, 4, 5, 7, 9, 10},               // bebop minor
    }};
    for (std::size_t s = 0; s < 24; ++s)
    {
        unsigned mask = 0;
        for (const int semitone : expected[s])
            mask |= 1u << semitone;
        check(scaleMask(s) == mask, std::string("mask of ") + kScaleNames[s]);
    }
}

void testRotatedMask()
{
    std::printf("rotated scale mask\n");
    // D major: D E F# G A B C#
    const unsigned dMajor = (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 11) | (1u << 1);
    check(rotatedScaleMask(static_cast<std::size_t>(ScaleId::major), 2) == dMajor, "D major");
    check(rotatedScaleMask(static_cast<std::size_t>(ScaleId::major), 0) == scaleMask(1), "root C is unrotated");
    check(rotatedScaleMask(static_cast<std::size_t>(ScaleId::chromatic), 7) == 0xFFFu, "chromatic is every root");
}

void testNearestInMask()
{
    std::printf("nearest note\n");
    const auto cMajor = rotatedScaleMask(1, 0);
    check(nearestInMask(60.2f, cMajor, -1, 0.0f).note == 60, "C+20c -> C");
    check(nearestInMask(61.4f, cMajor, -1, 0.0f).note == 62, "C#+40c -> D (C# is not in key)");
    check(nearestInMask(65.6f, cMajor, -1, 0.0f).note == 65, "F+60c -> F");
    check(nearestInMask(65.6f, cMajor, -1, 0.0f).valid, "valid choice");
    check(nearestInMask(59.4f, cMajor, -1, 0.0f).note == 59, "B-60c -> B");
    check(nearestInMask(-0.4f, cMajor, -1, 0.0f).note == 0, "very low notes still resolve");
    check(!nearestInMask(60.0f, 0, -1, 0.0f).valid, "empty mask has no target");

    // Hysteresis: between A and B (pentatonic gaps) the previous note sticks.
    const auto pent = rotatedScaleMask(14, 0);  // C D E G A
    const float nearBoundary = 69.0f + 0.52f;   // A +52c; G at 67, A at 69, C at 72
    check(nearestInMask(nearBoundary, pent, -1, 0.0f).note == 69, "A is nearest");
    // E=64, G=67: a pitch at 65.6 is closer to G (1.4) than E (1.6).
    check(nearestInMask(65.6f, pent, -1, 0.0f).note == 67, "closer to G without history");
    check(nearestInMask(65.6f, pent, 64, 0.30f).note == 64, "stays on E inside the stickiness band");
    check(nearestInMask(65.6f, pent, 64, 0.10f).note == 67, "leaves E once past the band");
    check(nearestInMask(65.6f, pent, 60, 0.50f).note == 67, "an unrelated previous note is ignored");

    // Exact tie resolves downward, deterministically.
    check(nearestInMask(61.0f, cMajor, -1, 0.0f).note == 60, "exact tie goes down");
}

// ── Parameters ─────────────────────────────────────────────────────────────

void testParameterTable()
{
    std::printf("parameter table\n");
    check(kParameterSpecs.size() == kParameterCount, "spec count");
    check(static_cast<std::size_t>(ParamId::outPitch) == kInputParameterCount, "outputs follow inputs");
    check(kParameterFields.size() == kInputParameterCount, "field table covers every input");

    const Parameters defaults;
    for (std::size_t i = 0; i < kParameterCount; ++i)
    {
        const ParamSpec& spec = kParameterSpecs[i];
        check(spec.minimum <= spec.defaultValue && spec.defaultValue <= spec.maximum,
              std::string("default in range: ") + spec.symbol);
        check(spec.output == (i >= kInputParameterCount), std::string("output flag: ") + spec.symbol);
        for (std::size_t j = i + 1; j < kParameterCount; ++j)
            check(std::string(spec.symbol) != kParameterSpecs[j].symbol, std::string("unique symbol: ") + spec.symbol);
        if (i < kInputParameterCount)
            check(defaults.*kParameterFields[i] == spec.defaultValue,
                  std::string("Parameters default matches spec: ") + spec.symbol);
    }

    Parameters wild;
    wild.scale = 99.0f;
    wild.glide = -5.0f;
    wild.quality = 7.0f;
    wild.amount = std::nanf("");
    const Parameters clamped = clampParameters(wild);
    check(clamped.scale == 23.0f && clamped.glide == 0.0f && clamped.quality == 2.0f && clamped.amount == 1.0f,
          "clampParameters bounds, rounds integers and replaces NaN with the default");
}

// ── Pitch tracker ──────────────────────────────────────────────────────────

void testTrackerAccuracyAcrossRates()
{
    std::printf("tracker accuracy\n");
    for (const double sr : {44100.0, 48000.0, 96000.0})
    {
        for (std::size_t r = 0; r < kRangeCount; ++r)
        {
            const PitchRange& range = kRanges[r];
            PitchTracker tracker;
            tracker.prepare(sr);
            tracker.setRange(range.minHz, range.maxHz);
            tracker.setVoicing(0.5f);
            for (const double f0 : {45.0, 82.4, 110.0, 196.0, 261.6, 440.0, 659.0, 880.0, 1200.0})
            {
                if (f0 < range.minHz * 1.1 || f0 > range.maxHz * 0.9)
                    continue;
                tracker.reset();
                const auto x = vowel(f0, sr, static_cast<int>(0.5 * sr));
                double worst = 0.0;
                int voiced = 0, total = 0;
                for (std::size_t i = 0; i + 32 <= x.size(); i += 32)
                {
                    tracker.process(&x[i], 32);
                    if (i > 0.25 * sr)
                    {
                        ++total;
                        if (tracker.result().voiced)
                        {
                            ++voiced;
                            worst = std::max(worst, std::fabs(cents(tracker.result().f0Hz, f0)));
                        }
                    }
                }
                char label[96];
                std::snprintf(label, sizeof(label), "%s %.0f Hz at %.0f", range.name, f0, sr);
                check(voiced == total, std::string("voiced throughout: ") + label);
                check(worst < 6.0, std::string("within 6 cents: ") + label);
            }
        }
    }
}

void testTrackerRejectsNoiseAndSilence()
{
    std::printf("tracker unvoiced input\n");
    PitchTracker tracker;
    tracker.prepare(48000.0);
    tracker.setRange(90.0f, 900.0f);
    tracker.setVoicing(0.5f);

    const auto n = noise(48000, 0.3f);
    int voiced = 0, total = 0;
    for (std::size_t i = 0; i + 32 <= n.size(); i += 32)
    {
        tracker.process(&n[i], 32);
        if (i > 12000)
        {
            ++total;
            voiced += tracker.result().voiced ? 1 : 0;
        }
    }
    check(voiced * 20 < total, "white noise is voiced less than 5% of the time");

    tracker.reset();
    const std::vector<float> silence(48000, 0.0f);
    voiced = 0;
    for (std::size_t i = 0; i + 32 <= silence.size(); i += 32)
    {
        tracker.process(&silence[i], 32);
        voiced += tracker.result().voiced ? 1 : 0;
    }
    check(voiced == 0, "silence is never voiced");

    tracker.reset();
    const auto quiet = vowel(150.0, 48000.0, 24000, 0.0005);
    voiced = 0;
    for (std::size_t i = 0; i + 32 <= quiet.size(); i += 32)
    {
        tracker.process(&quiet[i], 32);
        voiced += tracker.result().voiced ? 1 : 0;
    }
    check(voiced == 0, "a signal below the level gate is not tracked");
}

void testTrackerFollowsNoteChanges()
{
    std::printf("tracker note change\n");
    PitchTracker tracker;
    tracker.prepare(48000.0);
    tracker.setRange(90.0f, 900.0f);
    tracker.setVoicing(0.5f);
    const auto x = sequence({{200.0, 0.4}, {300.0, 0.4}}, 48000.0);

    int settled = -1;
    for (std::size_t i = 0; i + 32 <= x.size(); i += 32)
    {
        tracker.process(&x[i], 32);
        if (i >= 19200 && settled < 0 && tracker.result().voiced && std::fabs(cents(tracker.result().f0Hz, 300.0)) < 10.0)
            settled = static_cast<int>(i) - 19200;
    }
    check(settled >= 0, "reaches the new pitch");
    check(settled >= 0 && settled < 48000 * 40 / 1000, "within 40 ms of the change");
}

void testTrackerRejectsOctaveGlitch()
{
    std::printf("tracker octave glitch\n");
    PitchTracker tracker;
    tracker.prepare(48000.0);
    tracker.setRange(90.0f, 900.0f);
    tracker.setVoicing(0.5f);
    // A 1.5 ms burst of a different pitch in the middle of a steady note: it
    // can fool one analysis hop, but not the two in a row needed to believe a
    // large jump.
    auto x = vowel(200.0, 48000.0, 48000);
    const auto burst = vowel(400.0, 48000.0, 72);
    for (std::size_t i = 0; i < burst.size(); ++i)
        x[24000 + i] = burst[i];

    double worst = 0.0;
    for (std::size_t i = 0; i + 32 <= x.size(); i += 32)
    {
        tracker.process(&x[i], 32);
        if (i > 12000 && tracker.result().voiced)
            worst = std::max(worst, std::fabs(cents(tracker.result().f0Hz, 200.0)));
    }
    check(worst < 100.0, "a brief octave jump is not reported");
}

// ── Pitch correction through the engine ────────────────────────────────────

std::vector<float> renderVowel(Rig& rig, const double f0, const double seconds = 1.2)
{
    const auto x = vowel(f0, rig.sr, static_cast<int>(seconds * rig.sr));
    return rig.run(x);
}

double outputHz(const std::vector<float>& out, const double sr, const double expected)
{
    int count = 0;
    const float* seg = tail(out, sr, 0.35, count);
    return measureHz(seg, count, sr, expected / 1.4, expected * 1.4);
}

void testSnapsToScaleOnEveryTier()
{
    std::printf("snaps to scale\n");
    struct Case {
        double inHz;
        double expectedNote;
        int scale;
        int root;
        const char* label;
    };
    const double a4 = 440.0;
    const std::vector<Case> cases = {
        {a4 * std::pow(2.0, 0.30 / 12.0), 69.0, 0, 0, "chromatic: A4 +30c -> A4"},
        {a4 * std::pow(2.0, 0.70 / 12.0), 70.0, 0, 0, "chromatic: A4 +70c -> A#4"},
        {midiToHz(65.0f) * std::pow(2.0, 0.60 / 12.0), 65.0, 1, 0, "C major: F4 +60c -> F4 (F# not in key)"},
        {midiToHz(65.0f) * std::pow(2.0, 1.20 / 12.0), 67.0, 1, 0, "C major: F#4 +20c -> G4 (F# not in key)"},
        {midiToHz(75.0f) * std::pow(2.0, 0.20 / 12.0), 76.0, 1, 2, "D major: D#5 +20c -> E5 (D# not in key)"},
        {midiToHz(73.0f) * std::pow(2.0, 0.20 / 12.0), 73.0, 1, 2, "D major: C#5 +20c stays (C# is in key)"},
        {midiToHz(66.0f) * std::pow(2.0, 0.10 / 12.0), 67.0, 15, 9, "A minor pentatonic: F#4 +10c -> G4"},
    };

    for (int quality = 0; quality < 3; ++quality)
    {
        for (const Case& c : cases)
        {
            Rig rig;
            rig.p.quality = static_cast<float>(quality);
            rig.p.scale = static_cast<float>(c.scale);
            rig.p.root = static_cast<float>(c.root);
            rig.p.glide = 0.0f;
            const auto out = renderVowel(rig, c.inHz);
            const double target = midiToHz(static_cast<float>(c.expectedNote));
            const double got = outputHz(out, rig.sr, target);
            char label[160];
            std::snprintf(label, sizeof(label), "%s [%s]: %.2f Hz vs %.2f", c.label, kQualityLabel[quality], got, target);
            checkNear(cents(got, target), 0.0, 8.0, label);
            checkNear(targetNote(rig.state), c.expectedNote, 1.0e-3, std::string("status target ") + c.label);
        }
    }
}

void testEveryScaleOnlyProducesScaleNotes()
{
    std::printf("every scale\n");
    // Walk a chromatic sweep of input pitches and check the chosen target is a
    // member of the scale, using the independently held semitone sets.
    for (std::size_t scale = 0; scale < kScaleCount; ++scale)
    {
        for (const int root : {0, 5, 11})
        {
            const unsigned mask = rotatedScaleMask(scale, root);
            bool allMembers = true;
            for (float midi = 48.0f; midi < 84.0f; midi += 0.07f)
            {
                const NoteChoice c = nearestInMask(midi, static_cast<std::uint16_t>(mask), -1, 0.0f);
                if (!c.valid || !((mask >> (c.note % 12)) & 1u))
                    allMembers = false;
                // Nearest: no member is closer than the choice.
                for (int n = 40; n < 100; ++n)
                {
                    if (((mask >> (n % 12)) & 1u) && std::fabs(midi - static_cast<float>(n)) + 1.0e-4f < std::fabs(midi - static_cast<float>(c.note)))
                        allMembers = false;
                }
            }
            check(allMembers, std::string("members and nearest: ") + kScaleNames[scale]);
        }
    }
}

void testAmountScalesTheCorrection()
{
    std::printf("amount\n");
    const double sung = 440.0 * std::pow(2.0, 0.40 / 12.0);  // A4 +40c
    for (int quality = 0; quality < 3; ++quality)
    {
        for (const double amount : {0.0, 0.5, 1.0})
        {
            Rig rig;
            rig.p.quality = static_cast<float>(quality);
            rig.p.scale = 0.0f;
            rig.p.glide = 0.0f;
            rig.p.amount = static_cast<float>(amount);
            const auto out = renderVowel(rig, sung);
            const double expected = 440.0 * std::pow(2.0, 0.40 * (1.0 - amount) / 12.0);
            const double got = outputHz(out, rig.sr, expected);
            char label[96];
            std::snprintf(label, sizeof(label), "amount %.1f [%s]: %.1f cents off", amount, kQualityLabel[quality],
                          cents(got, expected));
            checkNear(cents(got, expected), 0.0, 8.0, label);
        }
    }
}

void testTransposeAddsToTheTarget()
{
    std::printf("transpose\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 0.0f;
        rig.p.glide = 0.0f;
        rig.p.transpose = 7.0f;
        const auto out = renderVowel(rig, 220.0 * std::pow(2.0, 0.2 / 12.0));
        const double expected = midiToHz(57.0f + 7.0f);
        checkNear(cents(outputHz(out, rig.sr, expected), expected), 0.0, 8.0,
                  std::string("A3 +20c up a fifth [") + kQualityLabel[quality] + "]");
    }
}

void testGlideSlidesBetweenNotes()
{
    std::printf("glide\n");
    // A4 +30c then A4 +80c: the corrected note changes A -> A# (chromatic).
    const auto x = sequence({{440.0 * std::pow(2.0, 0.30 / 12.0), 0.5}, {440.0 * std::pow(2.0, 0.80 / 12.0), 0.8}}, 48000.0);

    const auto trace = [&](const float glideMs, double& largestStep, double& settleSeconds) {
        Rig rig;
        rig.p.quality = 0.0f;
        rig.p.scale = 0.0f;
        rig.p.glide = glideMs;
        rig.p.stickiness = 0.0f;
        std::vector<float> out(x.size());
        double previous = 0.0;
        largestStep = 0.0;
        settleSeconds = -1.0;
        for (std::size_t i = 0; i + 32 <= x.size(); i += 32)
        {
            float dummy[32];
            processBlock(rig.state, rig.p, &x[i], &x[i], &out[i], dummy, 32);
            const double shift = shiftSemitones(rig.state);
            // Only the stretch after the note change counts.
            if (i > 24000)
            {
                largestStep = std::max(largestStep, std::fabs(shift - previous));
                if (settleSeconds < 0.0 && std::fabs(shift - 0.20) < 0.02 && std::fabs(previous - 0.20) < 0.02)
                    settleSeconds = static_cast<double>(i - 24000) / 48000.0;
            }
            previous = shift;
        }
    };

    double step0 = 0, settle0 = 0, step200 = 0, settle200 = 0;
    trace(0.0f, step0, settle0);
    trace(200.0f, step200, settle200);
    std::printf("  glide 0: largest step %.3f st, settled %.3f s;  glide 200: %.3f st, %.3f s\n", step0, settle0,
                step200, settle200);
    check(step0 > 0.3, "no glide: the correction jumps in a single step");
    check(settle0 >= 0.0 && settle0 < 0.06, "no glide: settles within one analysis window");
    check(step200 < 0.05, "200 ms glide: moves in small steps");
    check(settle200 > 0.15, "200 ms glide: takes a perceptible time to arrive");
}

void testStickinessHoldsTheNote()
{
    std::printf("stickiness\n");
    // Pentatonic C D E G A: 65.6 is closer to G than E. Start near E, drift up.
    const auto run = [](const float stickiness) {
        Rig rig;
        rig.p.quality = 0.0f;
        rig.p.scale = 14.0f;
        rig.p.root = 0.0f;
        rig.p.glide = 0.0f;
        rig.p.stickiness = stickiness;
        // Phase-continuous: E4 held, then a slow slide to a pitch nearer G4.
        const int frames = 72000;
        std::vector<float> in(static_cast<std::size_t>(frames));
        double phase = 0.0;
        for (int i = 0; i < frames; ++i)
        {
            const double t = i / rig.sr;
            const double midi = 64.0 + 1.6 * std::clamp((t - 0.5) / 0.4, 0.0, 1.0);
            const double f0 = midiToHz(static_cast<float>(midi));
            phase += f0 / rig.sr;
            double v = 0.0;
            for (int h = 1; h * f0 < 5000.0; ++h)
                v += vowelEnvelope(h * f0) * std::cos(2.0 * kPi * h * phase);
            in[static_cast<std::size_t>(i)] = static_cast<float>(0.1 * v);
        }
        rig.run(in);
        return targetNote(rig.state);
    };
    checkNear(run(0.0f), 67.0, 1.0e-3, "no stickiness: crosses to G");
    checkNear(run(40.0f), 64.0, 1.0e-3, "40 cents of stickiness: stays on E");
}

void testVoicingControlsSensitivity()
{
    std::printf("voicing\n");
    PitchTracker strict, lax;
    strict.prepare(48000.0);
    lax.prepare(48000.0);
    strict.setRange(90.0f, 900.0f);
    lax.setRange(90.0f, 900.0f);
    strict.setVoicing(0.0f);
    lax.setVoicing(1.0f);
    auto tone = vowel(180.0, 48000.0, 48000, 0.5);
    const auto hiss = noise(48000, 0.4f, 99u);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] += hiss[i];
    int strictVoiced = 0, laxVoiced = 0;
    for (std::size_t i = 0; i + 32 <= tone.size(); i += 32)
    {
        strict.process(&tone[i], 32);
        lax.process(&tone[i], 32);
        if (i > 12000)
        {
            strictVoiced += strict.result().voiced ? 1 : 0;
            laxVoiced += lax.result().voiced ? 1 : 0;
        }
    }
    std::printf("  noisy tone voiced: strict %d, lax %d\n", strictVoiced, laxVoiced);
    check(laxVoiced > strictVoiced, "a higher Voicing accepts a noisier pitch");
}

// ── MIDI ───────────────────────────────────────────────────────────────────

void testMidiNoteTargetsOnEveryTier()
{
    std::printf("midi note\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.source = 2.0f;
        rig.p.glide = 0.0f;
        rig.note(64);
        const auto out = renderVowel(rig, 146.8);  // D3 sung, E4 asked for
        const double expected = midiToHz(64.0f);
        checkNear(cents(outputHz(out, rig.sr, expected), expected), 0.0, 8.0,
                  std::string("sings the played note [") + kQualityLabel[quality] + "]");
        checkNear(targetNote(rig.state), 64.0, 1.0e-3, "status target");
    }
}

void testMidiNoteChangesFollowTheKeyboard()
{
    std::printf("midi note change\n");
    Rig rig;
    rig.p.quality = 0.0f;
    rig.p.source = 2.0f;
    rig.p.glide = 0.0f;
    const auto x = vowel(200.0, rig.sr, 96000);
    std::vector<float> out(x.size());
    rig.note(60);
    for (std::size_t i = 0; i < x.size(); i += 480)
    {
        if (i == 48000)
            rig.note(67);
        float dummy[480];
        processBlock(rig.state, rig.p, &x[i], &x[i], &out[i], dummy, 480);
    }
    const double first = measureHz(&out[30000], 12000, rig.sr, 200.0, 350.0);
    const double second = measureHz(&out[84000], 12000, rig.sr, 300.0, 520.0);
    checkNear(cents(first, midiToHz(60.0f)), 0.0, 8.0, "first note");
    checkNear(cents(second, midiToHz(67.0f)), 0.0, 8.0, "second note");
}

void testMidiChordSnapsToNearestHeldNote()
{
    std::printf("midi chord\n");
    Rig rig;
    rig.p.quality = 0.0f;
    rig.p.source = 1.0f;
    rig.p.glide = 0.0f;
    rig.note(60);
    rig.note(64);
    rig.note(67);  // C E G
    renderVowel(rig, 440.0);
    checkNear(targetNote(rig.state), 67.0 + 0.0, 1.0e-3, "A4 -> G4 (2 semitones) rather than C5 (3)");

    // Octave-agnostic: a held middle C also corrects a voice an octave down.
    Rig octave;
    octave.p.quality = 0.0f;
    octave.p.source = 1.0f;
    octave.p.glide = 0.0f;
    octave.note(60);
    renderVowel(octave, midiToHz(48.0f) * std::pow(2.0, 0.2 / 12.0));
    checkNear(targetNote(octave.state), 48.0, 1.0e-3, "a held pitch class applies in every octave");
}

void testMidiNoNotesFallback()
{
    std::printf("midi, no notes held\n");
    const double sung = midiToHz(65.0f) * std::pow(2.0, 1.2 / 12.0);  // F#4 +20c: nearest key note is G
    {
        Rig rig;
        rig.p.quality = 0.0f;
        rig.p.source = 2.0f;
        rig.p.noNotes = 0.0f;  // bypass
        rig.p.scale = 1.0f;
        const auto out = renderVowel(rig, sung);
        const double got = outputHz(out, rig.sr, sung);
        checkNear(cents(got, sung), 0.0, 6.0, "bypass leaves the voice alone");
        check(targetNote(rig.state) == 0.0f, "bypass reports no target");
    }
    {
        Rig rig;
        rig.p.quality = 0.0f;
        rig.p.source = 2.0f;
        rig.p.noNotes = 1.0f;  // use scale
        rig.p.scale = 1.0f;
        rig.p.glide = 0.0f;
        const auto out = renderVowel(rig, sung);
        const double target = midiToHz(67.0f);
        checkNear(cents(outputHz(out, rig.sr, target), target), 0.0, 8.0, "fallback snaps to the scale");
    }
    {
        Rig rig;
        rig.p.quality = 0.0f;
        rig.p.source = 2.0f;
        rig.p.noNotes = 0.0f;
        rig.p.glide = 0.0f;
        rig.note(64);
        rig.noteOff(64);
        const auto out = renderVowel(rig, sung);
        checkNear(cents(outputHz(out, rig.sr, sung), sung), 0.0, 6.0, "after note-off the voice is released");
    }
}

void testMidiPitchBend()
{
    std::printf("midi pitch bend\n");
    Rig rig;
    rig.p.quality = 0.0f;
    rig.p.source = 2.0f;
    rig.p.glide = 0.0f;
    rig.p.bendRange = 2.0f;
    rig.note(60);
    rig.bend(16383);
    renderVowel(rig, 200.0);
    checkNear(targetNote(rig.state), 62.0, 0.01, "full bend up is the bend range");

    Rig wide;
    wide.p.quality = 0.0f;
    wide.p.source = 2.0f;
    wide.p.glide = 0.0f;
    wide.p.bendRange = 12.0f;
    wide.note(60);
    wide.bend(0);
    renderVowel(wide, 200.0);
    checkNear(targetNote(wide.state), 48.0, 0.01, "full bend down on a 12 semitone range");

    Rig centre;
    centre.p.quality = 0.0f;
    centre.p.source = 2.0f;
    centre.p.glide = 0.0f;
    centre.note(60);
    centre.bend(8192);
    renderVowel(centre, 200.0);
    checkNear(targetNote(centre.state), 60.0, 0.001, "centre bend does nothing");
}

void testMidiSustainPedal()
{
    std::printf("midi sustain pedal\n");
    Rig rig;
    rig.p.quality = 0.0f;
    rig.p.source = 2.0f;
    rig.p.glide = 0.0f;
    rig.p.noNotes = 0.0f;
    rig.controller(64, 127);
    rig.note(62);
    rig.noteOff(62);
    renderVowel(rig, 200.0, 0.6);
    checkNear(targetNote(rig.state), 62.0, 1.0e-3, "note held by the pedal");
    rig.controller(64, 0);
    renderVowel(rig, 200.0, 0.6);
    check(targetNote(rig.state) == 0.0f, "released when the pedal lifts");

    Rig panic;
    panic.p.quality = 0.0f;
    panic.p.source = 2.0f;
    panic.p.noNotes = 0.0f;
    panic.note(62);
    panic.controller(123, 0);
    renderVowel(panic, 200.0, 0.6);
    check(targetNote(panic.state) == 0.0f, "all-notes-off clears the keys");
}

void testMidiChannelFilter()
{
    std::printf("midi channel\n");
    Rig rig;
    rig.p.quality = 0.0f;
    rig.p.source = 2.0f;
    rig.p.noNotes = 0.0f;
    rig.p.midiCh = 3.0f;
    rig.note(60, 100, 0);  // channel 1: ignored
    renderVowel(rig, 200.0, 0.6);
    check(targetNote(rig.state) == 0.0f, "other channels are ignored");
    rig.note(62, 100, 2);  // channel 3
    renderVowel(rig, 200.0, 0.6);
    checkNear(targetNote(rig.state), 62.0, 1.0e-3, "the chosen channel is heard");

    Rig omni;
    omni.p.quality = 0.0f;
    omni.p.source = 2.0f;
    omni.p.midiCh = 0.0f;
    omni.note(65, 100, 9);
    renderVowel(omni, 200.0, 0.6);
    checkNear(targetNote(omni.state), 65.0, 1.0e-3, "All listens on every channel");
}

void testNoteStackOverflowDropsTheOldest()
{
    std::printf("midi note stack\n");
    Rig rig;
    rig.p.source = 2.0f;
    for (int i = 0; i < 40; ++i)
        rig.note(40 + i);
    check(rig.state.noteCount == kMaxHeldNotes, "stack is capped");
    check(rig.state.noteStack[kMaxHeldNotes - 1] == 79, "newest note is on top");
    rig.note(79);  // re-press moves rather than duplicates
    check(rig.state.noteCount == kMaxHeldNotes, "re-pressing a held note does not add a copy");
}

// ── Formants ───────────────────────────────────────────────────────────────

// Spread (dB standard deviation) of the output harmonics relative to a
// reference envelope evaluated at `envelopeShift` times the harmonic's frequency.
double envelopeMismatch(const std::vector<float>& out, const double sr, const double f0Out, const double envelopeScale)
{
    int count = 0;
    const float* seg = tail(out, sr, 0.3, count);
    std::vector<double> ratios;
    for (int h = 1; h * f0Out < 3200.0; ++h)
    {
        const double f = h * f0Out;
        const double amp = goertzel(seg, count, f, sr);
        const double want = vowelEnvelope(f / envelopeScale);
        if (amp > 1.0e-5 && want > 1.0e-4)
            ratios.push_back(20.0 * std::log10(amp / want));
    }
    if (ratios.size() < 4)
        return 99.0;
    double mean = 0.0;
    for (const double r : ratios)
        mean += r;
    mean /= static_cast<double>(ratios.size());
    double var = 0.0;
    for (const double r : ratios)
        var += (r - mean) * (r - mean);
    return std::sqrt(var / static_cast<double>(ratios.size()));
}

void testFormantPreserveKeepsTheEnvelope()
{
    std::printf("formant preserve\n");
    const double sungHz = 150.0;
    const double shift = 7.0;  // semitones up
    const double ratio = std::pow(2.0, shift / 12.0);
    for (int quality = 1; quality <= 2; ++quality)
    {
        double spreadPreserve = 0.0, spreadFollow = 0.0;
        for (const int mode : {1, 0})
        {
            Rig rig;
            rig.p.quality = static_cast<float>(quality);
            rig.p.source = 2.0f;
            rig.p.glide = 0.0f;
            rig.p.formantMode = static_cast<float>(mode);
            rig.note(static_cast<int>(std::lround(hzToMidi(static_cast<float>(sungHz * ratio)))));
            // Choose the sung pitch so the shift is exactly `shift` semitones.
            const double noteHz = midiToHz(std::roundf(hzToMidi(static_cast<float>(sungHz * ratio))));
            const double input = noteHz / ratio;
            const auto out = renderVowel(rig, input, 1.5);
            const double outHz = outputHz(out, rig.sr, noteHz);
            // Preserve: the envelope stays at 1x. Follow: it has moved by `ratio`.
            const double expectedScale = (mode == 1) ? 1.0 : ratio;
            const double matched = envelopeMismatch(out, rig.sr, outHz, expectedScale);
            const double other = envelopeMismatch(out, rig.sr, outHz, (mode == 1) ? ratio : 1.0);
            (mode == 1 ? spreadPreserve : spreadFollow) = matched;
            std::printf("  %s %s: mismatch vs expected %.2f dB, vs other %.2f dB\n", kQualityLabel[quality],
                        mode == 1 ? "preserve" : "follow", matched, other);
            check(matched < other, std::string("the output envelope sits where the mode says [") + kQualityLabel[quality]
                                       + (mode == 1 ? " preserve]" : " follow]"));
            check(matched < 5.0, std::string("envelope within 5 dB spread [") + kQualityLabel[quality]
                                     + (mode == 1 ? " preserve]" : " follow]"));
        }
        (void)spreadPreserve;
        (void)spreadFollow;
    }
}

void testFormantShiftMovesTheEnvelope()
{
    std::printf("formant shift\n");
    const double scale = std::pow(2.0, 7.0 / 12.0);
    for (int quality = 1; quality <= 2; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.source = 2.0f;
        rig.p.glide = 0.0f;
        rig.p.formantMode = 1.0f;
        rig.p.formantShift = 7.0f;
        rig.note(60);
        const double inputHz = midiToHz(60.0f) / std::pow(2.0, 3.0 / 12.0);  // a 3 semitone shift
        const auto out = renderVowel(rig, inputHz, 1.5);
        const double outHz = outputHz(out, rig.sr, midiToHz(60.0f));
        const double moved = envelopeMismatch(out, rig.sr, outHz, scale);
        const double stayed = envelopeMismatch(out, rig.sr, outHz, 1.0);
        std::printf("  %s: shifted-envelope mismatch %.2f dB, unshifted %.2f dB\n", kQualityLabel[quality], moved, stayed);
        check(moved < stayed, std::string("Formant Shift moves the envelope [") + kQualityLabel[quality] + "]");
    }
}

void testFastTierFormantsFollowThePitch()
{
    std::printf("fast tier formants\n");
    const double ratio = std::pow(2.0, 7.0 / 12.0);
    Rig rig;
    rig.p.quality = 0.0f;
    rig.p.source = 2.0f;
    rig.p.glide = 0.0f;
    rig.p.formantMode = 1.0f;  // ignored by this tier
    const double noteHz = midiToHz(67.0f);
    rig.note(67);
    const auto out = renderVowel(rig, noteHz / ratio, 1.5);
    const double outHz = outputHz(out, rig.sr, noteHz);
    const double followed = envelopeMismatch(out, rig.sr, outHz, ratio);
    const double kept = envelopeMismatch(out, rig.sr, outHz, 1.0);
    std::printf("  followed %.2f dB, kept %.2f dB\n", followed, kept);
    check(followed < kept, "Fast resamples, so formants move with the pitch");
}

// ── Latency, level, routing ────────────────────────────────────────────────

void testLatencyOfAnImpulse()
{
    std::printf("latency\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        for (const double sr : {44100.0, 48000.0, 96000.0})
        {
            Rig rig(sr);
            rig.p.quality = static_cast<float>(quality);
            std::vector<float> x(static_cast<std::size_t>(sr), 0.0f);
            x[2000] = 1.0f;
            const auto out = rig.run(x);
            std::size_t peak = 0;
            for (std::size_t i = 0; i < out.size(); ++i)
                if (std::fabs(out[i]) > std::fabs(out[peak]))
                    peak = i;
            const int reported = latencySamples(rig.state);
            char label[96];
            std::snprintf(label, sizeof(label), "impulse arrives at the reported latency [%s %.0f Hz: %d]",
                          kQualityLabel[quality], sr, reported);
            check(static_cast<int>(peak) - 2000 == reported, label);
        }
    }
}

void testLatencyValues()
{
    std::printf("latency values\n");
    Rig rig;
    const auto ms = [&](const int samples) { return 1000.0 * samples / rig.sr; };
    const int fast = latencySamplesFor(rig.state, 0);
    const int good = latencySamplesFor(rig.state, 1);
    const int best = latencySamplesFor(rig.state, 2);
    std::printf("  fast %.1f ms, good %.1f ms, best %.1f ms\n", ms(fast), ms(good), ms(best));
    check(ms(fast) < 6.0, "Fast is under 6 ms");
    check(ms(good) < 35.0, "Good (Voice range) is under 35 ms");
    check(ms(best) < 50.0, "Best is under 50 ms");
    check(fast < good && good < best + 200, "Fast is the quickest tier");

    // Good's latency follows the range: a higher floor is faster.
    Rig lead;
    lead.p.range = 2.0f;
    lead.run(std::vector<float>(2000, 0.0f));
    check(latencySamplesFor(lead.state, 1) < good, "the Lead range lowers Good's latency");
    Rig bass;
    bass.p.range = 0.0f;
    bass.run(std::vector<float>(2000, 0.0f));
    check(latencySamplesFor(bass.state, 1) > good, "the Bass range raises it");
}

void testShiftedSignalIsDelayedByTheReportedLatency()
{
    std::printf("shifted latency\n");
    // An amplitude envelope with a sharp onset; measure when it appears.
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 0.0f;
        rig.p.transpose = 3.0f;
        rig.p.glide = 0.0f;
        auto x = vowel(220.0, rig.sr, 96000);
        for (std::size_t i = 0; i < 40000; ++i)
            x[i] *= 0.05f;  // quiet, then loud
        const auto out = rig.run(x);

        const auto onset = [](const std::vector<float>& s, const std::size_t from) {
            // First point where a 2 ms RMS crosses the midpoint between the quiet and loud levels.
            for (std::size_t i = from; i + 96 < s.size(); ++i)
                if (rms(&s[i], 96) > 0.5 * rms(&s[60000], 4800))
                    return static_cast<int>(i);
            return -1;
        };
        const int inOnset = onset(x, 30000);
        const int outOnset = onset(out, 30000);
        const double delayMs = 1000.0 * (outOnset - inOnset) / rig.sr;
        const double reportedMs = 1000.0 * latencySamples(rig.state) / rig.sr;
        std::printf("  %s: onset delay %.1f ms, reported %.1f ms\n", kQualityLabel[quality], delayMs, reportedMs);
        check(inOnset > 0 && outOnset > 0, "found both onsets");
        check(std::fabs(delayMs - reportedMs) < 8.0, std::string("onset delay within 8 ms of the report [")
                                                         + kQualityLabel[quality] + "]");
    }
}

void testLevelIsPreservedByShifting()
{
    std::printf("level\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        for (const double transpose : {-5.0, 4.0})
        {
            Rig rig;
            rig.p.quality = static_cast<float>(quality);
            rig.p.scale = 0.0f;
            rig.p.transpose = static_cast<float>(transpose);
            rig.p.glide = 0.0f;
            const auto x = vowel(190.0, rig.sr, 72000);
            const auto out = rig.run(x);
            const double in = rms(&x[48000], 12000);
            const double got = rms(&out[60000], 12000);
            const double db = 20.0 * std::log10(got / in);
            char label[96];
            std::snprintf(label, sizeof(label), "%s transpose %+.0f: %.2f dB", kQualityLabel[quality], transpose, db);
            checkNear(db, 0.0, 3.0, label);
        }
    }
}

void testStereoChannelsAreShiftedTogether()
{
    std::printf("stereo\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 0.0f;
        rig.p.transpose = 4.0f;
        rig.p.glide = 0.0f;
        const auto loud = vowel(200.0, rig.sr, 72000, 1.0);
        std::vector<float> quiet = loud;
        for (float& v : quiet)
            v *= 0.5f;
        std::vector<float> outL, outR;
        rig.run(loud, quiet, outL, outR);

        int count = 0;
        const float* l = tail(outL, rig.sr, 0.3, count);
        const float* r = tail(outR, rig.sr, 0.3, count);
        const double ratio = 20.0 * std::log10(rms(l, count) / rms(r, count));
        checkNear(ratio, 6.02, 0.5, std::string("level difference between channels survives [") + kQualityLabel[quality] + "]");

        const double expected = 200.0 * std::pow(2.0, 4.0 / 12.0);
        const double hzL = measureHz(l, count, rig.sr, expected / 1.2, expected * 1.2);
        const double hzR = measureHz(r, count, rig.sr, expected / 1.2, expected * 1.2);
        checkNear(cents(hzL, hzR), 0.0, 2.0, std::string("both channels at one pitch [") + kQualityLabel[quality] + "]");

        // Identical channels stay identical.
        Rig mono;
        mono.p = rig.p;
        std::vector<float> a, b;
        mono.run(loud, loud, a, b);
        double worst = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::fabs(a[i] - b[i])));
        check(worst < 1.0e-3, std::string("mono in gives mono out [") + kQualityLabel[quality] + "]");
    }
}

void testMixAndLevel()
{
    std::printf("mix and level\n");
    for (int quality = 1; quality <= 2; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 0.0f;
        rig.p.transpose = 5.0f;
        rig.p.mix = 0.0f;
        const auto x = vowel(210.0, rig.sr, 48000);
        const auto out = rig.run(x);
        const int delay = latencySamples(rig.state);
        double worst = 0.0;
        for (std::size_t i = static_cast<std::size_t>(delay) + 100; i < x.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::fabs(out[i] - x[i - static_cast<std::size_t>(delay)])));
        check(worst < 1.0e-5, std::string("Mix 0 is the input delayed by the latency [") + kQualityLabel[quality] + "]");
    }

    Rig quiet;
    quiet.p.quality = 1.0f;
    quiet.p.level = -6.0f;
    Rig loud;
    loud.p.quality = 1.0f;
    loud.p.level = 6.0f;
    const auto x = vowel(210.0, 48000.0, 48000);
    const auto a = quiet.run(x);
    const auto b = loud.run(x);
    checkNear(20.0 * std::log10(rms(&b[30000], 9000) / rms(&a[30000], 9000)), 12.0, 0.3, "Level spans 12 dB");
}

void testQualityChangeIsClickFree()
{
    std::printf("quality switch\n");
    Rig rig;
    rig.p.scale = 0.0f;
    rig.p.transpose = 2.0f;
    rig.p.glide = 0.0f;
    const auto x = vowel(180.0, rig.sr, 192000, 0.6);
    std::vector<float> out(x.size());
    for (std::size_t i = 0; i + 480 <= x.size(); i += 480)
    {
        if (i == 48000)
            rig.p.quality = 0.0f;
        if (i == 96000)
            rig.p.quality = 2.0f;
        if (i == 144000)
            rig.p.range = 2.0f;
        float dummy[480];
        processBlock(rig.state, rig.p, &x[i], &x[i], &out[i], dummy, 480);
    }
    double inStep = 0.0, outStep = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i)
        inStep = std::max(inStep, static_cast<double>(std::fabs(x[i] - x[i - 1])));
    // The first stretch is the input starting from silence, which is a step in
    // the input too; the switches come later.
    for (std::size_t i = 4000; i < out.size(); ++i)
    {
        outStep = std::max(outStep, static_cast<double>(std::fabs(out[i] - out[i - 1])));
        check(std::isfinite(out[i]), "finite output");
        if (!std::isfinite(out[i]))
            break;
    }
    std::printf("  largest step: in %.4f, out %.4f\n", inStep, outStep);
    check(outStep < 2.5 * inStep, "no discontinuity across quality and range changes");
}

void testNoClicksWhileCorrecting()
{
    std::printf("continuity\n");
    // A gliding voice that crosses several scale degrees: output must stay smooth.
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 1.0f;
        rig.p.glide = 30.0f;
        const double sr = rig.sr;
        const int frames = static_cast<int>(2.0 * sr);
        std::vector<float> x(static_cast<std::size_t>(frames));
        double phase = 0.0;
        for (int i = 0; i < frames; ++i)
        {
            const double f0 = 150.0 * std::pow(2.0, (i / sr) * 0.5);  // an octave in two seconds
            phase += f0 / sr;
            double v = 0.0;
            for (int h = 1; h * f0 < 5000.0; ++h)
                v += vowelEnvelope(h * f0) * std::cos(2.0 * kPi * h * phase);
            x[static_cast<std::size_t>(i)] = static_cast<float>(0.1 * v);
        }
        const auto out = rig.run(x);
        double inStep = 0.0, outStep = 0.0;
        for (std::size_t i = 1; i < x.size(); ++i)
            inStep = std::max(inStep, static_cast<double>(std::fabs(x[i] - x[i - 1])));
        for (std::size_t i = 20000; i < out.size(); ++i)
            outStep = std::max(outStep, static_cast<double>(std::fabs(out[i] - out[i - 1])));
        char label[96];
        std::snprintf(label, sizeof(label), "[%s] largest step %.3f vs input %.3f", kQualityLabel[quality], outStep, inStep);
        check(outStep < 3.0 * inStep, label);
    }
}

// ── Robustness ─────────────────────────────────────────────────────────────

void testSilenceAndIdleDecay()
{
    std::printf("silence\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 0.0f;
        rig.p.transpose = 3.0f;
        auto x = vowel(200.0, rig.sr, 48000);
        x.resize(48000 * 3, 0.0f);
        const auto out = rig.run(x);
        bool finite = true;
        for (const float v : out)
            finite = finite && std::isfinite(v);
        check(finite, std::string("finite throughout [") + kQualityLabel[quality] + "]");
        int count = 0;
        const float* end = tail(out, rig.sr, 0.5, count);
        check(rms(end, count) < 1.0e-6, std::string("falls silent [") + kQualityLabel[quality] + "]");
    }
}

void testIdleInputIsNotMoreExpensive()
{
    std::printf("idle cost\n");
    using Clock = std::chrono::steady_clock;
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 0.0f;
        const auto burst = vowel(200.0, rig.sr, 48000 * 3);
        const std::vector<float> silence(48000 * 3, 0.0f);

        const auto t0 = Clock::now();
        rig.run(burst);
        const auto t1 = Clock::now();
        rig.run(silence);
        const auto t2 = Clock::now();
        // Let denormals settle after the tail before timing a second silent run.
        rig.run(silence);
        const auto t3 = Clock::now();
        const double active = std::chrono::duration<double>(t1 - t0).count();
        const double idle = std::chrono::duration<double>(t2 - t1).count();
        const double idleAgain = std::chrono::duration<double>(t3 - t2).count();
        std::printf("  %s: active %.1f ms, idle %.1f ms, idle again %.1f ms (3 s of audio)\n", kQualityLabel[quality],
                    active * 1000.0, idle * 1000.0, idleAgain * 1000.0);
        check(idle < 2.0 * active + 0.005, std::string("silence after a note costs no more than the note [")
                                              + kQualityLabel[quality] + "]");
        check(idleAgain < 2.0 * active + 0.005, std::string("long silence stays cheap [") + kQualityLabel[quality] + "]");
    }
}

void testRealtimeBudget()
{
    std::printf("realtime budget\n");
    using Clock = std::chrono::steady_clock;
    // Worst case per tier: a voice that is always in need of correction, in the
    // widest range, at 96 kHz. A CI machine is slow and shared, so the bound is
    // generous; it still catches an accidental order-of-magnitude regression.
    const double limits[3] = {0.25, 0.25, 0.30};
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig(96000.0);
        rig.p.quality = static_cast<float>(quality);
        rig.p.range = 3.0f;
        rig.p.scale = 0.0f;
        rig.p.source = 2.0f;
        rig.note(64);
        const auto x = vowel(147.0, rig.sr, 96000 * 4);
        const auto t0 = Clock::now();
        rig.run(x, 256);
        const double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        const double load = seconds / 4.0;
        std::printf("  %s at 96 kHz, Wide range: %.1f%% of real time\n", kQualityLabel[quality], load * 100.0);
        check(load < limits[quality], std::string("within budget [") + kQualityLabel[quality] + "]");
    }
}

void testReactivationIsDeterministic()
{
    std::printf("reactivation\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig rig;
        rig.p.quality = static_cast<float>(quality);
        rig.p.scale = 0.0f;
        rig.p.transpose = 3.0f;
        const auto x = vowel(200.0, rig.sr, 48000);
        const auto first = rig.run(x);
        activate(rig.state, rig.sr);  // what a host does when it re-activates
        const auto second = rig.run(x);
        double worst = 0.0;
        for (std::size_t i = 0; i < first.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::fabs(first[i] - second[i])));
        check(worst == 0.0, std::string("a re-activated engine repeats itself exactly [") + kQualityLabel[quality] + "]");
    }
}

void testBlockSizeDoesNotChangeTheResult()
{
    std::printf("block size\n");
    for (int quality = 0; quality < 3; ++quality)
    {
        Rig a;
        a.p.quality = static_cast<float>(quality);
        a.p.scale = 0.0f;
        a.p.transpose = 3.0f;
        Rig b;
        b.p = a.p;
        const auto x = vowel(200.0, a.sr, 48000);
        const auto outA = a.run(x, 480);
        const auto outB = b.run(x, 97);
        double worst = 0.0;
        for (std::size_t i = 0; i < outA.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::fabs(outA[i] - outB[i])));
        // Sub-blocks follow the host's block edges, so control updates can land a
        // few samples apart; the audio differs by rounding-scale amounts only.
        check(worst < 1.0e-3, std::string("host block size is irrelevant [") + kQualityLabel[quality] + "]");
    }
}

void testInPlaceProcessing()
{
    std::printf("in place\n");
    Rig a;
    a.p.quality = 1.0f;
    a.p.scale = 0.0f;
    a.p.transpose = 3.0f;
    Rig b;
    b.p = a.p;
    const auto x = vowel(200.0, a.sr, 24000);
    const auto reference = a.run(x);
    std::vector<float> l = x, r = x;
    for (std::size_t i = 0; i < l.size(); i += 480)
        processBlock(b.state, b.p, &l[i], &r[i], &l[i], &r[i], 480);
    double worst = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(l[i] - reference[i])));
    check(worst == 0.0, "host buffers may be processed in place");
}

void testExtremeParametersStaySafe()
{
    std::printf("extreme parameters\n");
    Rig rig;
    rig.p.scale = 5.0f;
    rig.p.glide = 1000.0f;
    rig.p.stickiness = 50.0f;
    rig.p.transpose = 24.0f;
    rig.p.formantShift = 12.0f;
    rig.p.voicing = 1.0f;
    rig.p.level = 12.0f;
    rig.p.range = 3.0f;
    const auto x = vowel(100.0, rig.sr, 96000, 1.0);
    for (int quality = 0; quality < 3; ++quality)
    {
        rig.p.quality = static_cast<float>(quality);
        const auto out = rig.run(x);
        bool finite = true;
        double peak = 0.0;
        for (const float v : out)
        {
            finite = finite && std::isfinite(v);
            peak = std::max(peak, static_cast<double>(std::fabs(v)));
        }
        check(finite, std::string("finite at the extremes [") + kQualityLabel[quality] + "]");
        check(peak < 20.0, std::string("bounded at the extremes [") + kQualityLabel[quality] + "]");
    }

    // Garbage MIDI must not crash.
    const std::uint8_t junk[4][3] = {{0x00, 0, 0}, {0xF0, 1, 2}, {0x90, 200, 200}, {0xE0, 255, 255}};
    for (const auto& m : junk)
        midiEvent(rig.state, rig.p, m, 3);
    midiEvent(rig.state, rig.p, nullptr, 0);
    check(true, "junk MIDI survived");

    // Odd block sizes, including zero.
    float a[1] = {0.0f}, b[1] = {0.0f};
    processBlock(rig.state, rig.p, a, a, b, b, 0);
    processBlock(rig.state, rig.p, a, a, b, b, 1);
    check(true, "zero and one frame blocks survived");
}

// ── Every control does something ───────────────────────────────────────────

void testEveryControlHasAnEffect()
{
    std::printf("every control has an effect\n");

    // Material that exercises pitch, a note change, and MIDI.
    const double sr = 48000.0;
    const auto x = sequence({{200.0, 0.5},
                             {midiToHz(66.0f) * std::pow(2.0, 0.45 / 12.0), 0.5},
                             {300.0, 0.5},
                             {400.0 * std::pow(2.0, 0.3 / 12.0), 0.5}},
                            sr);

    struct Outcome {
        std::vector<float> audio;
        float target = 0.0f;
        float shift = 0.0f;
        int latency = 0;
    };

    const auto render = [&](const Parameters& p, const bool withMidi, const bool withBend, const int midiChannel) {
        Rig rig(sr);
        rig.p = p;
        if (withMidi)
        {
            rig.note(64, 100, midiChannel);
            rig.note(69, 100, midiChannel);
        }
        if (withBend)
            rig.bend(16383, midiChannel);
        Outcome o;
        o.audio = rig.run(x);
        o.target = targetNote(rig.state);
        o.shift = shiftSemitones(rig.state);
        o.latency = latencySamples(rig.state);
        return o;
    };

    const auto differs = [](const Outcome& a, const Outcome& b) {
        if (a.latency != b.latency || std::fabs(a.target - b.target) > 1.0e-4f || std::fabs(a.shift - b.shift) > 1.0e-4f)
            return true;
        double worst = 0.0;
        for (std::size_t i = 0; i < std::min(a.audio.size(), b.audio.size()); ++i)
            worst = std::max(worst, static_cast<double>(std::fabs(a.audio[i] - b.audio[i])));
        return worst > 1.0e-3;
    };

    Parameters base;
    base.quality = 1.0f;
    base.scale = 1.0f;
    base.glide = 0.0f;
    base.source = 0.0f;

    struct Probe {
        ParamId id;
        float value;
        bool midi;
        bool bend;
        Source source;
        float quality;
        float noNotesOverride;
        int midiChannel;
    };
    // Each control, set away from its default, in a situation where it matters.
    const std::vector<Probe> probes = {
        {ParamId::root, 3.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::scale, 3.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::source, 1.0f, true, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::noNotes, 0.0f, false, false, Source::midiExact, 1.0f, 1.0f, 0},
        {ParamId::midiCh, 5.0f, true, false, Source::midiExact, 1.0f, 0.0f, 0},
        {ParamId::bendRange, 12.0f, true, true, Source::midiExact, 1.0f, 1.0f, 0},
        {ParamId::amount, 0.3f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::glide, 300.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::transpose, 4.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::quality, 2.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::range, 2.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::formantMode, 0.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::formantShift, 6.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::mix, 0.4f, false, false, Source::scale, 1.0f, 1.0f, 0},
        {ParamId::level, -9.0f, false, false, Source::scale, 1.0f, 1.0f, 0},
    };

    for (const Probe& probe : probes)
    {
        Parameters reference = base;
        reference.source = static_cast<float>(probe.source);
        reference.quality = probe.quality;
        reference.noNotes = probe.noNotesOverride;
        Parameters changed = reference;
        changed.*kParameterFields[static_cast<std::size_t>(probe.id)] = probe.value;

        const Outcome a = render(reference, probe.midi, probe.bend, probe.midiChannel);
        const Outcome b = render(changed, probe.midi, probe.bend, probe.midiChannel);

        const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(probe.id)];
        check(differs(a, b), std::string("changing '") + spec.name + "' changes the result");
    }

    // Voicing and Stickiness only show on marginal material, so
    // testVoicingControlsSensitivity and testStickinessHoldsTheNote prove them.
    // Every input parameter has to be accounted for in one place or the other.
    std::array<bool, kInputParameterCount> covered {};
    for (const Probe& probe : probes)
        covered[static_cast<std::size_t>(probe.id)] = true;
    covered[static_cast<std::size_t>(ParamId::voicing)] = true;
    covered[static_cast<std::size_t>(ParamId::stickiness)] = true;
    // The MIDI Out controls act on the messages, not the audio: testMidiOutFollowsTheTargetNotes.
    covered[static_cast<std::size_t>(ParamId::midiOut)] = true;
    covered[static_cast<std::size_t>(ParamId::outChannel)] = true;
    covered[static_cast<std::size_t>(ParamId::outVelocity)] = true;
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        check(covered[i], std::string("effect test exists for '") + kParameterSpecs[i].name + "'");
}

// Note messages the host would receive: the quantised notes, one at a time.
void testMidiOutFollowsTheTargetNotes()
{
    auto x = sequence({{200.0, 0.4}, {300.0, 0.4}}, 48000.0);
    x.resize(x.size() + 14400, 0.0f);

    // G3 and D4 are both in C major; each is 35 cents sharp of the note it snaps to.
    Rig rig;
    rig.p.scale = 1.0f;
    rig.p.outChannel = 5.0f;
    rig.p.outVelocity = 80.0f;
    rig.run(x);

    std::vector<Rig::Sent> on, off;
    for (const auto& e : rig.sent)
        (e.status >= 0x90 ? on : off).push_back(e);
    check(on.size() == 2 && off.size() == 2, "two notes: two note-ons and two note-offs");
    if (on.size() == 2 && off.size() == 2)
    {
        check(on[0].note == 55 && on[1].note == 62, "note-ons are the snapped notes");
        check(on[0].status == 0x94 && off[0].status == 0x84, "messages use the chosen channel");
        check(on[0].velocity == 80 && off[0].velocity == 0, "velocity as set");
        check(off[0].note == 55 && off[1].note == 62, "each note is released");
        check(off[0].frame == on[1].frame, "legato: new note-on at the same frame as the old note-off");
        check(rig.sent[1].status >= 0x90 && rig.sent[2].status < 0x90, "new note-on goes before the old note-off");
        check(on[0].frame > 0.01 * 48000.0 && on[0].frame < 0.2 * 48000.0, "first note-on arrives shortly after onset");
        check(on[1].frame > 0.4 * 48000.0 && on[1].frame < 0.6 * 48000.0, "second note-on follows the second pitch");
        check(off[1].frame > 0.8 * 48000.0 && off[1].frame < 0.95 * 48000.0, "release follows the end of the sound");
    }

    // Transpose moves the sent notes with the audio.
    Rig up;
    up.p.transpose = 12.0f;
    up.run(x);
    check(!up.sent.empty() && up.sent[0].note == 67, "transpose applies to the MIDI note");

    // MIDI note mode sends the held key, bend or not.
    Rig held;
    held.p.source = 2.0f;
    held.note(64);
    held.bend(16383);
    held.run(x);
    check(!held.sent.empty() && held.sent[0].note == 64, "held key is sent, without the bend");

    // Switched off: nothing.
    Rig off_;
    off_.p.midiOut = 0.0f;
    off_.run(x);
    check(off_.sent.empty(), "MIDI Out off sends nothing");

    // Switching off mid-note releases it; so does a channel change.
    Rig mid;
    const std::size_t half = 12000;
    const std::vector<float> first(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(half));
    const std::vector<float> rest(x.begin() + static_cast<std::ptrdiff_t>(half), x.end());
    mid.run(first);
    check(mid.sent.size() == 1 && mid.sent[0].status == 0x90, "one note sounding");
    mid.p.midiOut = 0.0f;
    mid.run(rest);
    check(mid.sent.size() >= 2 && mid.sent[1].status == 0x80 && mid.sent[1].note == mid.sent[0].note,
          "turning it off releases the sounding note");

    Rig move;
    move.run(first);
    move.p.outChannel = 9.0f;
    move.run(rest);
    check(move.sent.size() >= 3 && move.sent[1].status == 0x80 && move.sent[2].status == 0x98,
          "a channel change releases on the old channel and starts on the new");

    // A reset (transport stop, re-activation) releases the note at the next block.
    Rig stop;
    stop.run(first);
    reset(stop.state);
    stop.run(rest);
    check(stop.sent.size() >= 2 && stop.sent[1].status == 0x80, "reset releases the sounding note");

    // Every on is matched by an off, whatever the material.
    const auto balanced = [](const Rig& r) {
        int sounding = 0;
        for (const auto& e : r.sent)
        {
            sounding += (e.status >= 0x90) ? 1 : -1;
            if (sounding < 0 || sounding > 2)
                return false;
        }
        return sounding == 0;
    };
    check(balanced(rig), "notes are balanced");
}

}  // namespace

int main()
{
    testScaleOrdinalsAreStable();
    testScaleSetsMatchIndependentReference();
    testRotatedMask();
    testNearestInMask();
    testParameterTable();

    testTrackerAccuracyAcrossRates();
    testTrackerRejectsNoiseAndSilence();
    testTrackerFollowsNoteChanges();
    testTrackerRejectsOctaveGlitch();

    testSnapsToScaleOnEveryTier();
    testEveryScaleOnlyProducesScaleNotes();
    testAmountScalesTheCorrection();
    testTransposeAddsToTheTarget();
    testGlideSlidesBetweenNotes();
    testStickinessHoldsTheNote();
    testVoicingControlsSensitivity();

    testMidiNoteTargetsOnEveryTier();
    testMidiNoteChangesFollowTheKeyboard();
    testMidiChordSnapsToNearestHeldNote();
    testMidiNoNotesFallback();
    testMidiPitchBend();
    testMidiSustainPedal();
    testMidiChannelFilter();
    testNoteStackOverflowDropsTheOldest();
    testMidiOutFollowsTheTargetNotes();

    testFormantPreserveKeepsTheEnvelope();
    testFormantShiftMovesTheEnvelope();
    testFastTierFormantsFollowThePitch();

    testLatencyOfAnImpulse();
    testLatencyValues();
    testShiftedSignalIsDelayedByTheReportedLatency();
    testLevelIsPreservedByShifting();
    testStereoChannelsAreShiftedTogether();
    testMixAndLevel();
    testQualityChangeIsClickFree();
    testNoClicksWhileCorrecting();

    testSilenceAndIdleDecay();
    testIdleInputIsNotMoreExpensive();
    testRealtimeBudget();
    testReactivationIsDeterministic();
    testBlockSizeDoesNotChangeTheResult();
    testInPlaceProcessing();
    testExtremeParametersStaySafe();
    testEveryControlHasAnEffect();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
