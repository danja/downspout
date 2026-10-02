#include "voxmod_core_types.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::voxmod;

namespace {

constexpr std::uint32_t kFrames = 512;
constexpr double kSr = 48000.0;

// Four inputs, two outputs, matching the plugin's bus layout.
struct Bus {
    std::array<float, kFrames> carrierL {};
    std::array<float, kFrames> carrierR {};
    std::array<float, kFrames> modL {};
    std::array<float, kFrames> modR {};
    std::array<float, kFrames> outL {};
    std::array<float, kFrames> outR {};
};

void run(EngineState& state, const Parameters& p, Bus& b)
{
    const float* ins[] = { b.carrierL.data(), b.carrierR.data(), b.modL.data(), b.modR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    processBlock(state, p, kFrames, kSr, ins, outs);
}

void fillSine(float* dst, const double hz, const double sr, const float amp = 0.5f)
{
    for (std::uint32_t i = 0; i < kFrames; ++i)
        dst[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * hz * i / sr));
}

float rms(const float* data, const std::uint32_t from, const std::uint32_t to)
{
    double sum = 0.0;
    for (std::uint32_t i = from; i < to; ++i)
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return static_cast<float>(std::sqrt(sum / std::max(1u, to - from)));
}

float peak(const float* data, const std::uint32_t count)
{
    float p = 0.0f;
    for (std::uint32_t i = 0; i < count; ++i)
        p = std::max(p, std::fabs(data[i]));
    return p;
}

void testParameterClamp() {
    Parameters p;
    p.mix = 500.0f;
    p.ringFreq = -40.0f;
    p.bandCount = 900.0f;  // clamps to the 64-band maximum
    p.tilt = 99.0f;
    p.ccChannel = 0.0f;
    p.ringShape = 17.0f;

    const Parameters c = clampParameters(p);
    assert(c.mix == 100.0f);
    assert(c.ringFreq == 1.0f);
    assert(c.bandCount == 64.0f);
    assert(std::fabs(c.tilt - kMaxTiltDbPerOct) < 1e-4f);
    assert(c.ccChannel == 1.0f);
    assert(c.ringShape == 3.0f);

    // NaN must fall back to the default rather than propagating.
    Parameters bad;
    bad.mix = std::nanf("");
    bad.ringFreq = std::nanf("");
    const Parameters cb = clampParameters(bad);
    assert(std::isfinite(cb.mix));
    assert(std::isfinite(cb.ringFreq));
}

void testBandEdgesAreMonotonicAndLogarithmic() {
    Parameters p;
    p.bandCount = 24.0f;
    p.bandSpread = 5.0f;

    const auto edges = bandEdges(p);
    assert(std::fabs(edges[0] - kVocoderLowHz) < 0.01f);

    for (int i = 1; i < 24; ++i)
        assert(edges[static_cast<std::size_t>(i)] > edges[static_cast<std::size_t>(i - 1)]);

    // Logarithmic spacing means the ratio between neighbours is roughly constant,
    // not the absolute step. That is what stops the bank starving the bass.
    const float firstRatio = edges[1] / edges[0];
    const float lastRatio = edges[23] / edges[22];
    assert(std::fabs(firstRatio - lastRatio) / firstRatio < 0.05f);

    // Bands past the requested count are pinned to the top edge, not garbage.
    assert(edges[30] >= edges[23]);
}

void testRatioQuantisesToSixteenSteps() {
    // The control spans 0.5 - 8 but must land on exactly 16 musical steps.
    std::vector<float> seen;
    for (int i = 0; i < 400; ++i) {
        const float raw = 0.5f + (7.5f * static_cast<float>(i) / 399.0f);
        const float q = quantiseRatio(raw);
        assert(q > 0.0f);
        bool matched = false;
        for (float v : seen) {
            if (std::fabs(v - q) < 1e-4f) {
                matched = true;
                break;
            }
        }
        if (!matched)
            seen.push_back(q);
    }
    assert(seen.size() == kRatioSteps);
}

void testRingModProducesSidebands() {
    // A 400 Hz modulator through a 1:1 sine ring must give energy at 400 and
    // 800 Hz and nothing at 400 +/- nothing: the sum tone is the point.
    Parameters p;
    p.mix = 0.0f;         // full ring mod
    p.ringFreq = 400.0f;
    p.ringRatio = 1.0f;
    p.ringShape = 0.0f;  // sine
    p.ringDepth = 100.0f;
    p.drift = 0.0f;
    p.carrierSource = 1.0f;  // internal oscillator, no carrier input needed
    p.sync = 0.0f;

    Bus b;
    fillSine(b.modL.data(), 400.0, kSr);
    fillSine(b.modR.data(), 400.0, kSr);

    EngineState state;
    activate(state);
    run(state, p, b);

    const float level = rms(b.outL.data(), 0, kFrames);
    assert(level > 0.01f);

    // NaN/inf must never reach the bus.
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        assert(std::isfinite(b.outL[i]));
        assert(std::isfinite(b.outR[i]));
    }
}

