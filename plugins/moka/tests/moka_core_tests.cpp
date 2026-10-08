#include "moka_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using downspout::moka::kModeCount;
using downspout::moka::kModelSpecs;
using downspout::moka::kPresets;
using downspout::moka::MokaEngine;
using downspout::moka::ModeWeights;
using downspout::moka::ModelSpec;
using downspout::moka::ParamId;
using downspout::moka::kParameterSpecs;
using downspout::moka::computeModeWeights;

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

// What a note-on does to the output before the attack ramp completes.
struct Onset {
    float firstSample = 0.0f;
    float maxSlew = 0.0f;
    float peak = 0.0f;
};

// Renders `frames` of whatever the engine is already sounding and returns the
// last frame's level, so a test can retrigger at a known point.
float renderTail(MokaEngine& engine, const int frames)
{
    float mono = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "moka rendered non-finite audio");
        mono = std::max(std::fabs(frame.left), std::fabs(frame.right));
    }
    return mono;
}

// Measures the onset of a note that was fired immediately before the call,
// plus the peak of the whole note.
Onset measureOnset(MokaEngine& engine, const int frames, const float previous = 0.0f)
{
    Onset onset;
    float last = previous;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        require(std::isfinite(frame.left) && std::isfinite(frame.right), "moka rendered non-finite audio");
        const float mono = std::max(std::fabs(frame.left), std::fabs(frame.right));
        if (i == 0)
            onset.firstSample = mono;
        // Only the onset window matters: later frames contain the note's own
        // high-frequency content and its natural waveform slope.
        if (i < 400)
            onset.maxSlew = std::max(onset.maxSlew, std::fabs(mono - last));
        onset.peak = std::max(onset.peak, mono);
        last = mono;
    }
    return onset;
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

// A struck bar rings from a finite displacement, not from an instantaneous
// step. Ramping the modal bank in over the mallet contact time is what keeps a
// note from opening with a broadband impulse.
void noteOnsetsDoNotClick()
{
    for (int model = 0; model <= 5; ++model)
    {
        for (const float mallet : {0.0f, 0.5f, 1.0f})
        {
            MokaEngine engine {48000.0f};
            engine.setParameter(ParamId::instrument, static_cast<float>(model));
            engine.setParameter(ParamId::mallet, mallet);
            engine.noteOn(60, 112);

            const Onset onset = measureOnset(engine, 48000);
            require(onset.firstSample < 1.0e-4f,
                    "moka note-on should start from silence, not a step");
            require(onset.maxSlew < 0.10f,
                    "moka note-on should not slew into the note: raise the onset impulse");
            require(onset.peak >= 0.25f,
                    "moka onset ramp should not swallow the note's level");
        }
    }
}

// Retriggering a held note restarts the modal bank while the tail is still
// ringing. That transition has to crossfade too.
void retriggerDoesNotClick()
{
    for (int model = 0; model <= 5; ++model)
    {
        MokaEngine engine {48000.0f};
        engine.setParameter(ParamId::instrument, static_cast<float>(model));
        engine.noteOn(60, 112);
        // 50 ms of ring-out, then strike the same note again while it rings.
        const float tail = renderTail(engine, 2400);
        engine.noteOn(60, 112);

        const Onset onset = measureOnset(engine, 8000, tail);
        require(onset.maxSlew < 0.10f, "moka retrigger should crossfade rather than cut the tail");
        require(onset.peak >= 0.25f, "moka retrigger should still render a full note");
    }
}

// Several notes starting together used to sum into a full-scale first sample.
void chordOnsetDoesNotClick()
{
    MokaEngine engine {48000.0f};
    engine.setParameter(ParamId::voices, 8.0f);
    engine.setParameter(ParamId::instrument, 4.0f);
    for (int i = 0; i < 8; ++i)
        engine.noteOn(55 + i * 3, 120);

    const Onset onset = measureOnset(engine, 24000);
    require(onset.firstSample < 1.0e-4f, "moka chord should start from silence, not a full-scale step");
    require(onset.peak <= 1.0f, "moka chord should stay within full scale");
}

