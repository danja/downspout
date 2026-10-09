#include "xoxolo_automaton.hpp"
#include "xoxolo_engine.hpp"
#include "xoxolo_generator.hpp"
#include "xoxolo_serialization.hpp"

#include "downspout/test_assert.h"
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace downspout::xoxolo;

TransportSnapshot playingTransport(double barBeat = 0.0)
{
    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = barBeat;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.meter = downspout::meterFromTimeSignature(4.0, 4.0);
    return transport;
}

void defaultPatternMatchesDrumkit()
{
    PatternState pattern = makeDefaultPattern();
    assert(pattern.totalSteps == kDefaultSteps);
    assert(pattern.notePreset == NotePresetId::downspout);
    assert(activeLaneCountForPreset(pattern.notePreset) == kDownspoutLaneCount);
    for (int lane = 0; lane < activeLaneCountForPreset(pattern.notePreset); ++lane)
        assert(pattern.lanes[static_cast<std::size_t>(lane)].midiNote == kDefaultLanes[static_cast<std::size_t>(lane)].note);
}

void presetAppliesAvlDrumkitsMap()
{
    PatternState pattern = makeDefaultPattern();
    applyNotePreset(pattern, NotePresetId::avlDrumkits);
    assert(pattern.notePreset == NotePresetId::avlDrumkits);
    assert(pattern.lanes[0].midiNote == 36);
    assert(activeLaneCountForPreset(pattern.notePreset) == kAvlDrumkitsLaneCount);
    assert(pattern.lanes[1].midiNote == 37);
    assert(pattern.lanes[2].midiNote == 38);
    assert(pattern.lanes[3].midiNote == 39);
    assert(pattern.lanes[20].midiNote == 56);
    assert(pattern.lanes[28].midiNote == 64);
    assert(std::string(laneSpecsForPreset(pattern.notePreset)[20].name) == "Cowbell");
}

void togglesCells()
{
    PatternState pattern = makeDefaultPattern();
    assert(!cellActive(pattern, 0, 0));
    setCell(pattern, 0, 0, true);
    assert(cellActive(pattern, 0, 0));
    setCell(pattern, 0, 0, false);
    assert(!cellActive(pattern, 0, 0));
}

void resizePreservesOnlyVisibleCells()
{
    PatternState pattern = makeDefaultPattern();
    resizePattern(pattern, 32, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 32);
    setCell(pattern, 0, 0, true);
    setCell(pattern, 0, 20, true);

    resizePattern(pattern, 13, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 13);
    assert(cellActive(pattern, 0, 0));

    resizePattern(pattern, 32, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == 32);
    assert(cellActive(pattern, 0, 0));
    assert(!cellActive(pattern, 0, 20));
}

void controlsClampStepRange()
{
    Controls controls {};
    controls.steps = 99;
    controls = clampControls(controls);
    assert(controls.steps == kMaxSteps);

    PatternState pattern = makeDefaultPattern();
    resizePattern(pattern, 4, ResolutionId::sixteenth, downspout::meterFromTimeSignature(4.0, 4.0));
    assert(pattern.totalSteps == kMinSteps);
}

void clampsNotesAndChannel()
{
    PatternState pattern = makeDefaultPattern();
    pattern.lanes[0].midiNote = -10;
    pattern.lanes[1].midiNote = 200;
    sanitizePattern(pattern);
    assert(pattern.lanes[0].midiNote == 0);
    assert(pattern.lanes[1].midiNote == 127);

    Controls controls {};
    controls.channel = 99;
    assert(clampControls(controls).channel == 16);
}

void stoppedTransportEmitsNoSequence()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    setCell(state.pattern, 0, 0, true);
    Controls controls {};
    activate(state, controls);

    TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = false;
    const BlockResult result = processBlock(state, controls, transport, 512, 48000.0);
    assert(result.eventCount == 0);
    assert(result.currentStep == -1);
}

