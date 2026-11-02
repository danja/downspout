#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace httplib { class Server; }

namespace downspout::sam {

class Engine;

// Embedded MCP (JSON-RPC 2.0, streamable HTTP) server for external, real-time control of one Sam
// instance: set or speak words and phonemes, and move the parameters.
//
// It is a transport plus a thin adapter onto Engine's main-thread API (setText, say, setParameter,
// getParameter), which is already safe to call while the audio thread runs. It never touches the
// audio thread itself. Binds 127.0.0.1 only. The Engine must outlive the server.
class SamMcpServer {
public:
    // Optional notifications so the plugin wrapper can tell the host and the panel what an
    // external client changed. Called on the server thread.
    struct Hooks {
        std::function<void(const std::string& text)> textChanged;
        std::function<void(std::uint32_t index, float value)> parameterChanged;
    };

    // preferredPort: first port tried; on EADDRINUSE the next nine are tried in turn, so several
    // instances in one host each get a server. port() says which one was bound.
    SamMcpServer(Engine& engine, int preferredPort, Hooks hooks = {});
    ~SamMcpServer();

    SamMcpServer(const SamMcpServer&) = delete;
    SamMcpServer& operator=(const SamMcpServer&) = delete;

    // Binds a port, then serves on a background thread. Returns false (and serves nothing) if no
    // port was free. No-op returning true when already running.
    bool start();
    void stop();

    // Actual bound port; 0 until start() succeeds.
    [[nodiscard]] int port() const { return port_.load(); }

    // Handles one JSON-RPC message and returns the response body, or "" for a notification.
    // Public so the tool surface can be tested without a socket.
    [[nodiscard]] std::string handleMessage(const std::string& body);

    // The first port the plugin asks for; Campione uses 7220-7229.
    static constexpr int kDefaultPort = 7230;

private:
    Engine& engine_;
    int preferredPort_;
    std::atomic<int> port_ {0};
    Hooks hooks_;
    std::thread thread_;
    std::unique_ptr<httplib::Server> svr_;
};

}  // namespace downspout::sam
