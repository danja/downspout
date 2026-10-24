#include "nam_dsp.hpp"
#include "nam_engine.hpp"
#include "nam_params.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace downspout::namfx;

#ifndef NAM_TEST_DATA_DIR
#error "NAM_TEST_DATA_DIR must point at plugins/nam/tests/data"
#endif

namespace {

const std::string kDataDir = NAM_TEST_DATA_DIR;
const std::string kWavenet = kDataDir + "/wavenet.nam";
const std::string kLstm = kDataDir + "/lstm.nam";

// ── Helpers ─────────────────────────────────────────────────────────────────

struct Lcg {
    std::uint32_t s = 12345;
    float next()  // [-1, 1)
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

std::vector<float> noise(const int n, const float amp, const std::uint32_t seed = 1)
{
    Lcg r;
    r.s = seed;
    std::vector<float> v(static_cast<std::size_t>(n));
    for (auto& x : v)
        x = amp * r.next();
    return v;
}

std::vector<float> sine(const int n, const double freq, const double sr, const float amp)
{
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<std::size_t>(i)] = amp * static_cast<float>(std::sin(2.0 * kPi * freq * i / sr));
    return v;
}

// A guitar-ish test signal: low, mid and high partials plus a little noise.
std::vector<float> richSignal(const int n, const double sr, const float amp)
{
    const auto a = sine(n, 82.0, sr, 0.4f), b = sine(n, 800.0, sr, 0.4f), c = sine(n, 5000.0, sr, 0.3f);
    const auto d = noise(n, 0.1f, 7);
    std::vector<float> v(static_cast<std::size_t>(n));
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = amp * (a[i] + b[i] + c[i] + d[i]);
    return v;
}

double rms(const std::vector<float>& v, const std::size_t from = 0)
{
    double s = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        s += static_cast<double>(v[i]) * v[i];
    return std::sqrt(s / static_cast<double>(std::max<std::size_t>(1, v.size() - from)));
}

double rmsDiff(const std::vector<float>& a, const std::vector<float>& b, const std::size_t from = 0)
{
    double s = 0.0;
    for (std::size_t i = from; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        s += d * d;
    }
    return std::sqrt(s / static_cast<double>(std::max<std::size_t>(1, a.size() - from)));
}

double maxAbs(const std::vector<float>& v, const std::size_t from = 0)
{
    double m = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        m = std::max(m, static_cast<double>(std::fabs(v[i])));
    return m;
}

bool allFinite(const std::vector<float>& v)
{
    return std::all_of(v.begin(), v.end(), [](const float x) { return std::isfinite(x); });
}

// Amplitude of the `freq` component of v[from..], by Goertzel.
double toneAmp(const std::vector<float>& v, const double freq, const double sr, const std::size_t from)
{
    const std::size_t n = v.size() - from;
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 2.0 * kPi * freq * static_cast<double>(i) / sr;
        re += v[from + i] * std::cos(w);
        im -= v[from + i] * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

struct Rendered {
    std::vector<float> l, r;
};

// Runs the engine over a whole signal in blocks of `block`.
Rendered render(Engine& e, const std::vector<float>& inL, const std::vector<float>& inR, const int block = 480)
{
    Rendered out;
    out.l.assign(inL.size(), 0.0f);
    out.r.assign(inL.size(), 0.0f);
    for (std::size_t pos = 0; pos < inL.size();) {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), inL.size() - pos));
        e.process(inL.data() + pos, inR.data() + pos, out.l.data() + pos, out.r.data() + pos, n);
        pos += static_cast<std::size_t>(n);
    }
    return out;
}

Rendered renderMono(Engine& e, const std::vector<float>& in, const int block = 480) { return render(e, in, in, block); }

// 16-bit PCM WAV, channel-major `taps` (1 or 2 channels), written under the system temp directory.
std::string writeWav(const std::string& name, const std::vector<float>& taps, const int channels, const int rate)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "downspout_nam_tests";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / name).string();
    std::ofstream f(path, std::ios::binary);
    const int frames = static_cast<int>(taps.size()) / channels;
    const auto u32 = [&](const std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](const std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    u32(static_cast<std::uint32_t>(36 + frames * channels * 2));
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(static_cast<std::uint16_t>(channels));
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * channels * 2));
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);
    f.write("data", 4);
    u32(static_cast<std::uint32_t>(frames * channels * 2));
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            const float v = std::clamp(taps[static_cast<std::size_t>(c * frames + i)], -1.0f, 0.99997f);
            u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 32768.0f))));
        }
    }
    return path;
}

