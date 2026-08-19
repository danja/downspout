#include "drumgen_engine.hpp"
#include "drumgen_pattern.hpp"
#include "drumgen_serialization.hpp"
#include "drumgen_state.hpp"
#include "drumgen_template.hpp"
#include "drumgen_transport.hpp"
#include "drumgen_variation.hpp"

#include <cassert>
#include <cmath>

using namespace downspout::drumgen;

namespace {

bool equalStep(const DrumStepCell& a, const DrumStepCell& b) {
    return a.velocity == b.velocity && a.flags == b.flags;
}

bool patternsDiffer(const PatternState& left, const PatternState& right) {
    if (left.bars != right.bars ||
        left.stepsPerBeat != right.stepsPerBeat ||
        left.stepsPerBar != right.stepsPerBar ||
        left.totalSteps != right.totalSteps) {
        return true;
    }

    for (int lane = 0; lane < kLaneCount; ++lane) {
        if (left.lanes[lane].midiNote != right.lanes[lane].midiNote) {
            return true;
        }
        for (int step = 0; step < left.totalSteps; ++step) {
            if (!equalStep(left.lanes[lane].steps[step], right.lanes[lane].steps[step])) {
                return true;
            }
        }
    }

    return false;
}

PatternState makeSingleHitPattern(int step, int note, int velocity) {
    PatternState pattern;
    pattern.version = kPatternStateVersion;
    pattern.bars = 1;
    pattern.stepsPerBeat = 4;
    pattern.stepsPerBar = 16;
    pattern.totalSteps = 16;
    pattern.generationSerial = 1;
    pattern.meter = ::downspout::Meter {};
    pattern.lanes[static_cast<int>(LaneId::kick)].midiNote = note;
    pattern.lanes[static_cast<int>(LaneId::kick)].steps[step].velocity = static_cast<std::uint8_t>(velocity);
    return pattern;
}

TransportSnapshot makePlayingTransport(double barBeat) {
    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = barBeat;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    transport.bpm = 120.0;
    transport.meter = ::downspout::Meter {};
    return transport;
}

TransportSnapshot makePlayingTransportForMeter(double barBeat, double beatsPerBar, double beatType) {
    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = barBeat;
    transport.beatsPerBar = beatsPerBar;
    transport.beatType = beatType;
    transport.bpm = 120.0;
    transport.meter = ::downspout::meterFromTimeSignature(beatsPerBar, beatType);
    return transport;
}

int activePendingCount(const EngineState& state) {
    int count = 0;
    for (const PendingNoteOff& pending : state.pendingNoteOffs) {
        if (pending.active) {
            count += 1;
        }
    }
    return count;
}

bool hasHit(const PatternState& pattern, const LaneId lane, const int step) {
    return pattern.lanes[static_cast<int>(lane)].steps[step].velocity > 0;
}

int countHits(const PatternState& pattern, const LaneId lane) {
    int count = 0;
    for (int step = 0; step < pattern.totalSteps; ++step) {
        if (hasHit(pattern, lane, step)) {
            count += 1;
        }
    }
    return count;
}

int countAllHits(const PatternState& pattern) {
    int count = 0;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < pattern.totalSteps; ++step) {
            if (pattern.lanes[lane].steps[step].velocity > 0) {
                count += 1;
            }
        }
    }
    return count;
}

void testDeterministicGeneration() {
    Controls controls;
    controls.seed = 12345u;
    controls.genre = GenreId::electro;
    controls.kitMap = KitMapId::gm;
    controls.bars = 4;
    controls.resolution = ResolutionId::sixteenthTriplet;

    PatternState first;
    PatternState second;
    regeneratePattern(first, controls, ::downspout::Meter {}, false);
    regeneratePattern(second, controls, ::downspout::Meter {}, false);

    assert(first.bars == second.bars);
    assert(first.stepsPerBeat == second.stepsPerBeat);
    assert(first.stepsPerBar == second.stepsPerBar);
    assert(first.totalSteps == second.totalSteps);
    assert(first.generationSerial == second.generationSerial);

    for (int lane = 0; lane < kLaneCount; ++lane) {
        assert(first.lanes[lane].midiNote == second.lanes[lane].midiNote);
        for (int step = 0; step < first.totalSteps; ++step) {
            assert(equalStep(first.lanes[lane].steps[step], second.lanes[lane].steps[step]));
        }
    }
}