void playStartEmitsCurrentStep()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    setCell(state.pattern, 0, 0, true);
    Controls controls {};
    activate(state, controls);

    const BlockResult result = processBlock(state, controls, playingTransport(), 512, 48000.0);
    assert(result.eventCount >= 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].data1 == 36);
    assert(result.currentStep == 0);
}

void boundaryEmitsLaterStep()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    setCell(state.pattern, 0, 1, true);
    Controls controls {};
    activate(state, controls);

    const BlockResult result = processBlock(state, controls, playingTransport(0.24), 2400, 48000.0);
    bool found = false;
    for (int i = 0; i < result.eventCount; ++i)
        found = found || (result.events[static_cast<std::size_t>(i)].type == MidiEventType::noteOn &&
                          result.events[static_cast<std::size_t>(i)].data1 == 36);
    assert(found);
}

void previewEmitsOnePair()
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    state.pattern.lanes[2].midiNote = 72;
    Controls controls {};
    controls.channel = 3;
    controls.previewLane = 2;
    controls.previewSerial = 1;
    activate(state, Controls {});

    const BlockResult result = processBlock(state, controls, TransportSnapshot {}, 4096, 48000.0);
    assert(result.eventCount == 2);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].channel == 9);
    assert(result.events[0].data1 == 72);
    assert(result.events[1].type == MidiEventType::noteOff);
    assert(result.events[1].channel == 9);
    assert(result.events[1].data1 == 72);
}

void serializationRoundTrips()
{
    PatternState pattern = makeDefaultPattern();
    pattern.totalSteps = 27;
    pattern.resolution = ResolutionId::sixteenth;
    pattern.channel = 9;
    applyNotePreset(pattern, NotePresetId::avlDrumkits);
    resizePattern(pattern, pattern.totalSteps, pattern.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
    pattern.lanes[3].midiNote = 72;
    setCell(pattern, 3, 7, true);
    setCell(pattern, 10, 26, true);

    const std::string text = serializePatternState(pattern);
    assert(text.find("length=27\n") != std::string::npos);
    assert(text.find("steps=3,") != std::string::npos);
    const auto parsed = deserializePatternState(text);
    assert(parsed.has_value());
    assert(parsed->resolution == ResolutionId::sixteenth);
    assert(parsed->channel == 9);
    assert(parsed->notePreset == NotePresetId::avlDrumkits);
    assert(parsed->totalSteps == 27);
    assert(parsed->lanes[3].midiNote == 72);
    assert(cellActive(*parsed, 3, 7));
    assert(cellActive(*parsed, 10, 26));
}

int activeCellCount(const PatternState& pattern)
{
    int count = 0;
    const int lanes = activeLaneCountForPreset(pattern.notePreset);
    for (int lane = 0; lane < lanes; ++lane) {
        for (int step = 0; step < pattern.totalSteps; ++step)
            count += cellActive(pattern, lane, step) ? 1 : 0;
    }
    return count;
}

bool patternsMatch(const PatternState& first, const PatternState& second)
{
    if (first.totalSteps != second.totalSteps || first.notePreset != second.notePreset)
        return false;
    const int lanes = activeLaneCountForPreset(first.notePreset);
    for (int lane = 0; lane < lanes; ++lane) {
        for (int step = 0; step < first.totalSteps; ++step) {
            if (cellActive(first, lane, step) != cellActive(second, lane, step))
                return false;
        }
    }
    return true;
}

void generationIsDeterministicAndSeeded()
{
    GenerationSettings settings {};
    settings.style = GenerationStyle::drumAndBass;
    settings.density = 0.7f;
    settings.tension = 0.65f;

    PatternState first = makeDefaultPattern();
    PatternState second = makeDefaultPattern();
    PatternState variation = makeDefaultPattern();
    generatePattern(first, settings, 12345u);
    generatePattern(second, settings, 12345u);
    generatePattern(variation, settings, 98765u);

    assert(patternsMatch(first, second));
    assert(!patternsMatch(first, variation));
    assert(activeCellCount(first) > 0);
}

void densityAndTensionAddOptionalHits()
{
    GenerationSettings sparse {};
    sparse.style = GenerationStyle::funk;
    sparse.density = 0.2f;
    sparse.tension = 0.1f;
    GenerationSettings dense = sparse;
    dense.density = 0.95f;
    GenerationSettings tense = dense;
    tense.tension = 1.0f;

    int sparseCount = 0;
    int denseCount = 0;
    int tenseCount = 0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        PatternState sparsePattern = makeDefaultPattern();
        PatternState densePattern = makeDefaultPattern();
        PatternState tensePattern = makeDefaultPattern();
        generatePattern(sparsePattern, sparse, seed);
        generatePattern(densePattern, dense, seed);
        generatePattern(tensePattern, tense, seed);
        sparseCount += activeCellCount(sparsePattern);
        denseCount += activeCellCount(densePattern);
        tenseCount += activeCellCount(tensePattern);
    }
    assert(denseCount > sparseCount);
    assert(tenseCount > denseCount);
}