std::shared_ptr<ModelRig> loadModel(const std::string& path, const double hostRate)
{
    std::string error;
    auto rig = ModelRig::load(path, hostRate, error);
    if (!rig)
        std::fprintf(stderr, "model load failed: %s\n", error.c_str());
    assert(rig);
    return rig;
}

// ── Parameters ──────────────────────────────────────────────────────────────

void testParameterTable()
{
    assert(kParameterCount == 20);  // append-only: a changed count is an index shift
    std::set<std::string> symbols;
    bool seenOutput = false;
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto& s = kParameterSpecs[i];
        assert(symbols.insert(s.symbol).second);
        assert(s.minimum < s.maximum);
        assert(s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        if (s.logarithmic)
            assert(s.minimum > 0.0f);
        if (s.output)
            seenOutput = true;
        else
            assert(!seenOutput);  // outputs come last
    }
    assert(kParameterSpecs[kInLevel].output && kParameterSpecs[kIrMs].output);
    assert(!kParameterSpecs[kNormalize].output);
}

// ── DSP blocks ──────────────────────────────────────────────────────────────

void testConvolverMatchesDirect()
{
    for (const int length : {1, 50, 128, 129, 300, 1000, 5000}) {
        const auto ir = noise(length, 0.5f, 3);
        const auto x = noise(20000, 1.0f, 9);
        std::vector<float> expected(x.size(), 0.0f);
        for (std::size_t n = 0; n < x.size(); ++n) {
            double acc = 0.0;
            for (int k = 0; k < length && static_cast<std::size_t>(k) <= n; ++k)
                acc += static_cast<double>(ir[static_cast<std::size_t>(k)]) * x[n - static_cast<std::size_t>(k)];
            expected[n] = static_cast<float>(acc);
        }
        PartitionedConvolver c;
        c.setImpulse(ir.data(), length);
        std::vector<float> y(x.size());
        Lcg r;
        r.s = 77;
        for (std::size_t pos = 0; pos < x.size();) {
            const int n = std::min<int>(1 + static_cast<int>((r.next() + 1.0f) * 150.0f), static_cast<int>(x.size() - pos));
            c.process(x.data() + pos, y.data() + pos, n);
            pos += static_cast<std::size_t>(n);
        }
        const double scale = std::max(1.0, maxAbs(expected));
        assert(rmsDiff(y, expected) < 1.0e-4 * scale);
        assert(maxAbs(y) > 0.0);

        // In place, after clear(), the same input gives the same output.
        c.clear();
        std::vector<float> z = x;
        c.process(z.data(), z.data(), static_cast<int>(z.size()));
        assert(rmsDiff(z, y) < 1.0e-5 * scale);
    }
}

void testConvolverIsZeroLatency()
{
    const std::vector<float> ir = {0.5f, 0.25f};
    PartitionedConvolver c;
    c.setImpulse(ir.data(), 2);
    const std::vector<float> in = {1.0f, 0.0f, 0.0f};
    std::vector<float> out(3);
    c.process(in.data(), out.data(), 3);
    assert(std::fabs(out[0] - 0.5f) < 1.0e-6f && std::fabs(out[1] - 0.25f) < 1.0e-6f);
}