void testFillRefreshKeepsEarlierBars() {
    Controls controls;
    controls.seed = 77u;
    controls.genre = GenreId::rock;
    controls.bars = 4;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);
    const PatternState original = pattern;

    regeneratePattern(pattern, controls, ::downspout::Meter {}, true);

    assert(pattern.generationSerial > original.generationSerial);
    assert(pattern.stepsPerBar == original.stepsPerBar);
    assert(pattern.totalSteps == original.totalSteps);

    const int prefixSteps = pattern.totalSteps - pattern.stepsPerBar;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < prefixSteps; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
    }
}

void testCompoundMeterShape() {
    Controls controls;
    controls.seed = 222u;
    controls.genre = GenreId::rock;
    controls.bars = 4;
    controls.resolution = ResolutionId::sixteenth;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::makeMeter(6, 8), false);

    assert(pattern.meter.numerator == 6);
    assert(pattern.meter.denominator == 8);
    assert(pattern.stepsPerBeat == 4);
    assert(pattern.stepsPerBar == 24);
    assert(pattern.totalSteps == 96);
}

void testCompoundMeterBackbeatLandsOnSecondPulse() {
    Controls controls;
    controls.seed = 333u;
    controls.genre = GenreId::rock;
    controls.bars = 2;
    controls.resolution = ResolutionId::sixteenth;
    controls.backbeatAmt = 1.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::makeMeter(6, 8), false);

    const int secondaryPulseStep = 12;
    const bool hasBackbeat =
        pattern.lanes[static_cast<int>(LaneId::snare)].steps[secondaryPulseStep].velocity > 0 ||
        pattern.lanes[static_cast<int>(LaneId::clap)].steps[secondaryPulseStep].velocity > 0;
    assert(hasBackbeat);
}

void testTripleMeterBackbeatLandsOnSecondBeat() {
    Controls controls;
    controls.seed = 444u;
    controls.genre = GenreId::rock;
    controls.bars = 2;
    controls.resolution = ResolutionId::sixteenth;
    controls.backbeatAmt = 1.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::makeMeter(3, 4), false);

    const int secondBeatStep = 4;
    const bool hasBackbeat =
        pattern.lanes[static_cast<int>(LaneId::snare)].steps[secondBeatStep].velocity > 0 ||
        pattern.lanes[static_cast<int>(LaneId::clap)].steps[secondBeatStep].velocity > 0;
    assert(hasBackbeat);
}

void testRockGenrePinsHardBackbeat() {
    Controls controls;
    controls.seed = 4545u;
    controls.genre = GenreId::rock;
    controls.styleMode = StyleModeId::autoMode;
    controls.bars = 1;
    controls.resolution = ResolutionId::sixteenth;
    controls.density = 0.12f;
    controls.kickAmt = 0.20f;
    controls.backbeatAmt = 0.20f;
    controls.hatAmt = 0.20f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBar == 16);
    assert(pattern.lanes[static_cast<int>(LaneId::kick)].steps[0].velocity >= 124);
    assert(pattern.lanes[static_cast<int>(LaneId::kick)].steps[8].velocity >= 118);
    assert(pattern.lanes[static_cast<int>(LaneId::snare)].steps[4].velocity >= 122);
    assert(pattern.lanes[static_cast<int>(LaneId::snare)].steps[12].velocity >= 124);
    assert(hasHit(pattern, LaneId::closedHat, 0));
    assert(hasHit(pattern, LaneId::closedHat, 2));
    assert(hasHit(pattern, LaneId::closedHat, 4));
}

