#include "canticle_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using downspout::canticle::CanticleEngine;
using downspout::canticle::ParamId;
using downspout::canticle::kParameterSpecs;

void require(const bool condition, const char* const message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

float renderEnergy(CanticleEngine& engine, const int frames)
{
    float energy = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "canticle rendered non-finite audio");
        energy += std::fabs(frame.left) + std::fabs(frame.right);
    }
    return energy;
}

float renderPeak(CanticleEngine& engine, const int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "canticle rendered non-finite audio");
        peak = std::max(peak, std::max(std::fabs(frame.left), std::fabs(frame.right)));
    }
    return peak;
}

float renderHighpassEnergy(CanticleEngine& engine, const int frames)
{
    float energy = 0.0f;
    float lastInput = 0.0f;
    float lastOutput = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "canticle rendered non-finite audio");
        const float mono = (frame.left + frame.right) * 0.5f;
        const float high = mono - lastInput + 0.92f * lastOutput;
        lastInput = mono;
        lastOutput = high;
        energy += std::fabs(high);
    }
    return energy;
}

void defaultsAndClamping()
{
    CanticleEngine engine {48000.0f};
    require(std::fabs(engine.getParameter(ParamId::model) -
                      kParameterSpecs[static_cast<std::size_t>(ParamId::model)].defaultValue) < 1.0e-6f,
            "canticle model default mismatch");

    engine.setParameter(ParamId::model, 3.6f);
    require(std::fabs(engine.getParameter(ParamId::model) - 4.0f) < 1.0e-6f,
            "canticle integer parameter should round");

    engine.setParameter(ParamId::output, -99.0f);
    require(std::fabs(engine.getParameter(ParamId::output) - 0.0f) < 1.0e-6f,
            "canticle unit parameter should clamp low");

    engine.setParameter(ParamId::articulation, 2.6f);
    require(std::fabs(engine.getParameter(ParamId::articulation) - 3.0f) < 1.0e-6f,
            "canticle articulation should round");

    engine.setParameter(ParamId::range, 99.0f);
    require(std::fabs(engine.getParameter(ParamId::range) - 3.0f) < 1.0e-6f,
            "canticle range should clamp high");
}

void allModelsRender()
{
    for (int model = 0; model <= 4; ++model)
    {
        CanticleEngine engine {48000.0f};
        engine.setParameter(ParamId::model, static_cast<float>(model));
        engine.noteOn(60 + model, 100);
        require(renderEnergy(engine, 8192) > 0.01f, "canticle model should render audible output");
    }
}

void polyphonyAndVoiceStealing()
{
    CanticleEngine engine {48000.0f};
    for (int i = 0; i < 16; ++i)
        engine.noteOn(48 + i, 96);

    require(engine.activeVoiceCount() == CanticleEngine::kMaxVoices, "canticle should cap active voices");
    require(renderEnergy(engine, 4096) > 0.01f, "canticle stolen chord should still render");
}

void noteOffReleases()
{
    CanticleEngine engine {48000.0f};
    engine.setParameter(ParamId::release, 0.02f);
    engine.noteOn(64, 100);
    require(engine.activeVoiceCount() == 1, "canticle note-on should activate one voice");
    require(renderEnergy(engine, 2048) > 0.001f, "canticle should sound before note-off");
    engine.noteOff(64);
    for (int i = 0; i < 96000 && engine.activeVoiceCount() > 0; ++i)
        (void)engine.processStereo();
    require(engine.activeVoiceCount() == 0, "canticle note-off should release and stop");
}

void allNotesOffReleasesChord()
{
    CanticleEngine engine {48000.0f};
    engine.noteOn(60, 100);
    engine.noteOn(64, 100);
    engine.noteOn(67, 100);
    require(engine.activeVoiceCount() == 3, "canticle chord should allocate three voices");

    const std::uint8_t allNotesOff[] = {0xB0, 123, 0};
    engine.handleMidi(allNotesOff, 3);
    for (int i = 0; i < 144000 && engine.activeVoiceCount() > 0; ++i)
        (void)engine.processStereo();
    require(engine.activeVoiceCount() == 0, "canticle all-notes-off should release the chord");
}

void outputIsBounded()
{
    CanticleEngine engine {48000.0f};
    engine.setParameter(ParamId::output, 1.0f);
    engine.setParameter(ParamId::drive, 1.0f);
    engine.setParameter(ParamId::metal, 1.0f);
    for (int i = 0; i < 12; ++i)
        engine.noteOn(48 + i, 127);
    require(renderPeak(engine, 24000) <= 1.0f, "canticle output should remain bounded");
}

void metalAddsBrightEdge()
{
    CanticleEngine plain {48000.0f};
    plain.setParameter(ParamId::model, 0.0f);
    plain.setParameter(ParamId::tone, 0.55f);
    plain.setParameter(ParamId::body, 0.58f);
    plain.setParameter(ParamId::metal, 0.0f);
    plain.noteOn(60, 108);
    const float plainHigh = renderHighpassEnergy(plain, 12000);

    CanticleEngine metallic {48000.0f};
    metallic.setParameter(ParamId::model, 0.0f);
    metallic.setParameter(ParamId::tone, 0.55f);
    metallic.setParameter(ParamId::body, 0.58f);
    metallic.setParameter(ParamId::metal, 1.0f);
    metallic.noteOn(60, 108);
    const float metalHigh = renderHighpassEnergy(metallic, 12000);

    require(metalHigh > plainHigh * 1.25f, "canticle metal should add a brighter edge");
}

} // namespace

int main()
{
    defaultsAndClamping();
    allModelsRender();
    polyphonyAndVoiceStealing();
    noteOffReleases();
    allNotesOffReleasesChord();
    outputIsBounded();
    metalAddsBrightEdge();

    std::cout << "canticle core tests passed\n";
    return 0;
}