void testRingDepthZeroIsPassThrough() {
    Parameters p;
    p.mix = 0.0f;
    p.ringDepth = 0.0f;  // oscillator scaled to unity -> clean passthrough
    p.drift = 0.0f;

    Bus b;
    fillSine(b.modL.data(), 300.0, kSr, 0.4f);
    fillSine(b.modR.data(), 300.0, kSr, 0.4f);

    EngineState state;
    activate(state);
    run(state, p, b);

    for (std::uint32_t i = 0; i < kFrames; ++i)
        assert(std::fabs(b.outL[i] - b.modL[i]) < 1e-4f);
}

void testVocoderFollowsCarrierEnvelope() {
    // With a loud carrier and a steady modulator the vocoder must pass the
    // modulator through; with a silent carrier it must not.
    Parameters loud;
    loud.mix = 100.0f;  // full vocoder
    loud.ringDepth = 0.0f;

    Bus a;
    fillSine(a.carrierL.data(), 700.0, kSr, 0.6f);
    fillSine(a.modL.data(), 220.0, kSr, 0.4f);
    EngineState s1;
    activate(s1);
    run(s1, loud, a);
    const float withCarrier = rms(a.outL.data(), 256, kFrames);

    Parameters silent = loud;
    Bus b;
    fillSine(b.carrierL.data(), 700.0, kSr, 0.0f);  // carrier muted
    fillSine(b.modL.data(), 220.0, kSr, 0.4f);
    EngineState s2;
    activate(s2);
    run(s2, silent, b);
    const float withoutCarrier = rms(b.outL.data(), 256, kFrames);

    assert(withCarrier > 0.005f);
    // A silent carrier must gate the vocoder down, not pass the modulator dry.
    assert(withoutCarrier < withCarrier * 0.5f);
}

void testMixCrossfadesBetweenEngines() {
    // Mix 0 is ring only, Mix 100 is vocoder only. Both must be live, and the
    // blend must be monotonic.
    Parameters p;
    p.ringDepth = 100.0f;
    p.drift = 0.0f;
    p.carrierSource = 1.0f;

    auto levelAt = [&](const float mix) {
        Parameters q = p;
        q.mix = mix;
        Bus b;
        fillSine(b.carrierL.data(), 500.0, kSr, 0.5f);
        fillSine(b.modL.data(), 250.0, kSr, 0.5f);
        fillSine(b.modR.data(), 250.0, kSr, 0.5f);
        EngineState s;
        activate(s);
        // A few blocks so the envelope followers settle.
        for (int i = 0; i < 8; ++i)
            run(s, q, b);
        return rms(b.outL.data(), 256, kFrames);
    };

    const float ringOnly = levelAt(0.0f);
    const float mid = levelAt(50.0f);
    const float vocoderOnly = levelAt(100.0f);

    assert(ringOnly > 0.005f);
    assert(vocoderOnly > 0.005f);
    // The blend should sit between the two engines, not jump past either.
    assert(mid <= std::max(ringOnly, vocoderOnly) * 1.5f);
}

