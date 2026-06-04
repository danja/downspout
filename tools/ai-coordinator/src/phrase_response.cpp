#include "ai_coordinator.hpp"

#include "sidecar_protocol.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace downspout::ai_coordinator {
namespace {

[[nodiscard]] std::optional<std::string_view> valueForKey(const std::string_view text, const std::string_view key)
{
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t keyPos = text.find(needle);
    if (keyPos == std::string_view::npos)
        return std::nullopt;
    const std::size_t colon = text.find(':', keyPos + needle.size());
    if (colon == std::string_view::npos)
        return std::nullopt;
    std::size_t start = colon + 1;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    if (start >= text.size())
        return std::nullopt;
    std::size_t end = start;
    if (text[start] == '"') {
        end = text.find('"', start + 1);
        if (end == std::string_view::npos)
            return std::nullopt;
        return text.substr(start + 1, end - start - 1);
    }
    while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != ']')
        ++end;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(start, end - start);
}

template <typename T>
bool parseInteger(const std::optional<std::string_view> value, T& target)
{
    if (!value.has_value())
        return false;
    T parsed {};
    const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (result.ec != std::errc() || result.ptr != value->data() + value->size())
        return false;
    target = parsed;
    return true;
}

bool parseFloat(const std::optional<std::string_view> value, float& target)
{
    if (!value.has_value())
        return false;
    std::string local(*value);
    char* end = nullptr;
    const float parsed = std::strtof(local.c_str(), &end);
    if (end == nullptr || *end != '\0')
        return false;
    target = parsed;
    return true;
}

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] int distinctNoteCount(const downspout::sidecar::Phrase& phrase)
{
    std::array<bool, 128> used {};
    int count = 0;
    for (int i = 0; i < phrase.eventCount; ++i) {
        const int note = clampi(phrase.events[static_cast<std::size_t>(i)].note, 0, 127);
        if (!used[static_cast<std::size_t>(note)]) {
            used[static_cast<std::size_t>(note)] = true;
            ++count;
        }
    }
    return count;
}

void repairLowPitchVariety(downspout::sidecar::Phrase& phrase, const TuneState& state)
{
    const int registerSpan = std::max(0, state.registerHigh - state.registerLow);
    const int minimumDistinct = registerSpan >= 12 ? 4 : (registerSpan >= 5 ? 3 : 2);
    if (phrase.eventCount < minimumDistinct || distinctNoteCount(phrase) >= minimumDistinct)
        return;

    TuneState fallbackState = state;
    fallbackState.seed = state.seed == 0 ? 17u : state.seed ^ 0x5bd1e995u;
    fallbackState.density = std::max(state.density, 0.55f);
    fallbackState.risk = std::max(state.risk, 0.35f);
    const downspout::sidecar::Phrase fallback = generateSoloPhrase(fallbackState);
    if (fallback.eventCount <= 0)
        return;

    for (int i = 0; i < phrase.eventCount; ++i) {
        const downspout::sidecar::PhraseEvent& source = fallback.events[static_cast<std::size_t>(i % fallback.eventCount)];
        phrase.events[static_cast<std::size_t>(i)].note = clampi(source.note, state.registerLow, state.registerHigh);
    }

    if (distinctNoteCount(phrase) >= minimumDistinct)
        return;

    phrase = fallback;
    phrase.bars = state.bars;
    phrase.beatsPerBar = state.beatsPerBar;
}

}  // namespace

std::optional<downspout::sidecar::Phrase> parsePhraseResponseJson(const std::string& text)
{
    downspout::sidecar::Phrase phrase {};
    parseInteger(valueForKey(text, "version"), phrase.version);
    parseInteger(valueForKey(text, "bars"), phrase.bars);
    parseInteger(valueForKey(text, "beats_per_bar"), phrase.beatsPerBar);
    parseInteger(valueForKey(text, "beatsPerBar"), phrase.beatsPerBar);
    phrase.version = downspout::sidecar::kPhraseStateVersion;
    phrase.bars = clampi(phrase.bars, 1, 8);
    phrase.beatsPerBar = clampi(phrase.beatsPerBar, 1, 12);

    const std::size_t eventsKey = text.find("\"events\"");
    if (eventsKey == std::string::npos)
        return std::nullopt;
    const std::size_t arrayOpen = text.find('[', eventsKey);
    const std::size_t arrayClose = arrayOpen == std::string::npos ? std::string::npos : text.find(']', arrayOpen);
    if (arrayOpen == std::string::npos || arrayClose == std::string::npos)
        return std::nullopt;

    std::size_t cursor = arrayOpen + 1;
    while (cursor < arrayClose && phrase.eventCount < downspout::sidecar::kMaxPhraseEvents) {
        const std::size_t objectOpen = text.find('{', cursor);
        if (objectOpen == std::string::npos || objectOpen >= arrayClose)
            break;
        const std::size_t objectClose = text.find('}', objectOpen);
        if (objectClose == std::string::npos || objectClose > arrayClose)
            return std::nullopt;

        const std::string_view object = std::string_view(text).substr(objectOpen, objectClose - objectOpen + 1);
        downspout::sidecar::PhraseEvent event {};
        if (!parseFloat(valueForKey(object, "beat"), event.beat) ||
            !parseFloat(valueForKey(object, "duration"), event.duration) ||
            !parseInteger(valueForKey(object, "note"), event.note) ||
            !parseInteger(valueForKey(object, "velocity"), event.velocity)) {
            return std::nullopt;
        }
        phrase.events[static_cast<std::size_t>(phrase.eventCount++)] = event;
        cursor = objectClose + 1;
    }

    downspout::sidecar::Controls validationControls {};
    validationControls.reg = downspout::sidecar::RegisterId::custom;
    validationControls.registerLow = 0;
    validationControls.registerHigh = 127;
    validationControls.bars = phrase.bars;
    if (!downspout::sidecar::validatePhrase(phrase, validationControls).valid)
        return std::nullopt;
    return phrase;
}