void generationSupportsBothNotePresets()
{
    PatternState pattern = makeDefaultPattern();
    applyNotePreset(pattern, NotePresetId::avlDrumkits);
    const int kickNote = pattern.lanes[0].midiNote;
    const int percussionNote = pattern.lanes[20].midiNote;

    GenerationSettings settings {};
    settings.style = GenerationStyle::latin;
    settings.density = 1.0f;
    settings.tension = 1.0f;
    generatePattern(pattern, settings, 42u);

    assert(pattern.notePreset == NotePresetId::avlDrumkits);
    assert(pattern.lanes[0].midiNote == kickNote);
    assert(pattern.lanes[20].midiNote == percussionNote);
    assert(activeCellCount(pattern) > 0);
}

void generationSettingsClampAndStylesHaveNames()
{
    GenerationSettings settings {};
    settings.style = GenerationStyle::count;
    settings.density = -2.0f;
    settings.tension = 3.0f;
    settings = clampGenerationSettings(settings);
    assert(settings.style == GenerationStyle::jazz);
    assert(settings.density == 0.0f);
    assert(settings.tension == 1.0f);
    assert(std::string(generationStyleName(GenerationStyle::jazz)) == "Jazz");
    assert(std::string(generationStyleName(GenerationStyle::drumAndBass)) == "Drum & Bass");
}

// ---- Cellular-automaton layer ----------------------------------------------------------------

// Steps (0-15) on which the closed hat (lane 4, note 42) or kick (lane 0, note 36) sounds on the pass
// that starts at `bar`, played from a cold start with the hat lane programmed on 0, 4, 8, 12.
std::set<int> stepsOnPass(const int caRule, const int caEvery, const bool evolveHat, const double bar, const int note)
{
    EngineState state {};
    state.pattern = makeDefaultPattern();
    for (const int step : {0, 4, 8, 12})
        setCell(state.pattern, 4, step, true);
    for (const int step : {0, 8})
        setCell(state.pattern, 0, step, true);
    state.pattern.lanes[4].evolve = evolveHat ? 1 : 0;
    Controls controls {};
    controls.caRule = caRule;
    controls.caEvery = caEvery;
    activate(state, controls);

    TransportSnapshot transport = playingTransport();
    transport.bar = bar;
    // 120 bpm, 16 steps a bar: a step is 6000 frames. Stop just short of the next bar line.
    const BlockResult result = processBlock(state, controls, transport, 95000, 48000.0);
    std::set<int> steps;
    for (int i = 0; i < result.eventCount; ++i) {
        const ScheduledMidiEvent& e = result.events[static_cast<std::size_t>(i)];
        if (e.type == MidiEventType::noteOn && e.data1 == note)
            steps.insert(static_cast<int>((e.frame + 3000) / 6000));
    }
    return steps;
}