void testQuarterResolutionUsesOneStepPerBeat() {
    Controls controls;
    controls.seed = 9090u;
    controls.genre = GenreId::rock;
    controls.bars = 2;
    controls.resolution = ResolutionId::quarter;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBeat == 1);
    assert(pattern.stepsPerBar == 4);
    assert(pattern.totalSteps == 8);
}

void testLowestDensityIsSparse() {
    Controls sparseControls;
    sparseControls.seed = 9091u;
    sparseControls.genre = GenreId::rock;
    sparseControls.bars = 1;
    sparseControls.resolution = ResolutionId::sixteenth;
    sparseControls.density = 0.0f;

    Controls defaultControls = sparseControls;
    defaultControls.density = 0.58f;

    PatternState sparse;
    PatternState normal;
    regeneratePattern(sparse, sparseControls, ::downspout::Meter {}, false);
    regeneratePattern(normal, defaultControls, ::downspout::Meter {}, false);

    assert(countAllHits(sparse) < countAllHits(normal));
    assert(countHits(sparse, LaneId::closedHat) <= 4);
}

void testExplicitStyleModesChangePatternShape() {
    Controls controls;
    controls.seed = 515u;
    controls.genre = GenreId::rock;
    controls.bars = 2;
    controls.resolution = ResolutionId::sixteenth;
    controls.backbeatAmt = 1.0f;
    controls.hatAmt = 0.9f;

    PatternState jig;
    controls.styleMode = StyleModeId::jig;
    regeneratePattern(jig, controls, ::downspout::makeMeter(6, 8), false);

    PatternState straight;
    controls.styleMode = StyleModeId::straight;
    regeneratePattern(straight, controls, ::downspout::makeMeter(6, 8), false);

    assert(patternsDiffer(jig, straight));
}

void testDiddleyStylePinsShaveAndHaircutPattern() {
    assert(static_cast<int>(StyleModeId::slipJig) == 5);
    assert(static_cast<int>(StyleModeId::diddley) == 6);
    assert(static_cast<int>(StyleModeId::count) == 7);

    Controls controls;
    controls.seed = 6161u;
    controls.genre = GenreId::rock;
    controls.styleMode = StyleModeId::diddley;
    controls.bars = 2;
    controls.resolution = ResolutionId::sixteenth;
    controls.density = 0.0f;
    controls.variation = 0.0f;
    controls.fill = 0.0f;
    controls.backbeatAmt = 0.0f;
    controls.hatAmt = 0.0f;
    controls.auxAmt = 1.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBar == 16);
    assert(hasHit(pattern, LaneId::clave, 0));
    assert(hasHit(pattern, LaneId::clave, 6));
    assert(hasHit(pattern, LaneId::clave, 12));
    assert(hasHit(pattern, LaneId::clave, 20));
    assert(hasHit(pattern, LaneId::clave, 24));
    assert(hasHit(pattern, LaneId::kick, 0));
    assert(hasHit(pattern, LaneId::kick, 12));
    assert(hasHit(pattern, LaneId::kick, 24));
    assert(hasHit(pattern, LaneId::snare, 6));
    assert(hasHit(pattern, LaneId::snare, 20));

    const auto roundTrip = deserializeControls(serializeControls(controls));
    assert(roundTrip.has_value());
    assert(roundTrip->styleMode == StyleModeId::diddley);
}

void testAmenGenrePinsBreakSignature() {
    Controls controls;
    controls.seed = 616u;
    controls.genre = GenreId::amen;
    controls.bars = 1;
    controls.resolution = ResolutionId::sixteenth;
    controls.density = 0.50f;
    controls.variation = 0.20f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBar == 16);
    assert(hasHit(pattern, LaneId::kick, 0));
    assert(hasHit(pattern, LaneId::kick, 6));
    assert(hasHit(pattern, LaneId::kick, 10));
    assert(hasHit(pattern, LaneId::snare, 4));
    assert(hasHit(pattern, LaneId::snare, 12));
    assert(hasHit(pattern, LaneId::snare, 7));
    assert(hasHit(pattern, LaneId::closedHat, 0));
    assert(hasHit(pattern, LaneId::closedHat, 2));
    assert(hasHit(pattern, LaneId::openHat, 3));
}