std::optional<downspout::sidecar::Phrase> loadPhraseResponseJson(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
        return std::nullopt;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parsePhraseResponseJson(buffer.str());
}

downspout::sidecar::Phrase constrainPhraseToTuneState(const downspout::sidecar::Phrase& rawPhrase,
                                                      const TuneState& rawState)
{
    TuneState state = rawState;
    state.bars = clampi(state.bars, 1, 8);
    state.beatsPerBar = clampi(state.beatsPerBar, 1, 12);
    state.registerLow = clampi(state.registerLow, 0, 127);
    state.registerHigh = clampi(state.registerHigh, 0, 127);
    if (state.registerHigh < state.registerLow)
        std::swap(state.registerLow, state.registerHigh);

    std::array<downspout::sidecar::PhraseEvent, downspout::sidecar::kMaxPhraseEvents> events = rawPhrase.events;
    const int rawCount = clampi(rawPhrase.eventCount, 0, downspout::sidecar::kMaxPhraseEvents);
    std::stable_sort(events.begin(), events.begin() + rawCount, [](const auto& left, const auto& right) {
        return left.beat < right.beat;
    });

    downspout::sidecar::Phrase phrase {};
    phrase.version = downspout::sidecar::kPhraseStateVersion;
    phrase.bars = state.bars;
    phrase.beatsPerBar = state.beatsPerBar;

    const float totalBeats = static_cast<float>(phrase.bars * phrase.beatsPerBar);
    float previousEnd = 0.0f;
    for (int i = 0; i < rawCount; ++i) {
        downspout::sidecar::PhraseEvent event = events[static_cast<std::size_t>(i)];
        event.beat = std::max(0.0f, event.beat);
        if (event.beat < previousEnd)
            event.beat = previousEnd;
        if (event.beat >= totalBeats)
            continue;

        event.duration = std::max(0.05f, event.duration);
        event.duration = std::min(event.duration, totalBeats - event.beat);
        if (event.duration <= 0.01f)
            continue;

        while (event.note < state.registerLow && event.note + 12 <= state.registerHigh)
            event.note += 12;
        while (event.note > state.registerHigh && event.note - 12 >= state.registerLow)
            event.note -= 12;
        event.note = clampi(event.note, state.registerLow, state.registerHigh);
        event.velocity = clampi(event.velocity, 1, 127);

        phrase.events[static_cast<std::size_t>(phrase.eventCount++)] = event;
        previousEnd = event.beat + event.duration;
        if (phrase.eventCount >= downspout::sidecar::kMaxPhraseEvents)
            break;
    }

    repairLowPitchVariety(phrase, state);
    return phrase;
}

std::string serializePhraseResponseJson(const downspout::sidecar::Phrase& phrase)
{
    std::ostringstream out;
    out << "{";
    out << "\"version\":" << downspout::sidecar::kPhraseStateVersion << ',';
    out << "\"bars\":" << phrase.bars << ',';
    out << "\"beats_per_bar\":" << phrase.beatsPerBar << ',';
    out << "\"events\":[";
    for (int i = 0; i < phrase.eventCount; ++i) {
        if (i > 0)
            out << ',';
        const downspout::sidecar::PhraseEvent& event = phrase.events[static_cast<std::size_t>(i)];
        out << "{\"beat\":" << event.beat
            << ",\"duration\":" << event.duration
            << ",\"note\":" << event.note
            << ",\"velocity\":" << event.velocity
            << '}';
    }
    out << "]}";
    return out.str();
}

}  // namespace downspout::ai_coordinator