// The Kalimba is a thumb-plucked steel tine: a hollow near-harmonic partial an
// octave-ish up and a much shorter ring than the glockenspiel it shares a
// modal table with.
void kalimbaPresetHasATineCharacter()
{
    MokaEngine kalimba {48000.0f};
    kalimba.applyPreset(kPresets.size() - 1);
    require(std::strcmp(kPresets[kPresets.size() - 1].name, "Kalimba") == 0,
            "moka should ship a Kalimba preset");

    // Same metal bar table as the glockenspiel, so it must not simply be it.
    MokaEngine glock {48000.0f};
    glock.applyPreset(1);
    require(kalimba.getParameter(ParamId::instrument) == glock.getParameter(ParamId::instrument),
            "moka kalimba should reuse the steel bar table");
    require(kalimba.getParameter(ParamId::decay) < glock.getParameter(ParamId::decay) - 0.1f,
            "moka kalimba should ring out faster than the glockenspiel");

    // Ring-out: energy left at 1 s relative to the peak of the first 100 ms.
    const auto ringRatio = [](const std::size_t preset)
    {
        MokaEngine engine {48000.0f};
        engine.applyPreset(preset);
        engine.noteOn(64, 100);
        float early = 0.0f;
        float late = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            const auto frame = engine.processStereo();
            const float mono = std::max(std::fabs(frame.left), std::fabs(frame.right));
            if (i < 4800)
                early = std::max(early, mono);
            if (i >= 43200)
                late = std::max(late, mono);
        }
        return early > 0.0f ? late / early : 0.0f;
    };
    require(ringRatio(kPresets.size() - 1) < 0.10f, "moka kalimba should be nearly silent after 1 s");
    require(ringRatio(kPresets.size() - 1) < ringRatio(1),
            "moka kalimba should decay faster than the glockenspiel");

    kalimba.noteOn(64, 100);
    const Onset onset = measureOnset(kalimba, 24000);
    require(onset.firstSample < 1.0e-4f, "moka kalimba should start from silence");
    require(onset.peak >= 0.25f, "moka kalimba should match the level of the other presets");
}

// The panel's partial ladder and the engine both read this, so pin the
// behaviour that keeps them in step.
void modeWeightsAreShared()
{
    // Table ratios are the identity at the stretch that maps them onto 1.0x
    // when the partial is the fundamental itself.
    const ModeWeights fundamental = computeModeWeights(0, 0.0f, 0.0f, 0.0f);
    require(std::fabs(fundamental.ratio[0] - 1.0f) < 1.0e-6f,
            "moka fundamental should stay at 1.0x");
    require(fundamental.weight[0] > 0.0f, "moka fundamental should always be present");

    // A hard beater is brighter: it keeps more of the upper partials than a
    // soft one at the same spread and position.
    const ModeWeights hard = computeModeWeights(1, 0.3f, 1.0f, 0.4f);
    const ModeWeights soft = computeModeWeights(1, 0.3f, 0.0f, 0.4f);
    require(hard.ratio[1] == soft.ratio[1], "moka mallet should not move the partial ratios");
    require(hard.weight[1] > soft.weight[1], "moka mallet should keep more of the upper partials");

    // Spread stretches partials away from the fundamental.
    const ModeWeights narrow = computeModeWeights(1, 0.0f, 0.75f, 0.45f);
    const ModeWeights wide = computeModeWeights(1, 1.0f, 0.75f, 0.45f);
    require(wide.ratio[1] > narrow.ratio[1], "moka spread should stretch upper partials up");

    // Position trades a fundamental-heavy centre strike for a bright edge one.
    const ModeWeights centre = computeModeWeights(0, 0.3f, 0.75f, 1.0f);
    const ModeWeights edge = computeModeWeights(0, 0.3f, 0.75f, 0.0f);
    require(centre.weight[0] > edge.weight[0], "moka centre strike should favour the fundamental");
    require(edge.weight[1] > centre.weight[1], "moka edge strike should favour the upper partials");

    // Unused slots stay empty rather than collapsing onto the fundamental.
    for (int model = 0; model <= 5; ++model)
    {
        const ModeWeights weights = computeModeWeights(model, 0.5f, 0.5f, 0.5f);
        const ModelSpec& spec = kModelSpecs[static_cast<std::size_t>(model)];
        for (std::size_t i = 0; i < kModeCount; ++i)
        {
            const bool used = spec.modes[i].ratio > 0.0f && spec.modes[i].level > 0.0f;
            require((weights.ratio[i] > 0.0f) == used,
                    "moka unused mode slots should not gain a ratio");
        }
    }
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

// The mallet burst noise has to be seeded from the note, otherwise every
// strike on the same voice reuses the identical burst waveform.
void malletBurstVariesWithTheNote()
{
    MokaEngine engine {48000.0f};
    engine.setParameter(ParamId::voices, 8.0f);
    engine.noteOn(48, 100);
    for (int i = 0; i < 48000; ++i)
        (void)engine.processStereo();

    MokaEngine same {48000.0f};
    MokaEngine different {48000.0f};
    same.setParameter(ParamId::voices, 8.0f);
    different.setParameter(ParamId::voices, 8.0f);
    same.noteOn(48, 100);
    different.noteOn(52, 100);

    std::vector<float> a;
    std::vector<float> b;
    for (int i = 0; i < 256; ++i)
    {
        a.push_back(same.processStereo().left);
        b.push_back(different.processStereo().left);
    }
    require(std::fabs(a[8] - b[8]) > 1.0e-6f,
            "moka mallet burst should differ between notes, not replay one waveform");
}

} // namespace

