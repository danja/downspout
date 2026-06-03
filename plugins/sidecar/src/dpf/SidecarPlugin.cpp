#include "DistrhoPlugin.hpp"

#include "sidecar_engine.hpp"
#include "sidecar_protocol.hpp"
#include "sidecar_serialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamChannel = 0,
    kParamBars,
    kParamRegister,
    kParamRegisterLow,
    kParamRegisterHigh,
    kParamDensity,
    kParamRisk,
    kParamHumanize,
    kParamMute,
    kParamGenerate,
    kParamAccept,
    kParamRetry,
    kParamSource,
    kParamStatusReady,
    kParamConnectionStatus,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStatePhrase = 0,
    kStateCount
};

constexpr const char* kStateKeyPhrase = "phrase";
constexpr int kCoordinatorPort = 37371;

enum class SourceMode : int {
    local = 0,
    server = 1
};

enum class ConnectionStatus : int {
    local = 0,
    serverOk = 1,
    serverOffline = 2,
    serverError = 3
};

using downspout::sidecar::BlockResult;
using downspout::sidecar::Controls;
using downspout::sidecar::EngineState;
using downspout::sidecar::MidiEventType;
using downspout::sidecar::Phrase;
using downspout::sidecar::RegisterId;
using downspout::sidecar::ScheduledMidiEvent;
using downspout::sidecar::TransportSnapshot;

ParameterEnumerationValue kRegisterEnumValues[] = {
    {0.0f, "Low"},
    {1.0f, "Mid"},
    {2.0f, "High"},
    {3.0f, "Custom"},
};

ParameterEnumerationValue kSourceEnumValues[] = {
    {0.0f, "Local"},
    {1.0f, "Server"},
};

ParameterEnumerationValue kConnectionStatusEnumValues[] = {
    {0.0f, "Local"},
    {1.0f, "Server OK"},
    {2.0f, "Server Offline"},
    {3.0f, "Server Error"},
};

TransportSnapshot toCoreTransport(const TimePosition& timePos)
{
    TransportSnapshot transport {};
    transport.valid = timePos.bbt.valid;
    transport.playing = timePos.playing;
    if (timePos.bbt.valid)
    {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1) +
                            (timePos.bbt.ticksPerBeat > 0.0
                                 ? timePos.bbt.tick / timePos.bbt.ticksPerBeat
                                 : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.bpm = timePos.bbt.beatsPerMinute;
    }
    return transport;
}

MidiEvent toDpfMidiEvent(const ScheduledMidiEvent& event)
{
    MidiEvent midi {};
    midi.frame = event.frame;
    midi.size = 3;
    midi.data[0] = static_cast<std::uint8_t>((event.type == MidiEventType::noteOn ? 0x90u : 0x80u) | (event.channel & 0x0fu));
    midi.data[1] = event.data1;
    midi.data[2] = event.data2;
    midi.dataExt = nullptr;
    return midi;
}

void setRanges(Parameter& parameter, const float minValue, const float maxValue, const float defaultValue)
{
    parameter.ranges.min = minValue;
    parameter.ranges.max = maxValue;
    parameter.ranges.def = defaultValue;
}

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] float clampf(const float value, const float minimum, const float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] const std::uint8_t* midiData(const MidiEvent& event)
{
    return event.dataExt != nullptr ? event.dataExt : event.data;
}

[[nodiscard]] std::string controlsToTuneStateJson(const Controls& controls,
                                                  const std::array<int, 12>& pitchClasses,
                                                  const std::uint32_t seed)
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"key\": 0,\n";
    out << "  \"scale\": \"major\",\n";
    out << "  \"genre\": \"jazz\",\n";
    out << "  \"tempo\": 120,\n";
    out << "  \"bars\": " << controls.bars << ",\n";
    out << "  \"beats_per_bar\": 4,\n";
    out << "  \"channel\": " << controls.channel << ",\n";
    out << "  \"register_low\": " << controls.registerLow << ",\n";
    out << "  \"register_high\": " << controls.registerHigh << ",\n";
    out << "  \"density\": " << controls.density << ",\n";
    out << "  \"risk\": " << controls.risk << ",\n";
    out << "  \"seed\": " << seed << ",\n";
    out << "  \"guide_pitch_classes\": [";
    bool first = true;
    for (std::size_t i = 0; i < pitchClasses.size(); ++i) {
        if (pitchClasses[i] <= 0)
            continue;
        if (!first)
            out << ", ";
        out << i;
        first = false;
    }
    out << "]\n";
    out << "}\n";
    return out.str();
}

struct HttpResult {
    int status = 0;
    std::string body;
};