void testHipHopGenrePinsSparseBackbeat() {
    Controls controls;
    controls.seed = 717u;
    controls.genre = GenreId::hipHop;
    controls.bars = 1;
    controls.resolution = ResolutionId::sixteenth;
    controls.density = 0.35f;
    controls.variation = 0.20f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBar == 16);
    assert(hasHit(pattern, LaneId::kick, 0));
    assert(hasHit(pattern, LaneId::kick, 7));
    assert(hasHit(pattern, LaneId::kick, 10));
    assert(hasHit(pattern, LaneId::snare, 4));
    assert(hasHit(pattern, LaneId::snare, 12));
    assert(hasHit(pattern, LaneId::closedHat, 0));
    assert(hasHit(pattern, LaneId::closedHat, 2));
    assert(hasHit(pattern, LaneId::openHat, 6));
}

void testJazzGenrePinsSwingRideShape() {
    Controls controls;
    controls.seed = 818u;
    controls.genre = GenreId::jazz;
    controls.bars = 1;
    controls.resolution = ResolutionId::sixteenth;
    controls.density = 0.42f;
    controls.variation = 0.25f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBar == 16);
    assert(hasHit(pattern, LaneId::kick, 0));
    assert(hasHit(pattern, LaneId::kick, 4));
    assert(hasHit(pattern, LaneId::kick, 8));
    assert(hasHit(pattern, LaneId::kick, 12));
    assert(hasHit(pattern, LaneId::closedHat, 0));
    assert(hasHit(pattern, LaneId::closedHat, 3));
    assert(hasHit(pattern, LaneId::closedHat, 4));
    assert(hasHit(pattern, LaneId::closedHat, 7));
    assert(hasHit(pattern, LaneId::closedHat, 8));
    assert(hasHit(pattern, LaneId::closedHat, 11));
    assert(hasHit(pattern, LaneId::closedHat, 12));
    assert(hasHit(pattern, LaneId::closedHat, 15));
    assert(hasHit(pattern, LaneId::openHat, 4));
    assert(hasHit(pattern, LaneId::openHat, 12));
    assert(hasHit(pattern, LaneId::snare, 6));
    assert(hasHit(pattern, LaneId::snare, 10));
}

void testFugueGenrePinsSparsePulse() {
    assert(static_cast<int>(GenreId::jazz) == 12);
    assert(static_cast<int>(GenreId::fugue) == 13);
    assert(static_cast<int>(GenreId::count) == 14);

    Controls controls;
    controls.seed = 933u;
    controls.genre = GenreId::fugue;
    controls.bars = 1;
    controls.resolution = ResolutionId::sixteenth;
    controls.density = 0.90f;
    controls.variation = 0.90f;
    controls.fill = 0.90f;
    controls.metalAmt = 1.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBar == 16);
    assert(hasHit(pattern, LaneId::kick, 0));
    assert(hasHit(pattern, LaneId::closedHat, 0));
    assert(hasHit(pattern, LaneId::closedHat, 4));
    assert(hasHit(pattern, LaneId::closedHat, 8));
    assert(hasHit(pattern, LaneId::closedHat, 12));
    assert(!hasHit(pattern, LaneId::snare, 4));
    assert(!hasHit(pattern, LaneId::snare, 12));
    assert(countHits(pattern, LaneId::crash) == 0);
    assert(countHits(pattern, LaneId::bash) == 0);
    assert(countAllHits(pattern) <= 6);
}

