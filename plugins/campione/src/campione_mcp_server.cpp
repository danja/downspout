// Suppress warnings from vendored headers
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#include "../../../third_party/cpp-httplib/httplib.h"
#include "../../../third_party/nlohmann/json.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "campione_mcp_server.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace downspout::campione {

// ── helpers ──────────────────────────────────────────────────────────────────

static json makeResult(const json& result, const json& id)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

static json makeError(int code, const std::string& msg, const json& id)
{
    return {{"jsonrpc", "2.0"}, {"id", id},
            {"error", {{"code", code}, {"message", msg}}}};
}

static json textContent(const std::string& text)
{
    return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
}

// ── tool schema helpers ───────────────────────────────────────────────────────

static json intProp(const char* desc)
{
    return {{"type", "integer"}, {"description", desc}};
}
static json numProp(const char* desc)
{
    return {{"type", "number"}, {"description", desc}};
}
static json strProp(const char* desc)
{
    return {{"type", "string"}, {"description", desc}};
}
static json boolProp(const char* desc)
{
    return {{"type", "boolean"}, {"description", desc}};
}

static json makeSchema(json props = json::object(), std::vector<std::string> required = {})
{
    // Ensure properties is always a JSON object, never null or array
    if (!props.is_object()) props = json::object();
    json s = {{"type", "object"}, {"properties", std::move(props)}};
    if (!required.empty()) s["required"] = std::move(required);
    return s;
}

// ── tool definitions ──────────────────────────────────────────────────────────

static json buildToolsList()
{
    return json::array({
        // Parameters
        {{"name","set_master_volume"},
         {"description","Set the master output volume (0–1)."},
         {"inputSchema", makeSchema({{"value", numProp("Volume 0.0–1.0")}}, {"value"})}},

        {{"name","set_midi_channel"},
         {"description","Set the MIDI channel filter. 0 = all channels, 1–16 = specific."},
         {"inputSchema", makeSchema({{"channel", intProp("0=all, 1-16=specific")}}, {"channel"})}},

        {{"name","set_crossfade_ms"},
         {"description","Set loop crossfade duration in milliseconds (0–100)."},
         {"inputSchema", makeSchema({{"ms", numProp("Crossfade duration ms")}}, {"ms"})}},

        {{"name","set_pitch_bend_range"},
         {"description","Set pitch-bend wheel range in semitones (0–24)."},
         {"inputSchema", makeSchema({{"semitones", numProp("Semitones 0–24")}}, {"semitones"})}},

        // Recording
        {{"name","start_recording"},
         {"description","Begin capturing audio input as a new zone."},
         {"inputSchema", makeSchema(json::object())}},

        {{"name","stop_recording"},
         {"description","Stop recording and auto-map the captured audio as a new zone."},
         {"inputSchema", makeSchema(json::object())}},

        // Zone management
        {{"name","load_zone"},
         {"description","Load a WAV file from disk and add it as a new zone."},
         {"inputSchema", makeSchema({{"path", strProp("Absolute path to WAV file")}}, {"path"})}},

        {{"name","remove_zone"},
         {"description","Remove a zone by index (0-based)."},
         {"inputSchema", makeSchema({{"index", intProp("Zone index")}}, {"index"})}},

        {{"name","update_zone"},
         {"description","Update zone metadata: root note, key range, loop toggle, and optional loop points."},
         {"inputSchema", makeSchema({
             {"index",       intProp("Zone index")},
             {"root_note",   intProp("Root MIDI note 0–127")},
             {"range_low",   intProp("Lowest MIDI note 0–127")},
             {"range_high",  intProp("Highest MIDI note 0–127")},
             {"loop_enabled",boolProp("Enable looping")},
             {"loop_start",  intProp("Loop start frame (optional)")},
             {"loop_end",    intProp("Loop end frame (optional)")}
         }, {"index","root_note","range_low","range_high","loop_enabled"})}},

        // Wave editing
        {{"name","normalize_zone"},
         {"description","Scale zone audio so the peak absolute sample equals 1.0. Saves to the source WAV."},
         {"inputSchema", makeSchema({{"index", intProp("Zone index")}}, {"index"})}},

        {{"name","trim_zone"},
         {"description","Remove leading and trailing silence from a zone. Saves to the source WAV."},
         {"inputSchema", makeSchema({
             {"index",        intProp("Zone index")},
             {"threshold_db", numProp("Silence threshold in dBFS (default –60)")}
         }, {"index"})}},

        {{"name","fade_zone"},
         {"description","Apply linear fade-in and fade-out to a zone. Saves to the source WAV."},
         {"inputSchema", makeSchema({
             {"index",      intProp("Zone index")},
             {"fade_in_ms", numProp("Fade-in duration ms (default 10)")},
             {"fade_out_ms",numProp("Fade-out duration ms (default 10)")}
         }, {"index"})}},

        {{"name","reverse_zone"},
         {"description","Reverse the audio in a zone. Saves to the source WAV."},
         {"inputSchema", makeSchema({{"index", intProp("Zone index")}}, {"index"})}},

        // Inspection
        {{"name","get_zones"},
         {"description","Return the current zone list as JSON."},
         {"inputSchema", makeSchema(json::object())}},

        {{"name","get_parameters"},
         {"description","Return current plugin parameters as JSON."},
         {"inputSchema", makeSchema(json::object())}},

        {{"name","get_port"},
         {"description","Return the port this MCP server is listening on."},
         {"inputSchema", makeSchema(json::object())}}
    });
}