void automatonMatchesReferenceRule()
{
    for (const int length : {8, 11, 16, 24, 31, 32}) {
        LaneState lane {};
        lane.evolve = 1;
        int hits = 0;
        for (int i = 0; i < length; ++i) {
            if ((i * 7 + length) % 5 == 0 || i == 0) {
                lane.steps[static_cast<std::size_t>(i)] = 1;
                ++hits;
            }
        }
        assert(hits > 0);
        for (int ruleIndex = 1; ruleIndex < kCaRuleCount; ++ruleIndex) {
            for (const int generation : {1, 2, 3, 7, 20}) {
                std::vector<int> row(static_cast<std::size_t>(length));
                for (int i = 0; i < length; ++i)
                    row[static_cast<std::size_t>(i)] = lane.steps[static_cast<std::size_t>(i)] != 0 ? 1 : 0;
                for (int g = 0; g < generation; ++g) {
                    std::vector<int> next(row.size());
                    for (int i = 0; i < length; ++i) {
                        const int idx = (row[static_cast<std::size_t>((i + length - 1) % length)] << 2) |
                                        (row[static_cast<std::size_t>(i)] << 1) |
                                        row[static_cast<std::size_t>((i + 1) % length)];
                        next[static_cast<std::size_t>(i)] = (kCaRules[static_cast<std::size_t>(ruleIndex)] >> idx) & 1;
                    }
                    row = next;
                }
                for (int i = 0; i < length; ++i)
                    assert(caStepActive(lane, length, ruleIndex, 1, generation, i) == (row[static_cast<std::size_t>(i)] != 0));
            }
        }
    }

    // Pass 0 and every 64th generation are the programmed pattern; a lane that is not evolving, or an
    // empty one, is never changed.
    LaneState lane {};
    lane.evolve = 1;
    for (const int step : {0, 4, 8, 12})
        lane.steps[static_cast<std::size_t>(step)] = 1;
    for (int step = 0; step < 16; ++step) {
        assert(caStepActive(lane, 16, 2, 1, 0, step) == (lane.steps[static_cast<std::size_t>(step)] != 0));
        assert(caStepActive(lane, 16, 2, 1, 64, step) == (lane.steps[static_cast<std::size_t>(step)] != 0));
        assert(caStepActive(lane, 16, 0, 1, 7, step) == (lane.steps[static_cast<std::size_t>(step)] != 0));
    }
    LaneState plain = lane;
    plain.evolve = 0;
    for (int step = 0; step < 16; ++step)
        assert(caStepActive(plain, 16, 2, 1, 1, step) == (plain.steps[static_cast<std::size_t>(step)] != 0));
    LaneState empty {};
    empty.evolve = 1;
    for (std::int64_t pass = 0; pass < 8; ++pass)
        for (int step = 0; step < 16; ++step)
            assert(!caStepActive(empty, 16, 9, 1, pass, step));  // rule 105 would fill an empty row

    // Every N passes holds a generation for N passes.
    for (int step = 0; step < 16; ++step) {
        assert(caStepActive(lane, 16, 2, 3, 0, step) == caStepActive(lane, 16, 2, 3, 2, step));
        assert(caStepActive(lane, 16, 2, 3, 3, step) == caStepActive(lane, 16, 2, 1, 1, step));
        assert(caStepActive(lane, 16, 2, 3, 5, step) == caStepActive(lane, 16, 2, 3, 3, step));
    }

    assert(caPassForStep(0, 16) == 0 && caPassForStep(15, 16) == 0 && caPassForStep(16, 16) == 1);
    assert(caPassForStep(-1, 16) == -1 && caPassForStep(-17, 16) == -2);
}

