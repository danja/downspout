#include "melevolve_core.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace downspout::melevolve;

namespace {

constexpr double kSampleRate = 48000.0;

struct Event {
    std::uint64_t frame = 0;
    std::uint8_t status = 0;
    std::uint8_t d1 = 0;
    std::uint8_t d2 = 0;

    bool operator==(const Event& other) const
    {
        return frame == other.frame && status == other.status && d1 == other.d1 && d2 == other.d2;
    }
};

Settings smallSettings()
{
    Settings settings = defaultSettings();
    settings.populationSize = 10;
    settings.bars = 2;
    return settings;
}

Session makeSession(const std::uint32_t seed, const Settings& settings = smallSettings())
{
    Session session;
    session.settings = settings;
    session.rng = Rng(seed);
    Context ctx = makeContext(session.settings, session.rng);
    session.population = Population::create(ctx);
    return session;
}

MidiEvent inputEvent(const std::uint32_t frame, const std::uint8_t status,
                     const std::uint8_t d1 = 0, const std::uint8_t d2 = 0)
{
    MidiEvent event;
    event.frame = frame;
    event.size = status >= 0xf0 ? 1 : 3;
    event.data = {status, d1, d2, 0};
    return event;
}

Transport hostAt(const std::uint64_t frame, const double bpm = 120.0, const bool playing = true)
{
    Transport transport;
    transport.valid = true;
    transport.playing = playing;
    transport.bpm = bpm;
    transport.beatsPerBar = 4.0;
    transport.beatType = 4.0;
    const double quarter = static_cast<double>(frame) / kSampleRate * bpm / 60.0;
    transport.bar = std::floor(quarter / 4.0);
    transport.barBeat = quarter - transport.bar * 4.0;
    return transport;
}

template <typename HostFn, typename InputFn>
std::vector<Event> runBlocks(Session& session, Engine& engine, const std::uint64_t totalFrames,
                             const std::uint32_t blockSize, HostFn hostFn, InputFn inputFn)
{
    std::vector<Event> events;
    for (std::uint64_t pos = 0; pos < totalFrames; pos += blockSize) {
        const std::uint32_t frames = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(blockSize, totalFrames - pos));
        const Transport host = hostFn(pos, frames);
        const std::vector<MidiEvent> inputs = inputFn(pos, frames);
        const MidiBlock block = process(session, engine, host, inputs.data(),
                                         static_cast<std::uint32_t>(inputs.size()), frames, kSampleRate);
        for (std::uint32_t i = 0; i < block.count; ++i) {
            const MidiEvent& event = block.events[i];
            events.push_back({pos + event.frame, event.data[0], event.data[1], event.data[2]});
        }
    }
    return events;
}

std::vector<Event> runBlocks(Session& session, Engine& engine, const std::uint64_t totalFrames,
                             const std::uint32_t blockSize, const Transport& host)
{
    return runBlocks(session, engine, totalFrames, blockSize,
                     [host](std::uint64_t, std::uint32_t) { return host; },
                     [](std::uint64_t, std::uint32_t) { return std::vector<MidiEvent> {}; });
}

std::vector<Event> noteOns(const std::vector<Event>& events)
{
    std::vector<Event> notes;
    for (const Event& event : events)
        if ((event.status & 0xf0) == 0x90 && event.d2 > 0) notes.push_back(event);
    return notes;
}

bool hasStatus(const std::vector<Event>& events, const std::uint8_t status)
{
    return std::any_of(events.begin(), events.end(), [status](const Event& event) {
        return event.status == status;
    });
}

bool hasCc123(const std::vector<Event>& events)
{
    return std::any_of(events.begin(), events.end(), [](const Event& event) {
        return event.status == 0xb0 && event.d1 == 123;
    });
}

