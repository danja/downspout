#include "moka_engine.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using downspout::moka::MokaEngine;
using downspout::moka::ParamId;
using downspout::moka::kParameterSpecs;
using downspout::moka::kPresets;

void require(const bool condition, const char* const message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

float renderEnergy(MokaEngine& engine, const int frames)
{
    float energy = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "moka rendered non-finite audio");
        energy += std::fabs(frame.left) + std::fabs(frame.right);
    }
    return energy;
}

float renderPeak(MokaEngine& engine, const int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "moka rendered non-finite audio");
        peak = std::max(peak, std::max(std::fabs(frame.left), std::fabs(frame.right)));
    }
    return peak;
}

float renderHighpassEnergy(MokaEngine& engine, const int frames)
{
    // Two cascaded one-pole sections (~2.3 kHz cutoff): the fundamental is
    // suppressed and only genuine upper-partial energy is measured.
    float energy = 0.0f;
    float lastInput = 0.0f;
    float firstOut = 0.0f;
    float lastHigh = 0.0f;
    float secondOut = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "moka rendered non-finite audio");
        const float mono = (frame.left + frame.right) * 0.5f;
        const float high1 = mono - lastInput + 0.70f * firstOut;
        lastInput = mono;
        firstOut = high1;
        const float high2 = high1 - lastHigh + 0.70f * secondOut;
        lastHigh = high1;
        secondOut = high2;
        energy += std::fabs(high2);
    }
    return energy;
}

void defaultsAndClamping()
{
    MokaEngine engine {48000.0f};
    require(std::fabs(engine.getParameter(ParamId::voices) - 4.0f) < 1.0e-6f,
            "moka voices should default to 4");
    require(engine.voiceCap() == 4, "moka voice cap should default to 4");
    require(std::fabs(engine.getParameter(ParamId::instrument) - 0.0f) < 1.0e-6f,
            "moka instrument default mismatch");

    engine.setParameter(ParamId::voices, 7.6f);
    require(std::fabs(engine.getParameter(ParamId::voices) - 8.0f) < 1.0e-6f,
            "moka voices should round to integers");
    require(engine.voiceCap() == 8, "moka voice cap should follow voices");

    engine.setParameter(ParamId::voices, 99.0f);
    require(std::fabs(engine.getParameter(ParamId::voices) - 12.0f) < 1.0e-6f,
            "moka voices should clamp high");
    engine.setParameter(ParamId::voices, -3.0f);
    require(std::fabs(engine.getParameter(ParamId::voices) - 1.0f) < 1.0e-6f,
            "moka voices should clamp low");

    engine.setParameter(ParamId::level, -99.0f);
    require(std::fabs(engine.getParameter(ParamId::level) - 0.0f) < 1.0e-6f,
            "moka level should clamp low");

    engine.setParameter(ParamId::instrument, 2.6f);
    require(std::fabs(engine.getParameter(ParamId::instrument) - 3.0f) < 1.0e-6f,
            "moka instrument should round");

    require(std::fabs(engine.getParameter(ParamId::release) - 0.30f) < 1.0e-6f,
            "moka release default mismatch");
    require(std::fabs(engine.getParameter(ParamId::width) - 0.70f) < 1.0e-6f,
            "moka width default mismatch");
    engine.setParameter(ParamId::release, 4.0f);
    require(std::fabs(engine.getParameter(ParamId::release) - 1.0f) < 1.0e-6f,
            "moka release should clamp high");
}

void allModelsRender()
{
    for (int model = 0; model <= 5; ++model)
    {
        MokaEngine engine {48000.0f};
        engine.setParameter(ParamId::instrument, static_cast<float>(model));
        engine.noteOn(60 + model, 100);
        require(renderEnergy(engine, 8192) > 0.01f, "moka model should render audible output");
    }
}

void defaultSingleNotesHaveUsefulLevel()
{
    for (int model = 0; model <= 5; ++model)
    {
        MokaEngine engine {48000.0f};
        engine.setParameter(ParamId::instrument, static_cast<float>(model));
        engine.noteOn(60, 100);
        require(renderPeak(engine, 48000) >= 0.25f,
                "moka default single-note output should reach -12 dBFS");
    }
}