[[nodiscard]] std::optional<HttpResult> postCoordinatorRequest(const char* path, const std::string& body)
{
#if defined(__unix__) || defined(__APPLE__)
    const int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0)
        return std::nullopt;

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(kCoordinatorPort));
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        close(client);
        return std::nullopt;
    }
    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(client);
        return std::nullopt;
    }

    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n";
    request << "Host: 127.0.0.1:" << kCoordinatorPort << "\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << body.size() << "\r\n";
    request << "Connection: close\r\n\r\n";
    request << body;

    const std::string requestText = request.str();
    const char* data = requestText.data();
    std::size_t remaining = requestText.size();
    while (remaining > 0) {
        const ssize_t sent = send(client, data, remaining, 0);
        if (sent <= 0) {
            close(client);
            return std::nullopt;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }

    std::string response;
    char buffer[4096];
    while (true) {
        const ssize_t got = recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        response.append(buffer, static_cast<std::size_t>(got));
        if (response.size() > 1024 * 1024)
            break;
    }
    close(client);

    HttpResult result {};
    if (response.rfind("HTTP/1.1 ", 0) != 0)
        return std::nullopt;
    result.status = std::atoi(response.substr(9, 3).c_str());
    const std::size_t bodyStart = response.find("\r\n\r\n");
    if (bodyStart != std::string::npos)
        result.body = response.substr(bodyStart + 4);
    return result;
#else
    (void)path;
    (void)body;
    return std::nullopt;
#endif
}

}  // namespace

class SidecarPlugin : public Plugin
{
public:
    SidecarPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        controls_ = downspout::sidecar::clampControls(controls_);
        downspout::sidecar::activate(engine_, controls_);
        Phrase phrase = downspout::sidecar::makeFallbackPhrase(controls_, 1u);
        downspout::sidecar::setPhrase(engine_, phrase);
        acceptedPhrase_ = phrase;
        hasAcceptedPhrase_ = true;
    }

