#include "basilico_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using downspout::basilico::BasilicoEngine;
using downspout::basilico::ParamId;
using downspout::basilico::kParameterSpecs;

void require(const bool condition, const char* const message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

float renderEnergy(BasilicoEngine& engine, const int frames)
{
    float energy = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "basilico rendered non-finite audio");
        energy += std::fabs(frame.left) + std::fabs(frame.right);
    }
    return energy;
}

float renderPeak(BasilicoEngine& engine, const int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "basilico rendered non-finite audio");
        peak = std::max(peak, std::max(std::fabs(frame.left), std::fabs(frame.right)));
    }
    return peak;
}

float estimateZeroCrossingPitch(const std::vector<float>& samples, const float sampleRate)
{
    int crossings = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        if ((samples[i - 1] < 0.0f && samples[i] >= 0.0f) || (samples[i - 1] > 0.0f && samples[i] <= 0.0f))
            ++crossings;
    return (static_cast<float>(crossings) * 0.5f) * sampleRate / static_cast<float>(samples.size());
}

float estimatePitch(BasilicoEngine& engine, const int warmupFrames, const int sampleFrames)
{
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(sampleFrames));
    float lastInput = 0.0f;
    float lastOutput = 0.0f;
    for (int i = 0; i < warmupFrames + sampleFrames; ++i)
    {
        const auto frame = engine.processStereo();
        const float mono = (frame.left + frame.right) * 0.5f;
        const float hp = mono - lastInput + 0.995f * lastOutput;
        lastInput = mono;
        lastOutput = hp;
        if (i >= warmupFrames)
            samples.push_back(hp);
    }
    return estimateZeroCrossingPitch(samples, 48000.0f);
}