void testCrashCymbalsStaySparseByDefault() {
    Controls controls;
    controls.seed = 919u;
    controls.genre = GenreId::rock;
    controls.bars = 4;
    controls.resolution = ResolutionId::sixteenth;
    controls.density = 0.74f;
    controls.variation = 0.65f;
    controls.fill = 0.35f;
    controls.metalAmt = 0.26f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(countHits(pattern, LaneId::crash) <= 2);
    for (int bar = 1; bar < pattern.bars - 1; ++bar) {
        const int barStart = bar * pattern.stepsPerBar;
        for (int step = 0; step < pattern.stepsPerBar; ++step) {
            assert(!hasHit(pattern, LaneId::crash, barStart + step));
        }
    }
}

void testRefreshBarKeepsOtherBars() {
    Controls controls;
    controls.seed = 313u;
    controls.genre = GenreId::bossa;
    controls.bars = 3;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);
    const PatternState original = pattern;

    refreshBar(pattern, controls, ::downspout::Meter {}, 1);

    const int barStart = pattern.stepsPerBar;
    const int barEnd = barStart + pattern.stepsPerBar;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < barStart; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barEnd; step < pattern.totalSteps; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
    }
}

void testRefreshFillBarTargetsChosenBar() {
    Controls controls;
    controls.seed = 123u;
    controls.genre = GenreId::electro;
    controls.bars = 4;
    controls.fill = 0.10f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);
    const PatternState original = pattern;

    refreshFillBar(pattern, controls, ::downspout::Meter {}, 1);

    const int barStart = pattern.stepsPerBar;
    const int barEnd = barStart + pattern.stepsPerBar;
    bool sawFillFlag = false;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < barStart; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barEnd; step < pattern.totalSteps; ++step) {
            assert(equalStep(pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barStart; step < barEnd; ++step) {
            if ((pattern.lanes[lane].steps[step].flags & kStepFlagFill) != 0) {
                sawFillFlag = true;
            }
        }
    }

    assert(pattern.generationSerial > original.generationSerial);
    assert(sawFillFlag);
}

void testTransportHelpers() {
    PatternState pattern;
    pattern.totalSteps = 16;

    assert(transportRestartDetected(false, -1, 0));
    assert(transportRestartDetected(true, 12, 4));
    assert(!transportRestartDetected(true, 4, 8));

    assert(std::fabs(localStepFromAbsolute(pattern, 18.5) - 2.5) < 1e-9);
    assert(localStepForBoundary(pattern, 17) == 1);
    assert(frameForBoundary(12.0, 16.0, 64, 14) == 32 || frameForBoundary(12.0, 16.0, 64, 14) == 31);
}

void testVariationBehavior() {
    Controls controls;
    controls.seed = 19u;
    controls.vary = 0.0f;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    VariationState variation;
    assert(!applyLoopVariation(pattern, variation, controls));
    assert(variation.completedLoops == 1);
    assert(variation.lastMutationLoop == 0);

    controls.vary = 1.0f;
    const int originalSerial = pattern.generationSerial;
    const bool changed = applyLoopVariation(pattern, variation, controls);
    assert(changed);
    assert(pattern.generationSerial > originalSerial);
    assert(variation.lastMutationLoop == variation.completedLoops);
}

void testStateSanitization() {
    PatternState raw;
    raw.bars = 999;
    raw.meter.numerator = 6;
    raw.meter.denominator = 8;
    raw.stepsPerBeat = 99;
    raw.stepsPerBar = 99;
    raw.totalSteps = 999;
    raw.lanes[0].midiNote = 999;
    raw.lanes[0].steps[0].velocity = 255;

    bool valid = false;
    const PatternState sanitized = sanitizePatternState(raw, &valid);
    assert(valid);
    assert(sanitized.version == kPatternStateVersion);
    assert(sanitized.bars == kMaxBars);
    assert(sanitized.meter.numerator == 6);
    assert(sanitized.meter.denominator == 8);
    assert(sanitized.stepsPerBeat == 4);
    assert(sanitized.stepsPerBar == 24);
    assert(sanitized.totalSteps == 96);
    assert(sanitized.lanes[0].midiNote == 127);
    assert(sanitized.lanes[0].steps[0].velocity == 127);

    VariationState variation;
    variation.version = 99;
    const VariationState sanitizedVariation = sanitizeVariationState(variation);
    assert(sanitizedVariation.version == kVariationStateVersion);
}

