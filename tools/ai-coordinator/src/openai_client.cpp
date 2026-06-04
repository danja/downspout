#include "ai_coordinator.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace downspout::ai_coordinator {
namespace {

[[nodiscard]] std::string trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

void loadCoordinatorEnvironment()
{
    if (std::getenv("OPENAI_API_KEY") != nullptr)
        return;

    const char* envPath = std::getenv("DOWNSPOUT_ENV_FILE");
    const std::string path = envPath != nullptr && *envPath != '\0' ? envPath : ".env";
    std::ifstream input(path);
    if (!input)
        return;

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (key == "OPENAI_API_KEY" && !value.empty()) {
            setenv("OPENAI_API_KEY", value.c_str(), 0);
            return;
        }
    }
}

bool hasOpenAiApiKey()
{
    loadCoordinatorEnvironment();
    const char* apiKey = std::getenv("OPENAI_API_KEY");
    return apiKey != nullptr && *apiKey != '\0';
}

namespace {

[[nodiscard]] std::string jsonEscape(const std::string& text)
{
    std::ostringstream out;
    for (const char ch : text) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch; break;
        }
    }
    return out.str();
}

[[nodiscard]] std::string jsonUnescape(const std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch != '\\' || i + 1 >= text.size()) {
            out.push_back(ch);
            continue;
        }
        const char next = text[++i];
        switch (next) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        default: out.push_back(next); break;
        }
    }
    return out;
}

[[nodiscard]] std::optional<std::string> loadSoloPrompt()
{
    const char* promptPathEnv = std::getenv("DOWNSPOUT_AI_SOLO_PROMPT");
    const std::filesystem::path promptPath =
        promptPathEnv != nullptr && *promptPathEnv != '\0'
            ? std::filesystem::path(promptPathEnv)
            : std::filesystem::path("tools/ai-coordinator/prompts/solo-system.txt");

    std::ifstream input(promptPath);
    if (!input) {
        std::cerr << "could not read AI solo prompt file: " << promptPath << '\n';
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string prompt = trim(buffer.str());
    if (prompt.empty()) {
        std::cerr << "AI solo prompt file is empty: " << promptPath << '\n';
        return std::nullopt;
    }
    return prompt;
}

[[nodiscard]] std::optional<std::string> makePayload(const TuneState& state)
{
    const char* modelEnv = std::getenv("DOWNSPOUT_OPENAI_MODEL");
    const std::string model = modelEnv != nullptr && *modelEnv != '\0' ? modelEnv : "gpt-5.4-mini";
    const std::optional<std::string> prompt = loadSoloPrompt();
    if (!prompt.has_value())
        return std::nullopt;
    const std::string request = buildSoloRequestJson(state);

    std::ostringstream out;
    out << "{";
    out << "\"model\":\"" << jsonEscape(model) << "\",";
    out << "\"input\":\"" << jsonEscape(*prompt) << "\\n\\n" << jsonEscape(request) << "\"";
    out << "}";
    return out.str();
}

[[nodiscard]] std::filesystem::path tempPath(const char* suffix)
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("downspout-openai-" + std::to_string(static_cast<unsigned long long>(getpid())) + suffix);
    return path;
}

[[nodiscard]] std::string runCurl(const std::filesystem::path& payloadPath,
                                  const std::filesystem::path& configPath)
{
    const std::string command = "curl --silent --show-error --config " +
                                configPath.string() +
                                " --data-binary @" + payloadPath.string();
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
        return output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        output += buffer;
    pclose(pipe);
    return output;
}

[[nodiscard]] std::optional<std::string> extractJsonStringValue(const std::string& text,
                                                                const std::string& key,
                                                                const std::size_t searchStart = 0)
{
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPos = text.find(needle, searchStart);
    if (keyPos == std::string::npos)
        return std::nullopt;
    const std::size_t colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos)
        return std::nullopt;
    const std::size_t quote = text.find('"', colon + 1);
    if (quote == std::string::npos)
        return std::nullopt;

    bool escaped = false;
    for (std::size_t i = quote + 1; i < text.size(); ++i) {
        const char ch = text[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"')
            return jsonUnescape(std::string_view(text).substr(quote + 1, i - quote - 1));
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extractResponseText(const std::string& raw)
{
    const std::size_t outputTextType = raw.find("\"type\": \"output_text\"");
    if (outputTextType != std::string::npos) {
        if (const auto text = extractJsonStringValue(raw, "text", outputTextType); text.has_value())
            return text;
    }
    const std::size_t compactOutputTextType = raw.find("\"type\":\"output_text\"");
    if (compactOutputTextType != std::string::npos) {
        if (const auto text = extractJsonStringValue(raw, "text", compactOutputTextType); text.has_value())
            return text;
    }
    if (const auto text = extractJsonStringValue(raw, "output_text"); text.has_value())
        return text;
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extractFirstJsonObject(const std::string& text)
{
    const std::size_t start = text.find('{');
    if (start == std::string::npos)
        return std::nullopt;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        const char ch = text[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = inString;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (inString)
            continue;
        if (ch == '{')
            ++depth;
        else if (ch == '}') {
            --depth;
            if (depth == 0)
                return text.substr(start, i - start + 1);
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<downspout::sidecar::Phrase> requestOpenAiPhrase(const TuneState& state,
                                                              std::string* rawResponse,
                                                              std::string* extractedText)
{
    loadCoordinatorEnvironment();
    const char* apiKey = std::getenv("OPENAI_API_KEY");
    if (apiKey == nullptr || *apiKey == '\0')
        return std::nullopt;

    const std::filesystem::path payloadPath = tempPath("-payload.json");
    const std::filesystem::path configPath = tempPath("-curl.conf");

    {
        const std::optional<std::string> payloadText = makePayload(state);
        if (!payloadText.has_value())
            return std::nullopt;
        std::ofstream payload(payloadPath);
        if (!payload)
            return std::nullopt;
        payload << *payloadText;
    }

    {
        std::ofstream config(configPath);
        if (!config) {
            std::filesystem::remove(payloadPath);
            return std::nullopt;
        }
        config << "url = \"https://api.openai.com/v1/responses\"\n";
        config << "header = \"Content-Type: application/json\"\n";
        config << "header = \"Authorization: Bearer " << apiKey << "\"\n";
    }
    chmod(configPath.c_str(), S_IRUSR | S_IWUSR);

    const std::string raw = runCurl(payloadPath, configPath);
    std::filesystem::remove(payloadPath);
    std::filesystem::remove(configPath);

    if (rawResponse != nullptr)
        *rawResponse = raw;
    const auto responseText = extractResponseText(raw);
    if (!responseText.has_value())
        return std::nullopt;
    if (extractedText != nullptr)
        *extractedText = *responseText;

    const auto phraseJson = extractFirstJsonObject(*responseText);
    if (!phraseJson.has_value())
        return std::nullopt;
    return parsePhraseResponseJson(*phraseJson);
}

}  // namespace downspout::ai_coordinator
