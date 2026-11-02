// Suppress warnings from vendored headers
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#include <thread>  // httplib.h uses std::thread but omits this include on MSVC
#include "../../../third_party/cpp-httplib/httplib.h"
#include "../../../third_party/nlohmann/json.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "sam_mcp_server.hpp"

#include "sam_engine.hpp"
#include "sam_params.hpp"
#include "sam_speech.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

using json = nlohmann::json;

namespace downspout::sam {
namespace {

// ── JSON-RPC helpers ─────────────────────────────────────────────────────────

json makeResult(const json& result, const json& id)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

json makeError(const int code, const std::string& message, const json& id)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

json textContent(const std::string& text)
{
    return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
}

json jsonContent(const json& value)
{
    return textContent(value.dump(2));
}

json prop(const char* type, const char* description)
{
    return {{"type", type}, {"description", description}};
}

json schema(json properties = json::object(), std::vector<std::string> required = {})
{
    json s = {{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty())
        s["required"] = std::move(required);
    return s;
}

// ── Parameter naming ─────────────────────────────────────────────────────────

std::string normalise(const std::string& s)
{
    std::string out;
    for (const char c : s) {
        if (c == ' ' || c == '-')
            out += '_';
        else
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Parameter index for a symbol ("start_bar") or display name ("Start Bar"), or -1.
int findParameter(const std::string& name)
{
    const std::string wanted = normalise(name);
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        if (wanted == normalise(kParameterSpecs[i].symbol) || wanted == normalise(kParameterSpecs[i].name))
            return static_cast<int>(i);
    }
    return -1;
}

// The labels of an enumerated parameter, or empty for a continuous one.
std::vector<std::string> labelsFor(const std::size_t index)
{
    std::vector<std::string> out;
    const auto take = [&out](const char* const* names, const std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            out.emplace_back(names[i]);
    };
    switch (static_cast<ParamId>(index)) {
    case ParamId::mode: take(kModeNames.data(), kModeNames.size()); break;
    case ParamId::advance: take(kAdvanceNames.data(), kAdvanceNames.size()); break;
    case ParamId::step: take(kStepNames.data(), kStepNames.size()); break;
    case ParamId::midiCh: take(kMidiChannelNames.data(), kMidiChannelNames.size()); break;
    case ParamId::monotone:
    case ParamId::phonetic:
    case ParamId::loop: take(kOffOnNames.data(), kOffOnNames.size()); break;
    default: break;
    }
    return out;
}

// A client may send a number, a boolean, or the label shown in the panel ("Transport", "1/8").
float resolveValue(const std::size_t index, const json& v)
{
    if (v.is_boolean())
        return v.get<bool>() ? 1.0f : 0.0f;
    if (v.is_number()) {
        const double d = v.get<double>();
        if (!std::isfinite(d))
            throw std::runtime_error("value must be a finite number");
        return static_cast<float>(d);
    }
    if (v.is_string()) {
        const std::string wanted = normalise(v.get<std::string>());
        const std::vector<std::string> labels = labelsFor(index);
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (normalise(labels[i]) == wanted)
                return static_cast<float>(i);
        }
        std::string known;
        for (const std::string& label : labels)
            known += (known.empty() ? "" : ", ") + label;
        throw std::runtime_error(known.empty() ? std::string("this parameter takes a number")
                                               : "unknown value; one of: " + known);
    }
    throw std::runtime_error("value must be a number, a boolean or a label");
}

json describeParameter(const Engine& engine, const std::size_t index)
{
    const ParamSpec& spec = kParameterSpecs[index];
    const float value = engine.getParameter(static_cast<std::uint32_t>(index));
    json p = {{"symbol", spec.symbol}, {"name", spec.name}, {"value", value},
              {"min", spec.minimum}, {"max", spec.maximum}, {"default", spec.defaultValue}};
    if (spec.unit[0] != '\0')
        p["unit"] = spec.unit;
    if (spec.output)
        p["read_only"] = true;
    if (spec.trigger)
        p["trigger"] = true;
    const std::vector<std::string> labels = labelsFor(index);
    if (!labels.empty()) {
        p["options"] = labels;
        const auto at = static_cast<std::size_t>(std::lround(value));
        if (at < labels.size())
            p["label"] = labels[at];
    }
    return p;
}

// ── Tool list ────────────────────────────────────────────────────────────────

json buildToolsList()
{
    json presets = json::array();
    for (const VoicePreset& v : kVoicePresets)
        presets.push_back(v.name);

    const json textProp = prop("string",
        "What to speak: space-separated words, or SAM phoneme codes when phonetic is true (e.g. "
        "\"/HEH3LOW2 WER4LD\": digits 1-8 are stress, / starts a breath). A token \"_\" is a rest.");
    const json phoneticProp = prop("boolean",
        "Optional. Read the text as SAM phonemes (true) or English (false). Sets the plugin's Phonetic "
        "switch before the text is applied, so the two always agree.");

    return json::array({
        {{"name", "say"},
         {"description", "Replace the text and speak it once, now, from the first word. Works in either mode and whatever "
                         "the host transport is doing, except that with Transport mode and the host rolling the transport "
                         "is already reading the text, so the new text is simply picked up at the playhead."},
         {"inputSchema", schema({{"text", textProp}, {"phonetic", phoneticProp}}, {"text"})}},

        {{"name", "set_text"},
         {"description", "Replace the text without speaking it. In Keys mode each MIDI note speaks the next word; in "
                         "Transport mode the words fall one per step from the start bar. Takes effect on the next block."},
         {"inputSchema", schema({{"text", textProp}, {"phonetic", phoneticProp}}, {"text"})}},

        {{"name", "get_text"},
         {"description", "The text currently loaded, and how it splits into word slots."},
         {"inputSchema", schema()}},

        {{"name", "check_text"},
         {"description", "Dry run: for each token, what SAM will actually read after cleaning, whether SAM accepts it, "
                         "and how long it lasts at the current voice. Use it to vet phonemes before speaking them. "
                         "Defaults to the loaded text and the current Phonetic switch; changes nothing."},
         {"inputSchema", schema({{"text", prop("string", "Text to check (optional; default: the loaded text)")},
                                 {"phonetic", prop("boolean", "Check as phonemes (optional; default: the current switch)")}})}},

        {{"name", "set_parameter"},
         {"description", "Set one parameter by symbol or panel name (see get_parameters). The value may be a number, a "
                         "boolean, or an option label such as \"Transport\" or \"1/8\". Out-of-range numbers are clamped. "
                         "Read-only outputs cannot be set."},
         {"inputSchema", schema({{"name", prop("string", "Parameter symbol, e.g. speed, start_bar, midi_ch")},
                                 {"value", {{"description", "Number, boolean or option label"}}}},
                                {"name", "value"})}},

        {{"name", "set_parameters"},
         {"description", "Set several parameters at once. Applied in the order given; stops at the first error and reports "
                         "what was already applied."},
         {"inputSchema", schema({{"values", {{"type", "object"},
                                             {"description", "Map of parameter symbol to value, e.g. {\"speed\": 90, \"mode\": \"Transport\"}"}}}},
                                {"values"})}},

        {{"name", "get_parameters"},
         {"description", "Every parameter with its current value, range, default, unit and option labels."},
         {"inputSchema", schema()}},

        {{"name", "set_voice"},
         {"description", "Apply one of the six SAM manual voices (sets speed, pitch, mouth and throat together)."},
         {"inputSchema", schema({{"preset", {{"type", "string"}, {"enum", presets}, {"description", "Voice name"}}}}, {"preset"})}},

        {{"name", "get_status"},
         {"description", "Mode, text, and the word being spoken right now (current_word is 1-based; 0 means silent)."},
         {"inputSchema", schema()}},

        {{"name", "get_port"},
         {"description", "The TCP port this server is listening on."},
         {"inputSchema", schema()}},
    });
}

}  // namespace

// ── SamMcpServer ─────────────────────────────────────────────────────────────

SamMcpServer::SamMcpServer(Engine& engine, const int preferredPort, Hooks hooks)
    : engine_(engine), preferredPort_(preferredPort), hooks_(std::move(hooks))
{
}

SamMcpServer::~SamMcpServer()
{
    stop();
}

namespace {

// DNS-rebinding and drive-by-browser guards, on top of the loopback-only bind: the Host must be a
// local name, a page's Origin (if any) must be local, and the body must be declared JSON, which a
// browser cannot send cross-site without a preflight that this server never grants.
bool isLocalHost(std::string host)
{
    const auto scheme = host.find("://");
    if (scheme != std::string::npos)
        host.erase(0, scheme + 3);
    const auto slash = host.find('/');
    if (slash != std::string::npos)
        host.erase(slash);
    if (!host.empty() && host.front() == '[') {
        const auto close = host.find(']');
        return close != std::string::npos && host.substr(0, close + 1) == "[::1]";
    }
    const auto colon = host.find(':');
    if (colon != std::string::npos)
        host.erase(colon);
    return host == "localhost" || host == "127.0.0.1";
}

bool acceptRequest(const httplib::Request& req, httplib::Response& res)
{
    if (!isLocalHost(req.get_header_value("Host"))) {
        res.status = 403;
        return false;
    }
    if (req.has_header("Origin") && !isLocalHost(req.get_header_value("Origin"))) {
        res.status = 403;
        return false;
    }
    if (req.get_header_value("Content-Type").find("application/json") == std::string::npos) {
        res.status = 415;
        return false;
    }
    return true;
}

}  // namespace

bool SamMcpServer::start()
{
    if (thread_.joinable())
        return true;

    // bind_to_port() decommissions a Server that fails, so each attempt gets a fresh one. SO_REUSEADDR
    // only (not SO_REUSEPORT), so a second instance fails to bind port N and moves on to N+1.
    for (int port = preferredPort_; port < preferredPort_ + 10; ++port) {
        svr_ = std::make_unique<httplib::Server>();
        svr_->set_socket_options([](socket_t sock) {
            int yes = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        });
        const auto handler = [this](const httplib::Request& req, httplib::Response& res) {
            if (!acceptRequest(req, res))
                return;
            const std::string body = handleMessage(req.body);
            if (body.empty())
                res.status = 202;
            else
                res.set_content(body, "application/json");
        };
        svr_->Post("/", handler);
        svr_->Post("/mcp", handler);
        if (svr_->bind_to_port("127.0.0.1", port)) {
            port_.store(port);
            break;
        }
    }
    if (port_.load() == 0) {
        svr_.reset();
        return false;
    }
    thread_ = std::thread([this] { svr_->listen_after_bind(); });
    return true;
}

void SamMcpServer::stop()
{
    if (svr_)
        svr_->stop();
    if (thread_.joinable())
        thread_.join();
    svr_.reset();
    port_.store(0);
}

std::string SamMcpServer::handleMessage(const std::string& body)
{
    json id = nullptr;
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        return makeError(-32700, "Parse error", nullptr).dump();
    }
    if (!req.is_object())
        return makeError(-32600, "Invalid request", nullptr).dump();
    id = req.value("id", json(nullptr));

