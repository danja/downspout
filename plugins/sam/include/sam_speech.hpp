#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace downspout::sam {

// ── SAM renderer ───────────────────────────────────────────────────────────
//
// A thin C++ face for the vendored SAM code (third_party/sam). SAM keeps all of
// its working state in globals, so renders are serialised by one process-wide
// lock; a word takes tens of microseconds, so the audio thread may render one.

inline constexpr std::size_t kMaxRenderSamples = 22050 * 10;  // SAM's own limit
inline constexpr std::size_t kMaxWordChars = 48;              // keeps the phoneme string in range

struct VoiceSettings {
    int speed = 72;
    int pitch = 64;
    int mouth = 128;
    int throat = 128;
    bool sing = false;      // fixed pitch: pitch is then a glottal period, see sam_params.hpp
    bool phonetic = false;  // the word is SAM phoneme codes, not English
};

// Renders one word (already cleaned, see cleanWord) to unsigned 8-bit samples at
// 22.05 kHz, where 128 is silence. Returns the sample count, or 0 when SAM rejects
// the word. `capacity` bounds `out`. Does not allocate.
[[nodiscard]] std::size_t renderWord(const char* word, std::size_t length, const VoiceSettings& settings,
                                     std::uint8_t* out, std::size_t capacity);

[[nodiscard]] inline std::size_t renderWord(const std::string& word, const VoiceSettings& settings,
                                            std::uint8_t* out, std::size_t capacity)
{
    return renderWord(word.data(), word.size(), settings, out, capacity);
}

// ── Script ─────────────────────────────────────────────────────────────────
//
// The edit-box text, split into one slot per whitespace-separated token. A token
// "_" is a rest. What SAM can read depends on the Phonetic switch, so a token is
// kept as typed and cleaned with sanitizeWord() when it is spoken; one that
// cleans to nothing is silent. The slot count always equals the token count, so
// the panel can highlight a slot by index.

inline constexpr std::size_t kMaxTextBytes = 2000;
inline constexpr std::size_t kMaxSlots = 512;

struct Token {
    std::size_t begin = 0;   // byte offset of the token in the source text
    std::size_t length = 0;
    std::string word;        // as typed (clipped to kMaxWordChars); "_" for a rest
    bool rest = false;
};

struct Script {
    std::string text;
    std::vector<Token> tokens;
    [[nodiscard]] std::size_t size() const noexcept { return tokens.size(); }
};

// Keeps printable ASCII, folds newlines and tabs to a space (run lengths are kept, so
// positions in the text do not move), drops everything else, and truncates to
// kMaxTextBytes.
[[nodiscard]] std::string sanitizeText(const std::string& raw);

// Upper-cases and removes what SAM cannot read. Phonetic words keep digits (stress
// marks) and '/'; English words keep letters and the punctuation SAM inflects on.
[[nodiscard]] std::string sanitizeWord(const std::string& token, bool phonetic);

// The same cleaning into a caller's buffer of at least kMaxWordChars characters, for the
// audio thread. Returns the length written.
std::size_t cleanWord(const std::string& token, bool phonetic, char* out);

[[nodiscard]] Script parseScript(const std::string& text);

}  // namespace downspout::sam
