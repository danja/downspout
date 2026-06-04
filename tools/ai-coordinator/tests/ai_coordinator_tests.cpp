#include "ai_coordinator.hpp"

#include "sidecar_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<unsigned char> readBytes(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(input)),
                                      std::istreambuf_iterator<char>());
}

void testParseTuneState()
{
    const std::string json = R"json({
      "key": "D",
      "scale": "dorian",
      "genre": "jazz",
      "tempo": 142,
      "bars": 8,
      "beats_per_bar": 4,
      "channel": 3,
      "register_low": 57,
      "register_high": 88,
      "density": 0.7,
      "risk": 0.45,
      "seed": 1234
    })json";

    const std::optional<downspout::ai_coordinator::TuneState> state =
        downspout::ai_coordinator::parseTuneStateJson(json);
    require(state.has_value(), "state JSON should parse");
    require(state->key == 2, "key name should map to semitone");
    require(state->scale == "dorian", "scale should parse");
    require(state->tempo == 142, "tempo should parse");
    require(state->bars == 8, "bars should parse");
    require(state->channel == 3, "channel should parse");
    require(state->registerLow == 57 && state->registerHigh == 88, "register should parse");
}

void testGeneratePhraseValidates()
{
    downspout::ai_coordinator::TuneState state {};
    state.key = 9;
    state.scale = "minor";
    state.bars = 4;
    state.beatsPerBar = 4;
    state.registerLow = 55;
    state.registerHigh = 84;
    state.density = 0.65f;
    state.risk = 0.35f;
    state.seed = 99;

    const downspout::sidecar::Phrase phrase = downspout::ai_coordinator::generateSoloPhrase(state);
    const downspout::sidecar::Controls controls = downspout::ai_coordinator::controlsFromTuneState(state);
    require(phrase.eventCount > 0, "generated phrase should contain events");
    require(downspout::sidecar::validatePhrase(phrase, controls).valid, "generated phrase should validate");
}

void testWriteMidiFile()
{
    downspout::ai_coordinator::TuneState state {};
    state.seed = 12;
    const downspout::sidecar::Phrase phrase = downspout::ai_coordinator::generateSoloPhrase(state);
    const std::string path = "/tmp/downspout-ai-coordinator-test.mid";
    require(downspout::ai_coordinator::writeMidiFile(path, phrase, 120, 1, 4), "MIDI file should write");

    const std::vector<unsigned char> bytes = readBytes(path);
    require(bytes.size() > 32, "MIDI file should not be empty");
    require(bytes[0] == 'M' && bytes[1] == 'T' && bytes[2] == 'h' && bytes[3] == 'd', "MIDI header should be present");
    require(bytes[14] == 'M' && bytes[15] == 'T' && bytes[16] == 'r' && bytes[17] == 'k', "MIDI track header should be present");
    std::remove(path.c_str());
}

void testAnalyzeMidiFileToTuneState()
{
    downspout::ai_coordinator::TuneState source {};
    source.key = 2;
    source.scale = "dorian";
    source.bars = 2;
    source.registerLow = 48;
    source.registerHigh = 72;
    source.seed = 44;

    const downspout::sidecar::Phrase phrase = downspout::ai_coordinator::generateSoloPhrase(source);
    const std::string path = "/tmp/downspout-ai-coordinator-analyze.mid";
    require(downspout::ai_coordinator::writeMidiFile(path, phrase, 120, 1, 4), "analysis fixture MIDI should write");

    const std::optional<downspout::ai_coordinator::TuneState> analyzed =
        downspout::ai_coordinator::analyzeMidiFileToTuneState(path);
    require(analyzed.has_value(), "MIDI context should analyze");
    require(analyzed->hasMidiContext, "analyzed state should mark MIDI context");
    require(analyzed->guidePitchClassCount > 0, "analyzed state should include guide pitch classes");
    require(analyzed->registerHigh >= analyzed->registerLow, "analyzed register should be ordered");
    require(analyzed->density > 0.0f, "analyzed density should be nonzero");
    std::remove(path.c_str());
}

void testSerializeTuneStateRoundTrip()
{
    downspout::ai_coordinator::TuneState state {};
    state.key = 5;
    state.scale = "mixolydian";
    state.hasMidiContext = true;
    state.guidePitchClassCount = 3;
    state.guidePitchClasses[0] = 5;
    state.guidePitchClasses[1] = 9;
    state.guidePitchClasses[2] = 0;

    const std::string json = downspout::ai_coordinator::serializeTuneStateJson(state);
    const auto roundTrip = downspout::ai_coordinator::parseTuneStateJson(json);
    require(roundTrip.has_value(), "serialized tune state should parse");
    require(roundTrip->key == 5, "serialized key should survive");
    require(roundTrip->scale == "mixolydian", "serialized scale should survive");
    require(roundTrip->guidePitchClassCount == 3, "guide pitch classes should survive");
    require(roundTrip->guidePitchClasses[1] == 9, "guide pitch class values should survive");
}

