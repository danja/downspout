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
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

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
    kParamPlay,
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
    requesting = 1,
    ready = 2,
    serverOffline = 3,
    serverError = 4
};

using downspout::sidecar::BlockResult;
using downspout::sidecar::Controls;
using downspout::sidecar::EngineState;
using downspout::sidecar::MidiEventType;
using downspout::sidecar::Phrase;
using downspout::sidecar::PhraseEvent;
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
    {1.0f, "Requesting"},
    {2.0f, "Ready"},
    {3.0f, "Server Offline"},
    {4.0f, "Server Error"},
};

struct WorkerResult {
    bool done = false;
    bool ok = false;
    bool connected = false;
    Phrase phrase {};
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

[[nodiscard]] Controls controlsForServerRequest(const Controls& rawControls)
{
    Controls controls = rawControls;
    controls.reg = RegisterId::custom;
    controls.registerLow = clampi(controls.registerLow, 0, 127);
    controls.registerHigh = clampi(controls.registerHigh, controls.registerLow, 127);

    constexpr int kMinimumSoloSpan = 18;
    const int span = controls.registerHigh - controls.registerLow;
    if (span >= kMinimumSoloSpan)
        return controls;

    const int center = (controls.registerLow + controls.registerHigh) / 2;
    controls.registerLow = clampi(center - (kMinimumSoloSpan / 2), 0, 127);
    controls.registerHigh = clampi(controls.registerLow + kMinimumSoloSpan, 0, 127);
    if (controls.registerHigh - controls.registerLow < kMinimumSoloSpan) {
        controls.registerHigh = clampi(center + (kMinimumSoloSpan / 2), 0, 127);
        controls.registerLow = clampi(controls.registerHigh - kMinimumSoloSpan, 0, 127);
    }
    return controls;
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

    ~SidecarPlugin() override
    {
        joinWorker();
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
        case kParamPlay:
        case kParamAccept:
        case kParamRetry:
            parameter.name = index == kParamGenerate
                ? "Generate"
                : (index == kParamPlay ? "Play" : (index == kParamAccept ? "Accept" : "Retry"));
            parameter.symbol = index == kParamGenerate
                ? "generate"
                : (index == kParamPlay ? "play" : (index == kParamAccept ? "accept" : "retry"));
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
            setRanges(parameter, 0.0f, 4.0f, 0.0f);
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
        case kParamPlay: return playCounter_;
        case kParamAccept: return acceptCounter_;
        case kParamRetry: return retryCounter_;
        case kParamSource: return static_cast<float>(static_cast<int>(sourceMode_));
        case kParamStatusReady:
            if (sourceMode_ == SourceMode::server &&
                (connectionStatus_ == ConnectionStatus::requesting || queuedPhraseReady_))
                return 0.0f;
            return engine_.phraseReady ? 1.0f : 0.0f;
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
            if (sourceMode_ == SourceMode::local) {
                setConnectionStatus(ConnectionStatus::local);
            } else if (connectionStatus_ == ConnectionStatus::local) {
                setConnectionStatus(ConnectionStatus::serverOffline);
            }
            break;
        case kParamGenerate:
            if (value > generateCounter_)
            {
                generateCounter_ = value;
                setGeneratedPhrase(static_cast<std::uint32_t>(std::lround(value)) + 1u);
            }
            break;
        case kParamPlay:
            if (value > playCounter_)
            {
                playCounter_ = value;
                armCurrentPhrase();
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
        const TransportSnapshot transport = toCoreTransport(getTimePosition());
        lastTransport_ = transport;
        applyAsyncResult(transport);
        activateQueuedPhrase(transport);

        Controls processControls = controls_;
        if (sourceMode_ == SourceMode::server &&
            (connectionStatus_ == ConnectionStatus::requesting || queuedPhraseReady_)) {
            processControls.mute = true;
        }

        const BlockResult result = downspout::sidecar::processBlock(engine_,
                                                                    processControls,
                                                                    transport,
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
            startServerRequest(generationControls, seed);
            return;
        }

        const std::uint32_t guidedSeed = seed ^ capturedSeed_;
        Phrase phrase = downspout::sidecar::makeFallbackPhrase(generationControls, guidedSeed == 0u ? seed : guidedSeed);
        downspout::sidecar::setPhrase(engine_, phrase);
        setConnectionStatus(ConnectionStatus::local);
    }

    void armCurrentPhrase()
    {
        if (!engine_.phraseReady && !queuedPhraseReady_)
            return;

        if (!queuedPhraseReady_) {
            queuedPhrase_ = engine_.phrase;
            queuedServerControls_ = engine_.controls;
            queuedPhraseReady_ = true;
        }
        queuedActivationBeat_ = nextActivationBeat(lastTransport_);
        setConnectionStatus(sourceMode_ == SourceMode::server ? ConnectionStatus::ready : ConnectionStatus::local);
    }

    void startServerRequest(const Controls& generationControls, const std::uint32_t seed)
    {
        if (worker_.joinable()) {
            WorkerResult result {};
            bool hasFinishedWorker = false;
            {
                std::lock_guard<std::mutex> lock(workerMutex_);
                hasFinishedWorker = workerResult_.done;
            }
            if (hasFinishedWorker)
                worker_.detach();
            else
                return;
        }

        const Controls serverControls = controlsForServerRequest(generationControls);
        queuedServerControls_ = serverControls;
        const std::uint32_t guidedSeed = seed ^ capturedSeed_;
        const std::string request = controlsToTuneStateJson(serverControls,
                                                            capturedPitchClasses_,
                                                            guidedSeed == 0u ? seed : guidedSeed);
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            workerResult_ = WorkerResult {};
        }
        queuedPhraseReady_ = false;
        setConnectionStatus(ConnectionStatus::requesting);

        worker_ = std::thread([this, request]() {
            WorkerResult result {};
            const std::optional<HttpResult> response = postCoordinatorRequest("/openai", request);
            if (!response.has_value()) {
                result.done = true;
                result.connected = false;
            } else if (response->status == 200) {
                result.connected = true;
                const auto phrase = downspout::sidecar::deserializePhraseJson(response->body);
                result.done = true;
                result.ok = phrase.has_value();
                if (phrase.has_value())
                    result.phrase = *phrase;
            } else {
                result.done = true;
                result.connected = true;
                result.ok = false;
            }

            std::lock_guard<std::mutex> lock(workerMutex_);
            workerResult_ = result;
        });
    }

    void setConnectionStatus(const ConnectionStatus status)
    {
        connectionStatus_ = status;
    }

    void applyAsyncResult(const TransportSnapshot& transport)
    {
        WorkerResult result {};
        bool hasResult = false;
        if (workerMutex_.try_lock()) {
            if (workerResult_.done) {
                result = workerResult_;
                workerResult_ = WorkerResult {};
                hasResult = true;
            }
            workerMutex_.unlock();
        }
        if (!hasResult)
            return;

        if (worker_.joinable())
            worker_.detach();

        if (sourceMode_ != SourceMode::server) {
            setConnectionStatus(ConnectionStatus::local);
            return;
        }

        if (!result.ok) {
            setConnectionStatus(result.connected ? ConnectionStatus::serverError : ConnectionStatus::serverOffline);
            return;
        }

        queuedPhrase_ = result.phrase;
        queuedPhraseReady_ = true;
        queuedActivationBeat_ = nextActivationBeat(transport);
        setConnectionStatus(ConnectionStatus::ready);
    }

    [[nodiscard]] double nextActivationBeat(const TransportSnapshot& transport) const
    {
        if (!transport.valid || transport.beatsPerBar <= 0.0)
            return 0.0;
        const double currentBeat = transport.bar * transport.beatsPerBar + transport.barBeat;
        const double currentBar = std::floor(currentBeat / transport.beatsPerBar);
        return (currentBar + 1.0) * transport.beatsPerBar;
    }

    void activateQueuedPhrase(const TransportSnapshot& transport)
    {
        if (!queuedPhraseReady_)
            return;
        if (transport.valid && transport.beatsPerBar > 0.0) {
            const double currentBeat = transport.bar * transport.beatsPerBar + transport.barBeat;
            if (currentBeat + 0.0001 < queuedActivationBeat_)
                return;
        }

        engine_.controls = queuedServerControls_;
        downspout::sidecar::setPhrase(engine_, phraseShiftedToActivation(queuedPhrase_, queuedActivationBeat_));
        queuedPhraseReady_ = false;
    }

    [[nodiscard]] Phrase phraseShiftedToActivation(const Phrase& phrase, const double activationBeat) const
    {
        Phrase shifted = phrase;
        const float phraseBeats = static_cast<float>(std::max(1, phrase.bars) * std::max(1, phrase.beatsPerBar));
        if (phraseBeats <= 0.0f)
            return shifted;

        const float offset = std::fmod(static_cast<float>(activationBeat), phraseBeats);
        if (std::fabs(offset) < 0.0001f)
            return shifted;

        for (int i = 0; i < shifted.eventCount; ++i) {
            PhraseEvent& event = shifted.events[static_cast<std::size_t>(i)];
            event.beat = std::fmod(event.beat + offset, phraseBeats);
            if (event.beat < 0.0f)
                event.beat += phraseBeats;
            event.duration = std::min(std::max(event.duration, 0.05f), phraseBeats - event.beat);
        }
        std::stable_sort(shifted.events.begin(),
                         shifted.events.begin() + shifted.eventCount,
                         [](const PhraseEvent& left, const PhraseEvent& right) {
                             return left.beat < right.beat;
                         });

        Phrase result {};
        result.version = shifted.version;
        result.bars = shifted.bars;
        result.beatsPerBar = shifted.beatsPerBar;
        float previousEnd = 0.0f;
        for (int i = 0; i < shifted.eventCount; ++i) {
            PhraseEvent event = shifted.events[static_cast<std::size_t>(i)];
            if (event.beat < previousEnd)
                event.beat = previousEnd;
            event.duration = std::min(event.duration, phraseBeats - event.beat);
            if (event.duration <= 0.01f || event.beat >= phraseBeats)
                continue;
            result.events[static_cast<std::size_t>(result.eventCount++)] = event;
            previousEnd = event.beat + event.duration;
        }
        return result.eventCount > 0 ? result : phrase;
    }

    void joinWorker()
    {
        if (worker_.joinable())
            worker_.join();
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
    std::thread worker_ {};
    std::mutex workerMutex_ {};
    WorkerResult workerResult_ {};
    Phrase queuedPhrase_ {};
    Controls queuedServerControls_ {};
    bool queuedPhraseReady_ = false;
    double queuedActivationBeat_ = 0.0;
    TransportSnapshot lastTransport_ {};
    float generateCounter_ = 0.0f;
    float playCounter_ = 0.0f;
    float acceptCounter_ = 0.0f;
    float retryCounter_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidecarPlugin)
};

Plugin* createPlugin()
{
    return new SidecarPlugin();
}

END_NAMESPACE_DISTRHO