void testSerializationRoundTrip() {
    Controls controls;
    controls.genre = GenreId::afro;
    controls.styleMode = StyleModeId::slipJig;
    controls.channel = 8;
    controls.kitMap = KitMapId::gm;
    controls.bars = 4;
    controls.resolution = ResolutionId::sixteenthTriplet;
    controls.vary = 0.65f;
    controls.seed = 999u;
    controls.actionMutate = 3;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    VariationState variation;
    variation.completedLoops = 7;
    variation.lastMutationLoop = 5;

    const auto controlsRoundTrip = deserializeControls(serializeControls(controls));
    const auto patternRoundTrip = deserializePatternState(serializePatternState(pattern));
    const auto variationRoundTrip = deserializeVariationState(serializeVariationState(variation));

    assert(controlsRoundTrip.has_value());
    assert(patternRoundTrip.has_value());
    assert(variationRoundTrip.has_value());
    assert(controlsRoundTrip->genre == controls.genre);
    assert(controlsRoundTrip->styleMode == controls.styleMode);
    assert(controlsRoundTrip->channel == controls.channel);
    assert(controlsRoundTrip->kitMap == controls.kitMap);
    assert(controlsRoundTrip->actionMutate == controls.actionMutate);
    assert(patternRoundTrip->bars == pattern.bars);
    assert(patternRoundTrip->meter.numerator == pattern.meter.numerator);
    assert(patternRoundTrip->meter.denominator == pattern.meter.denominator);
    assert(patternRoundTrip->stepsPerBeat == pattern.stepsPerBeat);
    assert(patternRoundTrip->generationSerial == pattern.generationSerial);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        assert(patternRoundTrip->lanes[lane].midiNote == pattern.lanes[lane].midiNote);
        for (int step = 0; step < kMaxPatternSteps; ++step) {
            assert(equalStep(patternRoundTrip->lanes[lane].steps[step], pattern.lanes[lane].steps[step]));
        }
    }
    assert(variationRoundTrip->completedLoops == 7);
    assert(variationRoundTrip->lastMutationLoop == 5);
}

void testEngineCarriesPendingNoteOffsAcrossBlocks() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(0, 36, 100);
    state.patternValid = true;

    const double sampleRate = 48000.0;
    const auto first = processBlock(state, controls, makePlayingTransport(0.0), 2048, sampleRate);
    assert(first.eventCount == 1);
    assert(first.events[0].type == MidiEventType::noteOn);
    assert(first.events[0].frame == 0);
    assert(first.events[0].channel == 9);
    assert(first.events[0].data1 == 36);
    assert(first.events[0].data2 == 100);
    assert(activePendingCount(state) == 1);
    assert(state.pendingNoteOffs[0].remainingSamples == 52);

    const double nextBarBeat = (2048.0 * 120.0) / (60.0 * sampleRate);
    const auto second = processBlock(state, controls, makePlayingTransport(nextBarBeat), 2048, sampleRate);
    assert(second.eventCount == 1);
    assert(second.events[0].type == MidiEventType::noteOff);
    assert(second.events[0].frame == 52);
    assert(second.events[0].channel == 9);
    assert(second.events[0].data1 == 36);
    assert(activePendingCount(state) == 0);
}

void testEngineStopClearsPendingNoteOffs() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(0, 36, 100);
    state.patternValid = true;
    state.wasPlaying = true;
    state.lastTransportStep = 3;
    state.pendingNoteOffs[0].active = true;
    state.pendingNoteOffs[0].note = 60;
    state.pendingNoteOffs[0].channel = 10;
    state.pendingNoteOffs[0].remainingSamples = 4000;

    TransportSnapshot stopped;
    stopped.valid = true;
    stopped.playing = false;
    stopped.beatsPerBar = 4.0;
    stopped.bpm = 120.0;

    const auto result = processBlock(state, controls, stopped, 2048, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].frame == 0);
    assert(result.events[0].channel == 9);
    assert(result.events[0].data1 == 60);
    assert(activePendingCount(state) == 0);
    assert(!state.wasPlaying);
    assert(state.lastTransportStep == -1);
}