void testRngScalesAndTiming()
{
    // Independent mulberry32 reference values for seed 1, taken from Melogen's util/rng.js.
    const std::array<double, 8> expectedValues {{
        0.62707394058816135, 0.00273572118021548, 0.52744703995995224, 0.98105096747167408,
        0.96837789821438491, 0.28110350295901299, 0.61283886060118675, 0.72074314113706350
    }};
    const std::array<std::uint32_t, 8> expectedStates {{
        1831565814u, 3663131627u, 1199730144u, 3031295957u,
        567894474u, 2399460287u, 4231026100u, 1767624617u
    }};
    Rng rng(1);
    for (std::size_t i = 0; i < expectedValues.size(); ++i) {
        const double value = rng.next();
        assert(std::fabs(value - expectedValues[i]) < 1.0e-12);
        assert(rng.state == expectedStates[i]);
    }
    assert(Rng(1).range(1, 6) == Rng(1).range(1, 6));

    const std::vector<std::vector<int>> expectedScales {
        {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
        {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10},
        {0, 2, 3, 5, 7, 8, 11}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10},
        {0, 3, 5, 6, 7, 10}, {0, 2, 4, 6, 8, 10},
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
    };
    assert(expectedScales.size() == static_cast<std::size_t>(ScaleId::count));
    for (int scale = 0; scale < static_cast<int>(ScaleId::count); ++scale)
        assert(scaleSteps(static_cast<ScaleId>(scale)) == expectedScales[static_cast<std::size_t>(scale)]);

    assert(degreeRange(ScaleId::major, Mode::melody) == 14);
    assert(degreeRange(ScaleId::major, Mode::bass) == 11);
    assert(degreeRange(ScaleId::majorPentatonic, Mode::melody) == 10);
    Settings settings = defaultSettings();
    settings.root = 0;
    settings.octave = 4;
    settings.scale = ScaleId::major;
    assert(degreeToMidi(0, settings) == 60);
    assert(degreeToMidi(7, settings) == 72);
    assert(degreeToMidi(13, settings) == 83);
    assert(isChordTone(0, ScaleId::major) && isChordTone(2, ScaleId::major)
           && isChordTone(4, ScaleId::major) && !isChordTone(1, ScaleId::major));

    const std::array<int, kTimeSignatureCount> perBar {{16, 12, 12, 20, 14}};
    const std::array<int, kTimeSignatureCount> perBeat {{4, 4, 6, 4, 2}};
    for (int i = 0; i < kTimeSignatureCount; ++i) {
        assert(stepsPerBar(i) == perBar[static_cast<std::size_t>(i)]);
        assert(stepsPerBeat(i) == perBeat[static_cast<std::size_t>(i)]);
    }
    assert(stepStrength(0, 0) == 3 && stepStrength(4, 0) == 2
           && stepStrength(2, 0) == 1 && stepStrength(1, 0) == 0);
    assert(std::fabs(stepSeconds(120.0) - 0.125) < 1.0e-12);
}

void testSettingsAndParameters()
{
    Settings input = defaultSettings();
    input.mode = Mode::bass;
    input.octave = 99;
    input.bars = 0;
    input.tempo = 1.0;
    input.crossoverRate = 2.0;
    input.mutationRate = -1.0;
    input.populationSize = 999;
    input.root = 40;
    input.scale = static_cast<ScaleId>(99);
    input.timeSignature = 99;
    input.midiChannel = 99;
    input.clockSource = static_cast<ClockSource>(99);

    const Settings valid = validateSettings(input);
    assert(valid.mode == Mode::bass);
    assert(valid.octave == 7);
    assert(valid.bars == 1);
    assert(valid.tempo == 40.0);
    assert(valid.crossoverRate == 1.0);
    assert(valid.mutationRate == 0.0);
    assert(valid.populationSize == 50);
    assert(valid.root == 11);
    assert(valid.scale == ScaleId::chromatic);
    assert(valid.timeSignature == kTimeSignatureCount - 1);
    assert(valid.midiChannel == 16);
    assert(valid.clockSource == ClockSource::host);

    const Params params = paramsFromSettings(valid);
    const Settings roundTrip = settingsFromParams(params);
    assert(roundTrip.mode == valid.mode && roundTrip.root == valid.root && roundTrip.scale == valid.scale
           && roundTrip.octave == valid.octave && roundTrip.bars == valid.bars
           && roundTrip.timeSignature == valid.timeSignature && roundTrip.tempo == valid.tempo
           && roundTrip.populationSize == valid.populationSize && roundTrip.midiChannel == valid.midiChannel
           && roundTrip.clockSource == valid.clockSource);
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        assert(params[i] >= kParameterSpecs[i].minimum - 0.001f);
        assert(params[i] <= kParameterSpecs[i].maximum + 0.001f);
    }

    Genome genome = createGenome(4);
    genome.kind = {1, 0, 1, 0};
    genome.degree = {2, 0, 7, 0};
    genome.velocity = {2, 0, 2, 0};
    const Settings base = defaultSettings();
    assert(decode(genome, base)[0].midi == 64);  // major second above C4
    Settings changed = base;
    changed.root = 1;
    assert(decode(genome, changed)[0].midi == 65);
    changed = base;
    changed.scale = ScaleId::minor;
    assert(decode(genome, changed)[0].midi == 63);
    changed = base;
    changed.octave = 5;
    assert(decode(genome, changed)[0].midi == 76);
    Genome highDegree = cloneGenome(genome);
    highDegree.degree[0] = 13;
    changed = base;
    changed.mode = Mode::bass;
    assert(decode(highDegree, changed)[0].midi != decode(highDegree, base)[0].midi);
    assert(degreeRange(ScaleId::major, changed.mode) != degreeRange(ScaleId::major, base.mode));

    changed = base;
    changed.bars = 2;
    assert(genomeLength(changed) != genomeLength(base));
    changed = base;
    changed.timeSignature = 2;  // 6/8
    assert(genomeLength(changed) != genomeLength(base));
}

