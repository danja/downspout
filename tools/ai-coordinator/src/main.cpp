#include "ai_coordinator.hpp"

#include "sidecar_protocol.hpp"
#include "sidecar_serialization.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void printUsage()
{
    std::cout
        << "usage:\n"
        << "  downspout-ai-coordinator generate state.json --out solo.mid [--phrase phrase.txt]\n"
        << "  downspout-ai-coordinator generate-from-midi source.mid --out solo.mid [--phrase phrase.txt]\n"
        << "  downspout-ai-coordinator analyze-midi source.mid --out state.json\n"
        << "  downspout-ai-coordinator build-request state.json --out request.json\n"
        << "  downspout-ai-coordinator build-request-from-midi source.mid --out request.json\n"
        << "  downspout-ai-coordinator render-response response.json --out solo.mid [--phrase phrase.txt] [--state state.json]\n"
        << "  downspout-ai-coordinator openai state.json --out solo.mid [--phrase phrase.txt] [--raw raw.json] [--debug]\n"
        << "  downspout-ai-coordinator openai-from-midi source.mid --out solo.mid [--phrase phrase.txt] [--raw raw.json] [--debug]\n"
        << "  downspout-ai-coordinator health\n"
        << "  downspout-ai-coordinator serve [--port 37371] [--debug]\n";
}

void enableDebug()
{
    setenv("DOWNSPOUT_AI_DEBUG", "1", 1);
}

}  // namespace

