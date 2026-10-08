#include "canticle_engine.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
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

void defaultSingleNotesHaveUsefulLevel()
{
    for (int model = 0; model <= 4; ++model)
    {
        CanticleEngine engine {48000.0f};
        engine.setParameter(ParamId::model, static_cast<float>(model));
        engine.noteOn(60, 100);
        require(renderPeak(engine, 96000) >= 0.32f,
                "canticle default single-note output should reach -10 dBFS");
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

void activeVoiceParameterChangesRemainBounded()
{
    CanticleEngine engine {48000.0f};
    for (int voice = 0; voice < 8; ++voice)
        engine.noteOn(48 + voice * 3, 110);
    require(renderEnergy(engine, 256) > 0.001f,
            "canticle should sound before active-voice parameter changes");

    for (int model = 0; model <= 4; ++model)
    {
        engine.setParameter(ParamId::model, static_cast<float>(model));
        engine.setParameter(ParamId::articulation, static_cast<float>(model % 4));
        engine.setParameter(ParamId::range, static_cast<float>((model + 1) % 4));
        engine.setParameter(ParamId::ensemble, static_cast<float>((model + 2) % 4));
        engine.setParameter(ParamId::detune, 0.17f * static_cast<float>(model));
        require(renderPeak(engine, 1024) <= 1.0f,
                "canticle active-voice parameter change should remain bounded");
    }
}

} // namespace

float estimateHz(CanticleEngine& engine)
{
    constexpr int warm = 12000, length = 24000;
    std::vector<float> mono(length);
    for (int i = 0; i < warm; ++i) (void)engine.processStereo();
    for (int i = 0; i < length; ++i) {
        const auto f = engine.processStereo();
        mono[static_cast<std::size_t>(i)] = 0.5f * (f.left + f.right);
    }
    // Autocorrelation peak between 100 Hz and 1 kHz.
    int bestLag = 48;
    double best = -1.0e30;
    for (int lag = 48; lag <= 480; ++lag) {
        double sum = 0.0;
        for (int i = 0; i + lag < length; ++i) sum += static_cast<double>(mono[static_cast<std::size_t>(i)]) * mono[static_cast<std::size_t>(i + lag)];
        if (sum > best) { best = sum; bestLag = lag; }
    }
    return 48000.0f / static_cast<float>(bestLag);
}

void sendBend(CanticleEngine& engine, int channel, int value14)
{
    const std::uint8_t msg[3] = {static_cast<std::uint8_t>(0xE0 | channel), static_cast<std::uint8_t>(value14 & 0x7F), static_cast<std::uint8_t>(value14 >> 7)};
    engine.handleMidi(msg, 3);
}

void sendNote(CanticleEngine& engine, bool on, int channel, int note)
{
    const std::uint8_t msg[3] = {static_cast<std::uint8_t>((on ? 0x90 : 0x80) | channel), static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(on ? 100 : 0)};
    engine.handleMidi(msg, 3);
}

void pitchBendIsPerChannel()
{
    CanticleEngine plain {48000.0f};
    sendNote(plain, true, 1, 60);
    const float base = estimateHz(plain);

    // Bend set on the note's own channel before the note, as Retune sends it.
    CanticleEngine bent {48000.0f};
    sendBend(bent, 2, 16383);  // full up = +2 semitones by default
    sendNote(bent, true, 2, 60);
    const float up = estimateHz(bent);
    require(std::fabs(up / base - std::pow(2.0f, 2.0f / 12.0f)) < 0.04f, "canticle should bend its own channel by 2 semitones");

    // A bend on another channel must not touch the note.
    CanticleEngine other {48000.0f};
    sendNote(other, true, 1, 60);
    sendBend(other, 5, 16383);
    require(std::fabs(estimateHz(other) / base - 1.0f) < 0.02f, "canticle bend must stay on its channel");

    // RPN 0 widens the range: +12 semitones doubles the frequency.
    CanticleEngine wide {48000.0f};
    const std::uint8_t rpn[4][3] = {{0xB3, 101, 0}, {0xB3, 100, 0}, {0xB3, 6, 12}, {0xB3, 101, 127}};
    for (const auto& m : rpn) wide.handleMidi(m, 3);
    sendBend(wide, 3, 16383);
    sendNote(wide, true, 3, 60);
    require(std::fabs(estimateHz(wide) / base - 2.0f) < 0.08f, "canticle RPN 0 should set the bend range");

    // The same note on two channels is two voices, and releasing one keeps the other.
    CanticleEngine pair {48000.0f};
    sendNote(pair, true, 1, 60);
    sendNote(pair, true, 2, 60);
    require(pair.activeVoiceCount() == 2, "canticle should key voices by channel and note");
    pair.setParameter(ParamId::release, 0.0f);
    sendNote(pair, false, 1, 60);
    for (int i = 0; i < 48000; ++i) (void)pair.processStereo();
    require(pair.activeVoiceCount() == 1, "canticle note-off must only release its own channel");
}

int main()
{
    defaultsAndClamping();
    allModelsRender();
    defaultSingleNotesHaveUsefulLevel();
    polyphonyAndVoiceStealing();
    noteOffReleases();
    allNotesOffReleasesChord();
    outputIsBounded();
    metalAddsBrightEdge();
    activeVoiceParameterChangesRemainBounded();
    pitchBendIsPerChannel();

    std::cout << "canticle core tests passed\n";
    return 0;
}