void automatonInTheEngine()
{
    const std::set<int> original = {0, 4, 8, 12};
    const std::set<int> odd = {1, 3, 5, 7, 9, 11, 13, 15};

    // Rule off, or lane not marked: every pass plays the grid.
    assert(stepsOnPass(0, 1, true, 1.0, 42) == original);
    assert(stepsOnPass(2, 1, false, 1.0, 42) == original);

    // Rule 90 on a marked lane: pass 0 is the grid, pass 1 the odd steps, pass 2 silent (generation 2).
    assert(stepsOnPass(2, 1, true, 0.0, 42) == original);
    assert(stepsOnPass(2, 1, true, 1.0, 42) == odd);
    assert(stepsOnPass(2, 1, true, 2.0, 42).empty());

    // The pass comes from the position: 64 passes later it is the grid again, and a cold start in the
    // middle of a piece agrees with continuous playback.
    assert(stepsOnPass(2, 1, true, 64.0, 42) == original);
    assert(stepsOnPass(2, 1, true, 65.0, 42) == odd);

    // Every 2 passes: passes 0 and 1 are the grid, 2 and 3 are generation 1.
    assert(stepsOnPass(2, 2, true, 1.0, 42) == original);
    assert(stepsOnPass(2, 2, true, 2.0, 42) == odd);
    assert(stepsOnPass(2, 2, true, 3.0, 42) == odd);

    // The kick lane is not marked, so it is never touched.
    assert((stepsOnPass(2, 1, true, 1.0, 36) == std::set<int> {0, 8}));
}

void automatonStateAndClamp()
{
    PatternState pattern = makeDefaultPattern();
    setCell(pattern, 4, 2, true);

    // Unused, nothing extra is written: a pattern saves exactly as before.
    assert(serializePatternState(pattern).find("evolve") == std::string::npos);

    pattern.lanes[4].evolve = 1;
    pattern.lanes[9].evolve = 1;
    const std::string text = serializePatternState(pattern);
    assert(text.find("evolve=4,1\n") != std::string::npos);
    assert(text.find("evolve=9,1\n") != std::string::npos);
    assert(text.find("evolve=0,") == std::string::npos);
    const auto parsed = deserializePatternState(text);
    assert(parsed.has_value());
    assert(parsed->lanes[4].evolve == 1 && parsed->lanes[9].evolve == 1 && parsed->lanes[0].evolve == 0);
    assert(cellActive(*parsed, 4, 2));

    // A saved flag for a lane out of range is rejected rather than ignored.
    assert(!deserializePatternState("evolve=99,1\n").has_value());

    Controls wild {};
    wild.caRule = 99;
    wild.caEvery = 0;
    const Controls clamped = clampControls(wild);
    assert(clamped.caRule == kCaRuleCount - 1);
    assert(clamped.caEvery == kMinCaEvery);
    wild.caRule = -3;
    wild.caEvery = 99;
    assert(clampControls(wild).caRule == 0 && clampControls(wild).caEvery == kMaxCaEvery);
}

// ---- Conductor CC set ------------------------------------------------------------------------

void ccEvent(EngineState& state, Controls& controls, const int status, const int d1, const int d2)
{
    MidiInputEvent e {};
    e.size = 3;
    e.data = {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(d1), static_cast<std::uint8_t>(d2), 0};
    handleMidi(state, controls, &e, 1);
}