void defaultPolyphonyIsFour()
{
    MokaEngine engine {48000.0f};
    for (int i = 0; i < 8; ++i)
        engine.noteOn(48 + i, 96);

    require(engine.activeVoiceCount() == 4, "moka should cap active voices at 4 by default");
    require(renderEnergy(engine, 4096) > 0.01f, "moka stolen chord should still render");
}

void polyphonyIsSelectable()
{
    MokaEngine wide {48000.0f};
    wide.setParameter(ParamId::voices, 8.0f);
    for (int i = 0; i < 8; ++i)
        wide.noteOn(48 + i, 96);
    require(wide.activeVoiceCount() == 8, "moka voices=8 should allow eight notes");

    MokaEngine mono {48000.0f};
    mono.setParameter(ParamId::voices, 1.0f);
    mono.noteOn(60, 100);
    mono.noteOn(64, 100);
    require(mono.activeVoiceCount() == 1, "moka voices=1 should steal down to one note");
    require(renderEnergy(mono, 4096) > 0.001f, "moka mono steal should still render");
}

void noteOffReleases()
{
    MokaEngine engine {48000.0f};
    engine.noteOn(64, 100);
    require(engine.activeVoiceCount() == 1, "moka note-on should activate one voice");
    require(renderEnergy(engine, 2048) > 0.001f, "moka should sound before note-off");
    engine.noteOff(64);
    for (int i = 0; i < 96000 && engine.activeVoiceCount() > 0; ++i)
        (void)engine.processStereo();
    require(engine.activeVoiceCount() == 0, "moka note-off should release and stop");
}

void allNotesOffReleasesChord()
{
    MokaEngine engine {48000.0f};
    engine.setParameter(ParamId::voices, 6.0f);
    engine.noteOn(60, 100);
    engine.noteOn(64, 100);
    engine.noteOn(67, 100);
    require(engine.activeVoiceCount() == 3, "moka chord should allocate three voices");

    const std::uint8_t allNotesOff[] = {0xB0, 123, 0};
    engine.handleMidi(allNotesOff, 3);
    for (int i = 0; i < 144000 && engine.activeVoiceCount() > 0; ++i)
        (void)engine.processStereo();
    require(engine.activeVoiceCount() == 0, "moka all-notes-off should release the chord");
}

void releaseShapesRingOut()
{
    MokaEngine choked {48000.0f};
    choked.setParameter(ParamId::instrument, 3.0f);
    choked.setParameter(ParamId::release, 0.0f);
    choked.noteOn(64, 100);
    choked.noteOff(64);
    for (int i = 0; i < 48000 && choked.activeVoiceCount() > 0; ++i)
        (void)choked.processStereo();
    require(choked.activeVoiceCount() == 0, "moka release=0 should choke quickly");

    MokaEngine ringing {48000.0f};
    ringing.setParameter(ParamId::instrument, 3.0f);
    ringing.setParameter(ParamId::release, 1.0f);
    ringing.noteOn(64, 100);
    ringing.noteOff(64);
    for (int i = 0; i < 24000; ++i)
        (void)ringing.processStereo();
    require(ringing.activeVoiceCount() == 1, "moka release=1 should still ring after 0.5 s");
}

void widthControlsStereoSpread()
{
    MokaEngine mono {48000.0f};
    mono.setParameter(ParamId::width, 0.0f);
    mono.noteOn(60, 108);
    float diff = 0.0f;
    float energy = 0.0f;
    for (int i = 0; i < 4096; ++i)
    {
        const auto frame = mono.processStereo();
        diff += std::fabs(frame.left - frame.right);
        energy += std::fabs(frame.left) + std::fabs(frame.right);
    }
    require(energy > 0.01f, "moka mono voice should render");
    require(diff < energy * 0.05f, "moka width=0 should collapse toward mono");

    MokaEngine wide {48000.0f};
    wide.setParameter(ParamId::width, 1.0f);
    wide.noteOn(60, 108);
    float wideDiff = 0.0f;
    float wideEnergy = 0.0f;
    for (int i = 0; i < 4096; ++i)
    {
        const auto frame = wide.processStereo();
        wideDiff += std::fabs(frame.left - frame.right);
        wideEnergy += std::fabs(frame.left) + std::fabs(frame.right);
    }
    require(wideDiff > wideEnergy * 0.005f, "moka width=1 should spread partials across channels");
}

