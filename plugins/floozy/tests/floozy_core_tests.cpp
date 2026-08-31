#include "floozy_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using downspout::floozy::FloozyEngine;
using downspout::floozy::MidiMessage;
using downspout::floozy::ParamId;
using downspout::floozy::kParameterSpecs;

void require(const bool condition, const char* const message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void require(const bool condition, const std::string& message)
{
    require(condition, message.c_str());
}

float renderPeak(FloozyEngine& engine, const int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "floozy rendered non-finite audio");
        peak = std::max(peak, std::max(std::fabs(frame.left), std::fabs(frame.right)));
    }
    return peak;
}

float renderEnergy(FloozyEngine& engine, const int frames)
{
    float energy = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "floozy rendered non-finite audio");
        energy += std::fabs(frame.left) + std::fabs(frame.right);
    }
    return energy;
}

float renderDcBlockedEnergy(FloozyEngine& engine, const int frames)
{
    float energy = 0.0f;
    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "floozy rendered non-finite audio");
        const float left = frame.left - leftX + 0.995f * leftY;
        const float right = frame.right - rightX + 0.995f * rightY;
        leftX = frame.left;
        rightX = frame.right;
        leftY = left;
        rightY = right;
        energy += std::fabs(left) + std::fabs(right);
    }
    return energy;
}

float estimateZeroCrossingPitch(const std::vector<float>& samples, const float sampleRate)
{
    int crossings = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        if ((samples[i - 1] < 0.0f && samples[i] >= 0.0f) || (samples[i - 1] > 0.0f && samples[i] <= 0.0f))
            ++crossings;

    return (static_cast<float>(crossings) * 0.5f) * sampleRate / static_cast<float>(samples.size());
}

float estimateRenderedPitch(FloozyEngine& engine, const int warmupFrames, const int sampleFrames)
{
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(sampleFrames));
    float lastInput = 0.0f;
    float lastOutput = 0.0f;

    for (int i = 0; i < warmupFrames + sampleFrames; ++i)
    {
        const auto frame = engine.processStereo();
        const float mono = (frame.left + frame.right) * 0.5f;
        const float highPassed = mono - lastInput + 0.995f * lastOutput;
        lastInput = mono;
        lastOutput = highPassed;

        if (i >= warmupFrames)
            samples.push_back(highPassed);
    }

    return estimateZeroCrossingPitch(samples, 48000.0f);
}

float renderWindowEnergy(FloozyEngine& engine, const int skipFrames, const int frames)
{
    for (int i = 0; i < skipFrames; ++i)
        (void)engine.processStereo();
    return renderDcBlockedEnergy(engine, frames);
}