void testPhraseResponseValidation()
{
    const std::string json = R"json({
      "version": 1,
      "bars": 2,
      "beats_per_bar": 4,
      "events": [
        {"beat": 0.0, "duration": 0.5, "note": 60, "velocity": 90},
        {"beat": 1.0, "duration": 0.5, "note": 64, "velocity": 88},
        {"beat": 2.0, "duration": 0.5, "note": 67, "velocity": 92}
      ]
    })json";

    const auto phrase = downspout::ai_coordinator::parsePhraseResponseJson(json);
    require(phrase.has_value(), "valid phrase response should parse");
    require(phrase->eventCount == 3, "phrase response event count should parse");
    require(phrase->events[1].note == 64, "phrase response note should parse");

    const std::string overlap = R"json({
      "version": 1,
      "bars": 2,
      "events": [
        {"beat": 0.0, "duration": 1.0, "note": 60, "velocity": 90},
        {"beat": 0.5, "duration": 0.5, "note": 64, "velocity": 88}
      ]
    })json";
    require(!downspout::ai_coordinator::parsePhraseResponseJson(overlap).has_value(),
            "overlapping phrase response should be rejected");
}

void testBuildSoloRequest()
{
    downspout::ai_coordinator::TuneState state {};
    state.key = 2;
    state.scale = "dorian";
    state.genre = "midi";
    state.hasMidiContext = true;
    state.guidePitchClassCount = 2;
    state.guidePitchClasses[0] = 2;
    state.guidePitchClasses[1] = 7;

    const std::string request = downspout::ai_coordinator::buildSoloRequestJson(state);
    require(request.find("\"protocol\": \"downspout.ai_solo.v1\"") != std::string::npos,
            "request should include protocol");
    require(request.find("\"tune_state\"") != std::string::npos,
            "request should include tune state");
    require(request.find("\"guide_pitch_classes\":[2,7]") != std::string::npos,
            "request should include guide pitch classes");
    require(request.find("\"response_schema\"") != std::string::npos,
            "request should include response schema");
    require(request.find("Do not return a phrase consisting of one repeated note") != std::string::npos,
            "request should reject one-note repeated phrases");
    require(request.find("\"quality_constraints\"") != std::string::npos,
            "request should include quality constraints");
}

int distinctNotes(const downspout::sidecar::Phrase& phrase)
{
    std::array<bool, 128> used {};
    int count = 0;
    for (int i = 0; i < phrase.eventCount; ++i) {
        const int note = std::max(0, std::min(127, phrase.events[static_cast<std::size_t>(i)].note));
        if (!used[static_cast<std::size_t>(note)]) {
            used[static_cast<std::size_t>(note)] = true;
            ++count;
        }
    }
    return count;
}

void testConstrainPhraseToTuneState()
{
    downspout::ai_coordinator::TuneState state {};
    state.bars = 1;
    state.beatsPerBar = 4;
    state.registerLow = 60;
    state.registerHigh = 72;

    downspout::sidecar::Phrase phrase {};
    phrase.bars = 4;
    phrase.beatsPerBar = 4;
    phrase.eventCount = 3;
    phrase.events[0] = {0.0f, 1.0f, 58, 90};
    phrase.events[1] = {0.5f, 1.0f, 76, 88};
    phrase.events[2] = {4.5f, 1.0f, 64, 82};

    const downspout::sidecar::Phrase constrained =
        downspout::ai_coordinator::constrainPhraseToTuneState(phrase, state);
    const downspout::sidecar::Controls controls = downspout::ai_coordinator::controlsFromTuneState(state);
    require(downspout::sidecar::validatePhrase(constrained, controls).valid,
            "constrained phrase should validate");
    require(constrained.eventCount == 2, "events outside the phrase should be dropped");
    require(constrained.events[0].note >= 60 && constrained.events[0].note <= 72,
            "low note should be folded into register");
    require(constrained.events[1].beat >= constrained.events[0].beat + constrained.events[0].duration,
            "overlap should be removed");
}

void testConstrainRepairsCollapsedPitchOutput()
{
    downspout::ai_coordinator::TuneState state {};
    state.key = 0;
    state.scale = "dorian";
    state.bars = 2;
    state.beatsPerBar = 4;
    state.registerLow = 60;
    state.registerHigh = 84;
    state.density = 0.75f;
    state.risk = 0.35f;
    state.seed = 77;

    downspout::sidecar::Phrase phrase {};
    phrase.bars = 2;
    phrase.beatsPerBar = 4;
    phrase.eventCount = 8;
    for (int i = 0; i < phrase.eventCount; ++i)
        phrase.events[static_cast<std::size_t>(i)] = {static_cast<float>(i), 0.5f, 64, 90};

    const downspout::sidecar::Phrase repaired =
        downspout::ai_coordinator::constrainPhraseToTuneState(phrase, state);
    const downspout::sidecar::Controls controls = downspout::ai_coordinator::controlsFromTuneState(state);
    require(downspout::sidecar::validatePhrase(repaired, controls).valid,
            "repaired collapsed phrase should validate");
    require(distinctNotes(repaired) >= 4,
            "collapsed one-note phrase should be repaired to multiple pitches");
}

}  // namespace

int main()
{
    testParseTuneState();
    testGeneratePhraseValidates();
    testWriteMidiFile();
    testAnalyzeMidiFileToTuneState();
    testSerializeTuneStateRoundTrip();
    testPhraseResponseValidation();
    testBuildSoloRequest();
    testConstrainPhraseToTuneState();
    testConstrainRepairsCollapsedPitchOutput();
    std::cout << "ai coordinator tests passed\n";
    return 0;
}
