#include "drumgen_serialization.hpp"

#include "drumgen_pattern.hpp"
#include "drumgen_state.hpp"
#include "drumgen_variation.hpp"

#include <charconv>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <vector>

namespace downspout::drumgen {
namespace {

template <typename T>
bool parseInteger(std::string_view text, T& value) {
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseFloat(std::string_view text, float& value) {
    std::string local(text);
    char* parseEnd = nullptr;
    value = std::strtof(local.c_str(), &parseEnd);
    return parseEnd != nullptr && *parseEnd == '\0';
}

std::vector<std::string_view> split(std::string_view text, char delimiter) {
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

std::string serializeControls(const Controls& controls) {
    std::ostringstream out;
    out << "version=1\n";
    out << "genre=" << static_cast<int>(controls.genre) << '\n';
    out << "channel=" << controls.channel << '\n';
    out << "kitMap=" << static_cast<int>(controls.kitMap) << '\n';
    out << "bars=" << controls.bars << '\n';
    out << "resolution=" << static_cast<int>(controls.resolution) << '\n';
    out << "density=" << controls.density << '\n';
    out << "variation=" << controls.variation << '\n';
    out << "fill=" << controls.fill << '\n';
    out << "vary=" << controls.vary << '\n';
    out << "seed=" << controls.seed << '\n';
    out << "kickAmt=" << controls.kickAmt << '\n';
    out << "backbeatAmt=" << controls.backbeatAmt << '\n';
    out << "hatAmt=" << controls.hatAmt << '\n';
    out << "auxAmt=" << controls.auxAmt << '\n';
    out << "tomAmt=" << controls.tomAmt << '\n';
    out << "metalAmt=" << controls.metalAmt << '\n';
    out << "actionNew=" << controls.actionNew << '\n';
    out << "actionMutate=" << controls.actionMutate << '\n';
    out << "actionFill=" << controls.actionFill << '\n';
    return out.str();
}

std::string serializePatternState(const PatternState& pattern) {
    std::ostringstream out;
    out << "version=" << pattern.version << '\n';
    out << "bars=" << pattern.bars << '\n';
    out << "stepsPerBeat=" << pattern.stepsPerBeat << '\n';
    out << "stepsPerBar=" << pattern.stepsPerBar << '\n';
    out << "totalSteps=" << pattern.totalSteps << '\n';
    out << "generationSerial=" << pattern.generationSerial << '\n';

    for (int lane = 0; lane < kLaneCount; ++lane) {
        out << "lane=" << lane << ',' << pattern.lanes[lane].midiNote;
        for (int step = 0; step < kMaxPatternSteps; ++step) {
            out << ','
                << static_cast<int>(pattern.lanes[lane].steps[step].velocity)
                << ':'
                << static_cast<int>(pattern.lanes[lane].steps[step].flags);
        }
        out << '\n';
    }

    return out.str();
}

std::string serializeVariationState(const VariationState& variation) {
    std::ostringstream out;
    out << "version=" << variation.version << '\n';
    out << "completedLoops=" << variation.completedLoops << '\n';
    out << "lastMutationLoop=" << variation.lastMutationLoop << '\n';
    return out.str();
}

std::optional<Controls> deserializeControls(const std::string& text) {
    Controls controls;

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) {
            continue;
        }

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        int intValue = 0;
        unsigned int uintValue = 0;
        float floatValue = 0.0f;

        if (key == "version") {
            continue;
        } else if (key == "genre" && parseInteger(value, intValue)) {
            controls.genre = static_cast<GenreId>(intValue);
        } else if (key == "channel" && parseInteger(value, intValue)) {
            controls.channel = intValue;
        } else if (key == "kitMap" && parseInteger(value, intValue)) {
            controls.kitMap = static_cast<KitMapId>(intValue);
        } else if (key == "bars" && parseInteger(value, intValue)) {
            controls.bars = intValue;
        } else if (key == "resolution" && parseInteger(value, intValue)) {
            controls.resolution = static_cast<ResolutionId>(intValue);
        } else if (key == "density" && parseFloat(value, floatValue)) {
            controls.density = floatValue;
        } else if (key == "variation" && parseFloat(value, floatValue)) {
            controls.variation = floatValue;
        } else if (key == "fill" && parseFloat(value, floatValue)) {
            controls.fill = floatValue;
        } else if (key == "vary" && parseFloat(value, floatValue)) {
            controls.vary = floatValue;
        } else if (key == "seed" && parseInteger(value, uintValue)) {
            controls.seed = uintValue;
        } else if (key == "kickAmt" && parseFloat(value, floatValue)) {
            controls.kickAmt = floatValue;
        } else if (key == "backbeatAmt" && parseFloat(value, floatValue)) {
            controls.backbeatAmt = floatValue;
        } else if (key == "hatAmt" && parseFloat(value, floatValue)) {
            controls.hatAmt = floatValue;
        } else if (key == "auxAmt" && parseFloat(value, floatValue)) {
            controls.auxAmt = floatValue;
        } else if (key == "tomAmt" && parseFloat(value, floatValue)) {
            controls.tomAmt = floatValue;
        } else if (key == "metalAmt" && parseFloat(value, floatValue)) {
            controls.metalAmt = floatValue;
        } else if (key == "actionNew" && parseInteger(value, intValue)) {
            controls.actionNew = intValue;
        } else if (key == "actionMutate" && parseInteger(value, intValue)) {
            controls.actionMutate = intValue;
        } else if (key == "actionFill" && parseInteger(value, intValue)) {
            controls.actionFill = intValue;
        } else {
            return std::nullopt;
        }
    }

    return clampControls(controls);
}

std::optional<PatternState> deserializePatternState(const std::string& text) {
    PatternState pattern;
    std::array<bool, kLaneCount> seenLane {};

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) {
            continue;
        }

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        int intValue = 0;

