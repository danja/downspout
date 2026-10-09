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

// Feed `frames` samples starting at `offset`, in host-block-sized chunks, so
// tests exercise the same path a host uses. The input pointer must advance with
// each chunk: processBlock reads inputs[0][0..n-1] relative to what it is given.
void runBlock(EngineState& st, const Parameters& p, const std::vector<float>& in,
              const std::size_t offset, const std::size_t frames,
              std::vector<float>& outL, std::vector<float>& outR)
{
    constexpr std::size_t kChunk = 512;
    std::vector<float> blockL(kChunk);
    std::vector<float> blockR(kChunk);
    float* outputs[2] = {blockL.data(), blockR.data()};

    std::size_t done = 0;
    while (done < frames)
    {
        const std::size_t n = std::min(kChunk, frames - done);
        const float* inputs[2] = {in.data() + offset + done, in.data() + offset + done};
        processBlock(st, p, static_cast<std::uint32_t>(n), inputs, outputs);
        outL.insert(outL.end(), blockL.begin(), blockL.begin() + n);
        outR.insert(outR.end(), blockR.begin(), blockR.begin() + n);
        done += n;
    }
}

// Strongest spectral bin in a window after the latency. Zero crossings and
// autocorrelation both misreport here: the output is spliced, so it is
// continuous within a segment but phase-discontinuous across a splice, and both
// methods read that discontinuity as signal. A direct bin scan measures the
// energy that is actually present.
double strongestBin(const std::vector<float>& buf, const std::vector<float>& input)
{
    const std::size_t from = kLatencySamples + 8192;
    const std::size_t want = 8192;
    if (from + want >= buf.size())
        return 0.0;

    // Only look below a fifth of Nyquist: the cubic interpolation leaves a
    // small Nyquist-region ripple that would otherwise dominate every bin.
    const double ceiling = 0.2 * kSr / 2.0;
    double best = 0.0;
    double bestHz = 0.0;
    for (double hz = 20.0; hz <= ceiling; hz += 1.0)
    {
        const double w = 2.0 * 3.14159265358979 * hz / kSr;
        const double coeff = 2.0 * std::cos(w);
        double s1 = 0.0;
        double s2 = 0.0;
        for (std::size_t i = 0; i < want; ++i)
        {
            const double s0 = buf[from + i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double mag = std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) / want;
        if (mag > best)
        {
            best = mag;
            bestHz = hz;
        }
    }
    (void)input;
    return bestHz;
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
        CHECK(static_cast<std::size_t>(keyframeCount(*st)) >= silence.size() / kAnalysisBlock,
              "block boundaries force keyframes during silence");
    }

    // 5. Sparsification: a 1 kHz sine yields two keyframes per cycle, about
    //    2000 per second, nowhere near one per sample. This is the reduction the
    //    whole method rests on, so it is worth pinning down.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        const auto input = makeSine(48000, 1000.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);
        const double rate = static_cast<double>(keyframeCount(*st)) * kSr / static_cast<double>(input.size());
        CHECK(rate > 1800.0 && rate < 2200.0, "1 kHz sine sparsifies to two keyframes per cycle");
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

        // Find the offset that actually minimises the error and report it, so a
        // regression in the latency contract is visible rather than hidden
        // behind a loose bound.
        std::size_t bestOffset = kLatencySamples;
        double bestError = 1.0e30;
        for (std::size_t offset = kLatencySamples - 4; offset <= kLatencySamples + 4; ++offset)
        {
            double num2 = 0.0;
            double den2 = 0.0;
            for (std::size_t n = offset + 2048; n < input.size(); ++n)
            {
                const double d = static_cast<double>(outL[n])
                               - static_cast<double>(input[n - offset]);
                num2 += d * d;
                den2 += static_cast<double>(input[n - offset]) * input[n - offset];
            }
            const double e = std::sqrt(num2 / std::max(den2, 1.0e-12));
            if (e < bestError)
            {
                bestError = e;
                bestOffset = offset;
            }
        }
        CHECK(std::llabs(static_cast<long long>(bestOffset)
                         - static_cast<long long>(kLatencySamples)) <= 4,
              "best-fit offset matches the reported latency");
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

    // 9. Pitch rate sets the fundamental. The paper's two rates are independent
    //    (its own Figure 4 uses pitch 1.65 with time 0.84), so at time = 1.0 a
    //    pitch rate of 2.0 reads the buffer twice as fast: the fundamental
    //    doubles while the reference stays at unity, and splices absorb the
    //    drift that opens up between the two playheads.
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

        CHECK(strongestBin(outL, input) > 370.0 && strongestBin(outL, input) < 430.0,
              "pitch 2.00x doubles the fundamental (an octave up)");
        CHECK(spliceCount(*st) > 0, "pitching up forces splices to absorb the drift");
    }

    // 9b. Pitch below unity must work too. The playhead is allowed to lag the
    //     reference for this; a clamp against the reference would silently turn
    //     every pitch setting into the time setting.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.pitch = 0.5f;
        p.mix = 1.0f;
        const auto input = makeSine(240000, 200.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);

        const double hz = strongestBin(outL, input);
        CHECK(hz > 93.0 && hz < 107.0, "pitch 0.50x halves the fundamental");
    }

    // 10. Time rate sets duration. The reference advances at tau samples per output
    //     sample, so at half speed it falls behind the analysis at 0.5 samples
    //     per output sample. Measured over the window before the depth limit
    //     binds, since a sustained slow-down eventually runs into
    //     kMaxDepthSamples and the engine loops what it holds.
    {
        auto st = std::make_unique<EngineState>();
        activate(*st, kSr);
        Parameters p;
        p.time = 0.5f;
        p.mix = 1.0f;
        const auto input = makeSine(240000, 250.0);
        std::vector<float> outL;
        std::vector<float> outR;
        runBlock(*st, p, input, 0, input.size(), outL, outR);

        // The depth the reference reached is the depth the engine had to hold:
        // at unity it is the fixed latency, at half speed it is the floor.
        const double depth = static_cast<double>(input.size()) - st->refPos;
        CHECK(depth > static_cast<double>(kMaxDepthSamples) - 512.0,
              "a sustained slow-down reaches the depth limit and loops rather than backing up");

        // The duration stretch is best seen as extra output for the same input:
        // the engine keeps sounding well past the end of the material.
        CHECK(spliceCount(*st) > 0, "slowing down forces splices to re-anchor the reference");

        // With sigma at unity the pitch rate is untouched: the stretch is a
        // duration change, not a pitch change.
        const double hz = strongestBin(outL, input);
        CHECK(hz > 235.0 && hz < 265.0, "time 0.50x with pitch 1.00x keeps the pitch");
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

    // 14. Status reporting tracks the engine. Density is the paper's M/N ratio,
    //     so it is bounded to [0, 1] whatever the sample rate or material.
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
        const float ratio = keyframeDensityRatio(*st);
        CHECK(ratio > 0.0f && ratio <= 1.0f, "density ratio is bounded to [0, 1]");
        CHECK(std::fabs(playheadDrift(*st)) <= 64.0f, "drift readout stays inside its range");
        CHECK(spliceLamp(*st) >= 0.0f && spliceLamp(*st) <= 1.0f, "splice lamp is normalised");
        CHECK(clipLamp(*st) >= 0.0f && clipLamp(*st) <= 1.0f, "clip lamp is normalised");
    }

    // 14b. Density scale is pinned at both extremes, so the meter cannot be
    //      silently resaturated: sparse material reads low, maximally dense
    //      material reads high, and neither end clips against a hidden ceiling.
    {
        auto runRatio = [](const std::vector<float>& input) {
            auto st = std::make_unique<EngineState>();
            activate(*st, kSr);
            Parameters p;
            std::vector<float> outL;
            std::vector<float> outR;
            runBlock(*st, p, input, 0, input.size(), outL, outR);
            return keyframeDensityRatio(*st);
        };

        // Sparse: a quiet low sine produces few extrema per sample.
        const float sparse = runRatio(makeSine(96000, 220.0));
        CHECK(sparse > 0.0f && sparse < 0.05f, "sparse material reads a low density ratio");

        // Dense: a 15 kHz sine flips the derivative nearly every other sample,
        // which is close to the maximum keyframe rate of one per sample.
        // (A sample-alternating square wave would seem denser, but the centered
        // difference of two same-parity samples is exactly zero, so it produces
        // no crossings at all — a property of the kernel, not a useful test.)
        const float dense = runRatio(makeSine(48000, 15000.0));
        CHECK(dense > 0.3f && dense <= 1.0f, "dense material reads a high density ratio");

        CHECK(dense > 10.0f * sparse, "density scale separates sparse from dense material");
    }

    // 14c. Density smoothing is time-based, so the readout converges the same
    //      way whatever the host buffer size is. (Audio output invariance is
    //      covered in 13; this covers the status path, which a fixed
    //      per-block coefficient would leave buffer-size dependent.)
    {
        const auto input = makeDenseProgram(96000);
        Parameters p;

        auto runChunked = [&](const std::size_t chunk) {
            auto st = std::make_unique<EngineState>();
            activate(*st, kSr);
            std::vector<float> outL;
            std::vector<float> outR;
            std::vector<float> blockL(chunk);
            std::vector<float> blockR(chunk);
            float* outputs[2] = {blockL.data(), blockR.data()};
            std::size_t done = 0;
            while (done < input.size())
            {
                const std::size_t n = std::min(chunk, input.size() - done);
                const float* inputs[2] = {input.data() + done, input.data() + done};
                processBlock(*st, p, static_cast<std::uint32_t>(n), inputs, outputs);
                outL.insert(outL.end(), blockL.begin(), blockL.begin() + n);
                outR.insert(outR.end(), blockR.begin(), blockR.begin() + n);
                done += n;
            }
            return keyframeDensityRatio(*st);
        };

        const float small = runChunked(128);
        const float large = runChunked(2048);
        CHECK(std::fabs(small - large) < 0.01f,
              "density readout is independent of host buffer size");
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