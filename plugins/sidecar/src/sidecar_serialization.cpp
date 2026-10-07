#include "sidecar_serialization.hpp"

#include "sidecar_protocol.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

namespace downspout::sidecar {
namespace {

template <typename T>
bool parseInteger(const std::string_view text, T& value)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseFloat(const std::string_view text, float& value)
{
    std::string local(text);
    char* parseEnd = nullptr;
    value = std::strtof(local.c_str(), &parseEnd);
    return parseEnd && *parseEnd == '\0';
}

[[nodiscard]] std::optional<std::string_view> jsonValueForKey(const std::string_view text, const std::string_view key)
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
    if (text[start] == '"') {
        const std::size_t end = text.find('"', start + 1);
        if (end == std::string_view::npos)
            return std::nullopt;
        return text.substr(start + 1, end - start - 1);
    }
    std::size_t end = start;
    while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != ']')
        ++end;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(start, end - start);
}

template <typename T>
bool parseJsonInteger(const std::string_view object, const std::string_view key, T& value)
{
    const auto text = jsonValueForKey(object, key);
    return text.has_value() && parseInteger(*text, value);
}

bool parseJsonFloat(const std::string_view object, const std::string_view key, float& value)
{
    const auto text = jsonValueForKey(object, key);
    return text.has_value() && parseFloat(*text, value);
}

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

}  // namespace

std::string serializePhrase(const Phrase& phrase)
{
    std::ostringstream out;
    out << "version=" << phrase.version << '\n';
    out << "bars=" << phrase.bars << '\n';
    out << "beatsPerBar=" << phrase.beatsPerBar << '\n';
    out << "eventCount=" << phrase.eventCount << '\n';
    for (int i = 0; i < phrase.eventCount; ++i)
    {
        const PhraseEvent& event = phrase.events[i];
        out << "event=" << event.beat << ','
            << event.duration << ','
            << event.note << ','
            << event.velocity << '\n';
    }
    return out.str();
}

std::optional<Phrase> deserializePhrase(const std::string& text)
{
    Phrase phrase {};
    int eventIndex = 0;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find('\n', start);
        const std::string_view line = end == std::string::npos
            ? std::string_view(text).substr(start)
            : std::string_view(text).substr(start, end - start);
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty())
            continue;

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        if (key == "version")
        {
            if (!parseInteger(value, phrase.version))
                return std::nullopt;
        }
        else if (key == "bars")
        {
            if (!parseInteger(value, phrase.bars))
                return std::nullopt;
        }
        else if (key == "beatsPerBar")
        {
            if (!parseInteger(value, phrase.beatsPerBar))
                return std::nullopt;
        }
        else if (key == "eventCount")
        {
            if (!parseInteger(value, phrase.eventCount))
                return std::nullopt;
        }
        else if (key == "event")
        {
            if (eventIndex >= kMaxPhraseEvents)
                return std::nullopt;
            PhraseEvent event {};
            std::size_t fieldStart = 0;
            for (int field = 0; field < 4; ++field)
            {
                const std::size_t fieldEnd = value.find(',', fieldStart);
                const std::string_view fieldText = fieldEnd == std::string_view::npos
                    ? value.substr(fieldStart)
                    : value.substr(fieldStart, fieldEnd - fieldStart);
                if ((field == 0 && !parseFloat(fieldText, event.beat)) ||
                    (field == 1 && !parseFloat(fieldText, event.duration)) ||
                    (field == 2 && !parseInteger(fieldText, event.note)) ||
                    (field == 3 && !parseInteger(fieldText, event.velocity)))
                {
                    return std::nullopt;
                }
                fieldStart = fieldEnd == std::string_view::npos ? value.size() : fieldEnd + 1;
            }
            phrase.events[eventIndex++] = event;
        }
        else
        {
            return std::nullopt;
        }
    }

    if (phrase.eventCount != eventIndex)
        return std::nullopt;
    Controls validationControls {};
    validationControls.reg = RegisterId::custom;
    validationControls.registerLow = 0;
    validationControls.registerHigh = 127;
    if (!validatePhrase(phrase, validationControls).valid)
        return std::nullopt;
    return phrase;
}

std::optional<Phrase> deserializePhraseJson(const std::string& text)
{
    Phrase phrase {};
    parseJsonInteger(std::string_view(text), "version", phrase.version);
    parseJsonInteger(std::string_view(text), "bars", phrase.bars);
    parseJsonInteger(std::string_view(text), "beats_per_bar", phrase.beatsPerBar);
    parseJsonInteger(std::string_view(text), "beatsPerBar", phrase.beatsPerBar);
    phrase.version = kPhraseStateVersion;
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
    while (cursor < arrayClose && phrase.eventCount < kMaxPhraseEvents) {
        const std::size_t objectOpen = text.find('{', cursor);
        if (objectOpen == std::string::npos || objectOpen >= arrayClose)
            break;
        const std::size_t objectClose = text.find('}', objectOpen);
        if (objectClose == std::string::npos || objectClose > arrayClose)
            return std::nullopt;

        const std::string_view object = std::string_view(text).substr(objectOpen, objectClose - objectOpen + 1);
        PhraseEvent event {};
        if (!parseJsonFloat(object, "beat", event.beat) ||
            !parseJsonFloat(object, "duration", event.duration) ||
            !parseJsonInteger(object, "note", event.note) ||
            !parseJsonInteger(object, "velocity", event.velocity)) {
            return std::nullopt;
        }
        phrase.events[static_cast<std::size_t>(phrase.eventCount++)] = event;
        cursor = objectClose + 1;
    }

    Controls validationControls {};
    validationControls.reg = RegisterId::custom;
    validationControls.registerLow = 0;
    validationControls.registerHigh = 127;
    validationControls.bars = phrase.bars;
    if (!validatePhrase(phrase, validationControls).valid)
        return std::nullopt;
    return phrase;
}

}  // namespace downspout::sidecar
