#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

// Forward-declare to keep the httplib include out of plugin headers
namespace httplib { class Server; }

namespace downspout::campione {

// Embedded JSON-RPC 2.0 / MCP HTTP server.
// Runs on a background thread; all Api callbacks are invoked from that thread.
// Zone and parameter mutations use atomic operations so no extra locking is
// required on the audio side.
class CampioneMcpServer
{
public:
    struct Api {
        // ── Read ──────────────────────────────────────────────────────────
        std::function<std::string()>       getZonesJson;
        std::function<std::string()>       getParametersJson;
        std::function<int()>               getPort;   // returns the actual bound port

        // ── Parameters ────────────────────────────────────────────────────
        std::function<void(float)>         setMasterVolume;
        std::function<void(float)>         setMidiChannel;
        std::function<void(float)>         setCrossfadeMs;
        std::function<void(float)>         setPitchBendRange;

        // ── Transport ─────────────────────────────────────────────────────
        std::function<void()>              startRecording;
        std::function<void()>              stopRecording;

        // ── Zone management ───────────────────────────────────────────────
        std::function<std::string(const std::string&)> loadZone;     // path → "" or error
        std::function<std::string(int)>                removeZone;   // index → "" or error
        std::function<std::string(int, int, int, int, bool,
                                  uint32_t, uint32_t, int)> updateZone;   // → "" or error; last int = octaveShift
        // ADSR+filter+pan+muted: negative float values = preserve existing; -2 int = preserve
        std::function<std::string(int, float, float, float, float,
                                  int, int, float, float, float, int)> updateZoneDsp; // ADSR+filter+pan+muted → "" or error
        std::function<std::string(int, int, int)> sliceZone;  // idx, numSlices, startNote → "" or error
        std::function<std::string()> mapDrum;    // map zones to GM drum layout → "" or error
        std::function<std::string()> mapSpread;  // spread zones over 4-octave keyboard → "" or error

        // ── Wave editing ──────────────────────────────────────────────────
        std::function<std::string(int)>         normalizeZone; // index → "" or error
        std::function<std::string(int, float)>  trimZone;      // index, threshold_db → "" or error
        std::function<std::string(int, float, float)> fadeZone; // index, in_ms, out_ms → "" or error
        std::function<std::string(int)>         reverseZone;   // index → "" or error

        // ── Utility ───────────────────────────────────────────────────────────
        std::function<void()>              clearZones;
        std::function<void()>              refreshUi;
        std::function<std::string()>       getRecordingStatus;
        std::function<std::string(const std::string&)> setRecordingDir;

        // ── Patch I/O ─────────────────────────────────────────────────────────
        // path="" → auto-generate timestamped path; returns "" or error
        std::function<std::string(const std::string&)> savePatch;
        // path must be absolute; loads zones+params from Turtle file; returns "" or error
        std::function<std::string(const std::string&)> loadPatch;
    };

    // preferredPort: first port to try; increments up to preferredPort+9 on EADDRINUSE.
    CampioneMcpServer(int preferredPort, Api api);
    ~CampioneMcpServer();

    // Starts the background server thread.  No-op if already running.
    void start();

    // Stops the server and joins the thread.
    void stop();

    // Actual bound port (valid after start()).
    int port() const { return port_; }

private:
    void serve();
    std::string dispatch(const std::string& body);

    int         preferredPort_;
    int         port_ = 0;
    Api         api_;
    std::thread thread_;
    std::unique_ptr<httplib::Server> svr_;
};

}  // namespace downspout::campione
