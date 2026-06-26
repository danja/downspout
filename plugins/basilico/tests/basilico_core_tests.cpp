#include "basilico_engine.hpp"
#include "basilico_modulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using downspout::basilico::BasilicoEngine;
using downspout::basilico::ParamId;
using downspout::basilico::TransportSnapshot;
using downspout::basilico::WobbleConfig;
using downspout::basilico::WobbleModulator;
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

float renderDifference(BasilicoEngine& a, BasilicoEngine& b, const int frames, const int skipFrames)
{
    float difference = 0.0f;
    float reference = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto left = a.processStereo();
        const auto right = b.processStereo();
        require(std::isfinite(left.left) && std::isfinite(right.left), "basilico rendered non-finite audio");
        require(std::isfinite(left.right) && std::isfinite(right.right), "basilico rendered non-finite audio");
        if (i >= skipFrames)
        {
            difference += std::fabs(left.left - right.left) + std::fabs(left.right - right.right);
            reference += std::fabs(left.left) + std::fabs(left.right);
        }
    }
    return reference > 0.0f ? difference / reference : 0.0f;
}

TransportSnapshot playingTransport(const double bpm, const double barBeat = 0.0)
{
    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = barBeat;
    transport.beatsPerBar = 4.0;
    transport.bpm = bpm;
    return transport;
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

    engine.setParameter(ParamId::wobbleDivision, 4.6f);
    require(std::fabs(engine.getParameter(ParamId::wobbleDivision) - 5.0f) < 1.0e-6f,
            "basilico appended integer parameter should round");

    engine.setParameter(ParamId::wobbleStart, 999.0f);
    require(std::fabs(engine.getParameter(ParamId::wobbleStart) - 360.0f) < 1.0e-6f,
            "basilico wobble start should clamp high");
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

void ampWobbleChangesLevel()
{
    BasilicoEngine steady {48000.0f};
    steady.setParameter(ParamId::model, 2.0f);
    steady.setParameter(ParamId::lfoFrequency, 4.0f);
    steady.setParameter(ParamId::ampWobble, 0.0f);
    steady.noteOn(36, 112);

    BasilicoEngine moving {48000.0f};
    moving.setParameter(ParamId::model, 2.0f);
    moving.setParameter(ParamId::lfoFrequency, 4.0f);
    moving.setParameter(ParamId::ampWobble, 1.0f);
    moving.noteOn(36, 112);

    require(renderDifference(steady, moving, 48000, 2048) > 0.12f,
            "basilico amp wobble should audibly modulate level");
}

void wobbleStartOffsetsCycleStart()
{
    WobbleModulator unshifted;
    unshifted.reset();
    WobbleModulator shifted;
    shifted.reset();

    WobbleConfig config {};
    config.freeRateHz = 0.5f;
    config.startDegrees = 0.0f;

    WobbleConfig shiftedConfig = config;
    shiftedConfig.startDegrees = 90.0f;

    float unshiftedValue = 0.0f;
    float shiftedValue = 0.0f;
    for (int i = 0; i < 256; ++i)
    {
        unshiftedValue = unshifted.process(config, 48000.0f).bipolar;
        shiftedValue = shifted.process(shiftedConfig, 48000.0f).bipolar;
    }

    require(shiftedValue > unshiftedValue + 0.75f,
            "basilico wobble start should offset the modulation cycle");
}

void phaseWobbleCreatesStereoMotion()
{
    BasilicoEngine engine {48000.0f};
    engine.setParameter(ParamId::model, 2.0f);
    engine.setParameter(ParamId::lfoFrequency, 3.0f);
    engine.setParameter(ParamId::phaseWobble, 1.0f);
    engine.noteOn(36, 116);

    float spread = 0.0f;
    float energy = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "basilico phase wobble rendered non-finite audio");
        if (i >= 4096)
        {
            spread += std::fabs(frame.left - frame.right);
            energy += std::fabs(frame.left) + std::fabs(frame.right);
        }
    }

    require(spread > energy * 0.015f, "basilico phase wobble should create stereo flange motion");
}

void tempoSyncChangesWobbleRate()
{
    BasilicoEngine slow {48000.0f};
    slow.setParameter(ParamId::model, 2.0f);
    slow.setParameter(ParamId::wobbleSync, 1.0f);
    slow.setParameter(ParamId::wobbleDivision, 2.0f);
    slow.setParameter(ParamId::wobbleShape, 4.0f);
    slow.setParameter(ParamId::ampWobble, 1.0f);
    slow.setTransport(playingTransport(60.0));
    slow.noteOn(36, 118);

    BasilicoEngine fast {48000.0f};
    fast.setParameter(ParamId::model, 2.0f);
    fast.setParameter(ParamId::wobbleSync, 1.0f);
    fast.setParameter(ParamId::wobbleDivision, 2.0f);
    fast.setParameter(ParamId::wobbleShape, 4.0f);
    fast.setParameter(ParamId::ampWobble, 1.0f);
    fast.setTransport(playingTransport(120.0));
    fast.noteOn(36, 118);

    require(renderDifference(slow, fast, 72000, 2048) > 0.10f,
            "basilico tempo sync should follow host bpm");
}

void squelchChangesAcidTone()
{
    BasilicoEngine plain {48000.0f};
    plain.setParameter(ParamId::model, 3.0f);
    plain.setParameter(ParamId::cutoff, 0.36f);
    plain.setParameter(ParamId::resonance, 0.40f);
    plain.setParameter(ParamId::filterEnv, 0.55f);
    plain.setParameter(ParamId::drive, 0.25f);
    plain.setParameter(ParamId::squelch, 0.0f);
    plain.noteOn(36, 116);

    BasilicoEngine squelchy {48000.0f};
    squelchy.setParameter(ParamId::model, 3.0f);
    squelchy.setParameter(ParamId::cutoff, 0.36f);
    squelchy.setParameter(ParamId::resonance, 0.40f);
    squelchy.setParameter(ParamId::filterEnv, 0.55f);
    squelchy.setParameter(ParamId::drive, 0.25f);
    squelchy.setParameter(ParamId::squelch, 1.0f);
    squelchy.noteOn(36, 116);

    require(renderDifference(plain, squelchy, 36000, 1024) > 0.10f,
            "basilico squelch should audibly change acid filter tone");
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
    ampWobbleChangesLevel();
    wobbleStartOffsetsCycleStart();
    phaseWobbleCreatesStereoMotion();
    tempoSyncChangesWobbleRate();
    squelchChangesAcidTone();
    extremeParametersStayFinite();

    std::cout << "basilico core tests passed\n";
    return 0;
}