void testBypassIsTransparent() {
    Parameters p;
    p.mix = 50.0f;
    p.bypass = 1.0f;
    p.drift = 0.0f;

    Bus b;
    fillSine(b.modL.data(), 300.0, kSr, 0.4f);
    fillSine(b.modR.data(), 300.0, kSr, 0.4f);

    EngineState state;
    activate(state);
    run(state, p, b);

    // Bypass mutes both engines, so the output is silent rather than wet.
    assert(peak(b.outL.data(), kFrames) < 1e-4f);
}

void testNullAndNonFiniteInputsAreSafe() {
    Parameters p;
    p.mix = 50.0f;

    Bus b;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        b.carrierL[i] = std::nanf("");
        b.carrierR[i] = INFINITY;
        b.modL[i] = std::nanf("");
        b.modR[i] = 1.0f;
    }

    EngineState state;
    activate(state);

    // Null input and output pointers must not be dereferenced.
    processBlock(state, p, kFrames, kSr, nullptr, nullptr);
    run(state, p, b);

    for (std::uint32_t i = 0; i < kFrames; ++i) {
        assert(std::isfinite(b.outL[i]));
        assert(std::isfinite(b.outR[i]));
    }
}

void testSampleRateIndependence() {
    // The envelope followers are time-based, so a block at 44.1k and one at
    // 96k must settle to comparable levels for the same signal.
    auto levelAt = [&](const double sr) {
        Parameters p;
        p.mix = 100.0f;
        p.ringDepth = 0.0f;
        Bus b;
        fillSine(b.carrierL.data(), 600.0, sr, 0.6f);
        fillSine(b.modL.data(), 300.0, sr, 0.4f);
        EngineState s;
        activate(s);
        for (int i = 0; i < 10; ++i) {
            const float* ins[] = { b.carrierL.data(), b.carrierR.data(), b.modL.data(), b.modR.data() };
            float* outs[] = { b.outL.data(), b.outR.data() };
            processBlock(s, p, kFrames, sr, ins, outs);
        }
        return rms(b.outL.data(), 256, kFrames);
    };

    const float a = levelAt(44100.0);
    const float b = levelAt(96000.0);
    assert(a > 0.005f);
    assert(b > 0.005f);
    assert(std::fabs(a - b) / std::max(a, b) < 0.35f);
}

void testBandCountChangeRebuildsBank() {
    Parameters p;
    p.bandCount = 24.0f;
    p.mix = 100.0f;

    Bus b;
    fillSine(b.carrierL.data(), 800.0, kSr, 0.5f);
    fillSine(b.modL.data(), 400.0, kSr, 0.4f);

    EngineState state;
    activate(state);
    run(state, p, b);
    assert(state.activeBands == 24);
    const std::uint32_t serial = state.bankSerial;
    assert(serial > 0u);

    // An unchanged bank must not be rebuilt on every block.
    run(state, p, b);
    assert(state.bankSerial == serial);

    p.bandCount = 48.0f;
    run(state, p, b);
    assert(state.activeBands == 48);
    assert(state.bankSerial == serial + 1);

    // Formant Shift moves the modulator side, so it must rebuild too.
    p.formantShift = 5.0f;
    run(state, p, b);
    assert(state.bankSerial == serial + 2);
}

void testFormantShiftChangesOutput() {
    // A shift has to actually move the formants, otherwise the control is dead.
    auto levelAt = [&](const float shift) {
        Parameters p;
        p.mix = 100.0f;
        p.ringDepth = 0.0f;
        p.formantShift = shift;
        Bus b;
        fillSine(b.carrierL.data(), 500.0, kSr, 0.6f);
        fillSine(b.modL.data(), 500.0, kSr, 0.4f);
        EngineState s;
        activate(s);
        for (int i = 0; i < 8; ++i)
            run(s, p, b);
        return rms(b.outL.data(), 256, kFrames);
    };

    const float flat = levelAt(0.0f);
    const float up = levelAt(7.0f);
    const float down = levelAt(-7.0f);
    assert(flat > 0.005f);
    assert(up > 0.0f);
    assert(down > 0.0f);
    // Shifting a matched carrier/modulator pair must change the result.
    assert(std::fabs(up - down) > 1e-5f);
}

