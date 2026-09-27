#include "quefrency_core.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* name, int line)
{
    if (!cond)
    {
        ++failures;
        std::printf("FAIL %s (line %d)\n", name, line);
    }
    else
    {
        std::printf("ok %s\n", name);
    }
}

#define CHECK(c, n) check((c), (n), __LINE__)

using namespace downspout::quefrency;

constexpr double kSr = 48000.0;

// Narrow-band magnitude at hz over buf[start, start+size) via Goertzel.
double goertzel(const std::vector<float>& buf, std::size_t start, std::size_t size, double hz, double sr)
{
    const double w = 2.0 * 3.14159265358979323846 * hz / sr;
    const double cw = std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (std::size_t n = 0; n < size; ++n)
    {
        s0 = buf[start + n] + 2.0 * cw * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - 2.0 * cw * s1 * s2);
}

void fillSine(std::vector<float>& buf, float freq, double sr)
{
    for (std::size_t n = 0; n < buf.size(); ++n)
        buf[n] = std::sin(2.0f * 3.14159265358979323846f * freq * static_cast<float>(n) / static_cast<float>(sr));
}

std::vector<float> noise(std::size_t length, std::uint32_t seed = 12345)
{
    std::vector<float> out(length);
    std::uint32_t state = seed;
    for (std::size_t n = 0; n < length; ++n)
    {
        state = (state * 1103515245u + 12345u) & 0x7fffffffu;
        out[n] = static_cast<float>(state) / static_cast<float>(0x3fffffffu) - 1.0f;
    }
    return out;
}

// Harmonics of f0 weighted by envelope(f), as in the source test suite.
std::vector<float> additive(float f0, double sr, std::size_t length)
{
    std::vector<float> out(length, 0.0f);
    for (int h = 1; h * f0 < sr / 2 - 500; ++h)
    {
        const float f = h * f0;
        const float amp = (std::exp(-std::pow((f - 1000.0f) / 250.0f, 2.0f)) + 0.02f) * 0.05f;
        const float w = 2.0f * 3.14159265358979323846f * f / static_cast<float>(sr);
        for (std::size_t n = 0; n < length; ++n)
            out[n] += amp * std::sin(w * n + h);
    }
    return out;
}

void runThrough(EngineState& st, const std::vector<float>& in, std::vector<float>& outL,
                std::vector<float>& outR, std::uint32_t chunk = 128)
{
    outL.assign(in.size(), 0.0f);
    outR.assign(in.size(), 0.0f);
    std::size_t done = 0;
    while (done < in.size())
    {
        const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::size_t>(chunk, in.size() - done));
        const float* ci[2] = {in.data() + done, in.data() + done};
        float* co[2] = {outL.data() + done, outR.data() + done};
        processBlock(st, n, ci, co);
        done += n;
    }
}

void setParams(EngineState& st, const Parameters& p)
{
    setParameter(st, 0, p.formantShift);
    setParameter(st, 1, p.formantDepth);
    setParameter(st, 2, p.formantTilt);
    setParameter(st, 3, p.pitchShift);
    setParameter(st, 4, p.pitchFine);
    setParameter(st, 5, p.freqShift);
    setParameter(st, 6, p.harmonicDepth);
    setParameter(st, 7, p.lifter);
    setParameter(st, 8, p.estimator);
    setParameter(st, 9, p.mix);
    setParameter(st, 10, p.output);
}

}  // namespace

