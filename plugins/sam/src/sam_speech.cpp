#include "sam_speech.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

extern "C" {
#include "reciter.h"
#include "sam.h"
}

// The vendored SAM code reads this global (it was main.c's debug switch).
extern "C" {
int debug = 0;
}

namespace downspout::sam {
namespace {

std::mutex& samLock()
{
    static std::mutex lock;
    return lock;
}

[[nodiscard]] unsigned char byteParameter(const int value, const int lo, const int hi) noexcept
{
    return static_cast<unsigned char>(std::clamp(value, lo, hi));
}

}  // namespace

std::size_t renderWord(const char* word, const std::size_t wordLength, const VoiceSettings& settings,
                       std::uint8_t* out, const std::size_t capacity)
{
    if (word == nullptr || wordLength == 0 || out == nullptr || capacity == 0)
        return 0;

    // TextToPhonemes() walks all 256 bytes of its input, so give it a zeroed block.
    unsigned char input[256] = {};
    const std::size_t length = std::min(wordLength, kMaxWordChars);
    for (std::size_t i = 0; i < length; ++i)
        input[i] = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(word[i])));

    std::lock_guard<std::mutex> guard(samLock());

    if (settings.phonetic) {
        input[length] = 0x9b;
    } else {
        input[length] = '[';
        if (!TextToPhonemes(input))
            return 0;
    }

    SetSingmode(settings.sing ? 1 : 0);
    SetSpeed(byteParameter(settings.speed, 1, 255));
    SetPitch(byteParameter(settings.pitch, 0, 255));
    SetMouth(byteParameter(settings.mouth, 0, 255));
    SetThroat(byteParameter(settings.throat, 0, 255));
    SetInput(input);
    if (!SAMMain())
        return 0;

    const int produced = GetBufferLength() / 50;
    if (produced <= 0)
        return 0;
    const std::size_t count = std::min({static_cast<std::size_t>(produced), capacity, kMaxRenderSamples});
    std::memcpy(out, GetBuffer(), count);
    return count;
}

std::string sanitizeText(const std::string& raw)
{
    std::string text;
    text.reserve(std::min(raw.size(), kMaxTextBytes));
    for (const char c : raw) {
        if (text.size() >= kMaxTextBytes)
            break;
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == '\n' || u == '\r' || u == '\t')
            text.push_back(' ');
        else if (u >= 0x20 && u < 0x7f)
            text.push_back(c);
    }
    return text;
}

std::size_t cleanWord(const std::string& token, const bool phonetic, char* out)
{
    std::size_t length = 0;
    for (const char c : token) {
        if (length >= kMaxWordChars)
            break;
        const unsigned char u = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(c)));
        const bool letter = u >= 'A' && u <= 'Z';
        const bool punctuation = u == '.' || u == ',' || u == '?' || u == '-';
        if (phonetic ? (letter || punctuation || u == '/' || (u >= '0' && u <= '9'))
                     : (letter || punctuation || u == '\''))
            out[length++] = static_cast<char>(u);
    }
    return length;
}

std::string sanitizeWord(const std::string& token, const bool phonetic)
{
    char buffer[kMaxWordChars + 1];
    return std::string(buffer, cleanWord(token, phonetic, buffer));
}

Script parseScript(const std::string& raw)
{
    Script script;
    script.text = sanitizeText(raw);
    const std::string& text = script.text;

    std::size_t i = 0;
    while (i < text.size() && script.tokens.size() < kMaxSlots) {
        while (i < text.size() && text[i] == ' ')
            ++i;
        if (i >= text.size())
            break;
        const std::size_t begin = i;
        while (i < text.size() && text[i] != ' ')
            ++i;
        Token token;
        token.begin = begin;
        token.length = i - begin;
        token.word = text.substr(begin, std::min(token.length, kMaxWordChars));
        token.rest = token.word == "_";
        script.tokens.push_back(std::move(token));
    }
    return script;
}

}  // namespace downspout::sam
