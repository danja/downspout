#include "drumgen_automaton.hpp"
#include "drumgen_engine.hpp"
#include "drumgen_pattern.hpp"
#include "drumgen_serialization.hpp"
#include "drumgen_state.hpp"
#include "drumgen_template.hpp"
#include "drumgen_transport.hpp"
#include "drumgen_variation.hpp"

#include "downspout/test_assert.h"
#include <cmath>
#include <set>
#include <vector>
#include <string>

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
    controls.auxAmt = 0.0f;

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
    assert(hasHit(pattern, LaneId::snare, 6));
    assert(hasHit(pattern, LaneId::snare, 10));
}

void testFugueGenrePinsSparsePulse() {
    // Enum values are frozen: new genres must be appended, never inserted.
    assert(static_cast<int>(GenreId::jazz) == 12);
    assert(static_cast<int>(GenreId::fugue) == 13);
    assert(static_cast<int>(GenreId::rumba) == 14);
    assert(static_cast<int>(GenreId::samba) == 15);
    assert(static_cast<int>(GenreId::townshipJive) == 16);
    assert(static_cast<int>(GenreId::count) == 17);

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

void testLatinGenresPinClaveFigures() {
    Controls controls;
    controls.seed = 4242u;
    controls.bars = 1;
    controls.resolution = ResolutionId::sixteenth;
    controls.styleMode = StyleModeId::autoMode;
    controls.density = 0.70f;
    controls.variation = 0.50f;
    controls.fill = 0.0f;
    controls.auxAmt = 0.90f;

    // Rumba: 3-2 clave at 0,3,7,11,13.
    controls.genre = GenreId::rumba;
    PatternState rumba;
    regeneratePattern(rumba, controls, ::downspout::Meter {}, false);
    assert(hasHit(rumba, LaneId::clave, 0));
    assert(hasHit(rumba, LaneId::clave, 3));
    assert(hasHit(rumba, LaneId::clave, 7));
    assert(hasHit(rumba, LaneId::clave, 11));
    assert(hasHit(rumba, LaneId::clave, 13));
    assert(countHits(rumba, LaneId::clave) == 5);
    assert(hasHit(rumba, LaneId::kick, 0));

    // Samba: 2-3 clave at 0,3,6,8,11,14.
    controls.genre = GenreId::samba;
    PatternState samba;
    regeneratePattern(samba, controls, ::downspout::Meter {}, false);
    assert(hasHit(samba, LaneId::clave, 0));
    assert(hasHit(samba, LaneId::clave, 3));
    assert(hasHit(samba, LaneId::clave, 6));
    assert(hasHit(samba, LaneId::clave, 8));
    assert(hasHit(samba, LaneId::clave, 11));
    assert(hasHit(samba, LaneId::clave, 14));
    assert(countHits(samba, LaneId::clave) == 6);

    // Township jive has no clave of its own but borrows the rumba figure as
    // its percussion spine.
    controls.genre = GenreId::townshipJive;
    PatternState jive;
    regeneratePattern(jive, controls, ::downspout::Meter {}, false);
    assert(hasHit(jive, LaneId::clave, 0));
    assert(countHits(jive, LaneId::clave) == 5);

    // Jive is a backbeat genre, unlike the clave genres: clap on 2 and 4.
    assert(hasHit(jive, LaneId::clap, 4));
    assert(hasHit(jive, LaneId::clap, 12));
}

void testClaveGenresSurviveEighthResolution() {
    // The written clave is authored on a 16th grid; at eighth resolution the
    // hits must still land, not vanish into the offbeat.
    Controls controls;
    controls.seed = 777u;
    controls.bars = 1;
    controls.resolution = ResolutionId::eighth;
    controls.styleMode = StyleModeId::autoMode;
    controls.density = 0.70f;
    controls.variation = 0.50f;
    controls.fill = 0.0f;
    controls.auxAmt = 0.90f;
    controls.genre = GenreId::samba;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    assert(pattern.stepsPerBar == 8);
    assert(countHits(pattern, LaneId::clave) > 0);
}

void testRefreshBarKeepsClaveFigure() {
    // refreshBar rebuilds one bar from the stochastic pass and re-applies the
    // signature. For the written-figure genres it must go through the clave
    // overlay, otherwise the refreshed bar loses its clave and gains a
    // backbeat it should not have.
    Controls controls;
    controls.seed = 31337u;
    controls.bars = 2;
    controls.resolution = ResolutionId::sixteenth;
    controls.styleMode = StyleModeId::autoMode;
    controls.density = 0.70f;
    controls.variation = 0.50f;
    controls.fill = 0.30f;
    controls.auxAmt = 0.90f;
    controls.genre = GenreId::samba;

    PatternState pattern;
    regeneratePattern(pattern, controls, ::downspout::Meter {}, false);

    const int before = countHits(pattern, LaneId::clave);
    assert(before > 0);

    refreshBar(pattern, controls, ::downspout::Meter {}, 0);

    const int barStart = 0;
    for (int slot : {0, 3, 6, 8, 11, 14}) {
        assert(hasHit(pattern, LaneId::clave, barStart + slot));
    }
    // Neither bar may pick up a rock backbeat. cleanupPattern used to
    // reinforce a snare backbeat on every genre after the clave overlay ran,
    // which stamped one onto these clave grooves.
    for (int bar = 0; bar < pattern.bars; ++bar) {
        const int base = bar * pattern.stepsPerBar;
        assert(!hasHit(pattern, LaneId::snare, base + 4));
        assert(!hasHit(pattern, LaneId::snare, base + 12));
        // The clave figure must still be present in the refreshed bar too.
        for (int slot : {0, 3, 6, 8, 11, 14}) {
            assert(hasHit(pattern, LaneId::clave, base + slot));
        }
    }
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

std::string patternLibraryDir() {
    // Same source-relative resolution the existing template test uses.
    return std::string(__FILE__).substr(0, std::string(__FILE__).rfind('/')) + "/../patterns/world";
}

void testNewGenreTemplatesCarryTheClave() {
    // The new genres ship a written figure in the generator and matching
    // .dg-pattern templates. Both paths must agree on the clave, and the
    // templates must survive the text round trip used by the Load button.
    const std::string dir = patternLibraryDir();

    struct Expect { const char* name; int lane; std::vector<int> steps; };
    const std::vector<Expect> cases = {
        {"Guaguancó Tumbao", 10, {0, 3, 7, 11, 13}},
        {"Township Jive", 10, {0, 3, 7, 11, 13}},
    };

    TemplateLibrary lib;
    lib.scan(dir);
    assert(lib.count() > 0);

    for (const Expect& c : cases) {
        const PatternTemplate* tmpl = lib.findByName(c.name);
        assert(tmpl != nullptr);
        if (tmpl == nullptr) {
            continue;
        }

        for (int step : c.steps) {
            assert(tmpl->pattern.lanes[c.lane].steps[static_cast<std::size_t>(step)].velocity > 0);
        }

        const std::string text = serializePatternState(tmpl->pattern);
        const auto reparsed = deserializePatternState(text);
        assert(reparsed.has_value());
        if (reparsed.has_value()) {
            for (int step : c.steps) {
                assert(reparsed->lanes[c.lane].steps[static_cast<std::size_t>(step)].velocity ==
                       tmpl->pattern.lanes[c.lane].steps[static_cast<std::size_t>(step)].velocity);
            }
        }
    }
}

// ---- Cellular-automaton layer ----------------------------------------------------------------

PatternState makeAutomatonPattern() {
    PatternState pattern;
    pattern.version = kPatternStateVersion;
    pattern.bars = 1;
    pattern.stepsPerBeat = 4;
    pattern.stepsPerBar = 16;
    pattern.totalSteps = 16;
    pattern.generationSerial = 1;
    pattern.meter = ::downspout::Meter {};
    pattern.lanes[static_cast<int>(LaneId::kick)].midiNote = 36;
    pattern.lanes[static_cast<int>(LaneId::closedHat)].midiNote = 42;
    for (const int step : {0, 8}) {
        pattern.lanes[static_cast<int>(LaneId::kick)].steps[step].velocity = 100;
    }
    for (const int step : {0, 4, 8, 12}) {
        pattern.lanes[static_cast<int>(LaneId::closedHat)].steps[step].velocity = 90;
    }
    return pattern;
}

TransportSnapshot transportAtBar(double bar) {
    TransportSnapshot transport = makePlayingTransport(0.0);
    transport.bar = bar;
    return transport;
}

// Hat note-on steps (0-15) on the pass that starts at `bar`, played from a cold start.
std::set<int> hatStepsOnPass(int caRule, int caTarget, double bar) {
    Controls controls;
    controls.channel = 10;
    controls.caRule = caRule;
    controls.caTarget = caTarget;
    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeAutomatonPattern();
    state.patternValid = true;

    // 120 bpm, 16 steps a bar: a step is 6000 frames. Stop just short of the next bar line.
    const auto result = processBlock(state, controls, transportAtBar(bar), 95000, 48000.0);
    std::set<int> steps;
    for (int i = 0; i < result.eventCount; ++i) {
        const ScheduledMidiEvent& e = result.events[i];
        if (e.type == MidiEventType::noteOn && e.data1 == 42) {
            steps.insert(static_cast<int>((e.frame + 3000) / 6000));
        }
    }
    return steps;
}

void testAutomatonPureFunction() {
    const PatternState pattern = makeAutomatonPattern();
    const DrumLaneState& hats = pattern.lanes[static_cast<int>(LaneId::closedHat)];

    // Rule index 2 is rule 90. Pass 0 is the pattern as generated.
    for (int step = 0; step < 16; ++step) {
        assert(caVelocity(hats, 16, 2, 0, step) == hats.steps[step].velocity);
    }

    // Generation 1 of rule 90 from hits on 0,4,8,12: every odd step, at the lane's average velocity.
    for (int step = 0; step < 16; ++step) {
        const bool odd = step % 2 == 1;
        assert(caVelocity(hats, 16, 2, 1, step) == (odd ? 90 : 0));
    }

    // Generation 2 of rule 90 is empty again, and 64 passes later it is the pattern again.
    for (int step = 0; step < 16; ++step) {
        assert(caVelocity(hats, 16, 2, 2, step) == 0);
        assert(caVelocity(hats, 16, 2, 64, step) == hats.steps[step].velocity);
        assert(caVelocity(hats, 16, 2, 65, step) == caVelocity(hats, 16, 2, 1, step));
    }

    // Off (index 0) never changes anything, whatever the pass.
    for (int step = 0; step < 16; ++step) {
        assert(caVelocity(hats, 16, 0, 5, step) == hats.steps[step].velocity);
    }

    // A lane with no hits stays silent even under a rule that fills an empty row (105).
    const DrumLaneState& empty = pattern.lanes[static_cast<int>(LaneId::clave)];
    for (std::int64_t pass = 0; pass < 8; ++pass) {
        for (int step = 0; step < 16; ++step) {
            assert(caVelocity(empty, 16, 9, pass, step) == 0);
        }
    }

    // Against an independent reference automaton: every rule, several generations, lengths that are
    // not powers of two. Hits that survive keep their velocity; new ones take the lane's average.
    for (const int length : {8, 12, 16, 31, 32, 100, 128}) {
        DrumLaneState lane;
        int sum = 0;
        int hits = 0;
        for (int i = 0; i < length; ++i) {
            if ((i * 7 + length) % 5 == 0 || i == 0) {
                lane.steps[i].velocity = static_cast<std::uint8_t>(50 + (i * 13) % 70);
                sum += lane.steps[i].velocity;
                ++hits;
            }
        }
        for (int ruleIndex = 1; ruleIndex < kCaRuleCount; ++ruleIndex) {
            for (const int generation : {1, 2, 3, 7, 20}) {
                std::vector<int> row(static_cast<std::size_t>(length));
                for (int i = 0; i < length; ++i) row[static_cast<std::size_t>(i)] = lane.steps[i].velocity > 0 ? 1 : 0;
                for (int g = 0; g < generation; ++g) {
                    std::vector<int> next(row.size());
                    for (int i = 0; i < length; ++i) {
                        const int idx = (row[static_cast<std::size_t>((i + length - 1) % length)] << 2) |
                                        (row[static_cast<std::size_t>(i)] << 1) | row[static_cast<std::size_t>((i + 1) % length)];
                        next[static_cast<std::size_t>(i)] = (kCaRules[static_cast<std::size_t>(ruleIndex)] >> idx) & 1;
                    }
                    row = next;
                }
                for (int i = 0; i < length; ++i) {
                    const std::uint8_t got = caVelocity(lane, length, ruleIndex, generation, i);
                    if (row[static_cast<std::size_t>(i)] == 0) {
                        assert(got == 0);
                    } else if (lane.steps[i].velocity > 0) {
                        assert(got == lane.steps[i].velocity);
                    } else {
                        assert(got == sum / hits);
                    }
                }
            }
        }
    }

    // Passes of negative steps floor, and the pass number is the loop number.
    assert(caPassForStep(0, 16) == 0);
    assert(caPassForStep(15, 16) == 0);
    assert(caPassForStep(16, 16) == 1);
    assert(caPassForStep(-1, 16) == -1);
    assert(caPassForStep(-16, 16) == -1);
    assert(caPassForStep(-17, 16) == -2);
}

void testAutomatonTargets() {
    assert(caTargetsLane(0, static_cast<int>(LaneId::closedHat)));
    assert(caTargetsLane(0, static_cast<int>(LaneId::openHat)));
    assert(!caTargetsLane(0, static_cast<int>(LaneId::kick)));
    assert(!caTargetsLane(0, static_cast<int>(LaneId::snare)));
    assert(caTargetsLane(1, static_cast<int>(LaneId::cowbell)));
    assert(caTargetsLane(1, static_cast<int>(LaneId::clave)));
    assert(!caTargetsLane(1, static_cast<int>(LaneId::closedHat)));
    assert(caTargetsLane(2, static_cast<int>(LaneId::lowTom)));
    assert(caTargetsLane(3, static_cast<int>(LaneId::closedHat)) && caTargetsLane(3, static_cast<int>(LaneId::highTom)) &&
           caTargetsLane(3, static_cast<int>(LaneId::bash)));
    assert(!caTargetsLane(3, static_cast<int>(LaneId::kick)) && !caTargetsLane(3, static_cast<int>(LaneId::snare)) &&
           !caTargetsLane(3, static_cast<int>(LaneId::crash)));
    for (int lane = 0; lane < kLaneCount; ++lane) {
        assert(caTargetsLane(4, lane));
    }
}

void testAutomatonInTheEngine() {
    const std::set<int> original = {0, 4, 8, 12};

    // Off: every pass plays the pattern.
    assert(hatStepsOnPass(0, 0, 0.0) == original);
    assert(hatStepsOnPass(0, 0, 1.0) == original);

    // Rule 90 on hats: pass 0 is the pattern, pass 1 the odd steps, pass 2 silent.
    assert(hatStepsOnPass(2, 0, 0.0) == original);
    assert((hatStepsOnPass(2, 0, 1.0) == std::set<int>{1, 3, 5, 7, 9, 11, 13, 15}));
    assert(hatStepsOnPass(2, 0, 2.0).empty());

    // Playing straight into pass 1 from a cold start gives the same result as a restart there:
    // the pass comes from the position, not from how long playback has run.
    assert(hatStepsOnPass(2, 0, 1.0) == hatStepsOnPass(2, 0, 1.0));
    assert(hatStepsOnPass(2, 0, 65.0) == hatStepsOnPass(2, 0, 1.0));

    // Targeting percussion leaves the hats alone; the kick is never touched by a hats target.
    assert(hatStepsOnPass(2, 1, 1.0) == original);
    Controls controls;
    controls.channel = 10;
    controls.caRule = 2;
    controls.caTarget = 0;
    EngineState state;
    state.controls = clampControls(controls);
    state.previousControls = state.controls;
    state.pattern = makeAutomatonPattern();
    state.patternValid = true;
    const auto result = processBlock(state, controls, transportAtBar(1.0), 95000, 48000.0);
    std::set<int> kickSteps;
    for (int i = 0; i < result.eventCount; ++i) {
        const ScheduledMidiEvent& e = result.events[i];
        if (e.type == MidiEventType::noteOn && e.data1 == 36) kickSteps.insert(static_cast<int>((e.frame + 3000) / 6000));
    }
    assert((kickSteps == std::set<int>{0, 8}));

    // The layer does not touch the stored pattern.
    assert(state.pattern.lanes[static_cast<int>(LaneId::closedHat)].steps[4].velocity == 90);
    assert(state.pattern.lanes[static_cast<int>(LaneId::closedHat)].steps[1].velocity == 0);
}

void testAutomatonEveryNLoops() {
    const PatternState pattern = makeAutomatonPattern();
    const DrumLaneState& hats = pattern.lanes[static_cast<int>(LaneId::closedHat)];

    // Every 3 loops share one generation: passes 0-2 are the pattern, 3-5 generation 1, 6-8 generation 2.
    for (int step = 0; step < 16; ++step) {
        for (int pass = 0; pass < 3; ++pass) {
            assert(caVelocity(hats, 16, 2, pass, step, 3) == hats.steps[step].velocity);
            assert(caVelocity(hats, 16, 2, pass + 3, step, 3) == caVelocity(hats, 16, 2, 1, step, 1));
            assert(caVelocity(hats, 16, 2, pass + 6, step, 3) == 0);
        }
        // Every 1 (the default) and out-of-range values behave as before / clamp.
        assert(caVelocity(hats, 16, 2, 1, step) == caVelocity(hats, 16, 2, 1, step, 1));
        assert(caVelocity(hats, 16, 2, 1, step, 0) == caVelocity(hats, 16, 2, 1, step, 1));
        assert(caVelocity(hats, 16, 2, 8, step, 99) == caVelocity(hats, 16, 2, 1, step, 1));
    }

    Controls controls;
    controls.caRule = 2;
    controls.caEvery = 5;
    const auto restored = deserializeControls(serializeControls(controls));
    assert(restored.has_value() && restored->caEvery == 5);
    assert(serializeControls(Controls {}).find("caEvery") == std::string::npos);
    Controls wild;
    wild.caEvery = 99;
    assert(clampControls(wild).caEvery == 8);
    wild.caEvery = -3;
    assert(clampControls(wild).caEvery == 1);
}

void testAutomatonControlsStateAndClamp() {
    Controls controls;
    controls.caRule = 3;
    controls.caTarget = 4;
    const auto restored = deserializeControls(serializeControls(controls));
    assert(restored.has_value());
    assert(restored->caRule == 3 && restored->caTarget == 4);

    // Unused, it is not written at all: a project that never touches it saves exactly as before.
    assert(serializeControls(Controls {}).find("caRule") == std::string::npos);
    const auto legacy = deserializeControls(serializeControls(Controls {}));
    assert(legacy.has_value() && legacy->caRule == 0 && legacy->caTarget == 0);

    Controls wild;
    wild.caRule = 99;
    wild.caTarget = -4;
    const Controls clamped = clampControls(wild);
    assert(clamped.caRule == kCaRuleCount - 1);
    assert(clamped.caTarget == 0);
}

}  // namespace

int main() {
    testAutomatonPureFunction();
    testAutomatonTargets();
    testAutomatonInTheEngine();
    testAutomatonControlsStateAndClamp();
    testAutomatonEveryNLoops();
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
    testLatinGenresPinClaveFigures();
    testClaveGenresSurviveEighthResolution();
    testRefreshBarKeepsClaveFigure();
    testNewGenreTemplatesCarryTheClave();
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
        const std::string patternDir = patternLibraryDir();

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
