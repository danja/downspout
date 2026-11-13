#include "downspout/test_assert.h"

#include "zmorph_engine.hpp"
#include "zmorph_params.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace downspout::zmorph;

namespace {

constexpr float kPi = 3.14159265358979323846f;

float rms(const std::vector<float>& v)
{
    double sum = 0.0;
    for (float x : v)
        sum += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(v.size())));
}

std::vector<float> tone(const int frames, const float hz, const float amp, const float sr)
{
    std::vector<float> wave(frames);
    for (int i = 0; i < frames; ++i) {
        const float phase = 2.0f * kPi * hz * static_cast<float>(i) / sr;
        wave[static_cast<std::size_t>(i)] = amp * std::sin(phase);
    }
    return wave;
}

}  // namespace

int main() {
    Engine e;
    activate(e, 48000.0);
    assert(e.allocated);

    ParamId target = kCutoff;
    assert(controllerTarget(kCcResonance, target) && target == kResonance);
    assert(controllerTarget(kCcMorph, target) && target == kMorph);
    assert(controllerTarget(kCcMix, target) && target == kMix);
    assert(controllerTarget(kCcLevel, target) && target == kLevel);
    assert(!controllerTarget(0, target));
    assert(std::fabs(noteToCutoff(69) - 440.0f) < 1.0f);
    assert(noteToCutoff(81) > noteToCutoff(69));
    assert(controllerToParameter(kResonance, 64) > 0.25f);
    assert(controllerToParameter(kMix, 0) < 0.1f);

    std::vector<float> inL(256, 0.0f), inR(256, 0.0f), outL(256), outR(256);
    Params p;
    inL[0] = 1.0f; inR[0] = 1.0f;
    processBlock(e, p, inL.data(), inR.data(), outL.data(), outR.data(), 256);
    for (int i = 0; i < 256; ++i) {
        assert(std::isfinite(outL[static_cast<std::size_t>(i)]) && std::isfinite(outR[static_cast<std::size_t>(i)]));
    }
    assert(std::fabs(outL[0]) > 0.0f || std::fabs(outR[0]) > 0.0f);

    reset(e);
    const auto low = tone(2000, 220.0f, 0.25f, 48000.0f);
    const auto high = tone(2000, 880.0f, 0.25f, 48000.0f);
    std::vector<float> outLow(2000), outHigh(2000);
    Params lowCutoff = p;
    lowCutoff.cutoff = 200.0f;
    Params highCutoff = p;
    highCutoff.cutoff = 4000.0f;
    processBlock(e, lowCutoff, low.data(), low.data(), outLow.data(), outLow.data(), 2000);
    reset(e);
    processBlock(e, highCutoff, low.data(), low.data(), outHigh.data(), outHigh.data(), 2000);
    assert(std::fabs(rms(outLow) - rms(outHigh)) > 0.01f);

    reset(e);
    Params resonanceLow = p;
    resonanceLow.resonance = 0.1f;
    Params resonanceHigh = p;
    resonanceHigh.resonance = 0.8f;
    std::vector<float> outResLow(2000), outResHigh(2000);
    processBlock(e, resonanceLow, low.data(), low.data(), outResLow.data(), outResLow.data(), 2000);
    reset(e);
    processBlock(e, resonanceHigh, low.data(), low.data(), outResHigh.data(), outResHigh.data(), 2000);
    assert(std::fabs(rms(outResLow) - rms(outResHigh)) > 0.001f);

    reset(e);
    Params morphLow = p;
    morphLow.morph = 0.0f;
    Params morphHigh = p;
    morphHigh.morph = 1.0f;
    std::vector<float> outMorphLow(2000), outMorphHigh(2000);
    processBlock(e, morphLow, low.data(), low.data(), outMorphLow.data(), outMorphLow.data(), 2000);
    reset(e);
    processBlock(e, morphHigh, low.data(), low.data(), outMorphHigh.data(), outMorphHigh.data(), 2000);
    assert(std::fabs(rms(outMorphLow) - rms(outMorphHigh)) > 0.01f);

    reset(e);
    Params dry = p;
    dry.mix = 0.0f;
    Params wet = p;
    wet.mix = 1.0f;
    std::vector<float> outDry(2000), outWet(2000);
    processBlock(e, dry, low.data(), low.data(), outDry.data(), outDry.data(), 2000);
    reset(e);
    processBlock(e, wet, low.data(), low.data(), outWet.data(), outWet.data(), 2000);
    assert(std::fabs(rms(outDry) - rms(outWet)) > 0.01f);

    reset(e);
    Params quiet = p;
    quiet.level = -12.0f;
    Params loud = p;
    loud.level = +6.0f;
    std::vector<float> outQuiet(2000), outLoud(2000);
    processBlock(e, quiet, low.data(), low.data(), outQuiet.data(), outQuiet.data(), 2000);
    reset(e);
    processBlock(e, loud, low.data(), low.data(), outLoud.data(), outLoud.data(), 2000);
    assert(rms(outLoud) > rms(outQuiet));

    reset(e);
    Params noteTrack = p;
    noteTrack.noteTracking = true;
    noteTrack.noteCutoff = noteToCutoff(60);
    std::vector<float> outNote(2000);
    processBlock(e, noteTrack, low.data(), low.data(), outNote.data(), outNote.data(), 2000);
    assert(rms(outNote) > 0.0f);
    assert(noteTrack.noteCutoff > 0.0f);

    std::cout << "zmorph core tests passed\n";
    return 0;
}