protected:
    const char* getLabel() const override { return "Sidecar"; }
    const char* getDescription() const override { return "AI-ready MIDI phrase sidecar and validated solo phrase player."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'd', 'C', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        switch (index)
        {
        case kParamChannel:
            parameter.name = "Channel";
            parameter.symbol = "channel";
            parameter.hints |= kParameterIsInteger;
            setRanges(parameter, 1.0f, 16.0f, 1.0f);
            break;
        case kParamBars:
            parameter.name = "Bars";
            parameter.symbol = "bars";
            parameter.hints |= kParameterIsInteger;
            setRanges(parameter, 1.0f, 8.0f, 4.0f);
            break;
        case kParamRegister:
            parameter.name = "Register";
            parameter.symbol = "register";
            parameter.hints |= kParameterIsInteger;
            setRanges(parameter, 0.0f, 3.0f, 1.0f);
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kRegisterEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kRegisterEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamRegisterLow:
            parameter.name = "Register Low";
            parameter.symbol = "register_low";
            parameter.hints |= kParameterIsInteger;
            setRanges(parameter, 0.0f, 127.0f, 55.0f);
            break;
        case kParamRegisterHigh:
            parameter.name = "Register High";
            parameter.symbol = "register_high";
            parameter.hints |= kParameterIsInteger;
            setRanges(parameter, 0.0f, 127.0f, 82.0f);
            break;
        case kParamDensity:
            parameter.name = "Density";
            parameter.symbol = "density";
            setRanges(parameter, 0.0f, 1.0f, 0.5f);
            break;
        case kParamRisk:
            parameter.name = "Risk";
            parameter.symbol = "risk";
            setRanges(parameter, 0.0f, 1.0f, 0.35f);
            break;
        case kParamHumanize:
            parameter.name = "Humanize";
            parameter.symbol = "humanize";
            setRanges(parameter, 0.0f, 1.0f, 0.0f);
            break;
        case kParamMute:
            parameter.name = "Mute";
            parameter.symbol = "mute";
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            setRanges(parameter, 0.0f, 1.0f, 0.0f);
            break;
        case kParamGenerate:
        case kParamAccept:
        case kParamRetry:
            parameter.name = index == kParamGenerate ? "Generate" : (index == kParamAccept ? "Accept" : "Retry");
            parameter.symbol = index == kParamGenerate ? "generate" : (index == kParamAccept ? "accept" : "retry");
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
            setRanges(parameter, 0.0f, 1048576.0f, 0.0f);
            break;
        case kParamSource:
            parameter.name = "Source";
            parameter.symbol = "source";
            parameter.hints |= kParameterIsInteger;
            setRanges(parameter, 0.0f, 1.0f, 0.0f);
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kSourceEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kSourceEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamStatusReady:
            parameter.name = "Phrase Ready";
            parameter.symbol = "status_ready";
            parameter.hints = kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger;
            setRanges(parameter, 0.0f, 1.0f, 1.0f);
            break;
        case kParamConnectionStatus:
            parameter.name = "Connection";
            parameter.symbol = "connection_status";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            setRanges(parameter, 0.0f, 3.0f, 0.0f);
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kConnectionStatusEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kConnectionStatusEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStatePhrase)
        {
            state.key = kStateKeyPhrase;
            state.label = "Phrase";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamChannel: return static_cast<float>(controls_.channel);
        case kParamBars: return static_cast<float>(controls_.bars);
        case kParamRegister: return static_cast<float>(static_cast<int>(controls_.reg));
        case kParamRegisterLow: return static_cast<float>(controls_.registerLow);
        case kParamRegisterHigh: return static_cast<float>(controls_.registerHigh);
        case kParamDensity: return controls_.density;
        case kParamRisk: return controls_.risk;
        case kParamHumanize: return controls_.humanize;
        case kParamMute: return controls_.mute ? 1.0f : 0.0f;
        case kParamGenerate: return generateCounter_;
        case kParamAccept: return acceptCounter_;
        case kParamRetry: return retryCounter_;
        case kParamSource: return static_cast<float>(static_cast<int>(sourceMode_));
        case kParamStatusReady: return engine_.phraseReady ? 1.0f : 0.0f;
        case kParamConnectionStatus: return static_cast<float>(static_cast<int>(connectionStatus_));
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamChannel: controls_.channel = static_cast<int>(std::lround(value)); break;
        case kParamBars: controls_.bars = static_cast<int>(std::lround(value)); break;
        case kParamRegister: controls_.reg = static_cast<RegisterId>(static_cast<int>(std::lround(value))); break;
        case kParamRegisterLow: controls_.registerLow = static_cast<int>(std::lround(value)); break;
        case kParamRegisterHigh: controls_.registerHigh = static_cast<int>(std::lround(value)); break;
        case kParamDensity: controls_.density = value; break;
        case kParamRisk: controls_.risk = value; break;
        case kParamHumanize: controls_.humanize = value; break;
        case kParamMute: controls_.mute = value >= 0.5f; break;
        case kParamSource:
            sourceMode_ = value >= 0.5f ? SourceMode::server : SourceMode::local;
            setConnectionStatus(sourceMode_ == SourceMode::server ? ConnectionStatus::serverOffline : ConnectionStatus::local);
            break;
        case kParamGenerate:
            if (value > generateCounter_)
            {
                generateCounter_ = value;
                setGeneratedPhrase(static_cast<std::uint32_t>(std::lround(value)) + 1u);
            }
            break;
        case kParamAccept:
            acceptCounter_ = std::max(acceptCounter_, value);
            if (engine_.phraseReady)
            {
                acceptedPhrase_ = engine_.phrase;
                hasAcceptedPhrase_ = true;
            }
            break;
        case kParamRetry:
            if (value > retryCounter_)
            {
                retryCounter_ = value;
                setGeneratedPhrase(static_cast<std::uint32_t>(std::lround(value)) + 1009u);
            }
            break;
        default:
            break;
        }
        controls_ = downspout::sidecar::clampControls(controls_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyPhrase) == 0)
        {
            if (hasAcceptedPhrase_)
                return String(downspout::sidecar::serializePhrase(acceptedPhrase_).c_str());
            if (engine_.phraseReady)
                return String(downspout::sidecar::serializePhrase(engine_.phrase).c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyPhrase) != 0 || value == nullptr)
            return;
        const auto phrase = downspout::sidecar::deserializePhrase(value);
        if (phrase.has_value())
        {
            downspout::sidecar::setPhrase(engine_, *phrase);
            acceptedPhrase_ = *phrase;
            hasAcceptedPhrase_ = true;
        }
    }

    void activate() override
    {
        downspout::sidecar::activate(engine_, controls_);
    }

    void run(const float**, float**, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        consumeInputMidi(midiEvents, midiEventCount);

        const BlockResult result = downspout::sidecar::processBlock(engine_,
                                                                    controls_,
                                                                    toCoreTransport(getTimePosition()),
                                                                    frames,
                                                                    getSampleRate());
        for (int i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result.events[i]));
    }

