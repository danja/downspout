#include "ai_coordinator.hpp"

#include "sidecar_protocol.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace downspout::ai_coordinator {
namespace {

[[nodiscard]] std::string httpResponse(const int status, const char* reason, const std::string& body)
{
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason << "\r\n";
    out << "Content-Type: application/json\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n\r\n";
    out << body;
    return out.str();
}

[[nodiscard]] bool debugEnabled()
{
    const char* value = std::getenv("DOWNSPOUT_AI_DEBUG");
    if (value == nullptr || *value == '\0')
        return false;
    const std::string text(value);
    return text != "0" && text != "false" && text != "FALSE" && text != "no" && text != "NO";
}

void debugBlock(const char* title, const std::string& text)
{
    if (!debugEnabled())
        return;
    std::cerr << "\n--- " << title << " ---\n";
    std::cerr << text << '\n';
    std::cerr << "--- end " << title << " ---\n";
}

void sendAll(const int client, const std::string& response)
{
    const char* data = response.data();
    std::size_t remaining = response.size();
    while (remaining > 0) {
        const ssize_t sent = send(client, data, remaining, 0);
        if (sent <= 0)
            return;
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}

[[nodiscard]] std::string readRequest(const int client)
{
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t got = recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0)
            return request;
        request.append(buffer, static_cast<std::size_t>(got));
        if (request.size() > 1024 * 1024)
            return request;
    }

    const std::size_t contentLengthPos = request.find("Content-Length:");
    if (contentLengthPos == std::string::npos)
        return request;
    std::size_t valueStart = contentLengthPos + std::strlen("Content-Length:");
    while (valueStart < request.size() && request[valueStart] == ' ')
        ++valueStart;
    const std::size_t valueEnd = request.find("\r\n", valueStart);
    const std::size_t bodyStart = request.find("\r\n\r\n");
    if (valueEnd == std::string::npos || bodyStart == std::string::npos)
        return request;
    const int contentLength = std::max(0, std::atoi(request.substr(valueStart, valueEnd - valueStart).c_str()));
    const std::size_t targetSize = bodyStart + 4 + static_cast<std::size_t>(contentLength);
    while (request.size() < targetSize) {
        const ssize_t got = recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        request.append(buffer, static_cast<std::size_t>(got));
    }
    return request;
}

[[nodiscard]] std::string requestBody(const std::string& request)
{
    const std::size_t bodyStart = request.find("\r\n\r\n");
    if (bodyStart == std::string::npos)
        return "";
    return request.substr(bodyStart + 4);
}

[[nodiscard]] std::string requestLine(const std::string& request)
{
    const std::size_t end = request.find("\r\n");
    return end == std::string::npos ? request : request.substr(0, end);
}

[[nodiscard]] std::string handleGenerate(const std::string& body)
{
    debugBlock("Sidecar /generate request body", body);
    const auto state = parseTuneStateJson(body);
    if (!state.has_value())
        return httpResponse(400, "Bad Request", "{\"error\":\"invalid tune state\"}\n");
    const downspout::sidecar::Phrase phrase = generateSoloPhrase(*state);
    const std::string response = serializePhraseResponseJson(phrase) + "\n";
    debugBlock("Sidecar /generate response body", response);
    return httpResponse(200, "OK", response);
}

[[nodiscard]] std::string handleOpenAi(const std::string& body)
{
    debugBlock("Sidecar /openai request body", body);
    if (!hasOpenAiApiKey()) {
        std::cerr << "Sidecar /openai request rejected: OPENAI_API_KEY is not configured\n";
        return httpResponse(503, "Service Unavailable", "{\"error\":\"OPENAI_API_KEY is not configured\"}\n");
    }
    const auto state = parseTuneStateJson(body);
    if (!state.has_value()) {
        std::cerr << "Sidecar /openai request rejected: invalid tune state\n";
        return httpResponse(400, "Bad Request", "{\"error\":\"invalid tune state\"}\n");
    }

    std::string rawResponse;
    std::string extractedText;
    const auto phrase = requestOpenAiPhrase(*state, &rawResponse, &extractedText);
    if (!phrase.has_value()) {
        std::cerr << "Sidecar /openai request failed: OpenAI response did not validate\n";
        return httpResponse(502, "Bad Gateway", "{\"error\":\"OpenAI response did not validate\"}\n");
    }
    const downspout::sidecar::Phrase constrained = constrainPhraseToTuneState(*phrase, *state);
    const downspout::sidecar::Controls controls = controlsFromTuneState(*state);
    if (!downspout::sidecar::validatePhrase(constrained, controls).valid) {
        std::cerr << "Sidecar /openai request failed: constrained phrase did not validate\n";
        return httpResponse(502, "Bad Gateway", "{\"error\":\"constrained phrase did not validate\"}\n");
    }
    std::cout << "Sidecar /openai request ok: " << constrained.eventCount << " events\n";
    const std::string response = serializePhraseResponseJson(constrained) + "\n";
    debugBlock("Sidecar /openai response body to plugin", response);
    return httpResponse(200, "OK", response);
}

[[nodiscard]] std::string handleRequest(const std::string& request)
{
    const std::string line = requestLine(request);
    if (line.rfind("GET /health ", 0) == 0) {
        return httpResponse(200, "OK", std::string("{\"ok\":true,\"openai_key\":") +
                                      (hasOpenAiApiKey() ? "true" : "false") + "}\n");
    }
    if (line.rfind("POST /generate ", 0) == 0)
        return handleGenerate(requestBody(request));
    if (line.rfind("POST /openai ", 0) == 0)
        return handleOpenAi(requestBody(request));
    return httpResponse(404, "Not Found", "{\"error\":\"not found\"}\n");
}

}  // namespace

int runLiveServer(const int port)
{
    loadCoordinatorEnvironment();

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "could not create server socket: " << std::strerror(errno) << '\n';
        return 1;
    }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "could not bind 127.0.0.1:" << port << ": " << std::strerror(errno) << '\n';
        close(server);
        return 1;
    }
    if (listen(server, 8) < 0) {
        std::cerr << "could not listen on 127.0.0.1:" << port << ": " << std::strerror(errno) << '\n';
        close(server);
        return 1;
    }

    std::cout << "downspout-ai-coordinator serving on http://127.0.0.1:" << port << '\n';
    std::cout << "OpenAI key configured: " << (hasOpenAiApiKey() ? "yes" : "no") << '\n';
    std::cout << "Endpoints: GET /health, POST /generate, POST /openai\n";
    std::cout << "Debug logging: " << (debugEnabled() ? "on" : "off") << '\n';
    if (!hasOpenAiApiKey())
        std::cout << "Sidecar Server mode will show Server Error for /openai until OPENAI_API_KEY is configured.\n";
    while (true) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0)
            continue;
        const std::string request = readRequest(client);
        sendAll(client, handleRequest(request));
        close(client);
    }
}

}  // namespace downspout::ai_coordinator