float estimateHz(MokaEngine& engine)
{
    constexpr int warm = 2400, length = 16000;
    std::vector<float> mono(length);
    for (int i = 0; i < warm; ++i) (void)engine.processStereo();
    for (int i = 0; i < length; ++i) {
        const auto f = engine.processStereo();
        mono[static_cast<std::size_t>(i)] = 0.5f * (f.left + f.right);
    }
    int bestLag = 48;
    double best = -1.0e30;
    for (int lag = 48; lag <= 480; ++lag) {
        double sum = 0.0;
        for (int i = 0; i + lag < length; ++i)
            sum += static_cast<double>(mono[static_cast<std::size_t>(i)]) * mono[static_cast<std::size_t>(i + lag)];
        if (sum > best) { best = sum; bestLag = lag; }
    }
    return 48000.0f / static_cast<float>(bestLag);
}

void midi3(MokaEngine& engine, int status, int a, int b)
{
    const std::uint8_t msg[3] = {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)};
    engine.handleMidi(msg, 3);
}

void pitchBendIsPerChannel()
{
    MokaEngine plain {48000.0f};
    midi3(plain, 0x91, 60, 100);
    const float base = estimateHz(plain);

    MokaEngine bent {48000.0f};
    midi3(bent, 0xE2, 0x7F, 0x7F);  // full up on channel 3 before its note
    midi3(bent, 0x92, 60, 100);
    require(std::fabs(estimateHz(bent) / base - std::pow(2.0f, 2.0f / 12.0f)) < 0.05f, "moka should bend its own channel by 2 semitones");

    MokaEngine other {48000.0f};
    midi3(other, 0x91, 60, 100);
    midi3(other, 0xE5, 0x7F, 0x7F);
    require(std::fabs(estimateHz(other) / base - 1.0f) < 0.03f, "moka bend must stay on its channel");

    MokaEngine ringing {48000.0f};
    midi3(ringing, 0x93, 60, 100);
    midi3(ringing, 0xE3, 0x7F, 0x7F);  // bend a note that is already ringing
    require(std::fabs(estimateHz(ringing) / base - std::pow(2.0f, 2.0f / 12.0f)) < 0.05f, "moka should bend a ringing note");

    MokaEngine pair {48000.0f};
    pair.setParameter(ParamId::voices, 4.0f);
    midi3(pair, 0x91, 60, 100);
    midi3(pair, 0x92, 60, 100);
    require(pair.activeVoiceCount() == 2, "moka should key voices by channel and note");
    midi3(pair, 0x81, 60, 0);  // releases channel 2's note only
    require(pair.activeVoiceCount() == 2, "moka voices keep ringing through release");
}

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
    noteOnsetsDoNotClick();
    retriggerDoesNotClick();
    chordOnsetDoesNotClick();
    kalimbaPresetHasATineCharacter();
    modeWeightsAreShared();
    renderingIsDeterministic();
    pitchBendIsPerChannel();
    malletBurstVariesWithTheNote();

    std::cout << "moka core tests passed\n";
    return 0;
}