private:
    [[nodiscard]] Controls controlsForGeneration() const
    {
        Controls generationControls = downspout::sidecar::clampControls(controls_);
        if (capturedNoteCount_ < 3)
            return generationControls;

        generationControls.reg = RegisterId::custom;
        generationControls.registerLow = clampi(capturedMinNote_ + 7, 0, 127);
        generationControls.registerHigh = clampi(capturedMaxNote_ + 19, 0, 127);
        if (generationControls.registerHigh < generationControls.registerLow)
            std::swap(generationControls.registerHigh, generationControls.registerLow);

        const float densityHint = clampf(0.30f + static_cast<float>(capturedNoteCount_) / 80.0f, 0.25f, 0.95f);
        generationControls.density = clampf((generationControls.density * 0.65f) + (densityHint * 0.35f), 0.0f, 1.0f);

        const float rangeHint = clampf(static_cast<float>(capturedMaxNote_ - capturedMinNote_) / 72.0f, 0.0f, 1.0f);
        generationControls.risk = clampf((generationControls.risk * 0.75f) + (rangeHint * 0.25f), 0.0f, 1.0f);
        return generationControls;
    }

    void setGeneratedPhrase(const std::uint32_t seed)
    {
        controls_ = downspout::sidecar::clampControls(controls_);
        const Controls generationControls = controlsForGeneration();
        engine_.controls = generationControls;
        if (sourceMode_ == SourceMode::server) {
            if (trySetServerPhrase(generationControls, seed))
                return;
            return;
        }

        const std::uint32_t guidedSeed = seed ^ capturedSeed_;
        Phrase phrase = downspout::sidecar::makeFallbackPhrase(generationControls, guidedSeed == 0u ? seed : guidedSeed);
        downspout::sidecar::setPhrase(engine_, phrase);
        setConnectionStatus(ConnectionStatus::local);
    }

    bool trySetServerPhrase(const Controls& generationControls, const std::uint32_t seed)
    {
        const std::uint32_t guidedSeed = seed ^ capturedSeed_;
        const std::string request = controlsToTuneStateJson(generationControls,
                                                            capturedPitchClasses_,
                                                            guidedSeed == 0u ? seed : guidedSeed);
        const std::optional<HttpResult> response = postCoordinatorRequest("/openai", request);
        if (!response.has_value()) {
            setConnectionStatus(ConnectionStatus::serverOffline);
            return false;
        }
        if (response->status != 200) {
            setConnectionStatus(ConnectionStatus::serverError);
            return false;
        }
        const auto phrase = downspout::sidecar::deserializePhraseJson(response->body);
        if (!phrase.has_value()) {
            setConnectionStatus(ConnectionStatus::serverError);
            return false;
        }
        downspout::sidecar::setPhrase(engine_, *phrase);
        setConnectionStatus(ConnectionStatus::serverOk);
        return true;
    }

    void setConnectionStatus(const ConnectionStatus status)
    {
        connectionStatus_ = status;
    }

    void consumeInputMidi(const MidiEvent* midiEvents, const uint32_t midiEventCount)
    {
        if (midiEvents == nullptr)
            return;

        for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const MidiEvent& event = midiEvents[i];
            if (event.size < 3)
                continue;
            const std::uint8_t* data = midiData(event);
            const std::uint8_t status = data[0] & 0xf0u;
            if (status != 0x90u || data[2] == 0u)
                continue;

            const int note = data[1];
            capturedMinNote_ = std::min(capturedMinNote_, note);
            capturedMaxNote_ = std::max(capturedMaxNote_, note);
            ++capturedPitchClasses_[static_cast<std::size_t>(note % 12)];
            capturedNoteCount_ = std::min(capturedNoteCount_ + 1, 4096);
            capturedSeed_ ^= static_cast<std::uint32_t>((note << 16) ^ (data[2] << 8) ^ (event.frame & 0xffu));
            capturedSeed_ = capturedSeed_ * 1664525u + 1013904223u;
            if (capturedSeed_ == 0u)
                capturedSeed_ = 1u;
        }
    }

    Controls controls_ {};
    EngineState engine_ {};
    Phrase acceptedPhrase_ {};
    bool hasAcceptedPhrase_ = false;
    int capturedNoteCount_ = 0;
    int capturedMinNote_ = 127;
    int capturedMaxNote_ = 0;
    std::uint32_t capturedSeed_ = 1u;
    std::array<int, 12> capturedPitchClasses_ {};
    SourceMode sourceMode_ = SourceMode::local;
    ConnectionStatus connectionStatus_ = ConnectionStatus::local;
    float generateCounter_ = 0.0f;
    float acceptCounter_ = 0.0f;
    float retryCounter_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidecarPlugin)
};

Plugin* createPlugin()
{
    return new SidecarPlugin();
}

END_NAMESPACE_DISTRHO
