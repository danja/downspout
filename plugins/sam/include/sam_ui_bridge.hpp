#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace downspout::sam {

// In-process channel from the plugin (DSP) side to its panel for text that an MCP client
// changed. DPF's updateStateValue does nothing in the VST3 wrapper and a panel cannot ask the
// plugin for its state, so the server publishes here and the panel polls from uiIdle().
//
// One channel per MCP port. The port is also exposed as the read-only mcp_port parameter, which
// is how a panel finds the channel of its own instance when several Sams share a host process.
// Plugin and panel live in the same binary, so a function-local static is shared between them.
class UiBridge {
public:
    static void publish(const int port, const std::string& text)
    {
        State& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        Channel& c = s.channels[port];
        c.text = text;
        ++c.serial;
    }

    // Serial of the latest publication on `port` (0 if none yet).
    static std::uint32_t serial(const int port)
    {
        State& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        const auto it = s.channels.find(port);
        return it == s.channels.end() ? 0 : it->second.serial;
    }

    // Copies the latest text if its serial differs from `seen`; updates `seen`. True if it did.
    static bool fetch(const int port, std::uint32_t& seen, std::string& text)
    {
        State& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        const auto it = s.channels.find(port);
        if (it == s.channels.end() || it->second.serial == seen)
            return false;
        seen = it->second.serial;
        text = it->second.text;
        return true;
    }

private:
    struct Channel {
        std::string text;
        std::uint32_t serial = 0;
    };
    struct State {
        std::mutex mutex;
        std::map<int, Channel> channels;
    };
    static State& state()
    {
        static State s;
        return s;
    }
};

}  // namespace downspout::sam