    const std::string method = req.value("method", std::string{});
    const json params = req.contains("params") && req["params"].is_object() ? req["params"] : json::object();

    if (method == "initialize") {
        const std::string proto = params.value("protocolVersion", std::string("2024-11-05"));
        return makeResult({{"protocolVersion", proto},
                           {"capabilities", {{"tools", json::object()}}},
                           {"serverInfo", {{"name", "sam"}, {"version", "1.0"}}}},
                          id).dump();
    }
    if (method == "notifications/initialized" || method.rfind("notifications/", 0) == 0)
        return "";
    if (method == "ping")
        return makeResult(json::object(), id).dump();
    if (method == "tools/list")
        return makeResult({{"tools", buildToolsList()}}, id).dump();
    if (method != "tools/call")
        return makeError(-32601, "Method not found: " + method, id).dump();

    const std::string name = params.value("name", std::string{});
    const json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();

    const auto setOne = [this](const std::size_t index, const json& value) {
        const ParamSpec& spec = kParameterSpecs[index];
        if (spec.output)
            throw std::runtime_error(std::string(spec.symbol) + " is read-only");
        engine_.setParameter(static_cast<std::uint32_t>(index), resolveValue(index, value));
        const float now = engine_.getParameter(static_cast<std::uint32_t>(index));
        if (hooks_.parameterChanged)
            hooks_.parameterChanged(static_cast<std::uint32_t>(index), now);
        return now;
    };
    const auto stringArg = [&args](const char* key) -> std::string {
        if (!args.contains(key) || !args[key].is_string())
            throw std::runtime_error(std::string("'") + key + "' must be a string");
        return args[key].get<std::string>();
    };
    // The panel's Phonetic switch is applied first so the text is never read under the wrong one.
    const auto applyPhonetic = [&]() {
        if (args.contains("phonetic"))
            setOne(paramIndex(ParamId::phonetic), args["phonetic"]);
    };
    const auto describeScript = [this]() {
        const std::string text = engine_.text();
        const Script script = parseScript(text);
        const bool phonetic = engine_.getParameter(paramIndex(ParamId::phonetic)) >= 0.5f;
        json slots = json::array();
        for (const Token& t : script.tokens)
            slots.push_back(t.rest ? json("_") : json(t.word));
        return json{{"text", text}, {"phonetic", phonetic}, {"slots", script.size()}, {"words", slots}};
    };