int main()
{
    using namespace downspout::quefrency;

    // 1. Frame size and latency follow the sample rate (source threshold).
    CHECK(frameSizeFor(44100.0) == 2048, "44100 -> 2048");
    CHECK(frameSizeFor(50000.0) == 2048, "50000 -> 2048");
    CHECK(frameSizeFor(50001.0) == 4096, "50001 -> 4096");
    CHECK(frameSizeFor(96000.0) == 4096, "96000 -> 4096");
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, 44100.0);
        CHECK(currentLatencySamples(*st) == 2047.0f, "latency 2047 at 44100");
        activate(*st, 96000.0);
        CHECK(currentLatencySamples(*st) == 4095.0f, "latency 4095 at 96000");
    }

    // 2. Impulse arrives at exactly the reported latency, nowhere else.
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        for (float est : {0.0f, 1.0f})
        {
            auto st = std::make_unique<EngineState>();
            activate(*st, sr);
            Parameters p;
            p.estimator = est;
            setParams(*st, p);
            const auto lat = static_cast<std::size_t>(currentLatencySamples(*st));
            std::vector<float> in(lat + 8192, 0.0f), oL, oR;
            in[1000] = 1.0f;
            runThrough(*st, in, oL, oR);
            char name[96];
            std::snprintf(name, sizeof(name), "impulse at latency %.0f est %.0f", sr, est);
            CHECK(std::fabs(oL[1000 + lat] - 1.0f) < 1e-4f, name);
            float stray = 0.0f;
            for (std::size_t n = 0; n < oL.size(); ++n)
                if (n != 1000 + lat)
                    stray = std::max(stray, std::fabs(oL[n]));
            std::snprintf(name, sizeof(name), "impulse stray %.0f est %.0f", sr, est);
            CHECK(stray < 1e-4f, name);
        }
    }

    // 3. Neutral noise reconstruction below -90 dB, both estimators.
    for (float est : {0.0f, 1.0f})
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.estimator = est;
        setParams(*st, p);
        const auto in = noise(48000);
        std::vector<float> oL, oR;
        runThrough(*st, in, oL, oR);
        const auto lat = static_cast<std::size_t>(currentLatencySamples(*st));
        double err = 0.0, power = 0.0;
        for (std::size_t n = lat; n < in.size(); ++n)
        {
            const double d = oL[n] - in[n - lat];
            err += d * d;
            power += in[n - lat] * in[n - lat];
        }
        char name[64];
        std::snprintf(name, sizeof(name), "neutral error est %.0f", est);
        CHECK(10.0 * std::log10(err / power) < -90.0, name);
    }

    // 4. Half mix at neutral equals the delayed input (dry/wet aligned).
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.mix = 0.5f;
        setParams(*st, p);
        const auto in = noise(24000, 7);
        std::vector<float> oL, oR;
        runThrough(*st, in, oL, oR);
        const auto lat = static_cast<std::size_t>(currentLatencySamples(*st));
        bool aligned = true;
        for (std::size_t n = lat; n < in.size(); n += 97)
            if (std::fabs(oL[n] - in[n - lat]) > 1e-4f)
                aligned = false;
        CHECK(aligned, "half mix does not comb");
    }

    // 5. Output gain acts on the whole signal.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.output = -6.0206f;
        setParams(*st, p);
        const auto in = noise(12000, 3);
        std::vector<float> oL, oR;
        runThrough(*st, in, oL, oR);
        const auto lat = static_cast<std::size_t>(currentLatencySamples(*st));
        bool ok = true;
        for (std::size_t n = lat; n < in.size(); n += 101)
            if (std::fabs(oL[n] - in[n - lat] * 0.5f) > 1e-4f)
                ok = false;
        CHECK(ok, "output gain halves");
    }

    // 6. Silence stays silent under transformations; extremes stay finite.
    {
        Parameters loud;
        loud.formantShift = 7.0f; loud.formantDepth = 200.0f; loud.formantTilt = 6.0f;
        loud.pitchShift = -5.0f; loud.freqShift = 300.0f; loud.harmonicDepth = 0.0f;
        for (float est : {0.0f, 1.0f})
        {
            auto st = std::make_unique<EngineState>();
            activate(*st, kSr);
            loud.estimator = est;
            setParams(*st, loud);
            std::vector<float> z(16384, 0.0f), oL, oR;
            runThrough(*st, z, oL, oR);
            bool silent = true;
            for (float s : oL)
                if (s != 0.0f)
                    silent = false;
            CHECK(silent, "silence stays silent");
        }
    }
    {
        const auto hiss = noise(24000);
        auto in = additive(110.0f, kSr, 24000);
        for (std::size_t n = 0; n < in.size(); ++n)
            in[n] += hiss[n] * 0.01f;
        const Parameters extremes[4] = {
            [] { Parameters p; p.formantShift = -12; p.formantDepth = 0; p.pitchShift = -24; p.harmonicDepth = 200; p.lifter = 0.5f; return p; }(),
            [] { Parameters p; p.formantShift = 12; p.formantDepth = 200; p.formantTilt = 6; p.pitchShift = 24; p.harmonicDepth = 0; p.lifter = 5; return p; }(),
            [] { Parameters p; p.formantTilt = -6; p.freqShift = -1000; p.pitchFine = -100; return p; }(),
            [] { Parameters p; p.freqShift = 1000; p.pitchFine = 100; p.output = 12; return p; }(),
        };
        for (const auto& xp : extremes)
        {
            for (float est : {0.0f, 1.0f})
            {
                auto st = std::make_unique<EngineState>();
                activate(*st, kSr);
                Parameters p = xp;
                p.estimator = est;
                setParams(*st, p);
                std::vector<float> oL, oR;
                runThrough(*st, in, oL, oR);
                bool finite = true;
                float worst = 0.0f;
                for (float s : oL)
                {
                    if (!std::isfinite(s)) finite = false;
                    worst = std::max(worst, std::fabs(s));
                }
                CHECK(finite, "extremes finite");
                CHECK(worst < 100.0f, "extremes bounded");
            }
        }
    }

    // 7. Pitch shift moves every partial by the ratio (octave up on 110 Hz).
    {
        const std::size_t size = 16384;
        const auto src = additive(110.0f, kSr, 3 * size);
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.pitchShift = 12.0f;
        setParams(*st, p);
        std::vector<float> oL, oR;
        runThrough(*st, src, oL, oR);
        const double g880 = goertzel(oL, 2 * size, size, 880.0, kSr);
        const double g990 = goertzel(oL, 2 * size, size, 990.0, kSr);
        const double g1100 = goertzel(oL, 2 * size, size, 1100.0, kSr);
        CHECK(g880 > 30.0 * g990, "octave keeps 880, drops 990");
        CHECK(g1100 > 30.0 * g990, "octave keeps 1100, drops 990");
    }

    // 8. Formant shift keeps the pitch grid (harmonics of 110 stay).
    {
        const std::size_t size = 16384;
        const auto src = additive(110.0f, kSr, 3 * size);
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.formantShift = 7.0f;
        setParams(*st, p);
        std::vector<float> oL, oR;
        runThrough(*st, src, oL, oR);
        const double g1650 = goertzel(oL, 2 * size, size, 1650.0, kSr);
        const double g1705 = goertzel(oL, 2 * size, size, 1705.0, kSr);
        CHECK(g1650 > 20.0 * g1705, "formant shift keeps harmonic grid");
    }

    // 9. CC map: 0->min, 127->max, 64->default (interior), estimator switch.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        CHECK(applyMidiEvent(*st, 0xB0, 73, 127), "CC73 handled");
        CHECK(st->params.pitchShift == 24.0f, "CC127 -> max");
        applyMidiEvent(*st, 0xB0, 73, 0);
        CHECK(st->params.pitchShift == -24.0f, "CC0 -> min");
        applyMidiEvent(*st, 0xB0, 73, 64);
        CHECK(st->params.pitchShift == 0.0f, "CC64 -> default");
        applyMidiEvent(*st, 0xBF, 73, 127);
        CHECK(st->params.pitchShift == 24.0f, "any channel answers");
        applyMidiEvent(*st, 0xB0, 78, 63);
        CHECK(!st->trueEnvelope, "estimator CC63 -> cepstral");
        applyMidiEvent(*st, 0xB0, 78, 64);
        CHECK(st->trueEnvelope, "estimator CC64 -> true envelope");
        CHECK(!applyMidiEvent(*st, 0xB0, 69, 127), "CC69 ignored");
        CHECK(!applyMidiEvent(*st, 0xB0, 81, 127), "CC81 ignored");
        CHECK(!applyMidiEvent(*st, 0x90, 73, 127), "notes ignored");
        CHECK(st->params.pitchShift == 24.0f, "ignored events change nothing");
    }

    // 10. State round trip; bogus rejected.
    {
        Parameters p;
        p.pitchShift = -7.0f; p.lifter = 3.25f; p.estimator = 1.0f; p.output = 6.0f;
        const std::string s = serializeParameters(p);
        const auto q = deserializeParameters(s);
        CHECK(q.has_value(), "state parses");
        CHECK(q->pitchShift == -7.0f && q->lifter == 3.25f && q->estimator == 1.0f && q->output == 6.0f,
              "state round trips");
        CHECK(!deserializeParameters("bogus").has_value(), "bogus state rejected");
        CHECK(!deserializeParameters("nope=1\n").has_value(), "unknown key rejected");
    }

    // 11. Identical output across block sizes (arbitrary host blocks).
    {
        auto a = std::make_unique<EngineState>();
        auto b = std::make_unique<EngineState>();
        activate(*a, kSr);
        activate(*b, kSr);
        Parameters p;
        p.pitchShift = 5.0f;
        p.formantShift = -3.0f;
        setParams(*a, p);
        setParams(*b, p);
        auto in = additive(130.0f, kSr, 8192);
        std::vector<float> aL, aR, bL, bR;
        runThrough(*a, in, aL, aR, 512);
        // Odd chunk sizes on b, including non-multiples of the hop.
        std::vector<float> tL(in.size(), 0.0f), tR(in.size(), 0.0f);
        std::size_t off = 0;
        std::uint32_t chunk = 100;
        while (off < in.size())
        {
            const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::size_t>(chunk, in.size() - off));
            const float* ci[2] = {in.data() + off, in.data() + off};
            float* co[2] = {tL.data() + off, tR.data() + off};
            processBlock(*b, n, ci, co);
            off += n;
            chunk = chunk == 100 ? 7 : (chunk == 7 ? 1024 : 100);
        }
        bL = tL;
        bR = tR;
        float diff = 0.0f;
        for (std::size_t n = 0; n < in.size(); ++n)
            diff = std::max(diff, std::fabs(aL[n] - bL[n]));
        CHECK(diff == 0.0f, "identical output across block sizes");
    }

    if (failures == 0) std::printf("all quefrency core tests passed\n");
    return failures == 0 ? 0 : 1;
}