void testSeedReproducibilityAndState()
{
    const Session a = makeSession(12345);
    const Session b = makeSession(12345);
    assert(serializeSession(a) == serializeSession(b));
    assert(a.population.entries[0].genome.kind == b.population.entries[0].genome.kind);
    assert(a.population.rows == b.population.rows);

    Session changed = makeSession(12345);
    changed.rng.next();
    assert(serializeSession(changed) != serializeSession(a));

    Session withSettings = makeSession(12345);
    withSettings.settings.root = 7;
    withSettings.settings.scale = ScaleId::dorian;
    withSettings.settings.midiChannel = 9;
    withSettings.settings.sendClock = true;
    const auto restoredSettings = deserializeSession(serializeSession(withSettings));
    assert(restoredSettings.has_value());
    assert(restoredSettings->settings.root == 7);
    assert(restoredSettings->settings.scale == ScaleId::dorian);
    assert(restoredSettings->settings.midiChannel == 9);
    assert(restoredSettings->settings.sendClock);

    const auto restored = deserializeSession(serializeSession(a));
    assert(restored.has_value());
    assert(serializeSession(*restored) == serializeSession(a));
    assert(restored->rng.state == a.rng.state);
    assert(restored->population.entries.size() == a.population.entries.size());
    assert(!deserializeSession("version=2\nrng=1\n").has_value());
    assert(!deserializeSession("not state").has_value());
}

ParsedAction action(const Action type, const int position = 0, const int rating = 0)
{
    ParsedAction parsed;
    parsed.type = type;
    parsed.position = position;
    parsed.rating = rating;
    return parsed;
}

void testVotesMoreEvolveNewAndReset()
{
    Session session = makeSession(777);
    Engine engine;
    const std::string before = serializeSession(session);
    assert(session.population.rows.size() == 5);

    applyAction(session, engine, action(Action::vote, 0, 1));
    const int ratedIndex = session.population.rows[0];
    assert(session.population.entries[static_cast<std::size_t>(ratedIndex)].rating == 1.0);
    assert(session.population.entries[static_cast<std::size_t>(ratedIndex)].voted);
    assert(!session.population.entries[static_cast<std::size_t>(ratedIndex)].carried);
    assert(statusView(session, engine).rated == 1);
    assert(statusView(session, engine).canEvolve);
    assert(serializeSession(session) != before);

    const int round = session.population.round;
    applyAction(session, engine, action(Action::more));
    assert(session.population.round > round);
    assert(session.population.generation == 0);

    applyAction(session, engine, action(Action::evolve));
    assert(session.population.generation == 1);
    assert(session.population.entries.size() == static_cast<std::size_t>(smallSettings().populationSize));
    assert(session.population.archive.size() == 1);
    assert(!session.population.rows.empty());
    assert(session.population.round == 1);

    applyAction(session, engine, action(Action::newPopulation));
    assert(session.population.generation == 0);
    assert(session.population.archive.empty());
    assert(session.population.ratedCount() == 0);

    session.settings.tempo = 999.0;
    applyAction(session, engine, action(Action::resetAll));
    assert(session.settings.tempo == defaultSettings().tempo);
    assert(session.population.generation == 0);
    const Session fresh;
    assert(serializeSession(session) == serializeSession(fresh));
}