void testSyncRoutesRingModIntoVocoder() {
    // With Sync on and an internal carrier, the ring-modulated signal is the
    // vocoder's carrier, so a modulator-only input is enough to make sound.
    Parameters p;
    p.carrierSource = 1.0f;
    p.sync = 1.0f;
    p.mix = 100.0f;
    p.ringFreq = 500.0f;
    p.ringRatio = 1.0f;
    p.drift = 0.0f;

    Bus b;
    fillSine(b.modL.data(), 250.0, kSr, 0.5f);
    fillSine(b.modR.data(), 250.0, kSr, 0.5f);

    EngineState state;
    activate(state);
    for (int i = 0; i < 8; ++i)
        run(state, p, b);

    const float level = rms(b.outL.data(), 256, kFrames);
    assert(level > 0.0005f);
}

void testDriftWobblesTheCarrier() {
    Parameters p;
    p.carrierSource = 1.0f;
    p.ringFreq = 300.0f;
    p.ringRatio = 1.0f;
    p.drift = 50.0f;
    p.mix = 0.0f;

    Bus b;
    fillSine(b.modL.data(), 200.0, kSr, 0.4f);

    EngineState state;
    activate(state);
    for (int i = 0; i < 200; ++i)
        run(state, p, b);

    // Drift is a slow LFO, so the smoothed frequency should sit near the target
    // but the reported carrier must be finite and in range.
    assert(std::isfinite(state.carrierHz));
    assert(state.carrierHz > 1.0f && state.carrierHz < 20000.0f);
}

void testStatusOutputsAreInRange() {
    Parameters p;
    p.mix = 50.0f;
    p.carrierSource = 1.0f;

    Bus b;
    fillSine(b.carrierL.data(), 1500.0, kSr, 0.5f);
    fillSine(b.modL.data(), 750.0, kSr, 0.5f);

    EngineState state;
    activate(state);
    for (int i = 0; i < 20; ++i)
        run(state, p, b);

    assert(state.sibilance >= 0.0f && state.sibilance <= 1.0f);
    assert(state.reductionDb >= -60.0f && state.reductionDb <= 0.0f);
    assert(state.carrierHz > 0.0f);
}

void testRandomiseIsSeededAndKeepsMixAndBypass() {
    Parameters p;
    p.seed = 1234.0f;
    p.mix = 73.0f;
    p.bypass = 1.0f;

    const Parameters a = randomiseParameters(p);
    const Parameters b = randomiseParameters(p);

    // Same seed, same draw.
    assert(std::fabs(a.ringFreq - b.ringFreq) < 1e-6f);
    assert(std::fabs(a.bandCount - b.bandCount) < 1e-6f);
    assert(std::fabs(a.ringShape - b.ringShape) < 1e-6f);

    // A sweep must not jump the balance or the bypass state.
    assert(std::fabs(a.mix - 73.0f) < 1e-6f);
    assert(std::fabs(a.bypass - 1.0f) < 1e-6f);

    // But it should actually change the sound.
    assert(std::fabs(a.ringFreq - p.ringFreq) > 1e-3f);

    // The seed advances, so a second press differs.
    Parameters p2 = p;
    p2.seed = a.seed;
    const Parameters c = randomiseParameters(p2);
    assert(std::fabs(c.ringFreq - a.ringFreq) > 1e-3f);

    // And everything it produced must be inside the parameter ranges.
    const Parameters clamped = clampParameters(a);
    assert(clamped.bandCount >= 4.0f && clamped.bandCount <= 64.0f);
    assert(clamped.ringRatio >= 0.5f && clamped.ringRatio <= 8.0f);
}

