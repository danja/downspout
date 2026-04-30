#include "ground_engine.hpp"
#include "ground_pattern.hpp"
#include "ground_serialization.hpp"
#include "ground_variation.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

using namespace downspout::ground;

namespace {

struct PhraseSnapshot {
    PhrasePlan plan {};
    std::array<NoteEvent, 512> events {};
    int eventCount = 0;
};

PhraseSnapshot capturePhrase(const FormState& form, const int phraseIndex)
{
    PhraseSnapshot snapshot;
    snapshot.plan = form.phrases[static_cast<std::size_t>(phraseIndex)];
    snapshot.eventCount = snapshot.plan.eventCount;
    for (int i = 0; i < snapshot.eventCount; ++i) {
        snapshot.events[static_cast<std::size_t>(i)] =
            form.events[static_cast<std::size_t>(snapshot.plan.eventStartIndex + i)];
    }
    return snapshot;
}

void assertPhraseEqual(const PhraseSnapshot& a, const PhraseSnapshot& b)
{
    assert(a.plan.role == b.plan.role);
    assert(a.plan.startBar == b.plan.startBar);
    assert(a.plan.bars == b.plan.bars);
    assert(a.plan.startStep == b.plan.startStep);
    assert(a.plan.stepCount == b.plan.stepCount);
    assert(a.plan.rootDegree == b.plan.rootDegree);
    assert(a.plan.registerOffset == b.plan.registerOffset);
    assert(std::fabs(a.plan.intensity - b.plan.intensity) < 1e-6f);
    assert(std::fabs(a.plan.motionBias - b.plan.motionBias) < 1e-6f);
    assert(a.eventCount == b.eventCount);
    for (int i = 0; i < a.eventCount; ++i) {
        assert(a.events[static_cast<std::size_t>(i)].startStep == b.events[static_cast<std::size_t>(i)].startStep);
        assert(a.events[static_cast<std::size_t>(i)].durationSteps == b.events[static_cast<std::size_t>(i)].durationSteps);
        assert(a.events[static_cast<std::size_t>(i)].note == b.events[static_cast<std::size_t>(i)].note);
        assert(a.events[static_cast<std::size_t>(i)].velocity == b.events[static_cast<std::size_t>(i)].velocity);
    }
}

void testDeterministicGeneration()
{
    Controls controls;
    controls.seed = 2222u;
    controls.style = StyleId::climb;
    controls.scale = ScaleId::dorian;
    controls.formBars = 16;
    controls.phraseBars = 4;
    controls.sequence = 0.65f;

    FormState first;
    FormState second;
    regenerateForm(first, controls, ::downspout::Meter {});
    regenerateForm(second, controls, ::downspout::Meter {});

    assert(first.formBars == second.formBars);
    assert(first.phraseBars == second.phraseBars);
    assert(first.phraseCount == second.phraseCount);
    assert(first.patternSteps == second.patternSteps);
    assert(first.eventCount == second.eventCount);
    assert(first.generationSerial == second.generationSerial);

    for (int i = 0; i < first.phraseCount; ++i) {
        assert(first.phrases[static_cast<std::size_t>(i)].role ==
               second.phrases[static_cast<std::size_t>(i)].role);
    }

    for (int i = 0; i < first.eventCount; ++i) {
        assert(first.events[static_cast<std::size_t>(i)].startStep ==
               second.events[static_cast<std::size_t>(i)].startStep);
        assert(first.events[static_cast<std::size_t>(i)].durationSteps ==
               second.events[static_cast<std::size_t>(i)].durationSteps);
        assert(first.events[static_cast<std::size_t>(i)].note ==
               second.events[static_cast<std::size_t>(i)].note);
        assert(first.events[static_cast<std::size_t>(i)].velocity ==
               second.events[static_cast<std::size_t>(i)].velocity);
    }
}

void testRefreshPhraseKeepsNeighbours()
{
    Controls controls;
    controls.seed = 1234u;
    controls.formBars = 16;
    controls.phraseBars = 4;
    controls.sequence = 0.30f;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});

    const PhraseSnapshot before0 = capturePhrase(form, 0);
    const PhraseSnapshot before1 = capturePhrase(form, 1);
    const PhraseSnapshot before2 = capturePhrase(form, 2);

    refreshPhrase(form, controls, 1);

    const PhraseSnapshot after0 = capturePhrase(form, 0);
    const PhraseSnapshot after1 = capturePhrase(form, 1);
    const PhraseSnapshot after2 = capturePhrase(form, 2);

    assertPhraseEqual(before0, after0);
    assertPhraseEqual(before2, after2);

    bool changed = before1.plan.role != after1.plan.role ||
                   before1.eventCount != after1.eventCount;
    for (int i = 0; !changed && i < std::min(before1.eventCount, after1.eventCount); ++i) {
        changed = before1.events[static_cast<std::size_t>(i)].startStep != after1.events[static_cast<std::size_t>(i)].startStep ||
                  before1.events[static_cast<std::size_t>(i)].durationSteps != after1.events[static_cast<std::size_t>(i)].durationSteps ||
                  before1.events[static_cast<std::size_t>(i)].note != after1.events[static_cast<std::size_t>(i)].note ||
                  before1.events[static_cast<std::size_t>(i)].velocity != after1.events[static_cast<std::size_t>(i)].velocity;
    }
    assert(changed);
}

