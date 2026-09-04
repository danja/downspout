#include "worms_serialization.hpp"

#include "worms_engine.hpp"

#include <charconv>
#include <sstream>
#include <string_view>
#include <vector>

namespace downspout::worms {
namespace {

template <typename T>
bool parseInteger(std::string_view text, T& value)
{
    const char* begin = text.data();
    const char* end   = text.data() + text.size();
    auto res = std::from_chars(begin, end, value);
    return res.ec == std::errc() && res.ptr == end;
}

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
    char* parseEnd = nullptr;
    value = std::strtof(local.c_str(), &parseEnd);
    return parseEnd && *parseEnd == '\0';
}

bool parseBool(std::string_view text, bool& value)
{
    int i = 0;
    if (!parseInteger(text, i)) return false;
    value = (i != 0);
    return true;
}

std::vector<std::string_view> split(std::string_view text, char delimiter)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find(delimiter, start);
        if (pos == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

}  // namespace

std::string serializeControls(const Controls& c)
{
    std::ostringstream out;
    out << "version=1\n";
    out << "root="      << c.root     << '\n';
    out << "reg="       << c.reg      << '\n';
    out << "stepSize="  << c.stepSize << '\n';
    out << "patLen="    << c.patLen   << '\n';
    out << "density="   << c.density  << '\n';
    out << "velocity="  << c.velocity << '\n';
    out << "vary="      << c.vary     << '\n';
    out << "seed="      << c.seed     << '\n';
    out << "condCh="    << c.condCh   << '\n';
    out << "quantize="  << (c.quantize ? 1 : 0) << '\n';
    out << "scale="     << c.scale    << '\n';
    out << "midiCh="    << c.midiCh   << '\n';
    out << "actionRandomize=" << c.actionRandomize << '\n';
    out << "actionMutate="    << c.actionMutate    << '\n';
    for (int i = 0; i < 6; ++i)
        out << "rule" << i << '=' << c.rule.turn[i] << '\n';
    return out.str();
}

std::optional<Controls> deserializeControls(const std::string& text)
{
    Controls c;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key   = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        int   iv = 0;
        float fv = 0.0f;
        bool  bv = false;

        if (key == "version")         { continue; }
        else if (key == "root"        && parseInteger(value, iv)) { c.root     = iv; }
        else if (key == "reg"         && parseInteger(value, iv)) { c.reg      = iv; }
        else if (key == "stepSize"    && parseInteger(value, iv)) { c.stepSize = iv; }
        else if (key == "patLen"      && parseInteger(value, iv)) { c.patLen   = iv; }
        else if (key == "density"     && parseFloat(value, fv))   { c.density  = fv; }
        else if (key == "velocity"    && parseFloat(value, fv))   { c.velocity = fv; }
        else if (key == "vary"        && parseFloat(value, fv))   { c.vary     = fv; }
        else if (key == "seed"        && parseFloat(value, fv))   { c.seed     = fv; }
        else if (key == "condCh"      && parseInteger(value, iv)) { c.condCh   = iv; }
        else if (key == "quantize"    && parseBool(value, bv))    { c.quantize = bv; }
        else if (key == "scale"       && parseInteger(value, iv)) { c.scale    = iv; }
        else if (key == "midiCh"      && parseInteger(value, iv)) { c.midiCh   = iv; }
        else if (key == "actionRandomize" && parseInteger(value, iv)) { c.actionRandomize = iv; }
        else if (key == "actionMutate"    && parseInteger(value, iv)) { c.actionMutate    = iv; }
        else if (key.size() == 5 && key.substr(0, 4) == "rule" && std::isdigit(key[4])) {
            const int idx = key[4] - '0';
            if (idx >= 0 && idx < 6 && parseInteger(value, iv))
                c.rule.turn[idx] = iv;
        } else {
            return std::nullopt;
        }
    }
    return clampControls(c);
}

std::string serializePattern(const PatternState& p)
{
    std::ostringstream out;
    out << "version=1\n";
    out << "patternSteps="   << p.patternSteps    << '\n';
    out << "stepsPerBeat="   << p.stepsPerBeat    << '\n';
    out << "stepsPerBar="    << p.stepsPerBar     << '\n';
    out << "eventCount="     << p.eventCount      << '\n';
    out << "generationSerial=" << p.generationSerial << '\n';
    out << "meterNumerator=" << p.meter.numerator << '\n';
    out << "meterDenominator=" << p.meter.denominator << '\n';
    for (int i = 0; i < p.eventCount; ++i) {
        const NoteEvent& ev = p.events[i];
        out << "event=" << ev.startStep << ',' << ev.durationSteps << ','
            << ev.note << ',' << ev.velocity << '\n';
    }
    return out.str();
}

std::optional<PatternState> deserializePattern(const std::string& text)
{
    PatternState p;
    int eventIdx = 0;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key   = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        int iv = 0;
        if (key == "version")               { continue; }
        else if (key == "patternSteps"    && parseInteger(value, iv)) { p.patternSteps    = iv; }
        else if (key == "stepsPerBeat"    && parseInteger(value, iv)) { p.stepsPerBeat    = iv; }
        else if (key == "stepsPerBar"     && parseInteger(value, iv)) { p.stepsPerBar     = iv; }
        else if (key == "eventCount"      && parseInteger(value, iv)) { p.eventCount      = iv; }
        else if (key == "generationSerial"&& parseInteger(value, iv)) { p.generationSerial = iv; }
        else if (key == "meterNumerator"  && parseInteger(value, iv)) { p.meter.numerator = iv; }
        else if (key == "meterDenominator"&& parseInteger(value, iv)) { p.meter.denominator = iv; }
        else if (key == "event") {
            if (eventIdx >= kMaxPatternEvents) return std::nullopt;
            const auto parts = split(value, ',');
            if (parts.size() != 4) return std::nullopt;
            NoteEvent& ev = p.events[eventIdx++];
            if (!parseInteger(parts[0], ev.startStep)     ||
                !parseInteger(parts[1], ev.durationSteps) ||
                !parseInteger(parts[2], ev.note)          ||
                !parseInteger(parts[3], ev.velocity))
                return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    p.eventCount = eventIdx;
    return p;
}

}  // namespace downspout::worms