void testResamplerGainAndAliasing()
{
    struct Case { double in, out; };
    for (const Case cs : {Case {48000, 44100}, Case {44100, 48000}, Case {48000, 96000}, Case {96000, 48000}, Case {192000, 48000}}) {
        StreamResampler rs;
        rs.configure(cs.in, cs.out);
        const int n = static_cast<int>(cs.in);  // one second
        const auto x = sine(n, 1000.0, cs.in, 0.5f);
        std::vector<float> y(static_cast<std::size_t>(n * 2 + 64));
        const int got = rs.process(x.data(), n, y.data(), static_cast<int>(y.size()));
        y.resize(static_cast<std::size_t>(got));
        assert(std::abs(got - static_cast<int>(cs.out)) <= 40);
        const double a = toneAmp(y, 1000.0, cs.out, static_cast<std::size_t>(got) / 2);
        assert(std::fabs(a - 0.5) < 0.5 * 0.01);  // passband gain within 1 %
    }
    // A tone above the output Nyquist must be rejected, not folded back.
    StreamResampler rs;
    rs.configure(96000.0, 48000.0);
    const auto x = sine(96000, 30000.0, 96000.0, 0.5f);
    std::vector<float> y(96000);
    const int got = rs.process(x.data(), 96000, y.data(), static_cast<int>(y.size()));
    y.resize(static_cast<std::size_t>(got));
    assert(maxAbs(y, static_cast<std::size_t>(got) / 2) < 0.5 * 0.003);  // > 44 dB down
}

void testBiquadsAndTone()
{
    constexpr double sr = 48000.0;
    const auto hp = designBiquad(FilterType::HighPass, sr, 100.0, 0.7071, 0.0);
    assert(std::fabs(20.0 * std::log10(biquadMagnitude(hp, 100.0, sr)) + 3.0) < 0.2);
    assert(biquadMagnitude(hp, 5000.0, sr) > 0.99);
    const auto lp = designBiquad(FilterType::LowPass, sr, 5000.0, 0.7071, 0.0);
    assert(std::fabs(20.0 * std::log10(biquadMagnitude(lp, 5000.0, sr)) + 3.0) < 0.2);

    const ToneCoeffs flat = designTone(sr, 0, 0, 0);
    for (const double f : {40.0, 300.0, 1000.0, 8000.0})
        assert(std::fabs(toneResponseDb(flat, f, sr)) < 0.01);
    const ToneCoeffs bass = designTone(sr, 12, 0, 0);
    assert(toneResponseDb(bass, 40.0, sr) > 9.0 && std::fabs(toneResponseDb(bass, 3000.0, sr)) < 1.0);
    const ToneCoeffs mid = designTone(sr, 0, -12, 0);
    assert(toneResponseDb(mid, 750.0, sr) < -10.0 && std::fabs(toneResponseDb(mid, 60.0, sr)) < 1.5);
    const ToneCoeffs treble = designTone(sr, 0, 0, 12);
    assert(toneResponseDb(treble, 12000.0, sr) > 9.0 && std::fabs(toneResponseDb(treble, 300.0, sr)) < 1.0);

    // The streaming filter agrees with the analytic response.
    Biquad b;
    b.set(designBiquad(FilterType::Peak, sr, 750.0, 0.8, 9.0));
    const auto x = sine(48000, 750.0, sr, 0.1f);
    std::vector<float> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = b.process(x[i]);
    assert(std::fabs(toneAmp(y, 750.0, sr, 24000) / 0.1 - std::pow(10.0, 9.0 / 20.0)) < 0.02);
}

// ── Engine without files ────────────────────────────────────────────────────

void testEmptyEnginePassesAudio()
{
    Engine e(48000.0f);
    const auto x = richSignal(9600, 48000.0, 0.5f);
    auto out = renderMono(e, x);
    assert(rmsDiff(out.l, x) < 1.0e-5 && rmsDiff(out.r, x) < 1.0e-5);

    // Stereo mode keeps the channels apart; mono sum folds them.
    const auto left = sine(9600, 440.0, 48000.0, 0.5f);
    const auto right = sine(9600, 660.0, 48000.0, 0.25f);
    e.setParameter(kChannels, kStereo);
    auto st = render(e, left, right);
    assert(rmsDiff(st.l, left) < 1.0e-5 && rmsDiff(st.r, right) < 1.0e-5);
    e.setParameter(kChannels, kMonoSum);
    auto mono = render(e, left, right);
    assert(rmsDiff(mono.l, mono.r) < 1.0e-6);
    assert(toneAmp(mono.l, 440.0, 48000.0, 4800) > 0.2 && toneAmp(mono.l, 660.0, 48000.0, 4800) > 0.1);
}

