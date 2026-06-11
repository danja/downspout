#include "ground_ai_state.hpp"
#include "ground_engine.hpp"
#include "ground_pattern.hpp"
#include "ground_serialization.hpp"
#include "ground_variation.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <string>

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

int firstPhraseNote(const FormState& form, const int phraseIndex)
{
    const PhrasePlan& phrase = form.phrases[static_cast<std::size_t>(phraseIndex)];
    int bestStep = form.patternSteps + 1;
    int note = -1;
    for (int i = 0; i < phrase.eventCount; ++i) {
        const NoteEvent& event = form.events[static_cast<std::size_t>(phrase.eventStartIndex + i)];
        if (event.startStep < bestStep) {
            bestStep = event.startStep;
            note = event.note;
        }
    }
    return note;
}

int totalDurationSteps(const FormState& form)
{
    int total = 0;
    for (int i = 0; i < form.eventCount; ++i) {
        total += form.events[static_cast<std::size_t>(i)].durationSteps;
    }
    return total;
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

void testSetPhraseRoleKeepsNeighbours()
{
    Controls controls;
    controls.seed = 451u;
    controls.formBars = 16;
    controls.phraseBars = 4;
    controls.sequence = 0.65f;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});

    const PhraseSnapshot before0 = capturePhrase(form, 0);
    const PhraseSnapshot before1 = capturePhrase(form, 1);
    const PhraseSnapshot before2 = capturePhrase(form, 2);

    setPhraseRole(form, controls, 1, PhraseRoleId::cadence);

    const PhraseSnapshot after0 = capturePhrase(form, 0);
    const PhraseSnapshot after1 = capturePhrase(form, 1);
    const PhraseSnapshot after2 = capturePhrase(form, 2);

    assertPhraseEqual(before0, after0);
    assertPhraseEqual(before2, after2);
    assert(after1.plan.role == PhraseRoleId::cadence);

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

void testNoteLengthControlsDurations()
{
    Controls shortControls;
    shortControls.seed = 909u;
    shortControls.style = StyleId::grounded;
    shortControls.formBars = 16;
    shortControls.phraseBars = 4;
    shortControls.density = 0.50f;
    shortControls.motion = 0.70f;
    shortControls.noteLength = 0.15f;
    shortControls.noteLengthVariation = 0.0f;

    Controls longControls = shortControls;
    longControls.noteLength = 0.95f;

    FormState shortForm;
    FormState longForm;
    regenerateForm(shortForm, shortControls, ::downspout::Meter {});
    regenerateForm(longForm, longControls, ::downspout::Meter {});

    assert(shortForm.eventCount > 0);
    assert(longForm.eventCount > 0);
    assert(totalDurationSteps(shortForm) < totalDurationSteps(longForm));
    assert(!structureControlsMatch(shortControls, longControls));
}

void testFugalFormPlansSubjectAnswerPedalCadence()
{
    Controls controls;
    controls.rootNote = 36;
    controls.scale = ScaleId::major;
    controls.formBars = 16;
    controls.phraseBars = 2;
    controls.sequence = 0.95f;
    controls.cadence = 0.85f;
    controls.density = 0.45f;
    controls.color = 0.0f;
    controls.seed = 7331u;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});

    assert(form.phraseCount == 8);
    assert(form.phrases[0].role == PhraseRoleId::statement);
    assert(form.phrases[0].rootDegree == 0);
    assert(form.phrases[1].role == PhraseRoleId::answer);
    assert(form.phrases[1].rootDegree == 4);
    assert(form.phrases[6].role == PhraseRoleId::pedal);
    assert(form.phrases[6].rootDegree == 0);
    assert(form.phrases[7].role == PhraseRoleId::cadence);
    assert(form.phrases[7].rootDegree == 4);
    assert(firstPhraseNote(form, 0) == controls.rootNote);
    assert(firstPhraseNote(form, 1) == controls.rootNote + 7);
}

void testTwelveBarBluesFormShapes()
{
    Controls standard;
    standard.rootNote = 36;
    standard.scale = ScaleId::mixolydian;
    standard.style = StyleId::jazz;
    standard.formShape = FormShapeId::blues12;
    standard.formBars = 32;
    standard.phraseBars = 8;
    standard.seed = 5150u;

    FormState form;
    regenerateForm(form, standard, ::downspout::Meter {});

    assert(form.formBars == 12);
    assert(form.phraseBars == 1);
    assert(form.phraseCount == 12);
    for (int i = 0; i < 12; ++i) {
        assert(form.phrases[static_cast<std::size_t>(i)].startBar == i);
        assert(form.phrases[static_cast<std::size_t>(i)].bars == 1);
    }

    const int rootI = 0;
    const int rootIV = 3;
    const int rootV = 4;
    const int expectedStandard[] = {
        rootI, rootI, rootI, rootI,
        rootIV, rootIV, rootI, rootI,
        rootV, rootIV, rootI, rootV
    };
    for (int i = 0; i < 12; ++i) {
        assert(form.phrases[static_cast<std::size_t>(i)].rootDegree == expectedStandard[i]);
    }
    assert(form.phrases[8].role == PhraseRoleId::climb);
    assert(form.phrases[10].role == PhraseRoleId::cadence);

    Controls quick = standard;
    quick.formShape = FormShapeId::blues12QuickChange;
    FormState quickForm;
    regenerateForm(quickForm, quick, ::downspout::Meter {});
    assert(quickForm.formBars == 12);
    assert(quickForm.phrases[1].rootDegree == rootIV);

    Controls jazz = standard;
    jazz.scale = ScaleId::blues;
    jazz.formShape = FormShapeId::blues12Jazz;
    FormState jazzForm;
    regenerateForm(jazzForm, jazz, ::downspout::Meter {});
    assert(jazzForm.formBars == 12);
    assert(jazzForm.phrases[5].rootDegree == 3);
    assert(jazzForm.phrases[9].role == PhraseRoleId::cadence);
}