    try {
        if (name == "set_text" || name == "say") {
            const std::string requested = stringArg("text");
            applyPhonetic();
            if (name == "say")
                engine_.say(requested);
            else
                engine_.setText(requested);
            json out = describeScript();
            out["truncated_or_cleaned"] = out["text"].get<std::string>() != requested;
            if (hooks_.textChanged)
                hooks_.textChanged(out["text"].get<std::string>());
            out["speaking"] = name == "say";
            return makeResult(jsonContent(out), id).dump();
        }

        if (name == "get_text")
            return makeResult(jsonContent(describeScript()), id).dump();

        if (name == "check_text") {
            const std::string text = args.contains("text") ? stringArg("text") : engine_.text();
            const bool phonetic = args.contains("phonetic") && args["phonetic"].is_boolean()
                                      ? args["phonetic"].get<bool>()
                                      : engine_.getParameter(paramIndex(ParamId::phonetic)) >= 0.5f;
            const auto get = [this](const ParamId p) { return static_cast<int>(std::lround(engine_.getParameter(paramIndex(p)))); };
            VoiceSettings voice;
            voice.speed = get(ParamId::speed);
            voice.pitch = get(ParamId::pitch);
            voice.mouth = get(ParamId::mouth);
            voice.throat = get(ParamId::throat);
            voice.sing = get(ParamId::monotone) != 0;
            voice.phonetic = phonetic;

            std::vector<std::uint8_t> buffer(kMaxRenderSamples);
            const Script script = parseScript(text);
            json tokens = json::array();
            std::size_t silent = 0;
            for (const Token& t : script.tokens) {
                json entry = {{"token", t.word}};
                if (t.rest) {
                    entry["rest"] = true;
                } else {
                    const std::string cleaned = sanitizeWord(t.word, phonetic);
                    entry["reads_as"] = cleaned;
                    const std::size_t samples = cleaned.empty() ? 0 : renderWord(cleaned, voice, buffer.data(), buffer.size());
                    entry["accepted"] = samples > 0;
                    entry["seconds"] = static_cast<double>(samples) / kSamRate;
                    if (samples == 0)
                        ++silent;
                }
                tokens.push_back(std::move(entry));
            }
            return makeResult(jsonContent({{"phonetic", phonetic}, {"slots", script.size()},
                                           {"silent_tokens", silent}, {"tokens", tokens}}), id).dump();
        }

        if (name == "set_parameter") {
            const std::string pname = stringArg("name");
            const int index = findParameter(pname);
            if (index < 0)
                throw std::runtime_error("unknown parameter '" + pname + "'; see get_parameters");
            if (!args.contains("value"))
                throw std::runtime_error("'value' is required");
            setOne(static_cast<std::size_t>(index), args["value"]);
            return makeResult(jsonContent(describeParameter(engine_, static_cast<std::size_t>(index))), id).dump();
        }

        if (name == "set_parameters") {
            if (!args.contains("values") || !args["values"].is_object())
                throw std::runtime_error("'values' must be an object of parameter: value");
            json applied = json::object();
            for (const auto& [key, value] : args["values"].items()) {
                const int index = findParameter(key);
                try {
                    if (index < 0)
                        throw std::runtime_error("unknown parameter '" + key + "'");
                    applied[kParameterSpecs[static_cast<std::size_t>(index)].symbol] = setOne(static_cast<std::size_t>(index), value);
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::string(e.what()) + " (applied so far: " + applied.dump() + ")");
                }
            }
            return makeResult(jsonContent({{"applied", applied}}), id).dump();
        }