// Hits (step, note, velocity) over `bars` bars at 120 bpm, 1000-frame blocks; `hook(pos, controls, state)`
// runs before each block.
template <typename Hook>
std::vector<std::array<long long, 3>> conductorRender(EngineState& state, Controls controls, const int bars, Hook hook)
{
    activate(state, controls);
    std::vector<std::array<long long, 3>> hits;
    for (long long pos = 0; pos < static_cast<long long>(bars) * 96000; pos += 1000) {
        hook(pos, controls, state);
        TransportSnapshot transport = playingTransport();
        const double quarter = static_cast<double>(pos) / 48000.0 * 2.0;
        transport.bar = std::floor(quarter / 4.0);
        transport.barBeat = quarter - transport.bar * 4.0;
        const BlockResult result = processBlock(state, controls, transport, 1000, 48000.0);
        for (int i = 0; i < result.eventCount; ++i) {
            const ScheduledMidiEvent& e = result.events[static_cast<std::size_t>(i)];
            // Steps are 6000 frames and play a sample early; the next bar's first step lands on the last frame of the
            // last block. Report each hit as (step, note, velocity) and leave that boundary hit out.
            if (e.type == MidiEventType::noteOn && pos + e.frame < static_cast<long long>(bars) * 96000 - 1000)
                hits.push_back({(pos + e.frame + 3000) / 6000, e.data1, e.data2});
        }
    }
    return hits;
}

void conductorCcSet()
{
    EngineState state {};
    Controls controls {};
    assert(controls.conductorCh == 0 && controls.density == 1.0f && controls.energy == 1.0f && controls.caShift == 0);

    ccEvent(state, controls, 0xbf, 21, 0);  // off: ignored
    assert(controls.density == 1.0f);

    controls.conductorCh = 16;
    ccEvent(state, controls, 0xbf, 21, 0);
    assert(controls.density == 0.0f);
    ccEvent(state, controls, 0xbf, 21, 127);
    assert(controls.density == 1.0f);
    ccEvent(state, controls, 0xbf, 22, 0);
    assert(controls.energy == 0.0f);
    ccEvent(state, controls, 0xbf, 23, 0);
    assert(controls.caShift == 0);
    ccEvent(state, controls, 0xbf, 23, 127);
    assert(controls.caShift == kCaGenerations - 1);

    const Controls before = controls;
    ccEvent(state, controls, 0xb0, 21, 0);  // another channel
    ccEvent(state, controls, 0xbf, 20, 0);  // scene is unused
    ccEvent(state, controls, 0xbf, 5, 0);
    assert(controls.density == before.density && controls.energy == before.energy && controls.caShift == before.caShift);

    assert(!state.restartPending);
    ccEvent(state, controls, 0xbf, 24, 100);
    assert(!state.restartPending);
    ccEvent(state, controls, 0xbf, 24, 127);
    assert(state.restartPending);

    Controls wild {};
    wild.conductorCh = 99;
    wild.density = 7.0f;
    wild.energy = -1.0f;
    wild.caShift = 500;
    const Controls clamped = clampControls(wild);
    assert(clamped.conductorCh == 16 && clamped.density == 1.0f && clamped.energy == 0.0f && clamped.caShift == kCaGenerations - 1);
}

void conductorDensityAndEnergy()
{
    const auto programme = [](EngineState& state) {
        state.pattern = makeDefaultPattern();
        for (int step = 0; step < 16; ++step)
            setCell(state.pattern, 4, step, true);  // closed hat on every step
    };
    EngineState a {};
    programme(a);
    const auto plain = conductorRender(a, Controls {}, 1, [](long long, Controls&, EngineState&) {});
    assert(plain.size() == 16);
    for (const auto& h : plain)
        assert(h[2] == 100);  // Energy 1 is the programmed velocity

    Controls thinned {};
    thinned.density = 0.5f;
    EngineState b {};
    programme(b);
    const auto half = conductorRender(b, thinned, 1, [](long long, Controls&, EngineState&) {});
    EngineState c {};
    programme(c);
    assert(half == conductorRender(c, thinned, 1, [](long long, Controls&, EngineState&) {}));  // repeatable
    assert(!half.empty() && half.size() < plain.size());
    for (const auto& h : half) {
        bool found = false;
        for (const auto& p : plain)
            found = found || (p[0] == h[0] && p[1] == h[1]);
        assert(found);  // a subset of the programmed hits: nothing new appears
    }

    Controls none {};
    none.density = 0.0f;
    EngineState d {};
    programme(d);
    assert(conductorRender(d, none, 1, [](long long, Controls&, EngineState&) {}).empty());

    Controls soft {};
    soft.energy = 0.0f;
    EngineState e {};
    programme(e);
    const auto quiet = conductorRender(e, soft, 1, [](long long, Controls&, EngineState&) {});
    assert(quiet.size() == plain.size());
    for (const auto& h : quiet)
        assert(h[2] == 40);
}