float expectedFrequency(const int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

void defaultsAndClamping()
{
    BasilicoEngine engine {48000.0f};
    require(std::fabs(engine.getParameter(ParamId::model) -
                      kParameterSpecs[static_cast<std::size_t>(ParamId::model)].defaultValue) < 1.0e-6f,
            "basilico model default mismatch");

    engine.setParameter(ParamId::model, 3.6f);
    require(std::fabs(engine.getParameter(ParamId::model) - 4.0f) < 1.0e-6f,
            "basilico integer parameter should round");

    engine.setParameter(ParamId::output, 99.0f);
    require(std::fabs(engine.getParameter(ParamId::output) - 1.0f) < 1.0e-6f,
            "basilico unit parameter should clamp high");
}

void allModelsRender()
{
    for (int model = 0; model <= 4; ++model)
    {
        BasilicoEngine engine {48000.0f};
        engine.setParameter(ParamId::model, static_cast<float>(model));
        engine.noteOn(40 + model * 3, 104);
        require(renderEnergy(engine, 4096) > 0.01f, "basilico model should render audible output");
    }
}

void noteOffStops()
{
    BasilicoEngine engine {48000.0f};
    engine.noteOn(43, 100);
    require(engine.active(), "basilico note-on should activate");
    require(renderEnergy(engine, 1024) > 0.001f, "basilico should sound before note-off");
    engine.noteOff(43);
    for (int i = 0; i < 96000 && engine.active(); ++i)
        (void)engine.processStereo();
    require(!engine.active(), "basilico note-off should release and stop");
}

void tracksMidiPitch()
{
    for (int model : {1, 2, 3})
    {
        for (int note : {36, 48, 60})
        {
            BasilicoEngine engine {48000.0f};
            engine.setParameter(ParamId::model, static_cast<float>(model));
            engine.setParameter(ParamId::waveform, 0.0f);
            engine.setParameter(ParamId::subLevel, 0.0f);
            engine.setParameter(ParamId::body, 0.0f);
            engine.setParameter(ParamId::bite, 0.0f);
            engine.setParameter(ParamId::drive, 0.0f);
            engine.setParameter(ParamId::driveType, 0.0f);
            engine.setParameter(ParamId::glide, 0.0f);
            engine.noteOn(note, 100);

            const float estimated = estimatePitch(engine, 12000, 24000);
            const float expected = expectedFrequency(note);
            require(std::fabs(estimated - expected) / expected < 0.025f,
                    "basilico should track MIDI pitch");
        }
    }
}

void glideMovesTowardTarget()
{
    BasilicoEngine engine {48000.0f};
    engine.setParameter(ParamId::glide, 0.65f);
    engine.noteOn(36, 100);
    for (int i = 0; i < 2048; ++i)
        (void)engine.processStereo();
    engine.noteOn(48, 100);
    const float immediate = engine.currentFrequency();
    for (int i = 0; i < 144000; ++i)
        (void)engine.processStereo();
    const float settled = engine.currentFrequency();
    require(immediate < expectedFrequency(48) - 1.0f, "basilico glide should not jump immediately");
    require(std::fabs(settled - expectedFrequency(48)) < 1.0f, "basilico glide should reach target");
}

void middleGlideIsAudible()
{
    BasilicoEngine engine {48000.0f};
    engine.setParameter(ParamId::glide, 0.50f);
    engine.noteOn(36, 100);
    for (int i = 0; i < 2048; ++i)
        (void)engine.processStereo();

    engine.noteOff(36);
    for (int i = 0; i < 256; ++i)
        (void)engine.processStereo();
    engine.noteOn(48, 100);
    for (int i = 0; i < 1024; ++i)
        (void)engine.processStereo();

    const float current = engine.currentFrequency();
    require(current > expectedFrequency(36) + 1.0f, "basilico middle glide should move upward");
    require(current < expectedFrequency(48) - 8.0f, "basilico middle glide should remain audible after attack");
}

void bodyChangesTone()
{
    BasilicoEngine dry {48000.0f};
    dry.setParameter(ParamId::model, 0.0f);
    dry.setParameter(ParamId::body, 0.0f);
    dry.setParameter(ParamId::drive, 0.0f);
    dry.noteOn(40, 104);

    BasilicoEngine resonant {48000.0f};
    resonant.setParameter(ParamId::model, 0.0f);
    resonant.setParameter(ParamId::body, 1.0f);
    resonant.setParameter(ParamId::drive, 0.0f);
    resonant.noteOn(40, 104);

    float difference = 0.0f;
    float reference = 0.0f;
    for (int i = 0; i < 12000; ++i)
    {
        const auto a = dry.processStereo();
        const auto b = resonant.processStereo();
        if (i >= 1000)
        {
            difference += std::fabs(a.left - b.left);
            reference += std::fabs(a.left);
        }
    }

    require(difference > reference * 0.30f, "basilico body should make an audible tonal difference");
}

void velocityAccentChangesOutput()
{
    BasilicoEngine soft {48000.0f};
    soft.setParameter(ParamId::accent, 0.9f);
    soft.noteOn(43, 32);
    const float softEnergy = renderEnergy(soft, 4096);

    BasilicoEngine loud {48000.0f};
    loud.setParameter(ParamId::accent, 0.9f);
    loud.noteOn(43, 118);
    const float loudEnergy = renderEnergy(loud, 4096);

    require(loudEnergy > softEnergy * 1.6f, "basilico velocity should affect output/accent");
}

void outputCanBoost()
{
    BasilicoEngine nominal {48000.0f};
    nominal.setParameter(ParamId::output, 0.50f);
    nominal.noteOn(40, 95);
    const float nominalPeak = renderPeak(nominal, 4096);

    BasilicoEngine boosted {48000.0f};
    boosted.setParameter(ParamId::output, 1.0f);
    boosted.noteOn(40, 95);
    const float boostedPeak = renderPeak(boosted, 4096);

    require(boostedPeak > nominalPeak * 1.25f, "basilico output should boost above midpoint");
}

void filterLfoChangesTone()
{
    BasilicoEngine steady {48000.0f};
    steady.setParameter(ParamId::model, 3.0f);
    steady.setParameter(ParamId::waveform, 2.0f);
    steady.setParameter(ParamId::cutoff, 0.36f);
    steady.setParameter(ParamId::resonance, 0.65f);
    steady.setParameter(ParamId::filterEnv, 0.0f);
    steady.setParameter(ParamId::lfoFrequency, 4.0f);
    steady.setParameter(ParamId::lfoDepth, 0.0f);
    steady.noteOn(36, 110);

    BasilicoEngine moving {48000.0f};
    moving.setParameter(ParamId::model, 3.0f);
    moving.setParameter(ParamId::waveform, 2.0f);
    moving.setParameter(ParamId::cutoff, 0.36f);
    moving.setParameter(ParamId::resonance, 0.65f);
    moving.setParameter(ParamId::filterEnv, 0.0f);
    moving.setParameter(ParamId::lfoFrequency, 4.0f);
    moving.setParameter(ParamId::lfoDepth, 0.80f);
    moving.noteOn(36, 110);

    float difference = 0.0f;
    float reference = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const auto a = steady.processStereo();
        const auto b = moving.processStereo();
        require(std::isfinite(a.left) && std::isfinite(b.left), "basilico LFO rendered non-finite audio");
        if (i >= 2048)
        {
            difference += std::fabs(a.left - b.left);
            reference += std::fabs(a.left);
        }
    }

    require(difference > reference * 0.08f, "basilico filter LFO should audibly modulate cutoff");
}

void extremeParametersStayFinite()
{
    BasilicoEngine engine {96000.0f};
    for (std::uint32_t i = 0; i < downspout::basilico::kParameterCount; ++i)
        engine.setParameter(i, kParameterSpecs[i].maximum);
    engine.noteOn(36, 127);
    require(renderPeak(engine, 8192) <= 1.0f, "basilico output should remain bounded");
}

} // namespace

int main()
{
    defaultsAndClamping();
    allModelsRender();
    noteOffStops();
    tracksMidiPitch();
    glideMovesTowardTarget();
    middleGlideIsAudible();
    bodyChangesTone();
    velocityAccentChangesOutput();
    outputCanBoost();
    filterLfoChangesTone();
    extremeParametersStayFinite();

    std::cout << "basilico core tests passed\n";
    return 0;
}
