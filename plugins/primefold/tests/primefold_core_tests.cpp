#include "primefold_core.hpp"

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

using namespace downspout::primefold;

void fillSine(std::vector<float>& buf, float freq, double sr)
{
    for (std::size_t n = 0; n < buf.size(); ++n)
        buf[n] = std::sin(2.0f * 3.14159265358979323846f * freq * static_cast<float>(n) / static_cast<float>(sr));
}

float peak(const std::vector<float>& buf, std::size_t skip)
{
    float m = 0.0f;
    for (std::size_t n = skip; n < buf.size(); ++n)
        m = std::max(m, std::fabs(buf[n]));
    return m;
}

// Dominant frequency via zero crossings over the second half.
float dominantFreq(const std::vector<float>& buf, double sr)
{
    std::size_t start = buf.size() / 2;
    int crossings = 0;
    for (std::size_t n = start + 1; n < buf.size(); ++n)
        if ((buf[n - 1] <= 0.0f) != (buf[n] <= 0.0f))
            ++crossings;
    const double seconds = static_cast<double>(buf.size() - start) / sr;
    return static_cast<float>(crossings / 2.0 / seconds);
}

}  // namespace

int main()
{
    using namespace downspout::primefold;

    // 1. Clamping keeps feedback <= 0.90 and grain integral.
    {
        Parameters p;
        p.feedback = 5.0f;
        p.grain = 9.0f;
        p.dry = -1.0f;
        const Parameters c = clampParameters(p);
        CHECK(c.feedback == 0.90f, "feedback clamped to 0.90");
        CHECK(c.grain == 2.0f, "grain clamped integral");
        CHECK(c.dry == 0.0f, "dry clamped");
    }

    // 2. Silence in -> silence out (no self-oscillation at default feedback).
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, 48000.0, 1024);
        Parameters p;
        std::vector<float> z(4096, 0.0f), oL(4096), oR(4096);
        const float* ins[2] = {z.data(), z.data()};
        float* outs[2] = {oL.data(), oR.data()};
        processBlock(*st, p, 4096, ins, outs);
        CHECK(peak(oL, 0) == 0.0f && peak(oR, 0) == 0.0f, "silence stays silent");
    }

    // 3. Each prime voice shifts a sine by its ratio with feedback at zero.
    for (int voice = 0; voice < 3; ++voice)
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, 48000.0, 1024);
        Parameters p;
        p.feedback = 0.0f;
        p.dry = 0.0f;
        p.mix = 1.0f;
        p.level2 = voice == 0 ? 1.0f : 0.0f;
        p.level3 = voice == 1 ? 1.0f : 0.0f;
        p.level5 = voice == 2 ? 1.0f : 0.0f;
        const double sr = 48000.0;
        const std::size_t N = 48000;
        std::vector<float> in(N), oL(N), oR(N);
        fillSine(in, 220.0f, sr);
        const float* ins[2] = {in.data(), in.data()};
        float* outs[2] = {oL.data(), oR.data()};
        std::size_t done = 0;
        while (done < N)
        {
            const std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::size_t>(512, N - done));
            const float* ci[2] = {ins[0] + done, ins[1] + done};
            float* co[2] = {outs[0] + done, outs[1] + done};
            processBlock(*st, p, chunk, ci, co);
            done += chunk;
        }
        const float expect = voice == 0 ? 440.0f : (voice == 1 ? 660.0f : 1100.0f);
        const float got = dominantFreq(oL, sr);
        const float err = std::fabs(got - expect) / expect;
        char name[64];
        std::snprintf(name, sizeof(name), "voice %d shifts to %.0f (got %.0f)", voice, expect, got);
        CHECK(err < 0.06f, name);
        CHECK(std::isfinite(peak(oL, 0)), "voice output finite");
    }

    // 4. Feedback supplies composites: with 2x enabled plus feedback, late
    // output differs from the no-feedback run (4x/6x/... recirculation).
    {
        auto stA = std::make_unique<EngineState>();
        auto stB = std::make_unique<EngineState>();
        activate(*stA, 48000.0, 1024);
        activate(*stB, 48000.0, 1024);
        Parameters noFb, fb;
        noFb.feedback = 0.0f; noFb.level2 = 0.0f; noFb.level3 = 1.0f; noFb.level5 = 0.0f;
        noFb.dry = 0.0f; noFb.mix = 1.0f;
        fb = noFb; fb.feedback = 0.85f; fb.level2 = 1.0f;
        const std::size_t N = 32768;
        std::vector<float> in(N), aL(N), aR(N), bL(N), bR(N);
        fillSine(in, 110.0f, 48000.0);
        auto run = [&](EngineState& st, Parameters& p, std::vector<float>& oL, std::vector<float>& oR) {
            std::size_t done = 0;
            while (done < N)
            {
                const std::uint32_t chunk = 512;
                const float* ci[2] = {in.data() + done, in.data() + done};
                float* co[2] = {oL.data() + done, oR.data() + done};
                processBlock(st, p, chunk, ci, co);
                done += chunk;
            }
        };
        run(*stA, noFb, aL, aR);
        run(*stB, fb, bL, bR);
        float diff = 0.0f;
        for (std::size_t n = N / 2; n < N; ++n)
            diff = std::max(diff, std::fabs(bL[n] - aL[n]));
        CHECK(diff > 1e-4f, "feedback adds composite energy");
        CHECK(peak(bL, N / 2) < 1.2f, "feedback loop bounded");
    }

    // 5. Full-scale burst at max feedback stays bounded over 10 s.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, 48000.0, 2048);
        Parameters p;
        p.feedback = 0.90f;
        p.level2 = p.level3 = p.level5 = 1.0f;
        p.mix = 1.0f;
        const std::size_t N = 480000;
        std::vector<float> in(512), oL(512), oR(512);
        fillSine(in, 220.0f, 48000.0);
        float worst = 0.0f;
        bool finite = true;
        for (std::size_t done = 0; done < N; done += 512)
        {
            const float* ci[2] = {in.data(), in.data()};
            float* co[2] = {oL.data(), oR.data()};
            processBlock(*st, p, 512, ci, co);
            for (float s : oL)
            {
                if (!std::isfinite(s)) finite = false;
                worst = std::max(worst, std::fabs(s));
            }
        }
        CHECK(finite, "10 s max-feedback run finite");
        CHECK(worst < 1.25f, "10 s max-feedback run bounded");
    }

    // 6. Block-size invariance: same input, different chunking, identical output.
    {
        auto a = std::make_unique<EngineState>();
        auto b = std::make_unique<EngineState>();
        activate(*a, 48000.0, 1024);
        activate(*b, 48000.0, 1024);
        Parameters p;
        const std::size_t N = 4096;
        std::vector<float> in(N), aL(N), aR(N), bL(N), bR(N);
        fillSine(in, 330.0f, 48000.0);
        for (std::size_t off = 0; off < N;)
        {
            const std::uint32_t chunk = 512;
            const float* ci[2] = {in.data() + off, in.data() + off};
            float* co[2] = {aL.data() + off, aR.data() + off};
            processBlock(*a, p, chunk, ci, co);
            off += chunk;
        }
        {
            const float* ci[2] = {in.data(), in.data()};
            float* co[2] = {bL.data(), bR.data()};
            processBlock(*b, p, static_cast<std::uint32_t>(N), ci, co);
        }
        float diff = 0.0f;
        for (std::size_t n = 0; n < N; ++n)
            diff = std::max(diff, std::fabs(aL[n] - bL[n]));
        CHECK(diff == 0.0f, "identical output across block sizes");
    }

    // 7. State round trip.
    {
        Parameters p;
        p.feedback = 0.77f; p.grain = 2.0f; p.level5 = 0.11f;
        const std::string s = serializeParameters(p);
        const auto q = deserializeParameters(s);
        CHECK(q.has_value(), "state parses");
        CHECK(q->feedback == 0.77f && q->grain == 2.0f && q->level5 == 0.11f, "state round trips");
        CHECK(!deserializeParameters("bogus").has_value(), "bogus state rejected");
    }

    // 8. Reported latency is the worst-case voice delay (grain + trigger +
    // search); the explicit in-loop delay is fixed at 256 and uncompensated.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, 48000.0, 512);
        CHECK(currentLatencySamples(*st) == static_cast<float>(worstVoiceDelay(512)),
              "latency == worst-case voice delay");
        CHECK(kFeedbackDelay == 256, "explicit loop delay present");
    }

    if (failures == 0) std::printf("all primefold core tests passed\n");
    return failures == 0 ? 0 : 1;
}