        if (key == "version") {
            continue;
        } else if (key == "bars" && parseInteger(value, intValue)) {
            pattern.bars = intValue;
        } else if (key == "stepsPerBeat" && parseInteger(value, intValue)) {
            pattern.stepsPerBeat = intValue;
        } else if (key == "stepsPerBar" && parseInteger(value, intValue)) {
            pattern.stepsPerBar = intValue;
        } else if (key == "totalSteps" && parseInteger(value, intValue)) {
            pattern.totalSteps = intValue;
        } else if (key == "generationSerial" && parseInteger(value, intValue)) {
            pattern.generationSerial = intValue;
        } else if (key == "lane") {
            const auto parts = split(value, ',');
            if (parts.size() != static_cast<std::size_t>(kMaxPatternSteps + 2)) {
                return std::nullopt;
            }

            int laneIndex = 0;
            int midiNote = 0;
            if (!parseInteger(parts[0], laneIndex) || !parseInteger(parts[1], midiNote)) {
                return std::nullopt;
            }
            if (laneIndex < 0 || laneIndex >= kLaneCount) {
                return std::nullopt;
            }

            pattern.lanes[laneIndex].midiNote = midiNote;
            seenLane[laneIndex] = true;

            for (int step = 0; step < kMaxPatternSteps; ++step) {
                const auto fields = split(parts[step + 2], ':');
                if (fields.size() != 2) {
                    return std::nullopt;
                }

                int velocity = 0;
                int flags = 0;
                if (!parseInteger(fields[0], velocity) || !parseInteger(fields[1], flags)) {
                    return std::nullopt;
                }

                pattern.lanes[laneIndex].steps[step].velocity = static_cast<std::uint8_t>(velocity);
                pattern.lanes[laneIndex].steps[step].flags = static_cast<std::uint8_t>(flags);
            }
        } else {
            return std::nullopt;
        }
    }

    for (bool seen : seenLane) {
        if (!seen) {
            return std::nullopt;
        }
    }

    return sanitizePatternState(pattern);
}

std::optional<VariationState> deserializeVariationState(const std::string& text) {
    VariationState variation;

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) {
            continue;
        }

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        std::int64_t int64Value = 0;
        int intValue = 0;

        if (key == "version") {
            continue;
        } else if (key == "completedLoops" && parseInteger(value, int64Value)) {
            variation.completedLoops = int64Value;
        } else if (key == "lastMutationLoop" && parseInteger(value, int64Value)) {
            variation.lastMutationLoop = int64Value;
        } else if (key == "legacyLastMutationLoop" && parseInteger(value, intValue)) {
            variation.lastMutationLoop = intValue;
        } else {
            return std::nullopt;
        }
    }

    return sanitizeVariationState(variation);
}

}  // namespace downspout::drumgen