void testRatesChannelTempoAndClockOutputHaveEffects()
{
    Settings settings = smallSettings();
    settings.bars = 1;
    settings.crossoverRate = 0.0;
    settings.mutationRate = 0.0;

    Session noCrossover = makeSession(5150, settings);
    noCrossover.population.rate(noCrossover.population.rows[0], 1.0);
    Context noCrossoverCtx = makeContext(noCrossover.settings, noCrossover.rng);
    noCrossover.population.evolve(noCrossoverCtx);

    Session withCrossover = makeSession(5150, settings);
    withCrossover.settings.crossoverRate = 1.0;
    withCrossover.population.rate(withCrossover.population.rows[0], 1.0);
    Context withCrossoverCtx = makeContext(withCrossover.settings, withCrossover.rng);
    withCrossover.population.evolve(withCrossoverCtx);
    assert(serializeSession(noCrossover) != serializeSession(withCrossover));

    Session noMutation = makeSession(5151, settings);
    noMutation.population.rate(noMutation.population.rows[0], 1.0);
    Context noMutationCtx = makeContext(noMutation.settings, noMutation.rng);
    noMutation.population.evolve(noMutationCtx);

    Session withMutation = makeSession(5151, settings);
    withMutation.settings.mutationRate = 1.0;
    withMutation.population.rate(withMutation.population.rows[0], 1.0);
    Context withMutationCtx = makeContext(withMutation.settings, withMutation.rng);
    withMutation.population.evolve(withMutationCtx);
    assert(serializeSession(noMutation) != serializeSession(withMutation));

    Session small = makeSession(5152, settings);
    Session large = makeSession(5152, settings);
    large.settings.populationSize = 20;
    Context largeCtx = makeContext(large.settings, large.rng);
    large.population = Population::create(largeCtx);
    assert(large.population.entries.size() > small.population.entries.size());

    Settings slow = settings;
    Settings fast = settings;
    fast.tempo = 240.0;
    Session slowSession = makeSession(5153, slow);
    Session fastSession = makeSession(5153, fast);
    Engine slowEngine, fastEngine;
    requestPlay(slowSession, slowEngine, 0);
    requestPlay(fastSession, fastEngine, 0);
    const auto slowNotes = noteOns(runBlocks(slowSession, slowEngine, 48000, 4800, Transport {}));
    const auto fastNotes = noteOns(runBlocks(fastSession, fastEngine, 48000, 4800, Transport {}));
    assert(!slowNotes.empty() && slowNotes != fastNotes);

    Settings channelA = settings;
    Settings channelB = settings;
    channelA.midiChannel = 1;
    channelB.midiChannel = 16;
    Session a = makeSession(5154, channelA);
    Session b = makeSession(5154, channelB);
    Engine engineA, engineB;
    requestPlay(a, engineA, 0);
    requestPlay(b, engineB, 0);
    const auto notesA = noteOns(runBlocks(a, engineA, 96000, 4800, Transport {}));
    const auto notesB = noteOns(runBlocks(b, engineB, 96000, 4800, Transport {}));
    assert(!notesA.empty());
    for (const Event& event : notesA) assert(event.status == 0x90);
    for (const Event& event : notesB) assert(event.status == 0x9f);

    Settings silentClock = settings;
    silentClock.sendClock = false;
    Session silent = makeSession(5155, silentClock);
    Engine silentEngine;
    requestPlay(silent, silentEngine, 0);
    const auto quiet = runBlocks(silent, silentEngine, 4800, 4800, Transport {});
    assert(!hasStatus(quiet, 0xf8) && !hasStatus(quiet, 0xfa));
}

void testDecode()
{
    Genome genome = createGenome(8);
    genome.kind = {1, 2, 2, 0, 1, 2, 1, 0};  // note, hold, hold, rest, note, hold, note, rest
    genome.degree = {0, 0, 0, 0, 7, 7, 0, 0};
    genome.velocity = {3, 0, 0, 0, 0, 0, 1, 0};

    const Settings settings = defaultSettings();
    const auto notes = decode(genome, settings);
    assert(notes.size() == 3);
    assert(notes[0].startStep == 0 && notes[0].lengthSteps == 3
           && notes[0].midi == 60 && notes[0].velocity == 120);
    assert(notes[1].startStep == 4 && notes[1].lengthSteps == 2
           && notes[1].midi == 72 && notes[1].velocity == 60);
    assert(notes[2].startStep == 6 && notes[2].lengthSteps == 1
           && notes[2].midi == 60 && notes[2].velocity == 80);

    genome.degree[0] = 255;  // clamped to the last available degree
    const auto clamped = decode(genome, settings);
    assert(clamped[0].midi <= 83 && clamped[0].midi >= 60);
}

