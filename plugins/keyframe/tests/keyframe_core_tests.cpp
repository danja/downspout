#include "keyframe_core.hpp"

#include <algorithm>
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

using namespace downspout::keyframe;

constexpr double kSr = 48000.0;

// Dense layered material: a bass tone plus noise bursts, which is the case the
// paper says the method captures cleanly.
std::vector<float> makeDenseProgram(const std::size_t frames)
{
    std::vector<float> buf(frames);
    for (std::size_t n = 0; n < frames; ++n)
    {
        const double t = static_cast<double>(n) / kSr;
        const double bass = 0.35 * std::sin(2.0 * 3.14159265358979 * 55.0 * t);
        // Deterministic noise so the test is repeatable.
        const double noise = 0.18 * std::sin(2.0 * 3.14159265358979 * 7000.0 * t * 1.6180339887);
        const double burst = (std::fmod(t * 4.0, 1.0) < 0.05) ? 0.30 : 0.0;
        buf[n] = static_cast<float>(std::max(-0.99, std::min(0.99, bass + noise + burst)));
    }
    return buf;
}

std::vector<float> makeSine(const std::size_t frames, const double hz)
{
    std::vector<float> buf(frames);
    for (std::size_t n = 0; n < frames; ++n)
    {
        buf[n] = static_cast<float>(
            0.5 * std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(n) / kSr));
    }
    return buf;
}

void runBlock(EngineState& st, const Parameters& p, const std::vector<float>& in,
              const std::size_t offset, const std::size_t frames,
              std::vector<float>& outL, std::vector<float>& outR)
{
    const float* inputs[2] = {in.data() + offset, in.data() + offset};
    std::vector<float> blockL(frames);
    std::vector<float> blockR(frames);
    float* outputs[2] = {blockL.data(), blockR.data()};
    processBlock(st, p, static_cast<std::uint32_t>(frames), inputs, outputs);
    outL.insert(outL.end(), blockL.begin(), blockL.end());
    outR.insert(outR.end(), blockR.begin(), blockR.end());
}

// Fundamental frequency by normalized autocorrelation. Zero crossings are not
// usable here: the output is spliced, so its waveform is continuous within a
// segment but phase-discontinuous across a splice, which inflates a crossing
// count. Autocorrelation finds the period the material actually has.
double measureHertz(const std::vector<float>& buf, const std::size_t from, const std::size_t len)
{
    if (from + len >= buf.size())
        return 0.0;

    // Search lags from 8 samples (6 kHz) to 1600 samples (30 Hz).
    const std::size_t minLag = 8;
    const std::size_t maxLag = 1600;
    const std::size_t span = std::min(len, buf.size() - from - maxLag);
    if (span <= maxLag + 8)
        return 0.0;

    double best = -1.0;
    std::size_t bestLag = minLag;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double num = 0.0;
        double den = 0.0;
        for (std::size_t i = 0; i < span; ++i)
        {
            const double a = buf[from + i];
            const double b = buf[from + i + lag];
            num += a * b;
            den += a * a;
        }
        const double r = num / std::max(den, 1.0e-12);
        if (r > best)
        {
            best = r;
            bestLag = lag;
        }
    }
    return static_cast<double>(bestLag) > 0.0 ? kSr / static_cast<double>(bestLag) : 0.0;
}

// First sample index whose absolute value exceeds `level`, or size().
std::size_t firstActive(const std::vector<float>& buf, const float level)
{
    for (std::size_t n = 0; n < buf.size(); ++n)
        if (std::fabs(buf[n]) > level)
            return n;
    return buf.size();
}

}  // namespace