// ── CampioneMcpServer ─────────────────────────────────────────────────────────

CampioneMcpServer::CampioneMcpServer(int preferredPort, Api api)
    : preferredPort_(preferredPort)
    , api_(std::move(api))
    , svr_(std::make_unique<httplib::Server>())
{}

CampioneMcpServer::~CampioneMcpServer()
{
    stop();
}

void CampioneMcpServer::start()
{
    if (thread_.joinable()) return;
    thread_ = std::thread([this]{ serve(); });
}

void CampioneMcpServer::stop()
{
    if (svr_) svr_->stop();
    if (thread_.joinable()) thread_.join();
}

void CampioneMcpServer::serve()
{
    // Find an available port
    for (int port = preferredPort_; port < preferredPort_ + 10; ++port) {
        if (svr_->bind_to_port("127.0.0.1", port)) {
            port_ = port;
            break;
        }
    }
    if (port_ == 0) return; // could not bind

    auto handler = [this](const httplib::Request& req, httplib::Response& res) {
        std::string body = dispatch(req.body);
        if (body.empty()) {
            // JSON-RPC notification: no response expected
            res.status = 202;
        } else {
            res.set_content(body, "application/json");
        }
    };
    svr_->Post("/",    handler);
    svr_->Post("/mcp", handler);

    svr_->listen_after_bind();
}

