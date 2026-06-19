#include "xoxolo_serialization.hpp"

#include "xoxolo_engine.hpp"

#include <charconv>
#include <sstream>
#include <string_view>

namespace downspout::xoxolo {
namespace {

template <typename T>
bool parseInteger(const std::string_view text, T& value)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

}  // namespace

std::string serializePatternState(const PatternState& rawPattern)
{
    PatternState pattern = rawPattern;
    sanitizePattern(pattern);

    std::ostringstream out;
    out << "version=" << kPatternStateVersion << '\n';
    out << "bars=" << pattern.bars << '\n';
    out << "resolution=" << static_cast<int>(pattern.resolution) << '\n';
    out << "channel=" << pattern.channel << '\n';
    out << "stepsPerBeat=" << pattern.stepsPerBeat << '\n';
    out << "stepsPerBar=" << pattern.stepsPerBar << '\n';
    out << "totalSteps=" << pattern.totalSteps << '\n';

    for (int lane = 0; lane < kLaneCount; ++lane) {
        const LaneState& laneState = pattern.lanes[static_cast<std::size_t>(lane)];
        out << "lane=" << lane << ',' << laneState.midiNote << '\n';
        out << "steps=" << lane << ',';
        for (int step = 0; step < pattern.totalSteps; ++step)
            out << (laneState.steps[static_cast<std::size_t>(step)] != 0 ? '1' : '0');
        out << '\n';
    }

    return out.str();
}

std::optional<PatternState> deserializePatternState(const std::string& text)
{
    PatternState pattern = makeDefaultPattern();

    std::size_t start = 0;
    while (start <= text.size()) {
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

        int intValue = 0;
        if (key == "version") {
            if (!parseInteger(value, intValue))
                return std::nullopt;
            pattern.version = intValue;
        } else if (key == "bars") {
            if (!parseInteger(value, intValue))
                return std::nullopt;
            pattern.bars = intValue;
        } else if (key == "resolution") {
            if (!parseInteger(value, intValue))
                return std::nullopt;
            pattern.resolution = static_cast<ResolutionId>(intValue);
        } else if (key == "channel") {
            if (!parseInteger(value, intValue))
                return std::nullopt;
            pattern.channel = intValue;
        } else if (key == "stepsPerBeat") {
            if (!parseInteger(value, intValue))
                return std::nullopt;
            pattern.stepsPerBeat = intValue;
        } else if (key == "stepsPerBar") {
            if (!parseInteger(value, intValue))
                return std::nullopt;
            pattern.stepsPerBar = intValue;
        } else if (key == "totalSteps") {
            if (!parseInteger(value, intValue))
                return std::nullopt;
            pattern.totalSteps = intValue;
        } else if (key == "lane") {
            const std::size_t comma = value.find(',');
            if (comma == std::string_view::npos)
                return std::nullopt;
            int lane = 0;
            int note = 0;
            if (!parseInteger(value.substr(0, comma), lane) ||
                !parseInteger(value.substr(comma + 1), note) ||
                lane < 0 ||
                lane >= kLaneCount) {
                return std::nullopt;
            }
            pattern.lanes[static_cast<std::size_t>(lane)].midiNote = note;
        } else if (key == "steps") {
            const std::size_t comma = value.find(',');
            if (comma == std::string_view::npos)
                return std::nullopt;
            int lane = 0;
            if (!parseInteger(value.substr(0, comma), lane) || lane < 0 || lane >= kLaneCount)
                return std::nullopt;
            const std::string_view bits = value.substr(comma + 1);
            const int count = static_cast<int>(std::min<std::size_t>(bits.size(), kMaxSteps));
            for (int step = 0; step < count; ++step) {
                const char bit = bits[static_cast<std::size_t>(step)];
                if (bit != '0' && bit != '1')
                    return std::nullopt;
                pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)] = bit == '1' ? 1 : 0;
            }
        } else {
            return std::nullopt;
        }
    }

    sanitizePattern(pattern);
    return pattern;
}

}  // namespace downspout::xoxolo