void testGainsAndSafety()
{
    Engine e(48000.0f);
    const auto x = sine(9600, 440.0, 48000.0, 0.1f);
    e.setParameter(kInputGain, 6.0f);
    e.setParameter(kOutputGain, 6.0f);
    for (int i = 0; i < 3; ++i)
        (void)renderMono(e, x);  // let the smoothers settle
    auto out = renderMono(e, x);
    assert(std::fabs(toneAmp(out.l, 440.0, 48000.0, 4800) / 0.1 - std::pow(10.0, 12.0 / 20.0)) < 0.05);

    // Hot input cannot exceed the safety clamp, and a meter reads it.
    e.setParameter(kInputGain, 24.0f);
    e.setParameter(kOutputGain, 24.0f);
    const auto loud = sine(9600, 440.0, 48000.0, 1.0f);
    for (int i = 0; i < 3; ++i)
        out = renderMono(e, loud);
    assert(maxAbs(out.l) <= 8.0 + 1.0e-6 && allFinite(out.l));
    assert(e.outputPeakDb() > 12.0f - 0.01f || maxAbs(out.l) >= 7.9);
}

void testGate()
{
    Engine e(48000.0f);
    e.setParameter(kGate, -40.0f);
    const auto hiss = noise(24000, 0.002f, 5);     // about -54 dBFS
    const auto played = sine(24000, 440.0, 48000.0, 0.3f);
    auto quiet = renderMono(e, hiss);
    assert(rms(quiet.l, 12000) < rms(hiss, 12000) * 0.05);  // closed: more than 26 dB down
    auto loud = renderMono(e, played);
    assert(std::fabs(rms(loud.l, 12000) / rms(played, 12000) - 1.0) < 0.02);  // open: untouched
    e.setParameter(kGate, kGateOff);
    quiet = renderMono(e, hiss);
    assert(rms(quiet.l, 12000) > rms(hiss, 12000) * 0.95);  // minimum means off
}

// ── Model ───────────────────────────────────────────────────────────────────

void testModelLoadingErrors()
{
    std::string error;
    assert(!ModelRig::load(kDataDir + "/does-not-exist.nam", 48000.0, error) && !error.empty());
    const std::string junk = writeWav("nam_test_not_a_model.nam", {0.0f}, 1, 48000);
    assert(!ModelRig::load(junk, 48000.0, error) && !error.empty());
    assert(!IrRig::load(kDataDir + "/does-not-exist.wav", 48000.0, error) && !error.empty());
    assert(!IrRig::load(kWavenet, 48000.0, error) && !error.empty());  // a model is not a WAV
}

void testModelProcessing()
{
    for (const auto& path : {kWavenet, kLstm}) {
        auto rig = loadModel(path, 48000.0);
        assert(!rig->resampling() && rig->latencySamples() == 0);
        assert(rig->hasLoudness());
        const auto x = richSignal(24000, 48000.0, 0.3f);

        Engine a(48000.0f);
        a.setModel(rig);
        const auto whole = renderMono(a, x, 256).l;
        assert(allFinite(whole) && rms(whole, 4800) > 1.0e-3);
        assert(rmsDiff(whole, x, 4800) > 1.0e-2);  // it is not a pass-through

        // The same stream in awkward block sizes gives the same samples.
        auto rig2 = loadModel(path, 48000.0);
        Engine b(48000.0f);
        b.setModel(rig2);
        const auto split = renderMono(b, x, 37).l;
        assert(rmsDiff(whole, split) < 1.0e-5);
    }
}

void testModelOffAndNormalise()
{
    auto rig = loadModel(kWavenet, 48000.0);
    const auto x = richSignal(24000, 48000.0, 0.3f);
    Engine e(48000.0f);
    e.setModel(rig);
    e.setParameter(kNormalize, 0.0f);
    const auto raw = renderMono(e, x).l;
    e.reset();
    e.setParameter(kNormalize, 1.0f);
    for (int i = 0; i < 2; ++i)
        (void)renderMono(e, x);
    const auto norm = renderMono(e, x).l;
    const double expectedDb = kNormalizeTargetDb - rig->loudnessDb();
    assert(std::fabs(expectedDb) > 0.5);  // the fixture must need a change for this to mean anything
    const double measuredDb = 20.0 * std::log10(rms(norm, 4800) / rms(raw, 4800));
    assert(std::fabs(measuredDb - expectedDb) < 0.7);

    e.setParameter(kModelEnable, 0.0f);
    for (int i = 0; i < 2; ++i)
        (void)renderMono(e, x);
    const auto bypass = renderMono(e, x).l;
    assert(!e.modelActive());
    assert(rmsDiff(bypass, x, 4800) < 1.0e-4);
}