float midiNoteToExpectedFrequency(const int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

void defaultsAndClamping()
{
    FloozyEngine engine {48000.0f};
    require(std::fabs(engine.getParameter(ParamId::masterGain) -
                      kParameterSpecs[static_cast<std::size_t>(ParamId::masterGain)].defaultValue) < 1.0e-6f,
            "floozy master gain default mismatch");

    engine.setParameter(ParamId::sourceAlgorithm, 2.7f);
    require(std::fabs(engine.getParameter(ParamId::sourceAlgorithm) - 3.0f) < 1.0e-6f,
            "floozy integer parameter should round");

    engine.setParameter(ParamId::sourceAlgorithm, 99.0f);
    require(std::fabs(engine.getParameter(ParamId::sourceAlgorithm) -
                      kParameterSpecs[static_cast<std::size_t>(ParamId::sourceAlgorithm)].maximum) < 1.0e-6f,
            "floozy integer parameter should clamp high");

    engine.setParameter(ParamId::sourceLevel, -1.0f);
    require(std::fabs(engine.getParameter(ParamId::sourceLevel)) < 1.0e-6f,
            "floozy unit parameter should clamp low");
}

void allAlgorithmsRender()
{
    for (int algorithm = 0; algorithm <= 6; ++algorithm)
    {
        FloozyEngine engine {48000.0f};
        engine.setParameter(ParamId::sourceAlgorithm, static_cast<float>(algorithm));
        engine.setParameter(ParamId::sourceLevel, 0.85f);
        engine.setParameter(ParamId::masterGain, 0.9f);
        engine.noteOn(48 + algorithm, 116);
        require(renderPeak(engine, 4096) > 0.0005f, "floozy algorithm did not render audible output");
    }
}

void velocityAffectsOutput()
{
    FloozyEngine soft {48000.0f};
    soft.setParameter(ParamId::sourceLevel, 0.9f);
    soft.noteOn(57, 28);
    const float softEnergy = renderEnergy(soft, 2048);

    FloozyEngine loud {48000.0f};
    loud.setParameter(ParamId::sourceLevel, 0.9f);
    loud.noteOn(57, 122);
    const float loudEnergy = renderEnergy(loud, 2048);

    require(loudEnergy > softEnergy * 1.8f, "floozy velocity should scale output");
}

void masterGainCanBoost()
{
    FloozyEngine nominal {48000.0f};
    nominal.setParameter(ParamId::masterGain, 0.50f);
    nominal.noteOn(60, 90);
    const float nominalEnergy = renderDcBlockedEnergy(nominal, 12000);

    FloozyEngine boosted {48000.0f};
    boosted.setParameter(ParamId::masterGain, 1.0f);
    boosted.noteOn(60, 90);
    const float boostedEnergy = renderDcBlockedEnergy(boosted, 12000);

    require(boostedEnergy > nominalEnergy * 1.55f, "floozy master gain should boost above nominal level");
}

void allInterfacesRender()
{
    for (int interfaceType = 0; interfaceType <= 11; ++interfaceType)
    {
        FloozyEngine engine {48000.0f};
        engine.setParameter(ParamId::interfaceType, static_cast<float>(interfaceType));
        engine.setParameter(ParamId::interfaceIntensity, 0.72f);
        engine.setParameter(ParamId::sourceLevel, 0.70f);
        engine.setParameter(ParamId::noiseLevel, 0.18f);
        engine.setParameter(ParamId::masterGain, 0.85f);
        engine.noteOn(55 + (interfaceType % 12), 112);
        require(renderPeak(engine, 8192) > 0.0002f, "floozy interface did not render audible output");
    }
}

void allInterfacesHaveUsableLevel()
{
    for (int interfaceType = 0; interfaceType <= 11; ++interfaceType)
    {
        FloozyEngine engine {48000.0f};
        engine.setParameter(ParamId::interfaceType, static_cast<float>(interfaceType));
        engine.setParameter(ParamId::interfaceIntensity, 0.72f);
        engine.setParameter(ParamId::sourceLevel, 0.70f);
        engine.setParameter(ParamId::noiseLevel, 0.18f);
        engine.setParameter(ParamId::masterGain, 0.85f);
        engine.noteOn(55 + (interfaceType % 12), 112);
        const float averageEnergy = renderEnergy(engine, 48000) / 48000.0f;
        require(averageEnergy > 0.004f, "floozy interface output level is too low");
    }
}

void sustainedInterfacesHaveAudibleAcEnergy()
{
    for (int interfaceType : {2, 3, 4, 5})
    {
        FloozyEngine engine {48000.0f};
        engine.setParameter(ParamId::interfaceType, static_cast<float>(interfaceType));
        engine.setParameter(ParamId::interfaceIntensity, 0.72f);
        engine.setParameter(ParamId::sourceLevel, 0.70f);
        engine.setParameter(ParamId::noiseLevel, 0.18f);
        engine.setParameter(ParamId::masterGain, 0.85f);
        engine.noteOn(60, 64);
        const float averageAcEnergy = renderDcBlockedEnergy(engine, 48000) / 48000.0f;
        require(averageAcEnergy > 0.015f, "floozy sustained interface has too little audible AC energy");
    }
}

void sustainedInterfacesTrackMidiPitch()
{
    for (int interfaceType : {2, 3, 4, 5})
    {
        for (int note : {48, 60, 72})
        {
            FloozyEngine engine {48000.0f};
            engine.setParameter(ParamId::interfaceType, static_cast<float>(interfaceType));
            engine.setParameter(ParamId::sourceAlgorithm, 3.0f);
            engine.setParameter(ParamId::sourceLevel, 0.70f);
            engine.setParameter(ParamId::noiseLevel, 0.0f);
            engine.setParameter(ParamId::reverbLevel, 0.0f);
            engine.setParameter(ParamId::filterFrequency, 0.85f);
            engine.setParameter(ParamId::filterQ, 0.10f);
            engine.setParameter(ParamId::masterGain, 0.50f);
            engine.noteOn(note, 100);

            const float estimated = estimateRenderedPitch(engine, 24000, 24000);
            const float expected = midiNoteToExpectedFrequency(note);
            require(std::fabs(estimated - expected) / expected < 0.025f,
                    "floozy sustained interface should track MIDI pitch");
        }
    }
}

void bodyControlsShapeResonatorTail()
{
    FloozyEngine shortBody {48000.0f};
    shortBody.setParameter(ParamId::interfaceType, 0.0f);
    shortBody.setParameter(ParamId::interfaceIntensity, 0.85f);
    shortBody.setParameter(ParamId::sourceLevel, 0.05f);
    shortBody.setParameter(ParamId::noiseLevel, 0.45f);
    shortBody.setParameter(ParamId::attack, 0.0f);
    shortBody.setParameter(ParamId::release, 0.35f);
    shortBody.setParameter(ParamId::delay1Feedback, 0.0f);
    shortBody.setParameter(ParamId::delay2Feedback, 0.0f);
    shortBody.setParameter(ParamId::filterFeedback, 0.0f);
    shortBody.setParameter(ParamId::reverbLevel, 0.0f);
    shortBody.setParameter(ParamId::masterGain, 0.7f);
    shortBody.noteOn(48, 118);
    (void)renderEnergy(shortBody, 2048);
    shortBody.noteOff(48);
    const float shortTail = renderWindowEnergy(shortBody, 12000, 24000);

    FloozyEngine ringingBody {48000.0f};
    ringingBody.setParameter(ParamId::interfaceType, 0.0f);
    ringingBody.setParameter(ParamId::interfaceIntensity, 0.85f);
    ringingBody.setParameter(ParamId::sourceLevel, 0.05f);
    ringingBody.setParameter(ParamId::noiseLevel, 0.45f);
    ringingBody.setParameter(ParamId::attack, 0.0f);
    ringingBody.setParameter(ParamId::release, 0.35f);
    ringingBody.setParameter(ParamId::delay1Feedback, 1.0f);
    ringingBody.setParameter(ParamId::delay2Feedback, 0.75f);
    ringingBody.setParameter(ParamId::filterFeedback, 0.25f);
    ringingBody.setParameter(ParamId::reverbLevel, 0.0f);
    ringingBody.setParameter(ParamId::masterGain, 0.7f);
    ringingBody.noteOn(48, 118);
    (void)renderEnergy(ringingBody, 2048);
    ringingBody.noteOff(48);
    const float ringingTail = renderWindowEnergy(ringingBody, 12000, 24000);

    require(ringingTail > shortTail * 6.0f,
            "floozy body feedback should extend resonator tail, short=" + std::to_string(shortTail) +
                " ringing=" + std::to_string(ringingTail));

    FloozyEngine lowTune {48000.0f};
    lowTune.setParameter(ParamId::interfaceType, 0.0f);
    lowTune.setParameter(ParamId::interfaceIntensity, 0.85f);
    lowTune.setParameter(ParamId::sourceLevel, 0.05f);
    lowTune.setParameter(ParamId::noiseLevel, 0.45f);
    lowTune.setParameter(ParamId::attack, 0.0f);
    lowTune.setParameter(ParamId::delay1Feedback, 1.0f);
    lowTune.setParameter(ParamId::delay2Feedback, 0.75f);
    lowTune.setParameter(ParamId::filterFeedback, 0.25f);
    lowTune.setParameter(ParamId::tuning, 0.25f);
    lowTune.setParameter(ParamId::reverbLevel, 0.0f);
    lowTune.noteOn(60, 118);

    FloozyEngine highTune {48000.0f};
    highTune.setParameter(ParamId::interfaceType, 0.0f);
    highTune.setParameter(ParamId::interfaceIntensity, 0.85f);
    highTune.setParameter(ParamId::sourceLevel, 0.05f);
    highTune.setParameter(ParamId::noiseLevel, 0.45f);
    highTune.setParameter(ParamId::attack, 0.0f);
    highTune.setParameter(ParamId::delay1Feedback, 1.0f);
    highTune.setParameter(ParamId::delay2Feedback, 0.75f);
    highTune.setParameter(ParamId::filterFeedback, 0.25f);
    highTune.setParameter(ParamId::tuning, 0.75f);
    highTune.setParameter(ParamId::reverbLevel, 0.0f);
    highTune.noteOn(60, 118);

    const float lowPitch = estimateRenderedPitch(lowTune, 8000, 24000);
    const float highPitch = estimateRenderedPitch(highTune, 8000, 24000);
    require(highPitch > lowPitch * 1.45f, "floozy body tuning should move resonator pitch");
}

void bodyAutomationAvoidsFullScaleImpulses()
{
    FloozyEngine engine {48000.0f};
    engine.setParameter(ParamId::interfaceType, 0.0f);
    engine.setParameter(ParamId::interfaceIntensity, 0.95f);
    engine.setParameter(ParamId::sourceLevel, 0.35f);
    engine.setParameter(ParamId::noiseLevel, 0.35f);
    engine.setParameter(ParamId::attack, 0.0f);
    engine.setParameter(ParamId::delay1Feedback, 1.0f);
    engine.setParameter(ParamId::delay2Feedback, 1.0f);
    engine.setParameter(ParamId::filterFeedback, 1.0f);
    engine.setParameter(ParamId::reverbLevel, 0.0f);
    engine.setParameter(ParamId::masterGain, 0.85f);
    engine.noteOn(45, 120);

    float previous = 0.0f;
    float maxDelta = 0.0f;
    float maxPeak = 0.0f;
    for (int frame = 0; frame < 48000; ++frame)
    {
        if (frame > 0 && frame % 512 == 0)
        {
            const bool alternate = ((frame / 512) % 2) == 0;
            engine.setParameter(ParamId::tuning, alternate ? 0.22f : 0.78f);
            engine.setParameter(ParamId::ratio, alternate ? 0.15f : 0.90f);
            engine.setParameter(ParamId::filterFeedback, alternate ? 0.35f : 1.0f);
        }

        const auto out = engine.processStereo();
        require(std::isfinite(out.left) && std::isfinite(out.right), "floozy body automation emitted non-finite audio");
        const float mono = (out.left + out.right) * 0.5f;
        maxPeak = std::max(maxPeak, std::fabs(mono));
        maxDelta = std::max(maxDelta, std::fabs(mono - previous));
        previous = mono;
    }

    require(maxPeak <= 1.0f, "floozy body automation should stay bounded");
    require(maxDelta < 1.45f,
            "floozy body automation should avoid full-scale impulses, delta=" + std::to_string(maxDelta));
}

void midiVoiceLifecycle()
{
    FloozyEngine engine {48000.0f};
    const std::uint8_t noteOn[] = {0x90, 60, 100};
    const std::uint8_t noteOff[] = {0x80, 60, 0};
    const std::uint8_t panic[] = {0xb0, 123, 0};

    engine.handleMidi(noteOn, 3);
    require(engine.activeVoiceCount() == 1, "floozy note on should activate one voice");
    engine.handleMidi(noteOff, 3);
    require(engine.activeVoiceCount() == 1, "floozy note off should release rather than hard-stop");
    engine.handleMidi(panic, 3);
    require(engine.activeVoiceCount() == 0, "floozy all-notes-off should clear voices");
}

void noteOffEventuallyStopsAllInterfaces()
{
    for (int interfaceType = 0; interfaceType <= 11; ++interfaceType)
    {
        FloozyEngine engine {48000.0f};
        engine.setParameter(ParamId::interfaceType, static_cast<float>(interfaceType));
        engine.setParameter(ParamId::interfaceIntensity, 0.80f);
        engine.setParameter(ParamId::delay1Feedback, 0.70f);
        engine.setParameter(ParamId::delay2Feedback, 0.45f);
        engine.setParameter(ParamId::filterFeedback, 0.15f);
        engine.noteOn(60, 112);
        require(renderEnergy(engine, 4096) > 0.0001f, "floozy should render before note-off");
        engine.noteOff(60);

        for (int i = 0; i < 144000 && engine.activeVoiceCount() > 0; ++i)
            (void)engine.processStereo();

        require(engine.activeVoiceCount() == 0, "floozy note-off should let every interface stop");
    }
}

void polyphonyIsCapped()
{
    FloozyEngine engine {48000.0f};
    // Default is 4 voices; send 9 notes to verify cap at active count
    for (int note = 48; note < 57; ++note)
        engine.noteOn(note, 100);
    const auto numVoicesParam = static_cast<std::uint32_t>(downspout::floozy::ParamId::numVoices);
    const auto defaultVoices = static_cast<std::size_t>(
        std::round(downspout::floozy::kParameterSpecs[numVoicesParam].defaultValue));
    require(engine.activeVoiceCount() == defaultVoices, "floozy polyphony cap mismatch");

    // Verify cap still works at max voices
    engine.setParameter(numVoicesParam, static_cast<float>(FloozyEngine::kMaxVoices));
    for (int note = 48; note < 57; ++note)
        engine.noteOn(note, 100);
    require(engine.activeVoiceCount() == FloozyEngine::kMaxVoices, "floozy polyphony cap at max voices mismatch");
}

void processBlockSchedulesMidi()
{
    FloozyEngine engine {48000.0f};
    std::array<float, 8192> left {};
    std::array<float, 8192> right {};
    std::array<MidiMessage, 2> midi {};
    midi[0].frame = 32;
    midi[0].size = 3;
    midi[0].data = {0x90, 60, 110, 0};
    midi[1].frame = 7000;
    midi[1].size = 3;
    midi[1].data = {0x80, 60, 0, 0};

    engine.processBlock(left.data(), right.data(), static_cast<std::uint32_t>(left.size()), midi.data(), 2);
    float preEnergy = 0.0f;
    float postEnergy = 0.0f;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        require(std::isfinite(left[i]) && std::isfinite(right[i]), "floozy processBlock emitted non-finite audio");
        if (i < 32)
            preEnergy += std::fabs(left[i]) + std::fabs(right[i]);
        else
            postEnergy += std::fabs(left[i]) + std::fabs(right[i]);
    }
    require(preEnergy < 0.0001f, "floozy processBlock should respect note-on frame offset");
    require(postEnergy > 0.0001f, "floozy processBlock should render after scheduled note-on");
}

void extremeParametersStayFinite()
{
    FloozyEngine engine {96000.0f};
    for (std::uint32_t i = 0; i < downspout::floozy::kParameterCount; ++i)
        engine.setParameter(i, kParameterSpecs[i].maximum);
    engine.setParameter(ParamId::masterGain, 1.0f);
    engine.noteOn(36, 127);
    require(renderPeak(engine, 8192) <= 1.0f, "floozy output should remain bounded");
}

} // namespace

int main()
{
    defaultsAndClamping();
    allAlgorithmsRender();
    velocityAffectsOutput();
    masterGainCanBoost();
    allInterfacesRender();
    allInterfacesHaveUsableLevel();
    sustainedInterfacesHaveAudibleAcEnergy();
    sustainedInterfacesTrackMidiPitch();
    bodyControlsShapeResonatorTail();
    bodyAutomationAvoidsFullScaleImpulses();
    midiVoiceLifecycle();
    noteOffEventuallyStopsAllInterfaces();
    polyphonyIsCapped();
    processBlockSchedulesMidi();
    extremeParametersStayFinite();

    std::cout << "floozy core tests passed\n";
    return 0;
}