int main(const int argc, char** argv)
{
    downspout::ai_coordinator::loadCoordinatorEnvironment();

    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        printUsage();
        return argc < 2 ? 1 : 0;
    }

    const std::string command = argv[1];
    if (command == "health") {
        std::cout << "downspout-ai-coordinator: ok\n";
        std::cout << "OPENAI_API_KEY: " << (downspout::ai_coordinator::hasOpenAiApiKey() ? "configured" : "missing") << '\n';
        std::cout << "live server: run `downspout-ai-coordinator serve --port 37371` for Sidecar Server mode\n";
        return downspout::ai_coordinator::hasOpenAiApiKey() ? 0 : 2;
    }

    if (command == "serve") {
        int port = 37371;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else if (arg == "--debug") {
                enableDebug();
            } else {
                std::cerr << "unknown or incomplete argument: " << arg << '\n';
                return 1;
            }
        }
        return downspout::ai_coordinator::runLiveServer(port);
    }

    if (command != "generate" && command != "generate-from-midi" &&
        command != "analyze-midi" && command != "build-request" &&
        command != "build-request-from-midi" && command != "render-response" &&
        command != "openai" && command != "openai-from-midi") {
        std::cerr << "unknown command: " << command << '\n';
        printUsage();
        return 1;
    }

    if (argc < 5) {
        printUsage();
        return 1;
    }

    const std::string inputPath = argv[2];
    std::string outputPath;
    std::string phrasePath;
    std::string statePath;
    std::string rawPath;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--phrase" && i + 1 < argc) {
            phrasePath = argv[++i];
        } else if (arg == "--state" && i + 1 < argc) {
            statePath = argv[++i];
        } else if (arg == "--raw" && i + 1 < argc) {
            rawPath = argv[++i];
        } else if (arg == "--debug") {
            enableDebug();
        } else {
            std::cerr << "unknown or incomplete argument: " << arg << '\n';
            return 1;
        }
    }

    if (outputPath.empty()) {
        std::cerr << "missing --out path\n";
        return 1;
    }

    if (command == "analyze-midi") {
        const auto state = downspout::ai_coordinator::analyzeMidiFileToTuneState(inputPath);
        if (!state.has_value()) {
            std::cerr << "could not analyze MIDI input: " << inputPath << '\n';
            return 1;
        }
        std::ofstream out(outputPath);
        if (!out) {
            std::cerr << "could not write state JSON: " << outputPath << '\n';
            return 1;
        }
        out << downspout::ai_coordinator::serializeTuneStateJson(*state);
        std::cout << "wrote " << outputPath << '\n';
        return 0;
    }

    if (command == "build-request" || command == "build-request-from-midi") {
        const auto state = command == "build-request"
            ? downspout::ai_coordinator::loadTuneStateJson(inputPath)
            : downspout::ai_coordinator::analyzeMidiFileToTuneState(inputPath);
        if (!state.has_value()) {
            std::cerr << "could not build request from input: " << inputPath << '\n';
            return 1;
        }
        std::ofstream out(outputPath);
        if (!out) {
            std::cerr << "could not write request JSON: " << outputPath << '\n';
            return 1;
        }
        out << downspout::ai_coordinator::buildSoloRequestJson(*state);
        std::cout << "wrote " << outputPath << '\n';
        return 0;
    }

    if (command == "render-response") {
        downspout::ai_coordinator::TuneState state {};
        state.registerLow = 0;
        state.registerHigh = 127;
        if (!statePath.empty()) {
            const auto loadedState = downspout::ai_coordinator::loadTuneStateJson(statePath);
            if (!loadedState.has_value()) {
                std::cerr << "could not read state JSON: " << statePath << '\n';
                return 1;
            }
            state = *loadedState;
        }

        const auto phrase = downspout::ai_coordinator::loadPhraseResponseJson(inputPath);
        if (!phrase.has_value()) {
            std::cerr << "could not read validated phrase response: " << inputPath << '\n';
            return 1;
        }
        const downspout::sidecar::Controls controls = downspout::ai_coordinator::controlsFromTuneState(state);
        if (!downspout::sidecar::validatePhrase(*phrase, controls).valid) {
            std::cerr << "phrase response is outside the requested state constraints\n";
            return 1;
        }
        if (!downspout::ai_coordinator::writeMidiFile(outputPath, *phrase, state.tempo, state.channel, phrase->beatsPerBar)) {
            std::cerr << "could not write MIDI file: " << outputPath << '\n';
            return 1;
        }
        if (!phrasePath.empty()) {
            std::ofstream phraseOutput(phrasePath);
            if (!phraseOutput) {
                std::cerr << "could not write phrase file: " << phrasePath << '\n';
                return 1;
            }
            phraseOutput << downspout::sidecar::serializePhrase(*phrase);
        }
        std::cout << "wrote " << outputPath << " with " << phrase->eventCount << " response events\n";
        return 0;
    }

    if (command == "openai" || command == "openai-from-midi") {
        if (!downspout::ai_coordinator::hasOpenAiApiKey()) {
            std::cerr << "OPENAI_API_KEY is not configured. Put it in .env or export it before using "
                      << command << ".\n";
            return 2;
        }

        const auto state = command == "openai"
            ? downspout::ai_coordinator::loadTuneStateJson(inputPath)
            : downspout::ai_coordinator::analyzeMidiFileToTuneState(inputPath);
        if (!state.has_value()) {
            std::cerr << "could not build OpenAI request from input: " << inputPath << '\n';
            return 1;
        }

        std::string rawResponse;
        std::string extractedText;
        const auto phrase = downspout::ai_coordinator::requestOpenAiPhrase(*state, &rawResponse, &extractedText);
        if (!rawPath.empty()) {
            std::ofstream raw(rawPath);
            if (raw)
                raw << rawResponse;
        }
        if (!phrase.has_value()) {
            std::cerr << "OpenAI response did not produce a valid Sidecar phrase\n";
            return 1;
        }

        const downspout::sidecar::Phrase constrainedPhrase =
            downspout::ai_coordinator::constrainPhraseToTuneState(*phrase, *state);
        const downspout::sidecar::Controls controls = downspout::ai_coordinator::controlsFromTuneState(*state);
        if (!downspout::sidecar::validatePhrase(constrainedPhrase, controls).valid) {
            std::cerr << "OpenAI phrase is outside the requested state constraints\n";
            return 1;
        }

        if (!downspout::ai_coordinator::writeMidiFile(outputPath, constrainedPhrase, state->tempo, state->channel, constrainedPhrase.beatsPerBar)) {
            std::cerr << "could not write MIDI file: " << outputPath << '\n';
            return 1;
        }
        if (!phrasePath.empty()) {
            std::ofstream phraseOutput(phrasePath);
            if (!phraseOutput) {
                std::cerr << "could not write phrase file: " << phrasePath << '\n';
                return 1;
            }
            phraseOutput << downspout::sidecar::serializePhrase(constrainedPhrase);
        }
        std::cout << "wrote " << outputPath << " with " << constrainedPhrase.eventCount << " OpenAI events\n";
        return 0;
    }

    const auto state = command == "generate"
        ? downspout::ai_coordinator::loadTuneStateJson(inputPath)
        : downspout::ai_coordinator::analyzeMidiFileToTuneState(inputPath);
    if (!state.has_value()) {
        std::cerr << "could not read input: " << inputPath << '\n';
        return 1;
    }

    const downspout::sidecar::Phrase phrase = downspout::ai_coordinator::generateSoloPhrase(*state);
    if (!downspout::ai_coordinator::writeMidiFile(outputPath, phrase, state->tempo, state->channel, state->beatsPerBar)) {
        std::cerr << "could not write MIDI file: " << outputPath << '\n';
        return 1;
    }

    if (!phrasePath.empty()) {
        std::ofstream phraseOutput(phrasePath);
        if (!phraseOutput) {
            std::cerr << "could not write phrase file: " << phrasePath << '\n';
            return 1;
        }
        phraseOutput << downspout::sidecar::serializePhrase(phrase);
    }

    std::cout << "wrote " << outputPath << " with " << phrase.eventCount << " events\n";
    return 0;
}
