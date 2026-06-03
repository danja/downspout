#include "sidecar_engine.hpp"
#include "sidecar_protocol.hpp"
#include "sidecar_serialization.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace sidecar = downspout::sidecar;

namespace {

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

sidecar::Phrase oneNotePhrase(const int note, const float beat, const float duration)
{
    sidecar::Phrase phrase {};
    phrase.version = sidecar::kPhraseStateVersion;
    phrase.bars = 1;
    phrase.beatsPerBar = 4;
    phrase.eventCount = 1;
    phrase.events[0].beat = beat;
    phrase.events[0].duration = duration;
    phrase.events[0].note = note;
    phrase.events[0].velocity = 90;
    return phrase;
}

void testValidationRejectsInvalidPhrase()
{
    sidecar::Controls controls {};

    sidecar::Phrase overlapping {};
    overlapping.version = sidecar::kPhraseStateVersion;
    overlapping.bars = 1;
    overlapping.beatsPerBar = 4;
    overlapping.eventCount = 2;
    overlapping.events[0] = {0.0f, 1.0f, 60, 90};
    overlapping.events[1] = {0.5f, 0.5f, 62, 90};
    require(!sidecar::validatePhrase(overlapping, controls).valid, "overlapping events should be rejected");

    sidecar::Phrase outOfRange = oneNotePhrase(100, 0.0f, 0.5f);
    require(!sidecar::validatePhrase(outOfRange, controls).valid, "notes outside the selected register should be rejected");
}

void testFallbackPhraseIsValidAndDeterministic()
{
    sidecar::Controls controls {};
    controls.bars = 4;
    controls.reg = sidecar::RegisterId::mid;
    controls.density = 0.7f;
    controls.risk = 0.2f;

    const sidecar::Phrase first = sidecar::makeFallbackPhrase(controls, 42u);
    const sidecar::Phrase second = sidecar::makeFallbackPhrase(controls, 42u);

    require(first.eventCount > 0, "fallback phrase should contain events");
    require(sidecar::validatePhrase(first, controls).valid, "fallback phrase should validate");
    require(first.eventCount == second.eventCount, "fallback phrase should be deterministic for a seed");
    require(std::fabs(first.events[0].beat - second.events[0].beat) < 0.0001f, "fallback event beat should be deterministic");
    require(first.events[0].note == second.events[0].note, "fallback event note should be deterministic");
}

void testSerializationRoundTrip()
{
    sidecar::Controls controls {};
    controls.reg = sidecar::RegisterId::custom;
    controls.registerLow = 0;
    controls.registerHigh = 127;

    sidecar::Phrase phrase = sidecar::makeFallbackPhrase(controls, 77u);
    phrase.events[0].note = 110;

    const std::string serialized = sidecar::serializePhrase(phrase);
    const std::optional<sidecar::Phrase> restored = sidecar::deserializePhrase(serialized);
    require(restored.has_value(), "serialized phrase should deserialize");
    require(restored->eventCount == phrase.eventCount, "round trip should preserve event count");
    require(restored->events[0].note == 110, "deserializer should allow full MIDI note range");
}

void testJsonPhraseDeserialization()
{
    const std::string json =
        "{\"version\":1,\"bars\":2,\"beats_per_bar\":4,\"events\":["
        "{\"beat\":0,\"duration\":0.5,\"note\":60,\"velocity\":90},"
        "{\"beat\":1.5,\"duration\":0.25,\"note\":64,\"velocity\":80}"
        "]}";

    const std::optional<sidecar::Phrase> restored = sidecar::deserializePhraseJson(json);
    require(restored.has_value(), "JSON phrase response should deserialize");
    require(restored->bars == 2, "JSON phrase should preserve bars");
    require(restored->beatsPerBar == 4, "JSON phrase should preserve beats per bar");
    require(restored->eventCount == 2, "JSON phrase should preserve event count");
    require(restored->events[1].note == 64, "JSON phrase should preserve event notes");
    require(!sidecar::deserializePhraseJson("{\"events\":[{\"beat\":0}]}").has_value(),
            "malformed JSON phrase response should be rejected");
}

void testPlaybackSchedulesNoteOnAndOff()
{
    sidecar::Controls controls {};
    controls.channel = 2;
    controls.reg = sidecar::RegisterId::custom;
    controls.registerLow = 0;
    controls.registerHigh = 127;

    sidecar::EngineState state {};
    sidecar::activate(state, controls);
    sidecar::setPhrase(state, oneNotePhrase(60, 0.0f, 0.5f));

    sidecar::TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    const sidecar::BlockResult result = sidecar::processBlock(state, controls, transport, 48000u, 48000.0);
    require(result.eventCount == 2, "one phrase note should schedule note-on and note-off");
    require(result.events[0].type == sidecar::MidiEventType::noteOn, "first scheduled event should be note-on");
    require(result.events[0].channel == 1u, "MIDI channel should be zero-based on output");
    require(result.events[0].data1 == 60u, "note-on pitch should match phrase");
    require(result.events[1].type == sidecar::MidiEventType::noteOff, "second scheduled event should be note-off");
    require(result.events[1].frame > result.events[0].frame, "note-off should follow note-on");
}

void testMuteClearsActiveNote()
{
    sidecar::Controls controls {};
    controls.reg = sidecar::RegisterId::custom;
    controls.registerLow = 0;
    controls.registerHigh = 127;

    sidecar::EngineState state {};
    sidecar::activate(state, controls);
    sidecar::setPhrase(state, oneNotePhrase(62, 0.0f, 2.0f));

    sidecar::TransportSnapshot transport {};
    transport.valid = true;
    transport.playing = true;
    transport.bar = 0.0;
    transport.barBeat = 0.0;
    transport.beatsPerBar = 4.0;
    transport.bpm = 120.0;

    sidecar::BlockResult first = sidecar::processBlock(state, controls, transport, 12000u, 48000.0);
    require(first.eventCount == 1, "first block should start the long note");
    require(first.events[0].type == sidecar::MidiEventType::noteOn, "first block should emit note-on");

    controls.mute = true;
    transport.barBeat = 0.5;
    const sidecar::BlockResult muted = sidecar::processBlock(state, controls, transport, 12000u, 48000.0);
    require(muted.eventCount == 1, "mute should clear the active note");
    require(muted.events[0].type == sidecar::MidiEventType::noteOff, "mute should emit note-off");
    require(muted.events[0].frame == 0u, "mute note-off should be immediate");
}

}  // namespace

int main()
{
    testValidationRejectsInvalidPhrase();
    testFallbackPhraseIsValidAndDeterministic();
    testSerializationRoundTrip();
    testJsonPhraseDeserialization();
    testPlaybackSchedulesNoteOnAndOff();
    testMuteClearsActiveNote();
    std::cout << "sidecar core tests passed\n";
    return 0;
}