void testResampledModel()
{
    auto rig = loadModel(kWavenet, 44100.0);  // trained at 48 kHz
    assert(rig->resampling() && rig->modelRate() == 48000.0);
    assert(rig->latencySamples() > 15 && rig->latencySamples() < 80);

    const auto x = richSignal(44100, 44100.0, 0.3f);
    Engine a(44100.0f);
    a.setModel(rig);
    const auto whole = renderMono(a, x, 441).l;
    assert(allFinite(whole) && rms(whole, 4410) > 1.0e-3);

    auto rig2 = loadModel(kWavenet, 44100.0);
    Engine b(44100.0f);
    b.setModel(rig2);
    const auto split = renderMono(b, x, 29).l;
    assert(rmsDiff(whole, split) < 1.0e-5);  // independent of host block size
    assert(rig->underruns() == 0 && rig2->underruns() == 0);

    // The dry path is delayed to line up with the wet one.
    Engine d(44100.0f);
    d.setModel(loadModel(kWavenet, 44100.0));
    d.setParameter(kMix, 0.0f);
    assert(d.latencySamples() == a.latencySamples() && d.latencySamples() > 0);
    const auto dry = renderMono(d, x).l;
    const int lat = d.latencySamples();
    double err = 0.0;
    for (std::size_t i = static_cast<std::size_t>(lat); i < x.size(); ++i)
        err = std::max(err, static_cast<double>(std::fabs(dry[i] - x[i - static_cast<std::size_t>(lat)])));
    assert(err < 1.0e-6);
}

void testModelSurvivesHostActivation()
{
    auto rig = loadModel(kWavenet, 48000.0);
    auto ir = IrRig::fromSamples({0.5f}, 1);
    Engine e(48000.0f);
    e.setModel(rig);
    e.setIr(ir);
    e.setParameter(kBass, 6.0f);
    e.setParameter(kOutputGain, -3.0f);
    const auto x = richSignal(9600, 48000.0, 0.3f);
    (void)renderMono(e, x);
    e.reset();  // what activate() does
    assert(e.modelActive() && e.irActive());
    assert(e.parameter(kBass) == 6.0f && e.parameter(kOutputGain) == -3.0f);
    const auto after = renderMono(e, x).l;
    assert(allFinite(after) && rms(after, 2400) > 1.0e-3);
}

void testRealtimeCost()
{
    auto rig = loadModel(kWavenet, 48000.0);
    Engine e(48000.0f);
    e.setModel(rig);
    const auto x = richSignal(48000 * 2, 48000.0, 0.3f);
    const auto start = std::chrono::steady_clock::now();
    (void)renderMono(e, x, 256);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  model cost: %.3f s for 2 s of audio (rerun alone if this fails under load)\n", seconds);
    assert(seconds < 2.0);
}

// ── Impulse responses ───────────────────────────────────────────────────────

