#include "downspout/test_assert.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <thread>
#include "../../../third_party/cpp-httplib/httplib.h"
#include "../../../third_party/nlohmann/json.hpp"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "sam_engine.hpp"
#include "sam_mcp_server.hpp"
#include "sam_params.hpp"
#include "sam_speech.hpp"
#include "sam_ui_bridge.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace downspout::sam;
using json = nlohmann::json;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kTestPort = 17230;  // far from the plugin's 7230, so a running Sam cannot interfere

void pass(const char* name) { std::printf("PASS %s\n", name); }

// What a tool call returned: the parsed JSON text, and whether the server flagged an error.
struct Reply {
    bool error = false;
    std::string raw;
    json body;
};

Reply call(SamMcpServer& server, const std::string& tool, const json& arguments = json::object())
{
    const json request = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                          {"params", {{"name", tool}, {"arguments", arguments}}}};
    const json response = json::parse(server.handleMessage(request.dump()));
    assert(response.contains("result"));
    Reply reply;
    reply.error = response["result"].value("isError", false);
    reply.raw = response["result"]["content"][0]["text"].get<std::string>();
    if (!reply.error)
        reply.body = json::parse(reply.raw);
    return reply;
}

// Renders `blocks` blocks of the engine in Keys mode with the host stopped; returns the peak.
double renderPeak(Engine& engine, const std::size_t frames)
{
    std::vector<float> left(256), right(256);
    double peak = 0.0;
    for (std::size_t done = 0; done < frames; done += 256) {
        engine.process(Transport {}, nullptr, 0, 256, left.data(), right.data());
        for (const float v : left)
            peak = std::max(peak, static_cast<double>(std::fabs(v)));
    }
    return peak;
}

struct Rig {
    Engine engine;
    SamMcpServer server;
    std::vector<std::string> texts;
    std::vector<std::pair<std::uint32_t, float>> changes;

    Rig() : server(engine, kTestPort, makeHooks())
    {
        engine.activate(kSampleRate);
    }

    SamMcpServer::Hooks makeHooks()
    {
        SamMcpServer::Hooks hooks;
        hooks.textChanged = [this](const std::string& t) { texts.push_back(t); };
        hooks.parameterChanged = [this](const std::uint32_t i, const float v) { changes.emplace_back(i, v); };
        return hooks;
    }

    float param(const ParamId id) const { return engine.getParameter(paramIndex(id)); }
};

// ── Protocol ───────────────────────────────────────────────────────────────