void outputIsBounded()
{
    MokaEngine engine {48000.0f};
    engine.setParameter(ParamId::voices, 12.0f);
    engine.setParameter(ParamId::level, 1.0f);
    engine.setParameter(ParamId::mallet, 1.0f);
    for (int i = 0; i < 12; ++i)
        engine.noteOn(48 + i, 127);
    require(renderPeak(engine, 24000) <= 1.0f, "moka output should remain bounded");
}

void malletAddsBrightEdge()
{
    MokaEngine soft {48000.0f};
    soft.setParameter(ParamId::instrument, 1.0f);
    soft.setParameter(ParamId::mallet, 0.0f);
    soft.noteOn(60, 108);
    const float softHigh = renderHighpassEnergy(soft, 6000);

    MokaEngine hard {48000.0f};
    hard.setParameter(ParamId::instrument, 1.0f);
    hard.setParameter(ParamId::mallet, 1.0f);
    hard.noteOn(60, 108);
    const float hardHigh = renderHighpassEnergy(hard, 6000);

    require(hardHigh > softHigh * 1.20f, "moka mallet should add a brighter edge");
}

void spreadAndPositionMorphSound()
{
    MokaEngine narrow {48000.0f};
    narrow.setParameter(ParamId::spread, 0.0f);
    narrow.setParameter(ParamId::position, 0.0f);
    narrow.noteOn(60, 108);
    float narrowSum = 0.0f;
    for (int i = 0; i < 8000; ++i)
    {
        const auto frame = narrow.processStereo();
        narrowSum += frame.left;
    }

    MokaEngine wide {48000.0f};
    wide.setParameter(ParamId::spread, 1.0f);
    wide.setParameter(ParamId::position, 1.0f);
    wide.noteOn(60, 108);
    float wideSum = 0.0f;
    for (int i = 0; i < 8000; ++i)
    {
        const auto frame = wide.processStereo();
        wideSum += frame.left;
    }

    require(std::fabs(narrowSum - wideSum) > 0.05f, "moka spread/position should morph the sound");
}

void presetsApply()
{
    MokaEngine engine {48000.0f};
    engine.applyPreset(3);
    require(std::fabs(engine.getParameter(ParamId::instrument) - kPresets[3].values[0]) < 1.0e-6f,
            "moka preset should set the instrument");
    for (std::size_t i = 0; i < kPresets[3].values.size(); ++i)
        require(std::fabs(engine.getParameter(i) - kPresets[3].values[i]) < 1.0e-6f,
                "moka preset should set every parameter");

    engine.noteOn(62, 100);
    require(renderEnergy(engine, 8192) > 0.01f, "moka preset voice should render");
}

void renderingIsDeterministic()
{
    MokaEngine first {48000.0f};
    MokaEngine second {48000.0f};
    first.noteOn(60, 100);
    second.noteOn(60, 100);
    for (int i = 0; i < 4096; ++i)
    {
        const auto a = first.processStereo();
        const auto b = second.processStereo();
        require(std::fabs(a.left - b.left) < 1.0e-6f && std::fabs(a.right - b.right) < 1.0e-6f,
                "moka rendering should be deterministic");
    }
}

} // namespace

int main()
{
    defaultsAndClamping();
    allModelsRender();
    defaultSingleNotesHaveUsefulLevel();
    defaultPolyphonyIsFour();
    polyphonyIsSelectable();
    noteOffReleases();
    allNotesOffReleasesChord();
    releaseShapesRingOut();
    widthControlsStereoSpread();
    outputIsBounded();
    malletAddsBrightEdge();
    spreadAndPositionMorphSound();
    presetsApply();
    renderingIsDeterministic();

    std::cout << "moka core tests passed\n";
    return 0;
}
