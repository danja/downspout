#include "rift_serialization.hpp"

#include "rift_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace downspout::rift {
namespace {

bool parseFloat(std::string_view text, float& value) {
    std::string local(text);
    char* parseEnd = nullptr;
    value = std::strtof(local.c_str(), &parseEnd);
    return parseEnd != nullptr && *parseEnd == '\0';
}

bool parseInt(std::string_view text, int& value) {
    std::string local(text);
    char* parseEnd = nullptr;
    const long parsed = std::strtol(local.c_str(), &parseEnd, 10);
    if (parseEnd == nullptr || *parseEnd != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

std::vector<std::string_view> split(std::string_view text, const char delimiter) {
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

[[nodiscard]] SequenceCellKind clampSequenceKind(const int value) {
    if (value < 0 || value >= kSequenceCellKindCount) {
        return SequenceCellKind::Empty;
    }
    return static_cast<SequenceCellKind>(value);
}

}  // namespace

std::string serializeParameters(const Parameters& parameters) {
    return "version=4\n"
           "grid=" + std::to_string(parameters.grid) + "\n"
           "density=" + std::to_string(parameters.density) + "\n"
           "damage=" + std::to_string(parameters.damage) + "\n"
           "memoryBars=" + std::to_string(parameters.memoryBars) + "\n"
           "drift=" + std::to_string(parameters.drift) + "\n"
           "pitch=" + std::to_string(parameters.pitch) + "\n"
           "mix=" + std::to_string(parameters.mix) + "\n"
           "blend=" + std::to_string(parameters.blend) + "\n"
           "hold=" + std::to_string(parameters.hold) + "\n"
           "sourceMode=" + std::to_string(parameters.sourceMode) + "\n"
           "sampleBeats=" + std::to_string(parameters.sampleBeats) + "\n"
           "chop=" + std::to_string(parameters.chop) + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text) {
    Parameters parameters;

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
        float parsed = 0.0f;

        if (key == "version") {
            continue;
        } else if (key == "grid" && parseFloat(value, parsed)) {
            parameters.grid = parsed;
        } else if (key == "density" && parseFloat(value, parsed)) {
            parameters.density = parsed;
        } else if (key == "damage" && parseFloat(value, parsed)) {
            parameters.damage = parsed;
        } else if (key == "memoryBars" && parseFloat(value, parsed)) {
            parameters.memoryBars = parsed;
        } else if (key == "drift" && parseFloat(value, parsed)) {
            parameters.drift = parsed;
        } else if (key == "pitch" && parseFloat(value, parsed)) {
            parameters.pitch = parsed;
        } else if (key == "mix" && parseFloat(value, parsed)) {
            parameters.mix = parsed;
        } else if (key == "blend" && parseFloat(value, parsed)) {
            parameters.blend = parsed;
        } else if (key == "hold" && parseFloat(value, parsed)) {
            parameters.hold = parsed;
        } else if (key == "sourceMode" && parseFloat(value, parsed)) {
            parameters.sourceMode = parsed;
        } else if (key == "sampleBeats" && parseFloat(value, parsed)) {
            parameters.sampleBeats = parsed;
        } else if (key == "chop" && parseFloat(value, parsed)) {
            parameters.chop = parsed;
        } else {
            return std::nullopt;
        }
    }

    return clampParameters(parameters);
}

std::string serializeSequencePattern(const SequencePattern& pattern) {
    std::string out = "version=1\ncells=";
    for (const SequenceCell& cell : pattern.cells) {
        out += std::to_string(static_cast<int>(cell.kind));
        out += ',';
    }
    out += '\n';
    return out;
}

std::optional<SequencePattern> deserializeSequencePattern(const std::string& text) {
    SequencePattern pattern;

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
            continue;
        }

        if (key != "cells") {
            return std::nullopt;
        }

        const std::vector<std::string_view> parts = split(value, ',');
        const std::size_t count = std::min(parts.size(), pattern.cells.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (parts[i].empty()) {
                continue;
            }

            int parsed = 0;
            if (!parseInt(parts[i], parsed)) {
                return std::nullopt;
            }
            pattern.cells[i].kind = clampSequenceKind(parsed);
        }
    }

    return pattern;
}

bool sequencePatternHasCells(const SequencePattern& pattern) {
    for (const SequenceCell& cell : pattern.cells) {
        if (cell.kind != SequenceCellKind::Empty) {
            return true;
        }
    }
    return false;
}

}  // namespace downspout::rift