void testEngineRestartReplaysCurrentStep() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(0, 36, 100);
    state.patternValid = true;
    state.wasPlaying = true;
    state.lastTransportStep = 8;
    state.pendingNoteOffs[0].active = true;
    state.pendingNoteOffs[0].note = 72;
    state.pendingNoteOffs[0].channel = 10;
    state.pendingNoteOffs[0].remainingSamples = 5000;

    const auto result = processBlock(state, controls, makePlayingTransport(0.0), 256, 48000.0);
    assert(result.eventCount == 2);
    assert(result.events[0].type == MidiEventType::noteOff);
    assert(result.events[0].frame == 0);
    assert(result.events[0].data1 == 72);
    assert(result.events[1].type == MidiEventType::noteOn);
    assert(result.events[1].frame == 0);
    assert(result.events[1].data1 == 36);
    assert(result.events[1].data2 == 100);
    assert(activePendingCount(state) == 1);
    assert(state.pendingNoteOffs[0].active);
    assert(state.pendingNoteOffs[0].note == 36);
    assert(state.pendingNoteOffs[0].remainingSamples == 1844);
}

void testEngineSchedulesBoundaryHitsWithinBlock() {
    Controls controls;
    controls.channel = 10;

    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeSingleHitPattern(1, 36, 100);
    state.patternValid = true;
    state.wasPlaying = true;
    state.lastTransportStep = 0;

    const auto result = processBlock(state, controls, makePlayingTransport(0.125), 4800, 48000.0);
    assert(result.eventCount == 1);
    assert(result.events[0].type == MidiEventType::noteOn);
    assert(result.events[0].channel == 9);
    assert(result.events[0].data1 == 36);
    assert(result.events[0].data2 == 100);
    assert(result.events[0].frame == 2999 || result.events[0].frame == 3000);
    assert(activePendingCount(state) == 1);
    assert(state.pendingNoteOffs[0].remainingSamples == 299 ||
           state.pendingNoteOffs[0].remainingSamples == 300);
}

void testEngineFillTriggerTargetsCurrentBar() {
    Controls controls;
    controls.seed = 987u;
    controls.bars = 4;
    controls.fill = 0.10f;

    EngineState state;
    activate(state, controls);
    const PatternState original = state.pattern;

    controls.actionFill = 1;
    const auto result = processBlock(state, controls, makePlayingTransport(0.0), 256, 48000.0);
    (void)result;

    const int barStart = 0;
    const int barEnd = state.pattern.stepsPerBar;
    bool sawFillFlag = false;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = barEnd; step < state.pattern.totalSteps; ++step) {
            assert(equalStep(state.pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
        for (int step = barStart; step < barEnd; ++step) {
            if ((state.pattern.lanes[lane].steps[step].flags & kStepFlagFill) != 0) {
                sawFillFlag = true;
            }
        }
    }

    assert(state.pattern.generationSerial > original.generationSerial);
    assert(sawFillFlag);
}

void testEngineFillTriggerUsesLastPulseInCompoundMeter() {
    Controls controls;
    controls.seed = 741u;
    controls.bars = 4;
    controls.fill = 0.10f;

    EngineState state;
    activate(state, controls);

    const ::downspout::Meter jigMeter = ::downspout::makeMeter(6, 8);
    regeneratePattern(state.pattern, controls, jigMeter, false);
    state.patternValid = true;
    const PatternState original = state.pattern;

    controls.actionFill = 1;
    const auto result = processBlock(state, controls, makePlayingTransportForMeter(4.0, 6.0, 8.0), 256, 48000.0);
    (void)result;

    const int firstBarStart = 0;
    const int firstBarEnd = state.pattern.stepsPerBar;
    const int secondBarEnd = firstBarEnd * 2;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = firstBarStart; step < firstBarEnd; ++step) {
            assert(equalStep(state.pattern.lanes[lane].steps[step], original.lanes[lane].steps[step]));
        }
    }

    bool sawNextBarFill = false;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = firstBarEnd; step < secondBarEnd; ++step) {
            if ((state.pattern.lanes[lane].steps[step].flags & kStepFlagFill) != 0) {
                sawNextBarFill = true;
            }
        }
    }

    assert(sawNextBarFill);
}

}  // namespace

