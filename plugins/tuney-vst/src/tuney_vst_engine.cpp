#include "tuney_vst_engine.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace downspout::tuney_vst {
namespace {

constexpr double kPi = 3.14159265358979323846;

float clampParameter(const ParameterSpec& spec, float value)
{
    value = std::clamp(value, spec.minimum, spec.maximum);
    return spec.integer ? std::round(value) : value;
}

std::string percentEncode(std::string_view value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool percentDecode(std::string_view value, std::string& out)
{
    out.clear();
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            out.push_back(value[i]);
            continue;
        }
        if (i + 2 >= value.size()) return false;
        const int hi = hexValue(value[i + 1]);
        const int lo = hexValue(value[i + 2]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return true;
}

std::vector<std::string_view> splitSemicolon(std::string_view text)
{
    std::vector<std::string_view> result;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find(';', begin);
        auto item = text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.remove_prefix(1);
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.remove_suffix(1);
        if (!item.empty()) result.push_back(item);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return result;
}

class ExpressionParser {
public:
    explicit ExpressionParser(std::string_view text) : text_(text) {}
    double parse()
    {
        const double value = expression();
        whitespace();
        if (pos_ != text_.size() || !std::isfinite(value)) throw std::invalid_argument("bad tuning expression");
        return value;
    }
private:
    double expression()
    {
        double value = term();
        for (;;) {
            whitespace();
            if (take('+')) value += term();
            else if (take('-')) value -= term();
            else return value;
        }
    }
    double term()
    {
        double value = power();
        for (;;) {
            whitespace();
            if (take('*')) value *= power();
            else if (take('/')) value /= power();
            else if (take('%')) value = std::fmod(value, power());
            else return value;
        }
    }
    double power()
    {
        double value = unary();
        whitespace();
        if (take('^')) value = std::pow(value, power());
        return value;
    }
    double unary()
    {
        whitespace();
        if (take('+')) return unary();
        if (take('-')) return -unary();
        if (text_.substr(pos_, 6) == "cents(") {
            pos_ += 6;
            const double value = expression();
            whitespace();
            if (!take(')')) throw std::invalid_argument("missing )");
            return std::pow(2.0, value / 1200.0);
        }
        if (take('(')) {
            const double value = expression();
            whitespace();
            if (!take(')')) throw std::invalid_argument("missing )");
            return value;
        }
        return number();
    }
    double number()
    {
        whitespace();
        const char* start = text_.data() + pos_;
        char* finish = nullptr;
        const double value = std::strtod(start, &finish);
        if (finish == start) throw std::invalid_argument("number expected");
        pos_ += static_cast<std::size_t>(finish - start);
        return value;
    }
    void whitespace() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    bool take(char c) { if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; } return false; }
    std::string_view text_;
    std::size_t pos_ = 0;
};

bool isAsciiAlpha(std::string_view c)
{
    return c.size() == 1 && ((c[0] >= 'A' && c[0] <= 'Z') || (c[0] >= 'a' && c[0] <= 'z'));
}

std::string stripLatinAccent(const std::string& c)
{
    static const std::pair<std::string_view, char> accents[] = {
        {"À",'A'},{"Á",'A'},{"Â",'A'},{"Ã",'A'},{"Ä",'A'},{"Å",'A'},{"à",'a'},{"á",'a'},{"â",'a'},{"ã",'a'},{"ä",'a'},{"å",'a'},
        {"Ç",'C'},{"ç",'c'},{"È",'E'},{"É",'E'},{"Ê",'E'},{"Ë",'E'},{"è",'e'},{"é",'e'},{"ê",'e'},{"ë",'e'},
        {"Ì",'I'},{"Í",'I'},{"Î",'I'},{"Ï",'I'},{"ì",'i'},{"í",'i'},{"î",'i'},{"ï",'i'},
        {"Ñ",'N'},{"ñ",'n'},{"Ò",'O'},{"Ó",'O'},{"Ô",'O'},{"Õ",'O'},{"Ö",'O'},{"Ø",'O'},{"ò",'o'},{"ó",'o'},{"ô",'o'},{"õ",'o'},{"ö",'o'},{"ø",'o'},
        {"Ù",'U'},{"Ú",'U'},{"Û",'U'},{"Ü",'U'},{"ù",'u'},{"ú",'u'},{"û",'u'},{"ü",'u'},{"Ý",'Y'},{"Ÿ",'Y'},{"ý",'y'},{"ÿ",'y'}
    };
    for (const auto& [accent, plain] : accents) if (c == accent) return std::string(1, plain);
    return c;
}

int centeredNote(int index, int span, int offset)
{
    return static_cast<int>(std::floor(index - (span - 1) / 2.0 + 19.5 + offset + 0.5));
}

} // namespace

