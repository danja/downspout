#include "downspout/test_assert.h"

#include "scope_engine.hpp"
#include "scope_params.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace downspout::scope;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kBlock = 512;

void renderSine(Engine& e, const Params& p, float* inL, float* inR, float* outL, float* outR,
                const int block, float& phase, const float hz, const float amp, const bool stereo)
{
    for (int i = 0; i < block; ++i) {
        inL[i] = amp * std::sin(phase);
        inR[i] = stereo ? inL[i] : 0.0f;
        phase += 2.0f * kPi * hz / 48000.0f;
        if (phase > 2.0f * kPi)
            phase -= 2.0f * kPi;
    }
    processBlock(e, p, inL, inR, outL, outR, static_cast<std::uint32_t>(block));
}

}  // namespace

int main()
{
    // Parameter table: controls first, then the read-only analysis outputs.
    const auto& table = parameterTable();
    assert(table.specs[kRange].minimum == 0.0f && table.specs[kRange].maximum == 2.0f);
    assert(!table.specs[kHold].output);
    assert(table.specs[kSpec0].output);
    assert(table.specs[kSpec0 + kSpecCount - 1].output);
    static_assert(kBands == kSpecCount, "engine bands and parameter outputs must agree");

    // Log-spaced band edges cover 20 Hz to 20 kHz monotonically.
    assert(bandEdgeHz(0) > 19.0f && bandEdgeHz(0) < 21.0f);
    assert(bandEdgeHz(kBands) > 19000.0f && bandEdgeHz(kBands) <= 20000.5f);
    for (int i = 0; i < kBands; ++i)
        assert(bandEdgeHz(i + 1) > bandEdgeHz(i));

    // Audio is passed through untouched.
    Engine e;
    activate(e, 48000.0);
    assert(e.allocated);

    std::vector<float> inL(kBlock, 0.0f), inR(kBlock, 0.0f), outL(kBlock), outR(kBlock);
    Params p;
    for (int i = 0; i < kBlock; ++i)
        inL[static_cast<std::size_t>(i)] = std::sin(static_cast<float>(i) * 0.1f) * 0.25f;
    inR = inL;
    processBlock(e, p, inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    for (int i = 0; i < kBlock; ++i) {
        assert(std::isfinite(outL[static_cast<std::size_t>(i)]));
        assert(std::isfinite(outR[static_cast<std::size_t>(i)]));
        assert(outL[static_cast<std::size_t>(i)] == inL[static_cast<std::size_t>(i)]);
        assert(outR[static_cast<std::size_t>(i)] == inR[static_cast<std::size_t>(i)]);
    }

    // A 1 kHz sine at -6 dBFS should light the band nearest 1 kHz and read
    // about -6 dB. Eight blocks give the analyser two full FFT frames.
    reset(e);
    float phase = 0.0f;
    for (int b = 0; b < 8; ++b)
        renderSine(e, p, inL.data(), inR.data(), outL.data(), outR.data(), kBlock, phase, 1000.0f, 0.5f, true);

    const auto& bands = getBands(e);
    int loudest = 0;
    for (int i = 1; i < kBands; ++i)
        if (bands[static_cast<std::size_t>(i)] > bands[static_cast<std::size_t>(loudest)])
            loudest = i;

    int nearest = 0;
    float best = 1.0e9f;
    for (int i = 0; i < kBands; ++i) {
        const float d = std::fabs(bandCentreHz(i) - 1000.0f);
        if (d < best) {
            best = d;
            nearest = i;
        }
    }
    assert(std::abs(loudest - nearest) <= 2);
    assert(bands[static_cast<std::size_t>(loudest)] > -9.5f);
    assert(bands[static_cast<std::size_t>(loudest)] < -2.0f);

    // Peak ≈ 0.5 (-6 dBFS); RMS ≈ 0.354 (-9 dBFS). Correlation of a mono
    // signal should be close to +1.
    const float peakDb = 20.0f * std::log10(getPeakL(e) + 1.0e-6f);
    const float rmsDb = 20.0f * std::log10(getRmsL(e) + 1.0e-6f);
    assert(std::fabs(peakDb - (-6.0f)) < 1.0f);
    assert(std::fabs(rmsDb - (-9.0f)) < 1.5f);
    assert(getCorr(e) > 0.9f);

    // Silence decays the bands back to the floor.
    reset(e);
    std::fill(inL.begin(), inL.end(), 0.0f);
    std::fill(inR.begin(), inR.end(), 0.0f);
    for (int b = 0; b < 8; ++b)
        processBlock(e, p, inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    for (int i = 0; i < kBands; ++i)
        assert(getBands(e)[static_cast<std::size_t>(i)] <= -100.0f);

    std::cout << "scope core tests passed\n";
    return 0;
}