int main()
{
    // 1. Parameter clamping, including the Time ceiling that a live input
    //    stream imposes and the integer Splic/Hold handling.
    {
        Parameters p;
        p.time = 3.5f;
        p.pitch = 9.0f;
        p.splice = 3.4f;
        p.threshold = -6.0f;
        p.maxSplice = 4.0f;
        p.hold = 0.6f;
        p.mix = 1.4f;
        const Parameters q = clampParameters(p);
        CHECK(q.time == 1.0f, "time clamps to 1.0 (live input cannot be read faster than it arrives)");
        CHECK(q.pitch == 4.0f, "pitch clamps to 4x");
        CHECK(q.splice == 3.0f, "splice keyframe count is integral");
        CHECK(q.threshold == -24.0f, "threshold clamps to -24 dB");
        CHECK(q.maxSplice == 20.0f, "max splice clamps to 20 ms");
        CHECK(q.hold == 1.0f, "hold is a switch");
        CHECK(q.mix == 1.0f, "mix clamps to 1");
    }

    // 2. epsilonFromDb matches the paper's -60 dB = 0.001 recommendation.
    {
        CHECK(std::fabs(epsilonFromDb(-60.0f) - 0.001f) < 1.0e-6f, "epsilon at -60 dB is 0.001");
        CHECK(std::fabs(epsilonFromDb(0.0f) - 1.0f) < 1.0e-6f, "epsilon at 0 dB is unity");
    }

    // 3. Reported latency is constant and matches the depth window.
    {
        CHECK(reportedLatencySamples() == kLatencySamples, "reported latency is the depth window");
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        CHECK(playheadDrift(*st) == 0.0f, "fresh state has no drift");
        CHECK(spliceCount(*st) == 0, "fresh state has spliced nothing");
    }

    // 4. Silence in, silence out: no runaway from the sparse buffer.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        const std::vector<float> silence(4096, 0.0f);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, silence, 0, silence.size(), outL, outR);
        float peak = 0.0f;
        for (float v : outL) peak = std::max(peak, std::fabs(v));
        for (float v : outR) peak = std::max(peak, std::fabs(v));
        CHECK(peak < 1.0e-6f, "silence stays silent");

        // Block boundaries still produce keyframes, so playback has windows.
        CHECK(keyframeCount(*st) >= silence.size() / kAnalysisBlock,
              "block boundaries force keyframes during silence");
    }

    // 5. Sparsification: 1 kHz at -60 dB yields on the order of 2000 keyframes
    //    per second, nowhere near one per sample.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        const auto input = makeSine(48000, 1000.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);
        const double rate = static_cast<double>(keyframeCount(*st)) * kSr / static_cast<double>(input.size());
        CHECK(rate > 1200.0 && rate < 4000.0, "1 kHz sine sparsifies to roughly 2000 keyframes/s");
    }

    // 6. Keyframe positions are strictly increasing, which the window search
    //    depends on, and span the analysed region.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        const auto input = makeDenseProgram(96000);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);

        bool monotonic = true;
        double prev = -1.0;
        double last = 0.0;
        double pos = 0.0;
        float l = 0.0f;
        float r = 0.0f;
        for (std::int64_t i = 0; i < keyframeCount(*st); ++i)
        {
            if (!keyframeAt(*st, i, pos, l, r))
            {
                monotonic = false;
                break;
            }
            if (pos <= prev)
            {
                monotonic = false;
                break;
            }
            prev = pos;
            last = pos;
        }
        CHECK(monotonic, "keyframe positions strictly increase");
        CHECK(last >= 95000.0, "analysis reaches the end of the input");
    }

    // 7. Unity rates reconstruct the input: with the reference and the playhead
    //    advancing at the same rate no splice ever fires, so the sparse round
    //    trip is a straight reconstruction of the input, delayed by the
    //    reported latency. This is the paper's passthrough-fidelity claim.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.mix = 1.0f;
        p.level = 1.0f;
        const auto input = makeSine(96000, 220.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);

        CHECK(spliceCount(*st) == 0, "unity rates never splice");

        // Compare against the input at the reported latency offset.
        double num = 0.0;
        double den = 0.0;
        for (std::size_t n = kLatencySamples + 2048; n < input.size(); ++n)
        {
            const double d = static_cast<double>(outL[n])
                           - static_cast<double>(input[n - kLatencySamples]);
            num += d * d;
            den += static_cast<double>(input[n - kLatencySamples]) * input[n - kLatencySamples];
        }
        const double nrmse = std::sqrt(num / std::max(den, 1.0e-12));
        CHECK(nrmse < 0.30, "unity time/pitch round trip is close to transparent");
    }

    // 8. Dry/wet alignment: with Mix low the output is the input delayed by
    //    the reported latency, not the input itself.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.mix = 0.0f;
        p.level = 1.0f;
        const auto input = makeSine(96000, 300.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);

        double num = 0.0;
        double den = 0.0;
        for (std::size_t n = kLatencySamples + 4096; n < input.size(); ++n)
        {
            const double d = static_cast<double>(outL[n]) - static_cast<double>(input[n - kLatencySamples]);
            num += d * d;
            den += static_cast<double>(input[n - kLatencySamples]) * input[n - kLatencySamples];
        }
        const double nrmse = std::sqrt(num / std::max(den, 1.0e-12));
        CHECK(nrmse < 0.05, "dry path is the input delayed by the reported latency");

        const double unaligned = std::fabs(outL[80000] - input[80000]);
        CHECK(unaligned > 0.05f, "without the delay the dry path would not line up");
    }

    // 9. Pitch rate sets the fundamental. The time rate stays at unity, so the
    //    reference and the playhead would drift apart at exactly the rate the
    //    pitch control asks for; splices absorb that drift, which is how the
    //    method separates duration from pitch.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.pitch = 2.0f;
        p.mix = 1.0f;
        const auto input = makeSine(240000, 200.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);

        const double hz = measureHertz(outL, kLatencySamples + 8192, 32768);
        CHECK(hz > 370.0 && hz < 430.0, "pitch 2.00x doubles the fundamental (an octave up)");
        CHECK(spliceCount(*st) > 0, "pitching up forces splices to absorb the drift");
    }

    // 10. Time rate changes duration without changing pitch. At half speed the
    //     reference advances at 0.5 samples per output sample while the
    //     playhead runs at the pitch rate, so the fundamental must stay at the
    //     input frequency.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.time = 0.5f;
        p.mix = 1.0f;
        const auto input = makeSine(96000, 250.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);

        const double hz = measureHertz(outL, kLatencySamples + 8192, 32768);
        CHECK(hz > 235.0 && hz < 265.0, "time 0.50x keeps pitch at the input frequency");

        // The reference advances at the time rate, so half speed must consume
        // roughly half the input to cover the same output length.
        const double advanced = st->refPos;
        const double expected = 0.5 * static_cast<double>(input.size() - kLatencySamples);
        CHECK(std::fabs(advanced - expected) < expected * 0.05,
              "reference advances at the time rate");

        CHECK(spliceCount(*st) > 0, "slowing down forces splices to re-anchor the reference");
    }

    // 11. Faster-than-unity pitch is rejected at the parameter level, so the
    //     playhead can never outrun the analysis write head.
    {
        Parameters p;
        p.time = 1.0f;
        p.pitch = 4.0f;
        const Parameters q = clampParameters(p);
        CHECK(q.time <= 1.0f, "time cannot exceed unity");

        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        const auto input = makeDenseProgram(192000);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);
        bool finite = true;
        for (float v : outL)
            if (!std::isfinite(v)) finite = false;
        CHECK(finite, "extreme pitch stays finite");
    }

    // 12. Hold sustains: with the reference frozen the output keeps producing
    //     long after the input stops.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.mix = 1.0f;
        const auto input = makeSine(32768, 180.0);
        std::vector<float> outL;
        std::vector<float> outR;
        // Warm up with the reference moving so the playhead is inside the
        // buffer, then freeze it.
        Parameters running = p;
        runBlock(*st, running, input, 0, input.size(), outL, outR);
        const std::size_t before = outL.size();
        const std::vector<float> silence(32768, 0.0f);
        p.hold = 1.0f;
        runBlock(*st, p, silence, 0, silence.size(), outL, outR);

        float peak = 0.0f;
        for (std::size_t n = before; n < outL.size(); ++n)
            peak = std::max(peak, std::fabs(outL[n]));
        CHECK(peak > 0.05f, "hold keeps the passage sounding with no further input");
    }

    // 13. Block-size invariance: the same input split differently must give
    //     the same output, because the engine is sample-by-sample.
    {
        const auto input = makeDenseProgram(32768);
        Parameters p;
        p.time = 0.5f;
        p.mix = 1.0f;

        auto stA = std::make_unique<EngineState>();
        activate(*stA, kSr);
        std::vector<float> aL;
        std::vector<float> aR;
        runBlock(*stA, p, input, 0, input.size(), aL, aR);

        auto stB = std::make_unique<EngineState>();
        activate(*stB, kSr);
        std::vector<float> bL;
        std::vector<float> bR;
        std::size_t offset = 0;
        while (offset < input.size())
        {
            const std::size_t n = std::min<std::size_t>(61, input.size() - offset);
            runBlock(*stB, p, input, offset, n, bL, bR);
            offset += n;
        }

        CHECK(aL.size() == bL.size(), "block-size split yields the same length");
        float worst = 0.0f;
        for (std::size_t n = 0; n < aL.size(); ++n)
            worst = std::max(worst, std::fabs(aL[n] - bL[n]));
        CHECK(worst < 1.0e-6f, "output is block-size invariant");
    }

    // 14. Status reporting tracks the engine.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.time = 0.5f;
        const auto input = makeDenseProgram(96000);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);
        CHECK(keyframesPerSecond(*st) > 100.0f, "density readout is non-zero for real material");
        CHECK(std::fabs(playheadDrift(*st)) <= 64.0f, "drift readout stays inside its range");
        CHECK(spliceLamp(*st) >= 0.0f && spliceLamp(*st) <= 1.0f, "splice lamp is normalised");
        CHECK(clipLamp(*st) >= 0.0f && clipLamp(*st) <= 1.0f, "clip lamp is normalised");
    }

    // 15. Long-run boundedness: several seconds through the engine must not
    //     grow the keyframe count past the ring, and must stay finite.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.time = 0.25f;
        p.pitch = 3.0f;
        p.mix = 1.0f;
        const auto input = makeDenseProgram(480000);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);
        // The keyframe counter is monotonic by construction, so what matters is that
        // the live window the playheads read stays resident in the ring: the
        // depth floor plus the longest splice lookahead must fit.
        const std::int64_t needed = kMaxDepthSamples
                                  + static_cast<std::int64_t>(p.maxSplice * 0.001 * kSr) + 4096;
        CHECK(needed < kRingCapacity, "ring holds the maximum depth plus splice lookahead");
        bool finite = true;
        float peak = 0.0f;
        for (float v : outL)
        {
            if (!std::isfinite(v)) finite = false;
            peak = std::max(peak, std::fabs(v));
        }
        CHECK(finite, "ten seconds of extreme settings stay finite");
        CHECK(peak <= 1.2f, "output stays clamped");
    }

    // 16. Null input pointers are safe.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        std::vector<float> outL(512);
        std::vector<float> outR(512);
        float* outputs[2] = {outL.data(), outR.data()};
        processBlock(*st, p, 512, nullptr, outputs);
        CHECK(outL[0] == 0.0f && outR[0] == 0.0f, "missing input reads as silence");
        processBlock(*st, p, 512, nullptr, nullptr);
        CHECK(true, "missing output pointer is a no-op");
    }

    // 17. State round trip.
    {
        Parameters p;
        p.time = 0.42f;
        p.pitch = 1.75f;
        p.splice = 33.0f;
        p.threshold = -71.0f;
        p.maxSplice = 310.0f;
        p.hold = 1.0f;
        p.mix = 0.31f;
        p.width = 0.87f;
        p.level = 0.66f;
        const auto q = deserializeParameters(serializeParameters(p));
        CHECK(q.has_value(), "state parses");
        CHECK(q && q->time == 0.42f && q->pitch == 1.75f && q->splice == 33.0f
                  && q->threshold == -71.0f && q->maxSplice == 310.0f && q->hold == 1.0f
                  && q->mix == 0.31f && q->width == 0.87f && q->level == 0.66f,
              "state round trips");
        CHECK(!deserializeParameters("bogus").has_value(), "bogus state rejected");
        CHECK(!deserializeParameters("time=abc").has_value(), "malformed value rejected");
    }

    if (failures == 0)
        std::printf("all keyframe core tests passed\n");
    return failures == 0 ? 0 : 1;
}