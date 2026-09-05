#include "campione_core_types.hpp"
#include "campione_engine.hpp"
#include "campione_pitch_utils.hpp"
#include "campione_sample_loader.hpp"
#include "campione_serialization.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::campione;

// ── Helpers ──────────────────────────────────────────────────────────────────

static SampleZone makeToneZone(int rootNote, int rangeLow, int rangeHigh,
                                double sampleRate = 44100.0, int nFrames = 1024)
{
    SampleZone z;
    z.rootNote  = rootNote;
    z.rangeLow  = rangeLow;
    z.rangeHigh = rangeHigh;
    z.channelCount = 1;
    z.sampleRate   = sampleRate;
    z.data.assign(static_cast<std::size_t>(nFrames), 0.5f);
    return z;
}

static std::vector<float> runBlock(EngineState& state,
                                    const Parameters& params,
                                    const std::vector<SampleZone>& zones,
                                    const std::vector<MidiInputEvent>& midi,
                                    uint32_t frames = 128,
                                    double sr = 44100.0)
{
    std::vector<float> out(frames, 0.0f);
    AudioBlock audio;
    audio.outputs[0]   = out.data();
    audio.outputs[1]   = nullptr;
    audio.channelCount = 1;
    processBlock(state, params, zones, midi.data(), static_cast<uint32_t>(midi.size()),
                 frames, sr, audio);
    return out;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void testNoteOnProducesAudio()
{
    const std::vector<SampleZone> zones = { makeToneZone(60, 0, 127) };
    EngineState state;
    Parameters params;
    activate(state);

    MidiInputEvent noteOn { 0, 3, {0x90, 60, 100, 0} };
    const auto out = runBlock(state, params, zones, {noteOn});

    bool hasAudio = false;
    for (float v : out) if (v > 0.001f) { hasAudio = true; break; }
    assert(hasAudio && "note-on should produce audio");
    std::puts("PASS: note-on produces audio");
}

static void testNoteOffSilencesAudio()
{
    const std::vector<SampleZone> zones = { makeToneZone(60, 0, 127) };
    EngineState state;
    Parameters params;
    activate(state);

    // Start note in first block
    MidiInputEvent noteOn { 0, 3, {0x90, 60, 100, 0} };
    runBlock(state, params, zones, {noteOn});

    // Note-off at start of second block
    MidiInputEvent noteOff { 0, 3, {0x80, 60, 0, 0} };
    const auto out = runBlock(state, params, zones, {noteOff});

    bool hasAudio = false;
    for (float v : out) if (v > 0.001f) { hasAudio = true; break; }
    assert(!hasAudio && "note-off should silence audio");
    std::puts("PASS: note-off silences audio");
}

static void testGapFill()
{
    // Zone covers only C4–B4 (60–71), but we play C3 (48) — gap fill should still produce audio
    const std::vector<SampleZone> zones = { makeToneZone(60, 60, 71) };
    EngineState state;
    Parameters params;
    activate(state);

    MidiInputEvent noteOn { 0, 3, {0x90, 48, 100, 0} };
    const auto out = runBlock(state, params, zones, {noteOn});

    bool hasAudio = false;
    for (float v : out) if (std::abs(v) > 0.0001f) { hasAudio = true; break; }
    assert(hasAudio && "gap-fill should produce audio for notes outside range");
    std::puts("PASS: gap-fill produces audio outside zone range");
}

static void testPolyphonyLimit()
{
    const std::vector<SampleZone> zones = { makeToneZone(60, 0, 127, 44100.0, 65536) };
    EngineState state;
    Parameters params;
    activate(state);

    // Fire more notes than kMaxVoices
    std::vector<MidiInputEvent> midi;
    for (int note = 60; note < 60 + kMaxVoices + 4; ++note)
        midi.push_back({ static_cast<uint32_t>(note - 60), 3, {0x90, static_cast<uint8_t>(note), 100, 0} });

    runBlock(state, params, zones, midi, 128);

    int activeCount = 0;
    for (const Voice& v : state.voices) if (v.active) ++activeCount;
    assert(activeCount <= kMaxVoices && "active voices must not exceed kMaxVoices");
    std::puts("PASS: polyphony capped at kMaxVoices");
}

static void testLoopWrap()
{
    SampleZone z = makeToneZone(60, 0, 127, 44100.0, 256);
    z.loopEnabled    = true;
    z.loopStart      = 50;
    z.loopEnd        = 100;
    z.crossfadeFrames = 0;
    const std::vector<SampleZone> zones = { z };

    EngineState state;
    Parameters params;
    activate(state);

    MidiInputEvent noteOn { 0, 3, {0x90, 60, 100, 0} };
    // Run enough blocks that position would exceed loopEnd many times
    runBlock(state, params, zones, {noteOn}, 128);
    for (int i = 0; i < 20; ++i) runBlock(state, params, zones, {}, 128);

    for (const Voice& v : state.voices) {
        if (!v.active) continue;
        assert(v.position >= 0.0 && "voice position must not go negative");
        assert(v.position < 200.0 && "loop position must stay near loop range");
    }
    std::puts("PASS: loop position stays within loop range");
}

static void testComputePlaybackRate()
{
    SampleZone z;
    z.rootNote   = 60;
    z.sampleRate = 44100.0;

    const double rSame    = computePlaybackRate(z, 60, 44100.0);
    const double rOctaveUp = computePlaybackRate(z, 72, 44100.0);
    const double rOctaveDn = computePlaybackRate(z, 48, 44100.0);

    assert(std::abs(rSame - 1.0)    < 0.001 && "same note = rate 1");
    assert(std::abs(rOctaveUp - 2.0) < 0.001 && "+12 semitones = rate 2");
    assert(std::abs(rOctaveDn - 0.5) < 0.001 && "-12 semitones = rate 0.5");
    std::puts("PASS: computePlaybackRate");
}

static void testSerializationRoundTrip()
{
    SampleZone z;
    z.rootNote        = 48;
    z.rangeLow        = 36;
    z.rangeHigh       = 60;
    z.midiChannel     = 2;
    z.loopEnabled     = true;
    z.loopStart       = 100;
    z.loopEnd         = 1000;
    z.crossfadeFrames = 50;
    z.sourcePath      = "/test/path with spaces/piano.wav";

    const std::string text = serializeZones({z});
    const auto result = deserializeZones(text);
    assert(result.has_value()            && "deserialization must succeed");
    assert(result->size() == 1           && "one zone");
    const SampleZone& r = (*result)[0];
    assert(r.rootNote        == 48       && "rootNote preserved");
    assert(r.rangeLow        == 36       && "rangeLow preserved");
    assert(r.rangeHigh       == 60       && "rangeHigh preserved");
    assert(r.midiChannel     == 2        && "midiChannel preserved");
    assert(r.loopEnabled     == true     && "loopEnabled preserved");
    assert(r.loopStart       == 100u     && "loopStart preserved");
    assert(r.loopEnd         == 1000u    && "loopEnd preserved");
    assert(r.crossfadeFrames == 50u      && "crossfadeFrames preserved");
    assert(r.sourcePath == "/test/path with spaces/piano.wav" && "sourcePath preserved");
    std::puts("PASS: zone serialization round-trip");
}

static void testParamsSerializationRoundTrip()
{
    Parameters p;
    p.masterVolume       = 0.65f;
    p.midiChannel        = 3.0f;
    p.crossfadeDurationMs = 45.0f;
    p.pitchBendRange     = 12.0f;

    const std::string text = serializeParameters(p);
    const auto result = deserializeParameters(text);
    assert(result.has_value()                             && "param deserialization must succeed");
    assert(std::abs(result->masterVolume - 0.65f)  < 0.0001f && "masterVolume");
    assert(std::abs(result->midiChannel  - 3.0f)   < 0.0001f && "midiChannel");
    assert(std::abs(result->crossfadeDurationMs - 45.0f) < 0.0001f && "crossfadeDurationMs");
    assert(std::abs(result->pitchBendRange - 12.0f) < 0.0001f && "pitchBendRange");
    std::puts("PASS: parameters serialization round-trip");
}

static void testEmptyZonesDeserialization()
{
    const auto result = deserializeZones("");
    assert(result.has_value()   && "empty string returns empty list");
    assert(result->empty()      && "should be empty");
    std::puts("PASS: empty zones deserialization");
}

static void testLoadRex2ZonesMissingFile()
{
    std::vector<SampleZone> zones;
    const std::string err = loadRex2Zones("/nonexistent/path/to/file.rx2", zones);
    assert(!err.empty() && "loadRex2Zones must return error for missing file");
    assert(zones.empty() && "no zones should be added on failure");
    std::puts("PASS: loadRex2Zones returns error for missing file");
}

static void testMidiChannelFilter()
{
    const std::vector<SampleZone> zones = { makeToneZone(60, 0, 127, 44100.0, 1024) };
    EngineState state;
    Parameters params;
    params.midiChannel = 2.0f; // only channel 2
    activate(state);

    // Send note-on on channel 3 (0x92 = status), should be filtered out
    MidiInputEvent noteOnCh3 { 0, 3, {0x92, 60, 100, 0} }; // channel 3
    const auto out = runBlock(state, params, zones, {noteOnCh3});

    bool hasAudio = false;
    for (float v : out) if (v > 0.001f) { hasAudio = true; break; }
    assert(!hasAudio && "MIDI channel filter should block note on wrong channel");
    std::puts("PASS: MIDI channel filter");
}

int main()
{
    testNoteOnProducesAudio();
    testNoteOffSilencesAudio();
    testGapFill();
    testPolyphonyLimit();
    testLoopWrap();
    testComputePlaybackRate();
    testSerializationRoundTrip();
    testParamsSerializationRoundTrip();
    testEmptyZonesDeserialization();
    testMidiChannelFilter();
    testLoadRex2ZonesMissingFile();

    std::puts("All campione core tests passed.");
    return 0;
}