void conductorMutationShiftsTheEvolveLayer()
{
    const auto programme = [](EngineState& state) {
        state.pattern = makeDefaultPattern();
        for (const int step : {0, 4, 8, 12})
            setCell(state.pattern, 4, step, true);
        state.pattern.lanes[4].evolve = 1;
    };
    Controls rule90 {};
    rule90.caRule = 2;  // rule 90: generation 1 of 0,4,8,12 is every odd step

    EngineState a {};
    programme(a);
    const auto original = conductorRender(a, rule90, 1, [](long long, Controls&, EngineState&) {});
    assert(original.size() == 4);

    Controls shifted = rule90;
    shifted.caShift = 1;
    EngineState b {};
    programme(b);
    const auto odd = conductorRender(b, shifted, 1, [](long long, Controls&, EngineState&) {});
    assert(odd.size() == 8);
    for (const auto& h : odd)
        assert(h[0] % 2 == 1);
}

void conductorRestartAtTheNextBarLine()
{
    // A 12-step pattern in a 16-step bar, one hit on step 0: the loop point drifts against the bar line.
    const auto programme = [](EngineState& state) {
        state.pattern = makeDefaultPattern();
        setCell(state.pattern, 4, 0, true);
    };
    Controls controls {};
    controls.steps = 12;
    controls.conductorCh = 16;

    EngineState free {};
    programme(free);
    const auto drifting = conductorRender(free, controls, 3, [](long long, Controls&, EngineState&) {});
    // Hits every 12 steps: 0, 12, 24, 36.
    assert(drifting.size() >= 3 && drifting[0][0] == 0 && drifting[1][0] == 12 && drifting[2][0] == 24);

    EngineState reset {};
    programme(reset);
    const auto restarted = conductorRender(reset, controls, 3, [](long long pos, Controls& c, EngineState& st) {
        if (pos == 40000)
            ccEvent(st, c, 0xbf, 24, 127);  // mid bar 0
    });
    // Unchanged up to the bar line, then the pattern starts again exactly on bar 1 (step 16) and runs on from there.
    assert(restarted.size() >= 4);
    assert(restarted[0][0] == 0 && restarted[1][0] == 12);  // bar 0 is untouched: the loop point at step 12 still falls there
    assert(restarted[2][0] == 16);                          // the restart: a fresh loop begins on bar 1
    assert(restarted[3][0] == 16 + 12);                     // and runs on from there
    // One restart only: the pending flag was consumed.
    assert(reset.restartStep == 16);
}

}  // namespace

int main()
{
    defaultPatternMatchesDrumkit();
    presetAppliesAvlDrumkitsMap();
    togglesCells();
    resizePreservesOnlyVisibleCells();
    controlsClampStepRange();
    clampsNotesAndChannel();
    stoppedTransportEmitsNoSequence();
    playStartEmitsCurrentStep();
    boundaryEmitsLaterStep();
    previewEmitsOnePair();
    serializationRoundTrips();
    generationIsDeterministicAndSeeded();
    densityAndTensionAddOptionalHits();
    generationSupportsBothNotePresets();
    generationSettingsClampAndStylesHaveNames();
    automatonMatchesReferenceRule();
    automatonInTheEngine();
    automatonStateAndClamp();
    conductorCcSet();
    conductorDensityAndEnergy();
    conductorMutationShiftsTheEvolveLayer();
    conductorRestartAtTheNextBarLine();
    std::cout << "xoxolo core tests passed\n";
    return 0;
}