void testVariationChangesOnLoop()
{
    Controls controls;
    controls.seed = 88u;
    controls.vary = 1.0f;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});
    const int originalSerial = form.generationSerial;

    VariationState variation;
    const bool changed = applyLoopVariation(form, variation, controls, 4.0, 0);

    assert(changed);
    assert(variation.completedFormLoops == 1);
    assert(variation.lastMutationLoop == 1);
    assert(form.generationSerial > originalSerial);
}

void testCompoundMeterShape()
{
    Controls controls;
    controls.seed = 181u;
    controls.formBars = 8;
    controls.phraseBars = 2;

    FormState form;
    regenerateForm(form, controls, ::downspout::makeMeter(6, 8));

    assert(form.meter.numerator == 6);
    assert(form.meter.denominator == 8);
    assert(form.stepsPerBeat == 4);
    assert(form.stepsPerBar == 24);
    assert(form.patternSteps == 192);
    assert(form.phrases[0].startStep == 0);
    assert(form.phrases[0].stepCount == 48);
    assert(form.phrases[1].startStep == 48);
}

void testGroundedPhraseGetsSyncopationAndLegato()
{
    Controls controls;
    controls.seed = 515u;
    controls.style = StyleId::grounded;
    controls.formBars = 16;
    controls.phraseBars = 4;
    controls.density = 0.50f;
    controls.motion = 0.75f;
    controls.sequence = 0.35f;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});

    const PhrasePlan& phrase = form.phrases[0];
    assert(phrase.role == PhraseRoleId::statement);

    bool hasSyncopation = false;
    bool hasLegatoTie = false;
    for (int i = 0; i < phrase.eventCount; ++i) {
        const NoteEvent& event = form.events[static_cast<std::size_t>(phrase.eventStartIndex + i)];
        const int localStep = event.startStep - phrase.startStep;
        if ((localStep % 4) != 0) {
            hasSyncopation = true;
        }

        const int nextStart = i + 1 < phrase.eventCount
            ? form.events[static_cast<std::size_t>(phrase.eventStartIndex + i + 1)].startStep
            : phrase.startStep + phrase.stepCount;
        if ((event.startStep + event.durationSteps) >= nextStart) {
            hasLegatoTie = true;
        }
    }

    assert(hasSyncopation);
    assert(hasLegatoTie);
}