void testInternalPlaybackIsBlockSizeIndependent()
{
    Settings settings = smallSettings();
    settings.clockSource = ClockSource::internal;
    Session a = makeSession(4242, settings);
    Session b = makeSession(4242, settings);
    Engine engineA, engineB;
    requestPlay(a, engineA, 0);
    requestPlay(b, engineB, 0);

    const Transport idle;
    const auto large = runBlocks(a, engineA, 220000, 4800, idle);
    const auto small = runBlocks(b, engineB, 220000, 997, idle);
    assert(noteOns(large) == noteOns(small));
    assert(!noteOns(large).empty());
    assert(hasStatus(large, 0x80));
    assert(hasCc123(large));
    const auto played = statusView(a, engineA);
    assert(played.lastNote == static_cast<int>(noteOns(large).back().d1));
    assert(played.lastVelocity > 0 && played.lastVelocity <= 127);

    for (std::size_t i = 1; i < large.size(); ++i) {
        assert(large[i].frame >= large[i - 1].frame);
        if (large[i].frame == large[i - 1].frame) {
            const int before = (large[i - 1].status & 0xf0) == 0x80 ? 0 : 1;
            const int after = (large[i].status & 0xf0) == 0x80 ? 0 : 1;
            assert(before <= after);
        }
    }
    assert(engineA.running == false);
    assert(statusView(a, engineA).playing == -1);
}

void testLoopRepeatsThePhrase()
{
    Settings settings = smallSettings();
    settings.bars = 1;
    settings.loop = true;
    Session session = makeSession(99, settings);
    Engine engine;
    requestPlay(session, engine, 0);

    const std::uint64_t barFrames = 96000;  // 16 sixteenths at 120 bpm, 48 kHz
    const auto events = runBlocks(session, engine, barFrames * 2, 4800, Transport {});
    const auto allNotes = noteOns(events);
    std::vector<Event> firstBar, secondBar;
    for (const Event& event : allNotes) {
        if (event.frame < barFrames)
            firstBar.push_back(event);
        else if (event.frame < barFrames * 2) {
            Event shifted = event;
            shifted.frame -= barFrames;
            secondBar.push_back(shifted);
        }
    }
    assert(!firstBar.empty());
    assert(firstBar == secondBar);
    assert(engine.running);
}

void testOutgoingMidiClock()
{
    Settings settings = smallSettings();
    settings.bars = 1;
    settings.sendClock = true;
    Session session = makeSession(11, settings);
    Engine engine;
    requestPlay(session, engine, 0);

    const auto first = runBlocks(session, engine, 4800, 4800, Transport {});
    assert(hasStatus(first, 0xfa));
    const std::array<std::uint64_t, 4> tickFrames {{1000, 2000, 3000, 4000}};
    for (const std::uint64_t frame : tickFrames) {
        assert(std::any_of(first.begin(), first.end(), [frame](const Event& event) {
            return event.frame == frame && event.status == 0xf8;
        }));
    }

    const auto rest = runBlocks(session, engine, 95200, 4800, Transport {});
    assert(hasStatus(rest, 0xfc));
    const auto status = statusView(session, engine);
    assert(status.clockTick >= 0 && status.clockTick < kMidiClockPpq);
    assert(status.sendClock);
}