int main() {
    testDeterministicGeneration();
    testFillRefreshKeepsEarlierBars();
    testCompoundMeterShape();
    testCompoundMeterBackbeatLandsOnSecondPulse();
    testTripleMeterBackbeatLandsOnSecondBeat();
    testRockGenrePinsHardBackbeat();
    testQuarterResolutionUsesOneStepPerBeat();
    testLowestDensityIsSparse();
    testExplicitStyleModesChangePatternShape();
    testDiddleyStylePinsShaveAndHaircutPattern();
    testAmenGenrePinsBreakSignature();
    testHipHopGenrePinsSparseBackbeat();
    testJazzGenrePinsSwingRideShape();
    testFugueGenrePinsSparsePulse();
    testCrashCymbalsStaySparseByDefault();
    testRefreshBarKeepsOtherBars();
    testRefreshFillBarTargetsChosenBar();
    testTransportHelpers();
    testVariationBehavior();
    testStateSanitization();
    testSerializationRoundTrip();
    testEngineCarriesPendingNoteOffsAcrossBlocks();
    testEngineStopClearsPendingNoteOffs();
    testEngineRestartReplaysCurrentStep();
    testEngineSchedulesBoundaryHitsWithinBlock();
    testEngineFillTriggerTargetsCurrentBar();
    testEngineFillTriggerUsesLastPulseInCompoundMeter();

    // Template library
    {
        const std::string patternDir =
            std::string(__FILE__).substr(0, std::string(__FILE__).rfind('/'))
            + "/../patterns/world";

        TemplateLibrary lib;
        lib.scan(patternDir);
        assert(lib.count() > 0 && "template library found no patterns");

        const PatternTemplate* maqsum = lib.findByName("Maqsum");
        assert(maqsum != nullptr && "Maqsum template not found");
        assert(maqsum->timeSig == "4/4" && "Maqsum time sig wrong");
        assert(maqsum->pattern.totalSteps == 16 && "Maqsum totalSteps wrong");
        assert(maqsum->pattern.bars == 1 && "Maqsum bars wrong");

        // Kick should have a hit at step 0
        assert(maqsum->pattern.lanes[static_cast<int>(LaneId::kick)].steps[0].velocity > 0
               && "Maqsum kick step 0 missing");
        // Hat should have a hit at step 2 (first Tek)
        assert(maqsum->pattern.lanes[static_cast<int>(LaneId::closedHat)].steps[2].velocity > 0
               && "Maqsum hat step 2 missing");

        // Kit note assignment
        assert(midiNoteForLane(LaneId::kick, KitMapId::fluesDrumkit) == 36);
        assert(midiNoteForLane(LaneId::kick, KitMapId::gm) == 36);
        assert(midiNoteForLane(LaneId::snare, KitMapId::fluesDrumkit) == 40);
        assert(midiNoteForLane(LaneId::snare, KitMapId::gm) == 38);

        // applyKitNotes fills in the correct notes
        PatternState pat = maqsum->pattern;
        applyKitNotes(pat, KitMapId::fluesDrumkit);
        assert(pat.lanes[static_cast<int>(LaneId::kick)].midiNote == 36);
        applyKitNotes(pat, KitMapId::gm);
        assert(pat.lanes[static_cast<int>(LaneId::snare)].midiNote == 38);
    }

    return 0;
}