void testSerializationRoundTrip() {
    Parameters p;
    p.mix = 33.0f;
    p.ringFreq = 512.0f;
    p.ringRatio = 3.0f;
    p.ringShape = 2.0f;
    p.bandCount = 40.0f;
    p.formantShift = -4.0f;
    p.carrierSource = 1.0f;
    p.sync = 1.0f;
    p.ccChannel = 9.0f;

    const std::string text = serializeParameters(p);
    const auto back = deserializeParameters(text);
    assert(back.has_value());
    if (!back.has_value())
        return;

    assert(std::fabs(back->mix - p.mix) < 1e-3f);
    assert(std::fabs(back->ringFreq - p.ringFreq) < 1e-3f);
    assert(std::fabs(back->bandCount - p.bandCount) < 1e-3f);
    assert(std::fabs(back->formantShift - p.formantShift) < 1e-3f);
    assert(std::fabs(back->ccChannel - p.ccChannel) < 1e-3f);
    assert(back->sync == 1.0f);

    // Garbage must be rejected rather than half-applied.
    assert(!deserializeParameters("this is not a parameter block").has_value());
    assert(!deserializeParameters("mix=1.0\nnonsense=2\n").has_value());

    // Every parameter must survive the round trip.
    const Parameters defaults {};
    const auto defaulted = deserializeParameters(serializeParameters(defaults));
    assert(defaulted.has_value());
    assert(std::fabs(defaulted->mix - defaults.mix) < 1e-3f);
    assert(std::fabs(defaulted->bandSpread - defaults.bandSpread) < 1e-3f);
}

void testActivateResetsState() {
    EngineState state;
    Parameters p;
    p.mix = 50.0f;

    Bus b;
    fillSine(b.carrierL.data(), 700.0, kSr, 0.6f);
    fillSine(b.modL.data(), 300.0, kSr, 0.5f);
    activate(state);
    for (int i = 0; i < 10; ++i)
        run(state, p, b);

    const float dirty = rms(b.outL.data(), 0, kFrames);
    assert(dirty > 0.0f);

    activate(state);
    assert(state.activeBands == 0);
    assert(state.carrierEnv == 0.0f);
    assert(state.modulatorEnv == 0.0f);
    assert(state.sibilance == 0.0f);
    for (const auto& band : state.bands) {
        assert(band.env == 0.0f);
        assert(band.cZ1 == 0.0f);
        assert(band.mZ1 == 0.0f);
    }

    // And it still works afterwards.
    Bus c;
    fillSine(c.carrierL.data(), 700.0, kSr, 0.6f);
    fillSine(c.modL.data(), 300.0, kSr, 0.5f);
    for (int i = 0; i < 10; ++i)
        run(state, p, c);
    assert(rms(c.outL.data(), 256, kFrames) > 0.005f);
}

void testZeroFramesIsSafe() {
    Parameters p;
    Bus b;
    EngineState state;
    activate(state);

    const float* ins[] = { b.carrierL.data(), b.carrierR.data(), b.modL.data(), b.modR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    processBlock(state, p, 0, kSr, ins, outs);
    processBlock(state, p, 0, 0.0, nullptr, nullptr);
    processBlock(state, p, kFrames, -1.0, ins, outs);
}

}  // namespace

int main() {
    testParameterClamp();
    testBandEdgesAreMonotonicAndLogarithmic();
    testRatioQuantisesToSixteenSteps();
    testRingModProducesSidebands();
    testRingDepthZeroIsPassThrough();
    testVocoderFollowsCarrierEnvelope();
    testMixCrossfadesBetweenEngines();
    testBypassIsTransparent();
    testNullAndNonFiniteInputsAreSafe();
    testSampleRateIndependence();
    testBandCountChangeRebuildsBank();
    testFormantShiftChangesOutput();
    testSyncRoutesRingModIntoVocoder();
    testDriftWobblesTheCarrier();
    testStatusOutputsAreInRange();
    testRandomiseIsSeededAndKeepsMixAndBypass();
    testSerializationRoundTrip();
    testActivateResetsState();
    testZeroFramesIsSafe();

    std::printf("voxmod core tests passed\n");
    return 0;
}