void testEngineRestartPhraseStatusAndStop()
{
    Controls controls;
    controls.formBars = 8;
    controls.phraseBars = 2;

    EngineState engine;
    activate(engine, controls);

    engine.formValid = true;
    engine.form.formBars = 8;
    engine.form.phraseBars = 2;
    engine.form.phraseCount = 4;
    engine.form.patternSteps = 128;
    engine.form.stepsPerBeat = 4;
    engine.form.stepsPerBar = 16;
    engine.form.meter = ::downspout::Meter {};
    engine.form.eventCount = 1;
    engine.form.phrases[0].role = PhraseRoleId::statement;
    engine.form.phrases[1].role = PhraseRoleId::answer;
    engine.form.phrases[2].role = PhraseRoleId::climb;
    engine.form.phrases[3].role = PhraseRoleId::cadence;
    engine.form.events[0] = NoteEvent{0, 8, 43, 100};

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.bar = 0.0;
    transport.barBeat = 1.0;

    BlockResult result = processBlock(engine, controls, transport, 64, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 43);
    assert(result.currentPhraseIndex == 0);
    assert(result.currentRole == PhraseRoleId::statement);

    engine.wasPlaying = true;
    engine.lastTransportStep = 40;
    engine.activeNote = 70;
    transport.bar = 2.0;
    transport.barBeat = 0.0;
    result = processBlock(engine, controls, transport, 64, 48000.0);
    assert(result.eventCount >= 1);
    assert(result.currentPhraseIndex == 1);
    assert(result.currentRole == PhraseRoleId::answer);

    transport.playing = false;
    result = processBlock(engine, controls, transport, 64, 48000.0);
    assert(result.eventCount <= 1);
    if (result.eventCount == 1) {
        assert(result.events[0].type == MidiEventType::noteOff);
    }
    assert(engine.activeNote == -1);
}

void testSerializationRoundTrip()
{
    Controls controls;
    controls.rootNote = 41;
    controls.scale = ScaleId::mixolydian;
    controls.style = StyleId::march;
    controls.formBars = 32;
    controls.phraseBars = 8;
    controls.sequence = 0.75f;
    controls.vary = 0.42f;
    controls.seed = 998u;
    controls.actionMutateCell = 7;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});

    VariationState variation;
    variation.completedFormLoops = 5;
    variation.lastMutationLoop = 3;

    const auto controlsRoundTrip = deserializeControls(serializeControls(controls));
    const auto formRoundTrip = deserializeFormState(serializeFormState(form));
    const auto variationRoundTrip = deserializeVariationState(serializeVariationState(variation));

    assert(controlsRoundTrip.has_value());
    assert(formRoundTrip.has_value());
    assert(variationRoundTrip.has_value());
    assert(controlsRoundTrip->rootNote == controls.rootNote);
    assert(controlsRoundTrip->scale == controls.scale);
    assert(controlsRoundTrip->style == controls.style);
    assert(controlsRoundTrip->actionMutateCell == controls.actionMutateCell);
    assert(formRoundTrip->phraseCount == form.phraseCount);
    assert(formRoundTrip->eventCount == form.eventCount);
    assert(formRoundTrip->meter.numerator == form.meter.numerator);
    assert(formRoundTrip->meter.denominator == form.meter.denominator);
    assert(formRoundTrip->stepsPerBar == form.stepsPerBar);
    assert(formRoundTrip->generationSerial == form.generationSerial);
    for (int i = 0; i < form.eventCount; ++i) {
        assert(formRoundTrip->events[static_cast<std::size_t>(i)].startStep ==
               form.events[static_cast<std::size_t>(i)].startStep);
        assert(formRoundTrip->events[static_cast<std::size_t>(i)].durationSteps ==
               form.events[static_cast<std::size_t>(i)].durationSteps);
        assert(formRoundTrip->events[static_cast<std::size_t>(i)].note ==
               form.events[static_cast<std::size_t>(i)].note);
        assert(formRoundTrip->events[static_cast<std::size_t>(i)].velocity ==
               form.events[static_cast<std::size_t>(i)].velocity);
    }
    assert(variationRoundTrip->completedFormLoops == 5);
    assert(variationRoundTrip->lastMutationLoop == 3);
}

}  // namespace

int main()
{
    testDeterministicGeneration();
    testRefreshPhraseKeepsNeighbours();
    testVariationChangesOnLoop();
    testCompoundMeterShape();
    testGroundedPhraseGetsSyncopationAndLegato();
    testEngineRestartPhraseStatusAndStop();
    testSerializationRoundTrip();
    return 0;
}