std::string CampioneMcpServer::dispatch(const std::string& body)
{
    json id    = nullptr;
    json req;

    try {
        req = json::parse(body);
        id  = req.value("id", json(nullptr));
    } catch (...) {
        return makeError(-32700, "Parse error", nullptr).dump();
    }

    const std::string method = req.value("method", std::string{});
    const json        params = req.value("params", json::object());

    // ── MCP lifecycle ─────────────────────────────────────────────────────
    if (method == "initialize") {
        // Echo whichever protocol version the client requested (we support both)
        const std::string proto = params.value("protocolVersion", std::string("2024-11-05"));
        return makeResult({
            {"protocolVersion", proto},
            {"capabilities",    {{"tools", json::object()}}},
            {"serverInfo",      {{"name", "campione"}, {"version", "1.0"}}}
        }, id).dump();
    }

    if (method == "notifications/initialized") {
        return ""; // notification, no response
    }

    if (method == "ping") {
        return makeResult({}, id).dump();
    }

    // ── Tool listing ──────────────────────────────────────────────────────
    if (method == "tools/list") {
        return makeResult({{"tools", buildToolsList()}}, id).dump();
    }

    // ── Tool calls ────────────────────────────────────────────────────────
    if (method == "tools/call") {
        const std::string name = params.value("name", std::string{});
        const json        args = params.value("arguments", json::object());

        try {
            // Parameters
            if (name == "set_master_volume") {
                float v = args.at("value").get<float>();
                if (v < 0.0f || v > 1.0f) throw std::runtime_error("value must be 0–1");
                api_.setMasterVolume(v);
                char buf[64]; std::snprintf(buf, sizeof(buf), "master volume set to %.3f", v);
                return makeResult(textContent(buf), id).dump();
            }

            if (name == "set_midi_channel") {
                int ch = args.at("channel").get<int>();
                if (ch < 0 || ch > 16) throw std::runtime_error("channel must be 0–16");
                api_.setMidiChannel(static_cast<float>(ch));
                char buf[48]; std::snprintf(buf, sizeof(buf), "MIDI channel set to %s",
                                            ch == 0 ? "all" : std::to_string(ch).c_str());
                return makeResult(textContent(buf), id).dump();
            }

            if (name == "set_crossfade_ms") {
                float ms = args.at("ms").get<float>();
                if (ms < 0.0f || ms > 100.0f) throw std::runtime_error("ms must be 0–100");
                api_.setCrossfadeMs(ms);
                char buf[64]; std::snprintf(buf, sizeof(buf), "crossfade set to %.1f ms", ms);
                return makeResult(textContent(buf), id).dump();
            }

            if (name == "set_pitch_bend_range") {
                float sem = args.at("semitones").get<float>();
                if (sem < 0.0f || sem > 24.0f) throw std::runtime_error("semitones must be 0–24");
                api_.setPitchBendRange(sem);
                char buf[64]; std::snprintf(buf, sizeof(buf), "pitch bend range set to %.1f semitones", sem);
                return makeResult(textContent(buf), id).dump();
            }

            // Recording
            if (name == "start_recording") {
                api_.startRecording();
                return makeResult(textContent("recording started"), id).dump();
            }

            if (name == "stop_recording") {
                api_.stopRecording();
                return makeResult(textContent("recording stopped and zone added"), id).dump();
            }

            // Zone management
            if (name == "load_zone") {
                const std::string path = args.at("path").get<std::string>();
                const std::string err  = api_.loadZone(path);
                if (!err.empty()) throw std::runtime_error(err);
                return makeResult(textContent("zone loaded: " + path), id).dump();
            }

            if (name == "remove_zone") {
                int idx = args.at("index").get<int>();
                const std::string err = api_.removeZone(idx);
                if (!err.empty()) throw std::runtime_error(err);
                char buf[48]; std::snprintf(buf, sizeof(buf), "zone %d removed", idx);
                return makeResult(textContent(buf), id).dump();
            }

            if (name == "update_zone") {
                int      idx   = args.at("index").get<int>();
                int      root  = args.at("root_note").get<int>();
                int      lo    = args.at("range_low").get<int>();
                int      hi    = args.at("range_high").get<int>();
                bool     loop  = args.at("loop_enabled").get<bool>();
                uint32_t lStart = args.value("loop_start", 0);
                uint32_t lEnd   = args.value("loop_end",   0);
                const std::string err = api_.updateZone(idx, root, lo, hi, loop, lStart, lEnd);
                if (!err.empty()) throw std::runtime_error(err);
                char buf[64]; std::snprintf(buf, sizeof(buf), "zone %d updated", idx);
                return makeResult(textContent(buf), id).dump();
            }

            // Wave editing
            if (name == "normalize_zone") {
                int idx = args.at("index").get<int>();
                const std::string err = api_.normalizeZone(idx);
                if (!err.empty()) throw std::runtime_error(err);
                char buf[64]; std::snprintf(buf, sizeof(buf), "zone %d normalized to peak", idx);
                return makeResult(textContent(buf), id).dump();
            }

            if (name == "trim_zone") {
                int   idx = args.at("index").get<int>();
                float thr = args.value("threshold_db", -60.0f);
                const std::string err = api_.trimZone(idx, thr);
                if (!err.empty()) throw std::runtime_error(err);
                char buf[64]; std::snprintf(buf, sizeof(buf), "zone %d trimmed at %.1f dBFS", idx, thr);
                return makeResult(textContent(buf), id).dump();
            }

            if (name == "fade_zone") {
                int   idx   = args.at("index").get<int>();
                float inMs  = args.value("fade_in_ms",  10.0f);
                float outMs = args.value("fade_out_ms", 10.0f);
                const std::string err = api_.fadeZone(idx, inMs, outMs);
                if (!err.empty()) throw std::runtime_error(err);
                char buf[80]; std::snprintf(buf, sizeof(buf),
                              "zone %d faded (in %.1f ms, out %.1f ms)", idx, inMs, outMs);
                return makeResult(textContent(buf), id).dump();
            }

            if (name == "reverse_zone") {
                int idx = args.at("index").get<int>();
                const std::string err = api_.reverseZone(idx);
                if (!err.empty()) throw std::runtime_error(err);
                char buf[48]; std::snprintf(buf, sizeof(buf), "zone %d reversed", idx);
                return makeResult(textContent(buf), id).dump();
            }

            // Inspection
            if (name == "get_zones") {
                return makeResult(textContent(api_.getZonesJson()), id).dump();
            }

            if (name == "get_parameters") {
                return makeResult(textContent(api_.getParametersJson()), id).dump();
            }

            if (name == "get_port") {
                return makeResult(textContent(std::to_string(port_)), id).dump();
            }

            return makeError(-32601, "Unknown tool: " + name, id).dump();

        } catch (const std::exception& e) {
            return makeResult(
                {{"content", json::array({{{"type","text"},{"text", std::string("Error: ") + e.what()}}})},
                 {"isError", true}},
                id).dump();
        }
    }

    return makeError(-32601, "Method not found: " + method, id).dump();
}

}  // namespace downspout::campione