        if (name == "get_parameters") {
            json list = json::array();
            for (std::size_t i = 0; i < kParameterCount; ++i)
                list.push_back(describeParameter(engine_, i));
            return makeResult(jsonContent({{"parameters", list}}), id).dump();
        }

        if (name == "set_voice") {
            const std::string wanted = normalise(stringArg("preset"));
            for (const VoicePreset& v : kVoicePresets) {
                if (normalise(v.name) != wanted)
                    continue;
                setOne(paramIndex(ParamId::speed), v.speed);
                setOne(paramIndex(ParamId::pitch), v.pitch);
                setOne(paramIndex(ParamId::throat), v.throat);
                setOne(paramIndex(ParamId::mouth), v.mouth);
                return makeResult(jsonContent({{"voice", v.name}, {"speed", v.speed}, {"pitch", v.pitch},
                                               {"throat", v.throat}, {"mouth", v.mouth}}), id).dump();
            }
            throw std::runtime_error("unknown voice '" + stringArg("preset") + "'");
        }

        if (name == "get_status") {
            const int word = static_cast<int>(std::lround(engine_.getParameter(paramIndex(ParamId::outWord))));
            const int mode = static_cast<int>(std::lround(engine_.getParameter(paramIndex(ParamId::mode))));
            json out = describeScript();
            out["mode"] = mode >= 0 && mode < static_cast<int>(kModeNames.size()) ? kModeNames[static_cast<std::size_t>(mode)] : "?";
            out["current_word"] = word;
            if (word > 0 && static_cast<std::size_t>(word) <= out["words"].size())
                out["current_token"] = out["words"][static_cast<std::size_t>(word - 1)];
            out["port"] = port_.load();
            return makeResult(jsonContent(out), id).dump();
        }

        if (name == "get_port")
            return makeResult(jsonContent({{"port", port_.load()}}), id).dump();

        return makeError(-32601, "Unknown tool: " + name, id).dump();
    } catch (const std::exception& e) {
        return makeResult({{"content", json::array({{{"type", "text"}, {"text", std::string("Error: ") + e.what()}}})},
                           {"isError", true}},
                          id).dump();
    }
}

}  // namespace downspout::sam