const std::array<ParameterSpec, kParamCount> kParameterSpecs {{
    {"Play", "play", 0, 1, 0, true, true}, {"Stop", "stop", 0, 1, 0, true, true},
    {"Loop", "loop", 0, 1, 0, true, true}, {"Rate", "rate", 0.25f, 4, 1, false, false},
    {"Audio", "audio_enabled", 0, 1, 1, true, true}, {"MIDI", "midi_enabled", 0, 1, 1, true, true},
    {"Gain", "gain", 0, 2, 1, false, false}, {"Waveform", "waveform", 0, 2, 2, true, false},
    {"Duty", "duty", 0, 1, 0.5f, false, false}, {"Audio Offset", "audio_note_offset", -99, 99, 44, true, false},
    {"Polyphony", "polyphony", 1, 32, 10, true, false}, {"Headroom", "headroom", 1, 32, 4, false, false},
    {"Minimum Note", "minimum_note_ms", 0, 4000, 500, false, false}, {"Map Length", "mapper_length", 0, 128, 0, true, false},
    {"Case Sensitive", "case_sensitive", 0, 1, 1, true, true}, {"Invert", "invert", 0, 1, 0, true, true},
    {"Map Offset", "mapper_offset", -99, 99, 0, true, false}, {"Range", "range_limit", 0, 128, 60, true, false},
    {"Limiter", "limiter", 0, 2, 0, true, false}, {"Tuning", "tuning_type", 0, 2, 0, true, false},
    {"Notes / Octave", "notes_per_octave", 1, 128, 12, true, false}, {"Just Limit", "just_limit", 0, 31, 0, true, false},
    {"Octave Ratio", "octave_ratio", 0.001f, 8, 2, false, false}, {"Detune", "detune", -1200, 1200, 0, false, false},
    {"Root Frequency", "root_frequency", 1, 20000, 440, false, false}, {"Root Note", "root_note", 0, 127, 69, true, false},
    {"MIDI Channel", "midi_channel", 1, 16, 1, true, false}, {"MIDI Velocity", "midi_velocity", 1, 127, 64, true, false},
    {"MIDI Offset", "midi_note_offset", -99, 99, 0, true, false}, {"Timing Scale", "timing_scale", 0.05f, 8, 3, false, false},
    {"Overlap", "overlap_ms", 0, 1000, 20, false, false}, {"Timing Seed", "timing_seed", 0, 9999, 0, true, false},
    {"Host Sync", "transport_sync", 0, 1, 0, true, true},
}};

std::vector<std::string> splitUtf8(std::string_view text)
{
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : c < 0xF8 ? 4 : 1;
        if (i + n > text.size()) n = 1;
        bool valid = n == 1 || (c >= 0xC2 && std::all_of(text.begin() + static_cast<std::ptrdiff_t>(i + 1), text.begin() + static_cast<std::ptrdiff_t>(i + n), [](char b) { return (static_cast<unsigned char>(b) & 0xC0) == 0x80; }));
        if (valid) out.emplace_back(text.substr(i, n));
        i += n;
    }
    return out;
}

double evaluateExpression(std::string_view text) { return ExpressionParser(text).parse(); }

std::string serializeState(const TuneyState& s)
{
    std::ostringstream out;
    out << "version=" << s.version << '\n' << "text=" << percentEncode(s.text) << '\n'
        << "alphabet=" << percentEncode(s.alphabet) << '\n' << "note_names=" << percentEncode(s.noteNames) << '\n'
        << "root=" << percentEncode(s.root) << '\n' << "begin=" << percentEncode(s.begin) << '\n'
        << "end=" << percentEncode(s.end) << '\n' << "notes=" << percentEncode(s.notes) << '\n' << "intervals=";
    for (std::size_t i = 0; i < s.intervals.size(); ++i) out << (i ? "," : "") << s.intervals[i];
    out << '\n' << "scale_offset=" << s.scaleOffset << '\n' << "ratios=" << percentEncode(s.ratioText) << '\n'
        << "table=" << percentEncode(s.tableText) << '\n' << "space_ms=" << s.spaceMs << '\n'
        << "dot_ms=" << s.dotMs << '\n' << "comma_ms=" << s.commaMs << '\n' << "colon_ms=" << s.colonMs << '\n'
        << "semicolon_ms=" << s.semicolonMs << '\n' << "blank_line_ms=" << s.blankLineMs << '\n'
        << "alpha_only=" << s.alphaOnly << '\n' << "strip_accents=" << s.stripAccents << '\n';
    return out.str();
}