void testAdditionalNamedFormShapes()
{
    Controls controls;
    controls.rootNote = 36;
    controls.scale = ScaleId::major;
    controls.formBars = 64;
    controls.phraseBars = 12;
    controls.seed = 9123u;

    controls.formShape = FormShapeId::classical16;
    FormState classical;
    regenerateForm(classical, controls, ::downspout::Meter {});
    assert(classical.formBars == 16);
    assert(classical.phraseBars == 4);
    assert(classical.phraseCount == 4);
    assert(classical.phrases[0].role == PhraseRoleId::statement);
    assert(classical.phrases[1].role == PhraseRoleId::answer);
    assert(classical.phrases[2].rootDegree == 3);
    assert(classical.phrases[3].role == PhraseRoleId::cadence);

    controls.formShape = FormShapeId::fugue16;
    FormState fugue;
    regenerateForm(fugue, controls, ::downspout::Meter {});
    assert(fugue.formBars == 16);
    assert(fugue.phraseBars == 2);
    assert(fugue.phraseCount == 8);
    assert(fugue.phrases[1].role == PhraseRoleId::answer);
    assert(fugue.phrases[1].rootDegree == 4);
    assert(fugue.phrases[5].role == PhraseRoleId::pedal);

    controls.scale = ScaleId::mixolydian;
    controls.formShape = FormShapeId::jazzAaba32;
    FormState aaba;
    regenerateForm(aaba, controls, ::downspout::Meter {});
    assert(aaba.formBars == 32);
    assert(aaba.phraseBars == 4);
    assert(aaba.phraseCount == 8);
    assert(aaba.phrases[4].role == PhraseRoleId::climb);
    assert(aaba.phrases[4].rootDegree == 2);

    controls.formShape = FormShapeId::rhythmChanges32;
    FormState rhythm;
    regenerateForm(rhythm, controls, ::downspout::Meter {});
    assert(rhythm.phrases[1].rootDegree == 5);
    assert(rhythm.phrases[3].role == PhraseRoleId::cadence);

    controls.formShape = FormShapeId::techno16;
    FormState techno;
    regenerateForm(techno, controls, ::downspout::Meter {});
    assert(techno.formBars == 16);
    assert(techno.phrases[0].role == PhraseRoleId::pedal);
    assert(techno.phrases[2].role == PhraseRoleId::climb);

    controls.formShape = FormShapeId::ambient8;
    FormState ambient;
    regenerateForm(ambient, controls, ::downspout::Meter {});
    assert(ambient.formBars == 8);
    assert(ambient.phraseBars == 2);
    assert(ambient.phrases[2].role == PhraseRoleId::release);

    controls.formShape = FormShapeId::rondo32;
    FormState rondo;
    regenerateForm(rondo, controls, ::downspout::Meter {});
    assert(rondo.formBars == 32);
    assert(rondo.phrases[0].rootDegree == rondo.phrases[2].rootDegree);
    assert(rondo.phrases[7].role == PhraseRoleId::cadence);
}

void testBassRegisterStaysInLane()
{
    Controls controls;
    controls.rootNote = 36;
    controls.scale = ScaleId::mixolydian;
    controls.style = StyleId::climb;
    controls.formShape = FormShapeId::blues12QuickChange;
    controls.formBars = 32;
    controls.phraseBars = 4;
    controls.density = 0.80f;
    controls.motion = 1.0f;
    controls.tension = 0.95f;
    controls.color = 1.0f;
    controls.cadence = 0.90f;
    controls.reg = 1;
    controls.registerArc = 1.0f;
    controls.sequence = 0.95f;
    controls.vary = 1.0f;
    controls.seed = 424242u;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});

    auto assertLane = [](const FormState& generated) {
        int low = 128;
        int high = -1;
        for (int i = 0; i < generated.eventCount; ++i) {
            const int note = generated.events[static_cast<std::size_t>(i)].note;
            low = std::min(low, note);
            high = std::max(high, note);
            assert(note >= 29);
            assert(note <= 60);
        }
        assert(generated.eventCount > 0);
        assert((high - low) <= 31);
    };

    assertLane(form);

    refreshPhrase(form, controls, 3);
    assertLane(form);

    mutatePhraseCell(form, controls, 4, 1.0f);
    assertLane(form);

    VariationState variation;
    assert(applyLoopVariation(form, variation, controls, 4.0, 2));
    assertLane(form);
}