void testIrLoadingAndLevel()
{
    // A delta of 0.5 is a gain of 0.5.
    std::vector<float> delta(2000, 0.0f);
    delta[0] = 0.5f;
    std::string error;
    auto ir = IrRig::load(writeWav("nam_test_delta.wav", delta, 1, 48000), 48000.0, error);
    assert(ir && !ir->stereo() && ir->lengthSamples() == 2000);
    Engine e(48000.0f);
    e.setIr(ir);
    const auto x = richSignal(9600, 48000.0, 0.5f);
    auto y = renderMono(e, x);
    std::vector<float> half(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        half[i] = 0.5f * x[i];
    assert(rmsDiff(y.l, half) < 1.0e-3 && rmsDiff(y.r, half) < 1.0e-3);

    // The same IR recorded at 44.1 kHz keeps its level when played at 48 kHz.
    auto ir441 = IrRig::load(writeWav("nam_test_delta441.wav", delta, 1, 44100), 48000.0, error);
    assert(ir441);
    Engine f(48000.0f);
    f.setIr(ir441);
    const std::vector<float> dc(24000, 0.5f);
    auto z = renderMono(f, dc);
    if (std::fabs(z.l[20000] - 0.25f) >= 0.25f * 0.03f)
        std::fprintf(stderr, "44.1k IR at 48k: DC out %g, IR length %d\n", static_cast<double>(z.l[20000]), ir441->lengthSamples());
    assert(std::fabs(z.l[20000] - 0.25f) < 0.25f * 0.03f);
    // Resampling keeps the kernel's pre-roll, so the IR is a little longer than the ratio alone gives.
    const int expectedLength = static_cast<int>(std::ceil(2000 * 48000.0 / 44100.0));
    assert(ir441->lengthSamples() >= expectedLength && ir441->lengthSamples() <= expectedLength + 80);

    // Longer than two seconds is trimmed.
    std::vector<float> longIr(48000 * 3, 0.0f);
    longIr[0] = 0.5f;
    auto trimmed = IrRig::load(writeWav("nam_test_long.wav", longIr, 1, 48000), 48000.0, error);
    assert(trimmed && trimmed->lengthSamples() == 96000);

    // The IR's response survives stereo handling: left and right taps stay on their channels.
    std::vector<float> st(2000 * 2, 0.0f);
    st[0] = 0.5f;
    st[2000] = 0.25f;
    auto stereo = IrRig::load(writeWav("nam_test_stereo.wav", st, 2, 48000), 48000.0, error);
    assert(stereo && stereo->stereo());
    Engine g(48000.0f);
    g.setIr(stereo);
    g.setParameter(kChannels, kStereo);
    auto sl = render(g, x, x);
    std::vector<float> quarter(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        quarter[i] = 0.25f * x[i];
    assert(rmsDiff(sl.l, half) < 1.0e-3 && rmsDiff(sl.r, quarter) < 1.0e-3);
    g.setParameter(kChannels, kMonoSum);
    sl = render(g, x, x);
    assert(rmsDiff(sl.l, half) < 1.0e-3 && rmsDiff(sl.r, quarter) < 1.0e-3);  // a stereo IR still widens a mono sum
}

void testIrCutsAndGain()
{
    auto ir = IrRig::fromSamples({1.0f}, 1);
    const auto lowTone = sine(48000, 60.0, 48000.0, 0.5f);
    const auto highTone = sine(48000, 12000.0, 48000.0, 0.5f);
    const auto settle = [](Engine& e, const std::vector<float>& x) {
        for (int i = 0; i < 2; ++i)
            (void)renderMono(e, x);
        return renderMono(e, x).l;
    };
    Engine e(48000.0f);
    e.setIr(ir);
    const double flatLow = rms(settle(e, lowTone), 4800);
    const double flatHigh = rms(settle(e, highTone), 4800);
    assert(std::fabs(flatLow / rms(lowTone, 4800) - 1.0) < 0.01);

    e.setParameter(kIrLowCut, 300.0f);
    assert(rms(settle(e, lowTone), 4800) < flatLow * 0.2);
    assert(rms(settle(e, highTone), 4800) > flatHigh * 0.95);
    e.setParameter(kIrLowCut, kLowCutOff);
    e.setParameter(kIrHighCut, 3000.0f);
    assert(rms(settle(e, highTone), 4800) < flatHigh * 0.1);
    assert(rms(settle(e, lowTone), 4800) > flatLow * 0.95);
    e.setParameter(kIrHighCut, kHighCutOff);
    e.setParameter(kIrGain, 6.0f);
    assert(std::fabs(rms(settle(e, lowTone), 4800) / flatLow - std::pow(10.0, 6.0 / 20.0)) < 0.03);
}

// ── Every control does something ────────────────────────────────────────────

void testEveryControlHasAnEffect()
{
    const auto left = richSignal(24000, 48000.0, 0.15f);
    const auto right = noise(24000, 0.15f, 21);
    std::vector<float> taps(1500);
    for (std::size_t i = 0; i < taps.size(); ++i)
        taps[i] = 0.4f * std::exp(-static_cast<float>(i) / 200.0f) * (i == 0 ? 1.0f : Lcg {static_cast<std::uint32_t>(i)}.next());
    const auto ir = IrRig::fromSamples(taps, 1);

    const auto build = [&](Engine& e) {
        e.setModel(loadModel(kWavenet, 48000.0));
        e.setIr(ir);
        e.setParameter(kChannels, kMonoSum);
    };
    const auto run = [&](Engine& e) {
        for (int i = 0; i < 2; ++i)
            (void)render(e, left, right);
        return render(e, left, right);
    };

    Engine reference(48000.0f);
    build(reference);
    const Rendered base = run(reference);
    assert(allFinite(base.l) && rms(base.l, 4800) > 1.0e-3);

    // The value each control is moved to. A new parameter without an entry fails here.
    const std::vector<std::pair<std::uint32_t, float>> moves = {
        {kInputGain, 12.0f},   {kGate, -20.0f},     {kBass, 12.0f},        {kMid, -12.0f},      {kTreble, 12.0f},
        {kIrLowCut, 400.0f},   {kIrHighCut, 2000.0f}, {kIrGain, 9.0f},     {kOutputGain, -9.0f}, {kMix, 0.0f},
        {kChannels, kStereo},  {kModelEnable, 0.0f}, {kIrEnable, 0.0f},    {kNormalize, 0.0f},
    };
    std::set<std::uint32_t> covered;
    for (const auto& [index, to] : moves) {
        covered.insert(index);
        Engine e(48000.0f);
        build(e);
        assert(e.parameter(index) != to);
        e.setParameter(index, to);
        assert(e.parameter(index) == to);
        const Rendered changed = run(e);
        assert(allFinite(changed.l) && allFinite(changed.r));
        const double d = std::max(rmsDiff(changed.l, base.l, 4800), rmsDiff(changed.r, base.r, 4800));
        if (d <= 1.0e-3 * rms(base.l, 4800))
            std::fprintf(stderr, "parameter %s has no measurable effect (diff %g)\n", kParameterSpecs[index].symbol, d);
        assert(d > 1.0e-3 * rms(base.l, 4800));
    }
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        if (!kParameterSpecs[i].output) {
            if (covered.count(i) == 0)
                std::fprintf(stderr, "no effect test for parameter %s\n", kParameterSpecs[i].symbol);
            assert(covered.count(i) == 1);
        }
    }

    // Outputs are read-only for the host.
    Engine e(48000.0f);
    const float before = e.parameter(kInLevel);
    e.setParameter(kInLevel, 5.0f);
    assert(e.parameter(kInLevel) == before);
}

void testMetersFollowSignal()
{
    Engine e(48000.0f);
    const auto x = sine(24000, 440.0, 48000.0, 0.5f);
    (void)renderMono(e, x);
    assert(std::fabs(e.inputPeakDb() - gainToDb(0.5f)) < 1.0f);
    assert(std::fabs(e.outputPeakDb() - gainToDb(0.5f)) < 1.0f);
    const std::vector<float> silence(48000 * 3, 0.0f);  // the meter falls at about 17 dB per second
    (void)renderMono(e, silence);
    assert(e.inputPeakDb() < -40.0f && e.outputPeakDb() < -40.0f);
}

}  // namespace

int main()
{
    testParameterTable();
    testConvolverMatchesDirect();
    testConvolverIsZeroLatency();
    testResamplerGainAndAliasing();
    testBiquadsAndTone();
    testEmptyEnginePassesAudio();
    testGainsAndSafety();
    testGate();
    testModelLoadingErrors();
    testModelProcessing();
    testModelOffAndNormalise();
    testResampledModel();
    testModelSurvivesHostActivation();
    testRealtimeCost();
    testIrLoadingAndLevel();
    testIrCutsAndGain();
    testEveryControlHasAnEffect();
    testMetersFollowSignal();
    std::puts("nam core tests passed");
    return 0;
}