bool deserializeState(std::string_view text, TuneyState& state, std::string* error)
{
    TuneyState candidate;
    bool sawVersion = false;
    std::size_t begin = 0;
    try {
        while (begin < text.size()) {
            const std::size_t end = text.find('\n', begin);
            const auto line = text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
            begin = end == std::string_view::npos ? text.size() : end + 1;
            if (line.empty()) continue;
            const std::size_t eq = line.find('=');
            if (eq == std::string_view::npos) throw std::invalid_argument("state line lacks =");
            const auto key = line.substr(0, eq);
            const auto value = line.substr(eq + 1);
            auto integer = [&] { return std::stoi(std::string(value)); };
            auto decoded = [&] { std::string v; if (!percentDecode(value, v)) throw std::invalid_argument("bad escape"); return v; };
            if (key == "version") { candidate.version = integer(); sawVersion = true; }
            else if (key == "text") candidate.text = decoded(); else if (key == "alphabet") candidate.alphabet = decoded();
            else if (key == "note_names") candidate.noteNames = decoded(); else if (key == "root") candidate.root = decoded();
            else if (key == "begin") candidate.begin = decoded(); else if (key == "end") candidate.end = decoded();
            else if (key == "notes") candidate.notes = decoded(); else if (key == "scale_offset") candidate.scaleOffset = integer();
            else if (key == "ratios") candidate.ratioText = decoded(); else if (key == "table") candidate.tableText = decoded();
            else if (key == "space_ms") candidate.spaceMs = integer(); else if (key == "dot_ms") candidate.dotMs = integer();
            else if (key == "comma_ms") candidate.commaMs = integer(); else if (key == "colon_ms") candidate.colonMs = integer();
            else if (key == "semicolon_ms") candidate.semicolonMs = integer(); else if (key == "blank_line_ms") candidate.blankLineMs = integer();
            else if (key == "alpha_only") candidate.alphaOnly = integer() != 0; else if (key == "strip_accents") candidate.stripAccents = integer() != 0;
            else if (key == "intervals") {
                candidate.intervals.clear();
                std::size_t p = 0;
                while (p <= value.size()) {
                    const auto comma = value.find(',', p);
                    const auto part = value.substr(p, comma == std::string_view::npos ? value.size() - p : comma - p);
                    const int v = std::stoi(std::string(part));
                    if (v < 0) throw std::invalid_argument("negative interval");
                    candidate.intervals.push_back(v);
                    if (comma == std::string_view::npos) break;
                    p = comma + 1;
                }
            }
        }
        if (!sawVersion || candidate.version != 1) throw std::invalid_argument("unsupported state version");
        if (candidate.alphabet.empty() || candidate.intervals.empty() || candidate.noteNames.empty()) throw std::invalid_argument("empty musical definition");
        state = std::move(candidate);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

TuneyEngine::TuneyEngine(double sampleRate) : sampleRate_(sampleRate)
{
    for (std::size_t i = 0; i < parameters_.size(); ++i) parameters_[i] = kParameterSpecs[i].defaultValue;
    rebuildDerived();
}

void TuneyEngine::setSampleRate(double sampleRate) { sampleRate_ = std::max(1000.0, sampleRate); rebuildSchedule(); }

void TuneyEngine::reset()
{
    for (auto& v : voices_) v = {};
    scheduleIndex_ = 0; playhead_ = 0; playing_ = false; stopRequested_ = false; playRequested_ = false;
    syncWasTransportPlaying_ = false; syncPlayheadBeats_ = 0.0; expectedHostBeat_ = -1.0; syncCycle_ = 0;
    typedRead_.store(typedWrite_.load());
}

void TuneyEngine::setParameter(std::uint32_t index, float value)
{
    if (index >= kParamCount) return;
    const float previous = parameters_[index];
    parameters_[index] = clampParameter(kParameterSpecs[index], value);
    if (index == kParamPlay && value > 0.5f) playRequested_ = true;
    if (index == kParamStop && value > 0.5f) stopRequested_ = true;
    if (index == kParamTransportSync && parameters_[index] != previous) {
        syncArmed_ = parameters_[index] > 0.5f;
        syncWasTransportPlaying_ = false;
        expectedHostBeat_ = -1.0;
        if (!syncArmed_) stopRequested_ = true;
    }
}

float TuneyEngine::getParameter(std::uint32_t index) const { return index < kParamCount ? parameters_[index] : 0.0f; }
std::string TuneyEngine::stateText() const { return serializeState(state_); }

bool TuneyEngine::setStateText(std::string_view text, std::string* error)
{
    TuneyState next;
    if (!deserializeState(text, next, error)) return false;
    const TuneyState previous = state_;
    state_ = std::move(next);
    try { rebuildDerived(); }
    catch (const std::exception& e) { state_ = previous; rebuildDerived(); if (error) *error = e.what(); return false; }
    return true;
}

void TuneyEngine::setText(std::string text) { state_.text = std::move(text); rebuildSchedule(); }
void TuneyEngine::clearText() { state_.text.clear(); rebuildSchedule(); }

void TuneyEngine::queueTypedCharacter(std::string_view utf8)
{
    if (utf8.empty() || utf8.size() > 7) return;
    const auto write = typedWrite_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % kTypedQueueSize;
    if (next == typedRead_.load(std::memory_order_acquire)) return;
    auto& item = typedQueue_[write];
    std::fill(item.bytes.begin(), item.bytes.end(), 0);
    std::copy(utf8.begin(), utf8.end(), item.bytes.begin());
    item.size = static_cast<std::uint8_t>(utf8.size());
    typedWrite_.store(next, std::memory_order_release);
    state_.text.append(utf8);
    rebuildSchedule();
}

void TuneyEngine::startPlayback() { rebuildSchedule(); playRequested_ = true; }
void TuneyEngine::stopPlayback() { stopRequested_ = true; }

int TuneyEngine::limitNote(int note) const
{
    const int range = static_cast<int>(parameters_[kParamRangeLimit]);
    const int offset = static_cast<int>(parameters_[kParamMapperOffset]);
    if (range <= 0) return note;
    const int low = centeredNote(0, range, offset), high = low + range - 1;
    auto mod = [](int a, int b) { const int r = a % b; return r < 0 ? r + b : r; };
    switch (static_cast<Limiter>(static_cast<int>(parameters_[kParamLimiter]))) {
    case Limiter::wrap: return low + mod(note - low, range);
    case Limiter::reflect: { if (range == 1) return low; const int period = range * 2 - 2; const int w = mod(note - low, period); return w < range ? low + w : low + period - w; }
    case Limiter::reflectRepeat: { const int period = range * 2; const int w = mod(note - low, period); return w < range ? low + w : high - (w % range); }
    }
    return note;
}

int TuneyEngine::mapCharacter(std::string_view utf8) const
{
    std::string key(utf8);
    if (parameters_[kParamCaseSensitive] < 0.5f && key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') key[0] += 'a' - 'A';
    std::vector<std::string> alphabet = alphabet_;
    if (parameters_[kParamCaseSensitive] < 0.5f) {
        alphabet.erase(std::remove_if(alphabet.begin(), alphabet.end(), [](const std::string& c) { return c.size() == 1 && c[0] >= 'A' && c[0] <= 'Z'; }), alphabet.end());
    }
    const auto it = std::find(alphabet.begin(), alphabet.end(), key);
    if (it == alphabet.end()) return std::numeric_limits<int>::min();
    int index = static_cast<int>(std::distance(alphabet.begin(), it));
    const int length = static_cast<int>(parameters_[kParamMapperLength]);
    const int span = length > 0 ? length : static_cast<int>(alphabet.size());
    if (parameters_[kParamInvert] > 0.5f) index = static_cast<int>(alphabet.size()) - index - 1;
    if (length > 0) index %= length;
    return limitNote(centeredNote(index, span, static_cast<int>(parameters_[kParamMapperOffset])));
}

int TuneyEngine::scaleTuningNumber(int note) const
{
    if (scaleSteps_.empty()) return note;
    const int count = static_cast<int>(scaleSteps_.size());
    const int shifted = note - state_.scaleOffset;
    int octave = shifted / count, index = shifted % count;
    if (index < 0) { index += count; --octave; }
    const int octaveLength = std::max(1, std::accumulate(state_.intervals.begin(), state_.intervals.end(), 0));
    return scaleSteps_[static_cast<std::size_t>(index)] + octaveLength * octave + state_.scaleOffset;
}

double TuneyEngine::frequencyForLogicalNote(int logicalNote) const
{
    const int note = scaleTuningNumber(logicalNote + static_cast<int>(parameters_[kParamAudioNoteOffset]));
    const int delta = note - static_cast<int>(parameters_[kParamRootNote]);
    double ratio = 1.0;
    const auto type = static_cast<TuningType>(static_cast<int>(parameters_[kParamTuningType]));
    if (type == TuningType::table && !table_.empty()) {
        int i = delta % static_cast<int>(table_.size()); if (i < 0) i += static_cast<int>(table_.size());
        return table_[static_cast<std::size_t>(i)] * std::pow(2.0, parameters_[kParamDetune] / 1200.0);
    }
    if (type == TuningType::ratios && !ratios_.empty()) {
        const int count = static_cast<int>(ratios_.size());
        int octave = delta / count, index = delta % count;
        if (index < 0) { index += count; --octave; }
        ratio = std::pow(ratios_.back(), octave) * (index == 0 ? 1.0 : ratios_[static_cast<std::size_t>(index - 1)]);
    } else {
        ratio = std::pow(parameters_[kParamOctaveRatio], static_cast<double>(delta) / parameters_[kParamNotesPerOctave]);
        const int limit = static_cast<int>(parameters_[kParamJustLimit]);
        if (limit > 0) {
            int bestDen = 1; double best = std::round(ratio), error = std::fabs(ratio - best);
            for (int den = 1; den <= limit; ++den) { const double n = std::round(ratio * den); const double e = std::fabs(ratio - n / den); if (e < error) { error = e; best = n; bestDen = den; } }
            ratio = best / bestDen;
        }
    }
    return parameters_[kParamRootFrequency] * ratio * std::pow(2.0, parameters_[kParamDetune] / 1200.0);
}

void TuneyEngine::rebuildDerived()
{
    alphabet_ = splitUtf8(state_.alphabet);
    scaleSteps_.clear();
    int semitone = 0;
    const auto allNames = splitUtf8(state_.noteNames);
    std::vector<std::string> names;
    const auto rootIt = std::find(allNames.begin(), allNames.end(), state_.root);
    const auto beginIt = std::find(allNames.begin(), allNames.end(), state_.begin);
    const auto endIt = std::find(allNames.begin(), allNames.end(), state_.end);
    if (rootIt == allNames.end() || beginIt == allNames.end() || endIt == allNames.end() || beginIt > rootIt || rootIt > endIt)
        throw std::invalid_argument("invalid scale note-name range");
    names.insert(names.end(), rootIt, endIt + 1);
    names.insert(names.end(), beginIt, rootIt);
    const auto selected = splitUtf8(state_.notes);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const bool includeBase = state_.notes.empty() || std::find(selected.begin(), selected.end(), names[i]) != selected.end();
        if (includeBase) scaleSteps_.push_back(semitone);
        const int interval = state_.intervals[i % state_.intervals.size()];
        if (state_.notes.empty()) for (int j = 1; j < interval; ++j) scaleSteps_.push_back(semitone + j);
        semitone += interval;
    }
    ratios_.clear(); table_.clear();
    for (auto item : splitSemicolon(state_.ratioText)) { const double v = evaluateExpression(item); if (v <= 0) throw std::invalid_argument("ratio must be positive"); ratios_.push_back(v); }
    for (auto item : splitSemicolon(state_.tableText)) { const double v = evaluateExpression(item); if (v <= 0) throw std::invalid_argument("frequency must be positive"); table_.push_back(v); }
    rebuildSchedule();
}

void TuneyEngine::rebuildSchedule()
{
    const int target = 1 - playbackSchedule_;
    auto& schedule = schedules_[target];
    auto& beatSchedule = beatSchedules_[target];
    std::size_t count = 0;
    std::size_t beatCount = 0;
    static constexpr double timings[] = {56.04, 63.29, 68.44, 72.75, 80.12, 85.36, 89.54, 94.59, 100.38, 107.68, 116.24, 128.57, 141.9, 157.76, 171.46, 188.67, 210.69, 246.01, 299.94, 419.5};
    std::mt19937 rng(static_cast<std::uint32_t>(parameters_[kParamTimingSeed]));
    std::uniform_int_distribution<std::size_t> pick(0, std::size(timings) - 1);
    double timeMs = 0.0;
    double timeBeats = 0.0;
    const double scale = parameters_[kParamTimingScale] / parameters_[kParamRate];
    const auto chars = splitUtf8(state_.text);
    std::string previous;
    for (const auto& sourceChar : chars) {
        if ((sourceChar == " " && (previous.empty() || previous == " " || previous == "\n")) ||
            (sourceChar == "\n" && previous != "\n") ||
            ((sourceChar == "\t" || sourceChar == "\r") && sourceChar != "\n")) {
            previous = sourceChar;
            continue;
        }
        const std::string c = state_.stripAccents ? stripLatinAccent(sourceChar) : sourceChar;
        double punctuation = 0.0;
        if (c == " ") punctuation = state_.spaceMs; else if (c == ".") punctuation = state_.dotMs;
        else if (c == ",") punctuation = state_.commaMs; else if (c == ":") punctuation = state_.colonMs;
        else if (c == ";") punctuation = state_.semicolonMs; else if (c == "\n" && previous == "\n") punctuation = state_.blankLineMs;
        const bool explicitTiming = c == " " || c == "." || c == "," || c == ":" || c == ";" || c == "\n";
        const bool alphabetic = isAsciiAlpha(c) || std::find(alphabet_.begin(), alphabet_.end(), c) != alphabet_.end();
        if (alphabetic || explicitTiming || !state_.alphaOnly) {
            const double duration = (punctuation + timings[pick(rng)]) * scale;
            double beatDuration = 0.25;
            if (c == ",") beatDuration = 0.5;
            else if (c == "." || c == ":" || c == ";") beatDuration = 1.0;
            else if (c == "\n") beatDuration = 4.0;
            const int note = mapCharacter(c);
            if (note != std::numeric_limits<int>::min()) {
                const auto on = static_cast<std::uint64_t>(std::llround(timeMs * sampleRate_ / 1000.0));
                const auto off = static_cast<std::uint64_t>(std::llround((timeMs + duration) * sampleRate_ / 1000.0));
                if (count + 2 <= schedule.size()) {
                    schedule[count++] = {on, note, true}; schedule[count++] = {off, note, false};
                }
                if (beatCount + 2 <= beatSchedule.size()) {
                    beatSchedule[beatCount++] = {timeBeats, note, true};
                    beatSchedule[beatCount++] = {timeBeats + beatDuration, note, false};
                }
            }
            timeMs += std::max(0.0, duration - parameters_[kParamOverlapMs] * scale);
            timeBeats += beatDuration;
        }
        previous = sourceChar;
    }
    std::stable_sort(schedule.begin(), schedule.begin() + static_cast<std::ptrdiff_t>(count), [](const auto& a, const auto& b) { return a.sample < b.sample; });
    scheduleCounts_[target] = count;
    beatScheduleCounts_[target] = beatCount;
    scheduleLengths_[target] = static_cast<std::uint64_t>(std::llround(timeMs * sampleRate_ / 1000.0)) + (count == 0 ? 0 : 1);
    beatScheduleLengths_[target] = timeBeats;
    preparedSchedule_.store(target, std::memory_order_release);
}

void TuneyEngine::emitMidi(int note, bool on, std::uint32_t frame, ProcessResult& result)
{
    if (parameters_[kParamMidiEnabled] < 0.5f || result.midiCount >= result.midi.size()) return;
    const int midiNote = std::clamp(note + static_cast<int>(parameters_[kParamMidiNoteOffset]), 0, 127);
    const int channel = static_cast<int>(parameters_[kParamMidiChannel]) - 1;
    result.midi[result.midiCount++] = {frame, {static_cast<std::uint8_t>((on ? 0x90 : 0x80) | channel), static_cast<std::uint8_t>(midiNote), static_cast<std::uint8_t>(on ? parameters_[kParamMidiVelocity] : 0)}};
}

void TuneyEngine::applyEvent(int note, bool on, std::uint32_t frame, ProcessResult& result,
                             bool honorMinimumNote)
{
    if (on) {
        for (const auto& v : voices_) if (v.active && v.logicalNote == note && v.releaseAt == UINT64_MAX) return;
        Voice* slot = nullptr;
        const int maxVoices = static_cast<int>(parameters_[kParamPolyphony]);
        int active = 0;
        for (auto& v : voices_) if (v.active) ++active; else if (!slot) slot = &v;
        if (!slot || active >= maxVoices) {
            Voice* oldest = nullptr;
            for (auto& v : voices_) if (v.active && (!oldest || v.serial < oldest->serial)) oldest = &v;
            if (oldest) slot = oldest;
        }
        if (!slot) return;
        if (slot->active && slot->midiOn) emitMidi(slot->logicalNote, false, frame, result);
        *slot = {true, note, frequencyForLogicalNote(note), 0.0, 0, UINT64_MAX, 1.0f, true, ++voiceSerial_};
        emitMidi(note, true, frame, result);
    } else {
        for (auto& v : voices_) if (v.active && v.logicalNote == note && v.releaseAt == UINT64_MAX) {
            v.releaseAt = honorMinimumNote
                ? std::max(v.age, static_cast<std::uint64_t>(parameters_[kParamMinimumNoteMs] * sampleRate_ / 1000.0))
                : v.age;
            emitMidi(note, false, frame, result);
            v.midiOn = false;
        }
    }
}

void TuneyEngine::allNotesOff(std::uint32_t frame, ProcessResult& result)
{
    for (auto& v : voices_) if (v.active) { if (v.midiOn) emitMidi(v.logicalNote, false, frame, result); v = {}; }
}

void TuneyEngine::drainTyped(ProcessResult& result)
{
    auto read = typedRead_.load(std::memory_order_relaxed);
    const auto write = typedWrite_.load(std::memory_order_acquire);
    while (read != write) {
        const auto& item = typedQueue_[read];
        const int note = mapCharacter(std::string_view(item.bytes.data(), item.size));
        if (note != std::numeric_limits<int>::min()) {
            applyEvent(note, true, 0, result);
            for (auto& voice : voices_) if (voice.active && voice.logicalNote == note && voice.releaseAt == UINT64_MAX) {
                voice.releaseAt = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(parameters_[kParamMinimumNoteMs] * sampleRate_ / 1000.0));
                break;
            }
        }
        read = (read + 1) % kTypedQueueSize;
    }
    typedRead_.store(read, std::memory_order_release);
}

float TuneyEngine::renderVoice(Voice& v)
{
    if (!v.active) return 0.0f;
    const std::uint64_t fade = static_cast<std::uint64_t>(4096.0 * sampleRate_ / 48000.0);
    if (v.releaseAt != UINT64_MAX && v.age >= v.releaseAt + fade) { v = {}; return 0.0f; }
    float env = fade ? std::min(1.0f, static_cast<float>(v.age) / fade) : 1.0f;
    if (v.releaseAt != UINT64_MAX && v.age >= v.releaseAt) env *= std::max(0.0f, 1.0f - static_cast<float>(v.age - v.releaseAt) / std::max<std::uint64_t>(1, fade));
    const double p = v.phase;
    float wave = 0.0f;
    switch (static_cast<Waveform>(static_cast<int>(parameters_[kParamWaveform]))) {
    case Waveform::sine: wave = static_cast<float>(std::sin(2.0 * kPi * p)); break;
    case Waveform::square: wave = p < parameters_[kParamDutyCycle] ? 1.0f : -1.0f; break;
    case Waveform::triangle: {
        const double duty = std::clamp<double>(parameters_[kParamDutyCycle], 0.001, 0.999);
        wave = static_cast<float>(p < duty ? -1.0 + 2.0 * p / duty : 1.0 - 2.0 * (p - duty) / (1.0 - duty));
        break;
    }}
    v.phase += v.frequency / sampleRate_; v.phase -= std::floor(v.phase); ++v.age;
    return wave * env;
}

void TuneyEngine::processSynced(float* left, float* right, std::uint32_t frames,
                                ProcessResult& result, const TransportSnapshot& transport)
{
    result.midiCount = 0;
    bool restartRequested = false;
    if (stopRequested_) {
        allNotesOff(0, result);
        playing_ = false;
        syncArmed_ = false;
        stopRequested_ = false;
    }
    if (playRequested_) {
        allNotesOff(0, result);
        playing_ = false;
        syncArmed_ = true;
        restartRequested = true;
        playRequested_ = false;
    }

    const bool transportUsable = transport.valid && transport.playing &&
        transport.bpm > 0.0 && transport.beatsPerBar > 0.0 && sampleRate_ > 0.0;
    const double absoluteBeat = transport.bar * transport.beatsPerBar + transport.barBeat;
    const double hostBeatPerSample = transportUsable ? transport.bpm / (60.0 * sampleRate_) : 0.0;
    const double continuityTolerance = std::max(1.0e-3, hostBeatPerSample * 4.0);
    const bool discontinuity = transportUsable && expectedHostBeat_ >= 0.0 &&
        std::fabs(absoluteBeat - expectedHostBeat_) > continuityTolerance;

    if (syncArmed_ && transportUsable &&
        (restartRequested || !syncWasTransportPlaying_ || discontinuity)) {
        allNotesOff(0, result);
        playbackSchedule_ = preparedSchedule_.load(std::memory_order_acquire);
        scheduleIndex_ = 0;
        syncCycle_ = 0;
        syncPlayheadBeats_ = 0.0;
        playing_ = true;
    } else if (!transportUsable || !syncArmed_) {
        if (playing_) allNotesOff(0, result);
        playing_ = false;
    }

    drainTyped(result);
    const auto& schedule = beatSchedules_[playbackSchedule_];
    const std::size_t scheduleCount = beatScheduleCounts_[playbackSchedule_];
    const double scheduleLength = beatScheduleLengths_[playbackSchedule_];
    const double sequenceBeatPerSample = hostBeatPerSample * parameters_[kParamRate];

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const double currentBeat = syncPlayheadBeats_ + frame * sequenceBeatPerSample;
        while (playing_) {
            if (scheduleIndex_ < scheduleCount) {
                const auto& event = schedule[scheduleIndex_];
                const double eventBeat = syncCycle_ * scheduleLength + event.beat;
                if (eventBeat <= currentBeat + 1.0e-12) {
                    ++scheduleIndex_;
                    applyEvent(event.note, event.on, frame, result, false);
                    continue;
                }
            }

            const double cycleEnd = (syncCycle_ + 1) * scheduleLength;
            if (scheduleLength <= 0.0 ||
                (scheduleIndex_ >= scheduleCount && currentBeat >= cycleEnd - 1.0e-12)) {
                if (parameters_[kParamLoop] > 0.5f && scheduleCount != 0 && scheduleLength > 0.0) {
                    allNotesOff(frame, result);
                    scheduleIndex_ = 0;
                    ++syncCycle_;
                    continue;
                }
                playing_ = false;
            }
            break;
        }

        for (auto& voice : voices_) if (voice.active && voice.midiOn &&
            voice.releaseAt != UINT64_MAX && voice.age >= voice.releaseAt) {
            emitMidi(voice.logicalNote, false, frame, result);
            voice.midiOn = false;
        }
        float sample = 0.0f;
        for (auto& voice : voices_) sample += renderVoice(voice);
        sample = std::clamp(sample * parameters_[kParamGain] / parameters_[kParamHeadroom], -1.0f, 1.0f);
        if (parameters_[kParamAudioEnabled] < 0.5f) sample = 0.0f;
        left[frame] = right[frame] = sample;
    }

    if (transportUsable && syncArmed_)
        syncPlayheadBeats_ += frames * sequenceBeatPerSample;
    expectedHostBeat_ = transportUsable ? absoluteBeat + frames * hostBeatPerSample : -1.0;
    syncWasTransportPlaying_ = transportUsable;
}