void testProtocolBasics()
{
    Rig rig;
    const json init = json::parse(rig.server.handleMessage(
        R"({"jsonrpc":"2.0","id":7,"method":"initialize","params":{"protocolVersion":"2025-03-26"}})"));
    assert(init["id"] == 7);
    assert(init["result"]["protocolVersion"] == "2025-03-26");
    assert(init["result"]["serverInfo"]["name"] == "sam");
    assert(rig.server.handleMessage(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
    const json ping = json::parse(rig.server.handleMessage(R"({"jsonrpc":"2.0","id":2,"method":"ping"})"));
    assert(ping["id"] == 2 && ping.contains("result"));

    assert(json::parse(rig.server.handleMessage("not json"))["error"]["code"] == -32700);
    assert(json::parse(rig.server.handleMessage("[1,2]"))["error"]["code"] == -32600);
    assert(json::parse(rig.server.handleMessage(R"({"jsonrpc":"2.0","id":3,"method":"nope"})"))["error"]["code"] == -32601);
    assert(json::parse(rig.server.handleMessage(
               R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"nope","arguments":{}}})"))["error"]["code"] == -32601);
    pass("testProtocolBasics");
}

void testToolListIsCompleteAndWellFormed()
{
    Rig rig;
    const json list = json::parse(rig.server.handleMessage(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"));
    std::set<std::string> names;
    for (const json& tool : list["result"]["tools"]) {
        assert(names.insert(tool["name"].get<std::string>()).second);
        assert(!tool["description"].get<std::string>().empty());
        assert(tool["inputSchema"]["type"] == "object");
        assert(tool["inputSchema"]["properties"].is_object());
        if (tool["inputSchema"].contains("required")) {
            for (const json& r : tool["inputSchema"]["required"])
                assert(tool["inputSchema"]["properties"].contains(r.get<std::string>()));
        }
    }
    for (const char* expected : {"say", "set_text", "get_text", "check_text", "set_parameter", "set_parameters",
                                 "get_parameters", "set_voice", "get_status", "get_port"})
        assert(names.count(expected) == 1);
    assert(names.size() == 10);
    pass("testToolListIsCompleteAndWellFormed");
}

// ── Text ───────────────────────────────────────────────────────────────────

void testSetTextReachesTheEngineAndTheHooks()
{
    Rig rig;
    const Reply r = call(rig.server, "set_text", {{"text", "hello  world _ again"}});
    assert(!r.error);
    assert(rig.engine.text() == "hello  world _ again");
    assert(r.body["slots"] == 4);
    assert(r.body["words"] == json::array({"hello", "world", "_", "again"}));
    assert(r.body["speaking"] == false);
    assert(rig.texts.size() == 1 && rig.texts[0] == "hello  world _ again");

    assert(call(rig.server, "get_text").body["text"] == "hello  world _ again");

    // Tabs and newlines fold to spaces, other control characters go, and the result says so.
    const Reply cleaned = call(rig.server, "set_text", {{"text", std::string("a\tb\nc\x01") + "d"}});
    assert(cleaned.body["text"] == "a b cd");
    assert(cleaned.body["truncated_or_cleaned"] == true);

    // An over-long text is cut at the plugin's own limit and reported.
    const Reply cut = call(rig.server, "set_text", {{"text", std::string(kMaxTextBytes + 300, 'z')}});
    assert(cut.body["text"].get<std::string>().size() == kMaxTextBytes);
    assert(cut.body["truncated_or_cleaned"] == true);
    pass("testSetTextReachesTheEngineAndTheHooks");
}

void testPhoneticSwitchTravelsWithTheText()
{
    Rig rig;
    assert(rig.param(ParamId::phonetic) == 0.0f);
    const Reply r = call(rig.server, "set_text", {{"text", "/HEHLOW"}, {"phonetic", true}});
    assert(!r.error && r.body["phonetic"] == true);
    assert(rig.param(ParamId::phonetic) == 1.0f);
    assert(call(rig.server, "set_text", {{"text", "hello"}, {"phonetic", false}}).body["phonetic"] == false);
    assert(rig.param(ParamId::phonetic) == 0.0f);
    // Leaving it out leaves the switch alone.
    (void)call(rig.server, "set_parameter", {{"name", "phonetic"}, {"value", 1}});
    (void)call(rig.server, "set_text", {{"text", "AA"}});
    assert(rig.param(ParamId::phonetic) == 1.0f);
    pass("testPhoneticSwitchTravelsWithTheText");
}

void testSayIsAudibleWithoutMidiOrTransport()
{
    Rig rig;
    assert(renderPeak(rig.engine, 8192) == 0.0);  // silent until asked
    const Reply r = call(rig.server, "say", {{"text", "AA IY"}, {"phonetic", true}});
    assert(!r.error && r.body["speaking"] == true);
    assert(renderPeak(rig.engine, 12000) > 0.05);
    assert(rig.engine.text() == "AA IY");

    // set_text alone stays silent.
    Rig quiet;
    (void)call(quiet.server, "set_text", {{"text", "AA IY"}, {"phonetic", true}});
    assert(renderPeak(quiet.engine, 12000) == 0.0);

    // Speaking again repeats it.
    (void)call(rig.server, "say", {{"text", "AA IY"}});
    assert(renderPeak(rig.engine, 12000) > 0.05);
    pass("testSayIsAudibleWithoutMidiOrTransport");
}

void testBadTextArgumentsAreReportedNotThrown()
{
    Rig rig;
    for (const char* tool : {"say", "set_text"}) {
        assert(call(rig.server, tool, json::object()).error);
        assert(call(rig.server, tool, {{"text", 5}}).error);
    }
    // A phonetic that is not a boolean is an error, and the text is left alone.
    rig.engine.setText("keep me");
    assert(call(rig.server, "set_text", {{"text", "x"}, {"phonetic", "maybe"}}).error);
    assert(rig.engine.text() == "keep me");
    pass("testBadTextArgumentsAreReportedNotThrown");
}

void testCheckTextVetsPhonemes()
{
    Rig rig;
    rig.engine.setText("keep me");
    // "HEHLOW" is English-shaped but not valid phonemes (the aspirate is "/H"); SAM refuses it, and
    // that is precisely what a client wants to learn before speaking it.
    const Reply r = call(rig.server, "check_text", {{"text", "/HEH3LOW2 HEHLOW !!! _ WER4LD"}, {"phonetic", true}});
    assert(!r.error);
    assert(r.body["slots"] == 5);
    const json& tokens = r.body["tokens"];
    assert(tokens[0]["reads_as"] == "/HEH3LOW2" && tokens[0]["accepted"] == true && tokens[0]["seconds"].get<double>() > 0.1);
    assert(tokens[1]["reads_as"] == "HEHLOW" && tokens[1]["accepted"] == false && tokens[1]["seconds"] == 0.0);
    assert(tokens[2]["reads_as"] == "" && tokens[2]["accepted"] == false);
    assert(tokens[3]["rest"] == true);
    assert(tokens[4]["accepted"] == true);
    assert(r.body["silent_tokens"] == 2);
    // The same word read as English is fine: the switch matters.
    assert(call(rig.server, "check_text", {{"text", "HEHLOW"}, {"phonetic", false}}).body["tokens"][0]["accepted"] == true);
    assert(rig.engine.text() == "keep me");  // a dry run changes nothing

    // A slower voice takes longer to say the same thing (SAM's speed number: lower is faster).
    const double fast = call(rig.server, "check_text", {{"text", "WER4LD"}, {"phonetic", true}}).body["tokens"][0]["seconds"];
    (void)call(rig.server, "set_parameter", {{"name", "speed"}, {"value", 160}});
    const double slow = call(rig.server, "check_text", {{"text", "WER4LD"}, {"phonetic", true}}).body["tokens"][0]["seconds"];
    assert(slow > fast);
    // With no arguments it checks the loaded text under the current switch.
    assert(call(rig.server, "check_text").body["slots"] == 2);
    pass("testCheckTextVetsPhonemes");
}

// ── Parameters ─────────────────────────────────────────────────────────────

void testSetParameterAcceptsNumbersBooleansAndLabels()
{
    Rig rig;
    assert(!call(rig.server, "set_parameter", {{"name", "speed"}, {"value", 90}}).error);
    assert(rig.param(ParamId::speed) == 90.0f);

    // Symbol or panel name, any case.
    assert(!call(rig.server, "set_parameter", {{"name", "Start Bar"}, {"value", 5}}).error);
    assert(rig.param(ParamId::startBar) == 5.0f);
    assert(!call(rig.server, "set_parameter", {{"name", "START_BAR"}, {"value", 6}}).error);
    assert(rig.param(ParamId::startBar) == 6.0f);

    // Labels as the panel shows them.
    assert(!call(rig.server, "set_parameter", {{"name", "mode"}, {"value", "Transport"}}).error);
    assert(rig.param(ParamId::mode) == 1.0f);
    assert(!call(rig.server, "set_parameter", {{"name", "step"}, {"value", "1/8"}}).error);
    assert(rig.param(ParamId::step) == 1.0f);
    assert(!call(rig.server, "set_parameter", {{"name", "midi_ch"}, {"value", "Ch 3"}}).error);
    assert(rig.param(ParamId::midiCh) == 3.0f);
    assert(!call(rig.server, "set_parameter", {{"name", "advance"}, {"value", "key = word"}}).error);
    assert(rig.param(ParamId::advance) == 1.0f);

    // Booleans for the switches; the reply reads the value back with its label.
    const Reply loop = call(rig.server, "set_parameter", {{"name", "loop"}, {"value", false}});
    assert(rig.param(ParamId::loop) == 0.0f && loop.body["label"] == "Off");

    // Out-of-range numbers clamp, and the reply says what was kept.
    const Reply clamped = call(rig.server, "set_parameter", {{"name", "speed"}, {"value", 9999}});
    assert(clamped.body["value"] == 200.0 && rig.param(ParamId::speed) == 200.0f);
    pass("testSetParameterAcceptsNumbersBooleansAndLabels");
}

void testSetParameterRejectsBadRequests()
{
    Rig rig;
    const std::size_t before = rig.changes.size();
    assert(call(rig.server, "set_parameter", {{"name", "volume"}, {"value", 1}}).error);                 // unknown
    assert(call(rig.server, "set_parameter", {{"name", "out_word"}, {"value", 3}}).error);               // read-only
    assert(call(rig.server, "set_parameter", {{"name", "mcp_port"}, {"value", 80}}).error);              // read-only
    assert(call(rig.server, "set_parameter", {{"name", "mode"}, {"value", "Sideways"}}).error);          // unknown label
    assert(call(rig.server, "set_parameter", {{"name", "speed"}, {"value", "fast"}}).error);             // not a number
    assert(call(rig.server, "set_parameter", {{"name", "speed"}, {"value", json::array()}}).error);
    assert(call(rig.server, "set_parameter", {{"name", "speed"}}).error);                                // no value
    assert(call(rig.server, "set_parameter", {{"value", 1}}).error);                                     // no name
    assert(rig.changes.size() == before);                                                                // nothing was applied
    assert(rig.param(ParamId::speed) == 72.0f);
    pass("testSetParameterRejectsBadRequests");
}

void testSetParametersAppliesInOrderAndReportsPartialFailure()
{
    Rig rig;
    const Reply ok = call(rig.server, "set_parameters", {{"values", {{"speed", 100}, {"mode", "Transport"}, {"loop", 0}}}});
    assert(!ok.error);
    assert(rig.param(ParamId::speed) == 100.0f && rig.param(ParamId::mode) == 1.0f && rig.param(ParamId::loop) == 0.0f);
    assert(ok.body["applied"].size() == 3);

    // nlohmann sorts object keys: "level" < "zzz", so level lands and zzz fails.
    const Reply bad = call(rig.server, "set_parameters", {{"values", {{"level", 0.2}, {"zzz", 1}}}});
    assert(bad.error);
    assert(bad.raw.find("zzz") != std::string::npos && bad.raw.find("level") != std::string::npos);
    assert(std::fabs(rig.param(ParamId::level) - 0.2f) < 1.0e-6f);

    assert(call(rig.server, "set_parameters", {{"values", 5}}).error);
    assert(call(rig.server, "set_parameters", json::object()).error);
    pass("testSetParametersAppliesInOrderAndReportsPartialFailure");
}

void testParameterHooksFireWithTheStoredValue()
{
    Rig rig;
    (void)call(rig.server, "set_parameter", {{"name", "speed"}, {"value", 9999}});
    assert(rig.changes.size() == 1);
    assert(rig.changes[0].first == paramIndex(ParamId::speed) && rig.changes[0].second == 200.0f);  // the clamped value
    pass("testParameterHooksFireWithTheStoredValue");
}

void testGetParametersDescribesEveryParameter()
{
    Rig rig;
    const Reply r = call(rig.server, "get_parameters");
    const json& list = r.body["parameters"];
    assert(list.size() == kParameterCount);
    std::set<std::string> symbols;
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        const json& p = list[i];
        assert(p["symbol"] == kParameterSpecs[i].symbol);
        assert(symbols.insert(p["symbol"].get<std::string>()).second);
        assert(p["min"].get<double>() < p["max"].get<double>());
        assert(p["value"].get<double>() == kParameterSpecs[i].defaultValue);
    }
    assert(list[paramIndex(ParamId::mode)]["options"] == json::array({"Keys", "Transport"}));
    assert(list[paramIndex(ParamId::step)]["options"].size() == kStepNames.size());
    assert(list[paramIndex(ParamId::midiCh)]["label"] == "All");
    assert(list[paramIndex(ParamId::outWord)]["read_only"] == true);
    assert(list[paramIndex(ParamId::speak)]["trigger"] == true);
    // Every writable parameter accepts its own default back through set_parameter.
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        assert(!call(rig.server, "set_parameter", {{"name", kParameterSpecs[i].symbol}, {"value", kParameterSpecs[i].defaultValue}}).error);
    pass("testGetParametersDescribesEveryParameter");
}

void testSetVoiceAppliesAllFourKnobs()
{
    Rig rig;
    for (const VoicePreset& v : kVoicePresets) {
        assert(!call(rig.server, "set_voice", {{"preset", v.name}}).error);
        assert(rig.param(ParamId::speed) == static_cast<float>(v.speed));
        assert(rig.param(ParamId::pitch) == static_cast<float>(v.pitch));
        assert(rig.param(ParamId::throat) == static_cast<float>(v.throat));
        assert(rig.param(ParamId::mouth) == static_cast<float>(v.mouth));
    }
    assert(!call(rig.server, "set_voice", {{"preset", "old lady"}}).error);   // case and spacing forgiven
    assert(rig.param(ParamId::pitch) == 32.0f);
    assert(call(rig.server, "set_voice", {{"preset", "Dalek"}}).error);
    assert(call(rig.server, "set_voice", json::object()).error);
    pass("testSetVoiceAppliesAllFourKnobs");
}

void testStatusFollowsTheSpeech()
{
    Rig rig;
    (void)call(rig.server, "set_text", {{"text", "AA IY UW"}, {"phonetic", true}});
    (void)call(rig.server, "set_parameter", {{"name", "mode"}, {"value", "Transport"}});
    Reply s = call(rig.server, "get_status");
    assert(s.body["mode"] == "Transport" && s.body["current_word"] == 0 && s.body["slots"] == 3);
    assert(!s.body.contains("current_token"));

    // Play the transport for a moment on word 2 (one 1/4 step each at 120 bpm).
    std::vector<float> left(256), right(256);
    Transport t;
    t.valid = true;
    t.playing = true;
    t.bar = 0.0;
    t.barBeat = 1.0;
    rig.engine.process(t, nullptr, 0, 256, left.data(), right.data());
    s = call(rig.server, "get_status");
    assert(s.body["current_word"] == 2 && s.body["current_token"] == "IY");
    assert(s.body["port"] == 0);  // never started in this test
    pass("testStatusFollowsTheSpeech");
}

// ── Concurrency ────────────────────────────────────────────────────────────

void testControlWhileTheAudioThreadRuns()
{
    Rig rig;
    std::atomic<bool> stop {false};
    std::thread audio([&] {
        std::vector<float> left(128), right(128);
        while (!stop.load())
            rig.engine.process(Transport {}, nullptr, 0, 128, left.data(), right.data());
    });
    for (int i = 0; i < 300; ++i) {
        const std::string text = (i % 2 ? "AA IY UW " : "OW AE ") + std::to_string(i);
        assert(!call(rig.server, i % 3 ? "say" : "set_text", {{"text", text}, {"phonetic", true}}).error);
        assert(!call(rig.server, "set_parameter", {{"name", "speed"}, {"value", 40 + i % 100}}).error);
        assert(!call(rig.server, "get_status").error);
    }
    stop.store(true);
    audio.join();
    pass("testControlWhileTheAudioThreadRuns");
}

// ── Socket ─────────────────────────────────────────────────────────────────

void testServesOverLoopbackAndRefusesOtherCallers()
{
    Engine engine;
    SamMcpServer server(engine, kTestPort);
    assert(server.port() == 0);
    assert(server.start());
    assert(server.port() >= kTestPort && server.port() < kTestPort + 10);
    assert(server.start());  // already running: a no-op

    const json ask = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                      {"params", {{"name", "say"}, {"arguments", {{"text", "hello"}}}}}};
    httplib::Client client("127.0.0.1", server.port());
    client.set_connection_timeout(2);
    client.set_read_timeout(5);

    const auto ok = client.Post("/mcp", ask.dump(), "application/json");
    assert(ok && ok->status == 200);
    assert(json::parse(ok->body)["result"]["content"][0]["text"].get<std::string>().find("hello") != std::string::npos);
    assert(engine.text() == "hello");

    const auto root = client.Post("/", R"({"jsonrpc":"2.0","id":1,"method":"ping"})", "application/json");
    assert(root && root->status == 200);
    const auto note = client.Post("/mcp", R"({"jsonrpc":"2.0","method":"notifications/initialized"})", "application/json");
    assert(note && note->status == 202);

    // A page in a browser can send text/plain cross-site without a preflight; that must not be enough.
    const auto plain = client.Post("/mcp", ask.dump(), "text/plain");
    assert(plain && plain->status == 415);
    // A page's Origin, or a rebinding Host, is refused.
    const auto foreign = client.Post("/mcp", httplib::Headers {{"Origin", "http://evil.example"}}, ask.dump(), "application/json");
    assert(foreign && foreign->status == 403);
    const auto local = client.Post("/mcp", httplib::Headers {{"Origin", "http://localhost:3000"}}, ask.dump(), "application/json");
    assert(local && local->status == 200);
    assert(engine.text() == "hello");

    // GET is not part of the protocol here.
    const auto get = client.Get("/mcp");
    assert(get && get->status == 404);

    // A second instance takes the next port; stopping frees the port and a restart works.
    Engine second;
    SamMcpServer other(second, kTestPort);
    assert(other.start());
    assert(other.port() != server.port());
    const int first = server.port();
    server.stop();
    assert(server.port() == 0);
    httplib::Client gone("127.0.0.1", first);
    gone.set_connection_timeout(1);
    assert(!gone.Post("/mcp", ask.dump(), "application/json"));
    assert(server.start());
    other.stop();
    pass("testServesOverLoopbackAndRefusesOtherCallers");
}

void testServerGivesUpWhenEveryPortIsTaken()
{
    std::vector<std::unique_ptr<Engine>> engines;
    std::vector<std::unique_ptr<SamMcpServer>> servers;
    for (int i = 0; i < 10; ++i) {
        engines.push_back(std::make_unique<Engine>());
        servers.push_back(std::make_unique<SamMcpServer>(*engines.back(), kTestPort + 100));
        assert(servers.back()->start());
    }
    Engine extra;
    SamMcpServer last(extra, kTestPort + 100);
    assert(!last.start());
    assert(last.port() == 0);
    pass("testServerGivesUpWhenEveryPortIsTaken");
}

// ── Panel bridge ───────────────────────────────────────────────────────────

void testUiBridgeDeliversOnlyNewText()
{
    std::uint32_t seen = 0;
    std::string text;
    assert(!UiBridge::fetch(91, seen, text));
    assert(UiBridge::serial(91) == 0);
    UiBridge::publish(91, "one");
    UiBridge::publish(92, "elsewhere");
    assert(UiBridge::serial(91) == 1);
    assert(UiBridge::fetch(91, seen, text) && text == "one");
    assert(!UiBridge::fetch(91, seen, text));                       // already seen
    UiBridge::publish(91, "two");
    assert(UiBridge::fetch(91, seen, text) && text == "two");
    std::uint32_t other = 0;
    assert(UiBridge::fetch(92, other, text) && text == "elsewhere"); // channels are per port
    pass("testUiBridgeDeliversOnlyNewText");
}

}  // namespace

int main()
{
    testProtocolBasics();
    testToolListIsCompleteAndWellFormed();
    testSetTextReachesTheEngineAndTheHooks();
    testPhoneticSwitchTravelsWithTheText();
    testSayIsAudibleWithoutMidiOrTransport();
    testBadTextArgumentsAreReportedNotThrown();
    testCheckTextVetsPhonemes();
    testSetParameterAcceptsNumbersBooleansAndLabels();
    testSetParameterRejectsBadRequests();
    testSetParametersAppliesInOrderAndReportsPartialFailure();
    testParameterHooksFireWithTheStoredValue();
    testGetParametersDescribesEveryParameter();
    testSetVoiceAppliesAllFourKnobs();
    testStatusFollowsTheSpeech();
    testControlWhileTheAudioThreadRuns();
    testServesOverLoopbackAndRefusesOtherCallers();
    testServerGivesUpWhenEveryPortIsTaken();
    testUiBridgeDeliversOnlyNewText();
    std::printf("All SAM MCP tests passed.\n");
    return 0;
}