void testIncomingMidiClockStartStopAndContinue()
{
    Settings settings = smallSettings();
    settings.bars = 1;
    settings.clockSource = ClockSource::midi;
    Session session = makeSession(21, settings);
    Engine engine;
    requestPlay(session, engine, 0);

    std::vector<MidiEvent> inputs {inputEvent(0, 0xfa)};
    for (std::uint32_t i = 1; i < 48; ++i)
        inputs.push_back(inputEvent(static_cast<std::uint32_t>(i) * 1000, 0xf8));
    const auto clocked = runBlocks(session, engine, 48000, 48000,
                                   [](std::uint64_t, std::uint32_t) { return Transport {}; },
                                   [&inputs](std::uint64_t, std::uint32_t) { return inputs; });
    assert(!noteOns(clocked).empty());
    const auto running = statusView(session, engine);
    assert(running.clockRunning);
    assert(std::fabs(running.clockBpm - 120.0) < 0.5);
    assert(running.clockTick >= 0 && running.clockTick < kMidiClockPpq);

    const std::vector<MidiEvent> stop {inputEvent(0, 0xfc)};
    const auto stopped = runBlocks(session, engine, 1000, 1000,
                                   [](std::uint64_t, std::uint32_t) { return Transport {}; },
                                   [&stop](std::uint64_t, std::uint32_t) { return stop; });
    assert(hasCc123(stopped));
    assert(!statusView(session, engine).clockRunning);
    assert(engine.selectedRow == 0);

    std::vector<MidiEvent> resumed {inputEvent(0, 0xfb)};
    for (std::uint32_t i = 1; i < 12; ++i)
        resumed.push_back(inputEvent(static_cast<std::uint32_t>(i) * 1000, 0xf8));
    const auto continued = runBlocks(session, engine, 12000, 12000,
                                     [](std::uint64_t, std::uint32_t) { return Transport {}; },
                                     [&resumed](std::uint64_t, std::uint32_t) { return resumed; });
    assert(hasStatus(continued, 0xfa) == false);
    assert(!noteOns(continued).empty() || hasCc123(continued));
}

void testHostTransportMatchesInternalAndRestarts()
{
    Settings settings = smallSettings();
    settings.bars = 1;
    settings.clockSource = ClockSource::internal;
    Session internalSession = makeSession(31337, settings);
    Engine internalEngine;
    requestPlay(internalSession, internalEngine, 0);
    const auto internalNotes = noteOns(runBlocks(internalSession, internalEngine, 192000, 4800, Transport {}));

    settings.clockSource = ClockSource::host;
    Session hostSession = makeSession(31337, settings);
    Engine hostEngine;
    requestPlay(hostSession, hostEngine, 0);
    const auto hostEvents = runBlocks(hostSession, hostEngine, 192000, 4800,
                                      [](std::uint64_t pos, std::uint32_t) { return hostAt(pos); },
                                      [](std::uint64_t, std::uint32_t) { return std::vector<MidiEvent> {}; });
    assert(!internalNotes.empty());
    assert(noteOns(hostEvents) == internalNotes);
    assert(!hostEngine.running);  // a non-looping row must not restart just because the host keeps playing

    requestPlay(hostSession, hostEngine, 0);
    const auto resumedByButton = runBlocks(hostSession, hostEngine, 4800, 4800,
                                           [](std::uint64_t pos, std::uint32_t) { return hostAt(pos); },
                                           [](std::uint64_t, std::uint32_t) { return std::vector<MidiEvent> {}; });
    assert(!noteOns(resumedByButton).empty());

    const auto stopped = runBlocks(hostSession, hostEngine, 4800, 4800,
                                   [](std::uint64_t pos, std::uint32_t) { return hostAt(pos, 120.0, false); },
                                   [](std::uint64_t, std::uint32_t) { return std::vector<MidiEvent> {}; });
    assert(hasCc123(stopped));
    assert(!statusView(hostSession, hostEngine).clockRunning);

    const auto restarted = runBlocks(hostSession, hostEngine, 4800, 4800,
                                     [](std::uint64_t pos, std::uint32_t) { return hostAt(pos); },
                                     [](std::uint64_t, std::uint32_t) { return std::vector<MidiEvent> {}; });
    assert(!noteOns(restarted).empty());
    assert(statusView(hostSession, hostEngine).clockRunning);
}

}  // namespace

int main()
{
    testRngScalesAndTiming();
    testSettingsAndParameters();
    testSeedReproducibilityAndState();
    testVotesMoreEvolveNewAndReset();
    testRatesChannelTempoAndClockOutputHaveEffects();
    testDecode();
    testInternalPlaybackIsBlockSizeIndependent();
    testLoopRepeatsThePhrase();
    testOutgoingMidiClock();
    testIncomingMidiClockStartStopAndContinue();
    testHostTransportMatchesInternalAndRestarts();
    return 0;
}