void TuneyEngine::process(float* left, float* right, std::uint32_t frames, ProcessResult& result)
{
    process(left, right, frames, result, {});
}

void TuneyEngine::process(float* left, float* right, std::uint32_t frames,
                          ProcessResult& result, const TransportSnapshot& transport)
{
    if (parameters_[kParamTransportSync] > 0.5f) {
        processSynced(left, right, frames, result, transport);
        return;
    }
    syncWasTransportPlaying_ = false;
    expectedHostBeat_ = -1.0;
    result.midiCount = 0;
    if (stopRequested_) { allNotesOff(0, result); playing_ = false; stopRequested_ = false; }
    if (playRequested_) { allNotesOff(0, result); playbackSchedule_ = preparedSchedule_.load(std::memory_order_acquire); scheduleIndex_ = 0; playhead_ = 0; playing_ = true; playRequested_ = false; }
    drainTyped(result);
    const auto& schedule = schedules_[playbackSchedule_];
    const std::size_t scheduleCount = scheduleCounts_[playbackSchedule_];
    const std::uint64_t scheduleLength = scheduleLengths_[playbackSchedule_];
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (playing_ && scheduleIndex_ < scheduleCount && schedule[scheduleIndex_].sample <= playhead_) {
            const auto& e = schedule[scheduleIndex_++]; applyEvent(e.note, e.on, frame, result);
        }
        for (auto& v : voices_) if (v.active && v.midiOn && v.releaseAt != UINT64_MAX && v.age >= v.releaseAt) {
            emitMidi(v.logicalNote, false, frame, result); v.midiOn = false;
        }
        float sample = 0.0f;
        for (auto& v : voices_) sample += renderVoice(v);
        sample = std::clamp(sample * parameters_[kParamGain] / parameters_[kParamHeadroom], -1.0f, 1.0f);
        if (parameters_[kParamAudioEnabled] < 0.5f) sample = 0.0f;
        left[frame] = right[frame] = sample;
        if (playing_) {
            ++playhead_;
            if (scheduleIndex_ >= scheduleCount && playhead_ >= scheduleLength) {
                if (parameters_[kParamLoop] > 0.5f && scheduleCount != 0) { allNotesOff(frame, result); scheduleIndex_ = 0; playhead_ = 0; }
                else playing_ = false;
            }
        }
    }
}

} // namespace downspout::tuney_vst
