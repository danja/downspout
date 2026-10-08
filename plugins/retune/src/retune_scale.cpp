#include "retune_scale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace downspout::retune {

namespace {

constexpr int kMaxDegrees = 1024;

std::string trimmed(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}

bool parseNumber(const std::string& token, double& out)
{
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end != token.c_str() && *end == '\0' && std::isfinite(out);
}

// One pitch line to cents. Cents (contains '.') or ratio (a/b or a).
bool parsePitch(const std::string& rawLine, double& cents)
{
    std::istringstream words(trimmed(rawLine));
    std::string token;
    if (!(words >> token)) return false;
    if (token.find('.') != std::string::npos) return parseNumber(token, cents);

    const auto slash = token.find('/');
    double num = 0.0;
    double den = 1.0;
    if (slash == std::string::npos) {
        if (!parseNumber(token, num)) return false;
    } else if (!parseNumber(token.substr(0, slash), num) || !parseNumber(token.substr(slash + 1), den)) {
        return false;
    }
    if (num <= 0.0 || den <= 0.0) return false;
    cents = 1200.0 * std::log2(num / den);
    return true;
}

}  // namespace

std::optional<Scale> parseScl(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.front() == '!') continue;
        lines.push_back(line);
    }
    // description, count, then pitches. The description may be blank.
    if (lines.size() < 2) return std::nullopt;

    Scale scale;
    scale.description = trimmed(lines[0]);

    double countValue = 0.0;
    std::istringstream countWords(trimmed(lines[1]));
    std::string countToken;
    if (!(countWords >> countToken) || !parseNumber(countToken, countValue)) return std::nullopt;
    const int count = static_cast<int>(countValue);
    if (count < 1 || count > kMaxDegrees || static_cast<double>(count) != countValue) return std::nullopt;

    if (static_cast<int>(lines.size()) < 2 + count) return std::nullopt;
    for (int i = 0; i < count; ++i) {
        double cents = 0.0;
        if (!parsePitch(lines[2 + i], cents)) return std::nullopt;
        scale.cents.push_back(cents);
    }
    for (std::size_t i = 2 + count; i < lines.size(); ++i)
        if (!trimmed(lines[i]).empty()) return std::nullopt;  // junk after the declared pitches

    if (scale.cents.back() <= 0.0) return std::nullopt;
    return scale;
}

std::optional<Scale> loadSclFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() > 256 * 1024) return std::nullopt;
    return parseScl(text);
}

Scale equalTemperament()
{
    Scale scale;
    scale.description = "12-tone equal temperament";
    for (int i = 1; i <= 12; ++i) scale.cents.push_back(100.0 * i);
    return scale;
}

double noteCents(const Scale& scale, int rootNote, int note)
{
    const int n = scale.size();
    if (n == 0) return 100.0 * (note - rootNote);
    const int offset = note - rootNote;
    int octave = offset / n;
    int degree = offset % n;
    if (degree < 0) {
        degree += n;
        --octave;
    }
    const double base = degree == 0 ? 0.0 : scale.cents[degree - 1];
    return base + octave * scale.period();
}

std::optional<NoteTuning> tuneNote(const Scale& scale, int rootNote, int note, double bendRangeSemitones)
{
    if (bendRangeSemitones <= 0.0) return std::nullopt;
    const double cents = noteCents(scale, rootNote, note);
    // Anchor the 12-TET grid at the root so the root itself needs no bend.
    const int steps = static_cast<int>(std::lround(cents / 100.0));
    const int carrier = rootNote + steps;
    if (carrier < 0 || carrier > 127) return std::nullopt;

    const double correction = cents - 100.0 * steps;  // within +-50 cents
    const double fraction = correction / (bendRangeSemitones * 100.0);
    if (std::fabs(fraction) > 1.0) return std::nullopt;
    NoteTuning t;
    t.carrier = carrier;
    t.bend = std::clamp(static_cast<int>(std::lround(8192.0 + fraction * 8192.0)), 0, 16383);
    return t;
}

}  // namespace downspout::retune