void testDubAndJazzStylesAreExplicit()
{
    Controls dub;
    dub.rootNote = 36;
    dub.scale = ScaleId::minor;
    dub.style = StyleId::dub;
    dub.formBars = 8;
    dub.phraseBars = 2;
    dub.density = 0.52f;
    dub.motion = 0.38f;
    dub.registerArc = 0.35f;
    dub.seed = 2024u;

    Controls jazz = dub;
    jazz.scale = ScaleId::mixolydian;
    jazz.style = StyleId::jazz;
    jazz.motion = 0.72f;
    jazz.color = 0.65f;

    FormState dubForm;
    FormState jazzForm;
    regenerateForm(dubForm, dub, ::downspout::Meter {});
    regenerateForm(jazzForm, jazz, ::downspout::Meter {});

    assert(dubForm.eventCount > 0);
    assert(jazzForm.eventCount > dubForm.eventCount);

    const auto dubRoundTrip = deserializeControls(serializeControls(dub));
    const auto jazzRoundTrip = deserializeControls(serializeControls(jazz));
    assert(dubRoundTrip.has_value());
    assert(jazzRoundTrip.has_value());
    assert(dubRoundTrip->style == StyleId::dub);
    assert(jazzRoundTrip->style == StyleId::jazz);

    assert(styleName(StyleId::dub) == std::string("dub"));
    assert(styleName(StyleId::jazz) == std::string("jazz"));

    const std::string jazzSummary = summarizeGroundAiState(jazz, jazzForm, 0);
    assert(jazzSummary.find("\"style\":\"jazz\"") != std::string::npos);
}

void testAiStateSummaryIncludesCurrentPhraseContext()
{
    Controls controls;
    controls.rootNote = 38;
    controls.scale = ScaleId::dorian;
    controls.style = StyleId::climb;
    controls.formBars = 16;
    controls.phraseBars = 4;
    controls.color = 0.72f;

    FormState form;
    regenerateForm(form, controls, ::downspout::Meter {});

    const std::string summary = summarizeGroundAiState(controls, form, 1);
    assert(summary.find("\"form_shape\":\"free\"") != std::string::npos);
    assert(summary.find("\"plugin\":\"ground\"") != std::string::npos);
    assert(summary.find("\"key\":2") != std::string::npos);
    assert(summary.find("\"scale\":\"dorian\"") != std::string::npos);
    assert(summary.find("\"style\":\"climb\"") != std::string::npos);
    assert(summary.find("\"current_phrase_index\":1") != std::string::npos);
    assert(summary.find("\"current_role\"") != std::string::npos);
    assert(summary.find("\"guide_events\"") != std::string::npos);
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
    controls.formShape = FormShapeId::blues12Minor;
    controls.formBars = 32;
    controls.phraseBars = 8;
    controls.color = 0.72f;
    controls.sequence = 0.75f;
    controls.noteLength = 0.61f;
    controls.noteLengthVariation = 0.27f;
    controls.vary = 0.42f;
    controls.seed = 998u;
    controls.phraseRoleOverrides[1] = static_cast<int>(PhraseRoleId::cadence) + 1;
    controls.phraseRoleOverrides[3] = static_cast<int>(PhraseRoleId::pedal) + 1;
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
    assert(controlsRoundTrip->formShape == controls.formShape);
    assert(controlsRoundTrip->color > 0.71f && controlsRoundTrip->color < 0.73f);
    assert(controlsRoundTrip->noteLength > 0.60f && controlsRoundTrip->noteLength < 0.62f);
    assert(controlsRoundTrip->noteLengthVariation > 0.26f && controlsRoundTrip->noteLengthVariation < 0.28f);
    assert(controlsRoundTrip->phraseRoleOverrides[1] == static_cast<int>(PhraseRoleId::cadence) + 1);
    assert(controlsRoundTrip->phraseRoleOverrides[3] == static_cast<int>(PhraseRoleId::pedal) + 1);
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
    testSetPhraseRoleKeepsNeighbours();
    testVariationChangesOnLoop();
    testCompoundMeterShape();
    testGroundedPhraseGetsSyncopationAndLegato();
    testNoteLengthControlsDurations();
    testFugalFormPlansSubjectAnswerPedalCadence();
    testTwelveBarBluesFormShapes();
    testAdditionalNamedFormShapes();
    testBassRegisterStaysInLane();
    testDubAndJazzStylesAreExplicit();
    testAiStateSummaryIncludesCurrentPhraseContext();
    testEngineRestartPhraseStatusAndStop();
    testSerializationRoundTrip();
    return 0;
}
