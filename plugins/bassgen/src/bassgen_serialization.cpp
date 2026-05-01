#include "bassgen_serialization.hpp"

#include "bassgen_pattern.hpp"
#include "bassgen_state.hpp"
#include "bassgen_variation.hpp"

#include <charconv>
#include <sstream>
#include <string_view>
#include <vector>

namespace downspout::bassgen {
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
    return parseEnd && *parseEnd == '\0';
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
    out << "version=2\n";
    out << "rootNote=" << controls.rootNote << '\n';
    out << "scale=" << static_cast<int>(controls.scale) << '\n';
    out << "genre=" << static_cast<int>(controls.genre) << '\n';
    out << "styleMode=" << static_cast<int>(controls.styleMode) << '\n';
    out << "channel=" << controls.channel << '\n';
    out << "lengthBeats=" << controls.lengthBeats << '\n';
    out << "subdivision=" << static_cast<int>(controls.subdivision) << '\n';
    out << "density=" << controls.density << '\n';
    out << "reg=" << controls.reg << '\n';
    out << "hold=" << controls.hold << '\n';
    out << "accent=" << controls.accent << '\n';
    out << "vary=" << controls.vary << '\n';
    out << "seed=" << controls.seed << '\n';
    out << "actionNew=" << controls.actionNew << '\n';
    out << "actionNotes=" << controls.actionNotes << '\n';
    out << "actionRhythm=" << controls.actionRhythm << '\n';
    return out.str();
}

std::string serializePatternState(const PatternState& pattern) {
    std::ostringstream out;
    out << "version=" << pattern.version << '\n';
    out << "patternSteps=" << pattern.patternSteps << '\n';
    out << "stepsPerBeat=" << pattern.stepsPerBeat << '\n';
    out << "stepsPerBar=" << pattern.stepsPerBar << '\n';
    out << "meterNumerator=" << pattern.meter.numerator << '\n';
    out << "meterDenominator=" << pattern.meter.denominator << '\n';
    out << "eventCount=" << pattern.eventCount << '\n';
    out << "generationSerial=" << pattern.generationSerial << '\n';
    for (int index = 0; index < pattern.eventCount; ++index) {
        const NoteEvent& event = pattern.events[index];
        out << "event=" << event.startStep << ','
            << event.durationSteps << ','
            << event.note << ','
            << event.velocity << '\n';
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
        float floatValue = 0.0f;
        unsigned int uintValue = 0;

        if (key == "version") {
            continue;
        } else if (key == "rootNote" && parseInteger(value, intValue)) {
            controls.rootNote = intValue;
        } else if (key == "scale" && parseInteger(value, intValue)) {
            controls.scale = static_cast<ScaleId>(intValue);
        } else if (key == "genre" && parseInteger(value, intValue)) {
            controls.genre = static_cast<GenreId>(intValue);
        } else if (key == "styleMode" && parseInteger(value, intValue)) {
            controls.styleMode = static_cast<StyleModeId>(intValue);
        } else if (key == "channel" && parseInteger(value, intValue)) {
            controls.channel = intValue;
        } else if (key == "lengthBeats" && parseInteger(value, intValue)) {
            controls.lengthBeats = intValue;
        } else if (key == "subdivision" && parseInteger(value, intValue)) {
            controls.subdivision = static_cast<SubdivisionId>(intValue);
        } else if (key == "density" && parseFloat(value, floatValue)) {
            controls.density = floatValue;
        } else if (key == "reg" && parseInteger(value, intValue)) {
            controls.reg = intValue;
        } else if (key == "hold" && parseFloat(value, floatValue)) {
            controls.hold = floatValue;
        } else if (key == "accent" && parseFloat(value, floatValue)) {
            controls.accent = floatValue;
        } else if (key == "vary" && parseFloat(value, floatValue)) {
            controls.vary = floatValue;
        } else if (key == "seed" && parseInteger(value, uintValue)) {
            controls.seed = uintValue;
        } else if (key == "actionNew" && parseInteger(value, intValue)) {
            controls.actionNew = intValue;
        } else if (key == "actionNotes" && parseInteger(value, intValue)) {
            controls.actionNotes = intValue;
        } else if (key == "actionRhythm" && parseInteger(value, intValue)) {
            controls.actionRhythm = intValue;
        } else {
            return std::nullopt;
        }
    }

    return clampControls(controls);
}

std::optional<PatternState> deserializePatternState(const std::string& text) {
    PatternState pattern;
    int eventIndex = 0;

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

        if (key == "version") {
            if (!parseInteger(value, pattern.version)) {
                return std::nullopt;
            }
        } else if (key == "patternSteps") {
            if (!parseInteger(value, pattern.patternSteps)) {
                return std::nullopt;
            }
        } else if (key == "stepsPerBeat") {
            if (!parseInteger(value, pattern.stepsPerBeat)) {
                return std::nullopt;
            }
        } else if (key == "stepsPerBar") {
            if (!parseInteger(value, pattern.stepsPerBar)) {
                return std::nullopt;
            }
        } else if (key == "meterNumerator") {
            if (!parseInteger(value, pattern.meter.numerator)) {
                return std::nullopt;
            }
        } else if (key == "meterDenominator") {
            if (!parseInteger(value, pattern.meter.denominator)) {
                return std::nullopt;
            }
        } else if (key == "eventCount") {
            if (!parseInteger(value, pattern.eventCount)) {
                return std::nullopt;
            }
        } else if (key == "generationSerial") {
            if (!parseInteger(value, pattern.generationSerial)) {
                return std::nullopt;
            }
        } else if (key == "event") {
            if (eventIndex >= kMaxEvents) {
                return std::nullopt;
            }
            const auto parts = split(value, ',');
            if (parts.size() != 4 ||
                !parseInteger(parts[0], pattern.events[eventIndex].startStep) ||
                !parseInteger(parts[1], pattern.events[eventIndex].durationSteps) ||
                !parseInteger(parts[2], pattern.events[eventIndex].note) ||
                !parseInteger(parts[3], pattern.events[eventIndex].velocity)) {
                return std::nullopt;
            }
            ++eventIndex;
        } else {
            return std::nullopt;
        }
    }

    bool valid = false;
    PatternState sanitized = sanitizePatternState(pattern, &valid);
    if (!valid && sanitized.eventCount <= 0) {
        return std::nullopt;
    }
    sanitized.eventCount = eventIndex;
    sanitized = sanitizePatternState(sanitized, &valid);
    return sanitized;
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

        if (key == "version") {
            if (!parseInteger(value, variation.version)) {
                return std::nullopt;
            }
        } else if (key == "completedLoops") {
            if (!parseInteger(value, variation.completedLoops)) {
                return std::nullopt;
            }
        } else if (key == "lastMutationLoop") {
            if (!parseInteger(value, variation.lastMutationLoop)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }

    return sanitizeVariationState(variation);
}

}  // namespace downspout::bassgen
