#include "orchid_serialization.hpp"

#include "orchid_engine.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace downspout::orchid {
namespace {

bool parseFloat(std::string_view text, float& value) {
    std::string local(text);
    char* parseEnd = nullptr;
    value = std::strtof(local.c_str(), &parseEnd);
    return parseEnd != nullptr && *parseEnd == '\0';
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

}  // namespace

std::string serializeParameters(const Parameters& parameters) {
    return "version=1\n"
           "sensitivity=" + std::to_string(parameters.sensitivity) + "\n"
           "periodicity=" + std::to_string(parameters.periodicity) + "\n"
           "stabilityMs=" + std::to_string(parameters.stabilityMs) + "\n"
           "pitchLow=" + std::to_string(parameters.pitchLow) + "\n"
           "pitchHigh=" + std::to_string(parameters.pitchHigh) + "\n"
           "grid=" + std::to_string(parameters.grid) + "\n"
           "holdUnits=" + std::to_string(parameters.holdUnits) + "\n"
           "releaseMs=" + std::to_string(parameters.releaseMs) + "\n"
           "cooldownMs=" + std::to_string(parameters.cooldownMs) + "\n"
           "loopPeriods=" + std::to_string(parameters.loopPeriods) + "\n"
           "mix=" + std::to_string(parameters.mix) + "\n"
           "liveUnder=" + std::to_string(parameters.liveUnder) + "\n";
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
        } else if (key == "sensitivity" && parseFloat(value, parsed)) {
            parameters.sensitivity = parsed;
        } else if (key == "periodicity" && parseFloat(value, parsed)) {
            parameters.periodicity = parsed;
        } else if (key == "stabilityMs" && parseFloat(value, parsed)) {
            parameters.stabilityMs = parsed;
        } else if (key == "pitchLow" && parseFloat(value, parsed)) {
            parameters.pitchLow = parsed;
        } else if (key == "pitchHigh" && parseFloat(value, parsed)) {
            parameters.pitchHigh = parsed;
        } else if (key == "grid" && parseFloat(value, parsed)) {
            parameters.grid = parsed;
        } else if (key == "holdUnits" && parseFloat(value, parsed)) {
            parameters.holdUnits = parsed;
        } else if (key == "releaseMs" && parseFloat(value, parsed)) {
            parameters.releaseMs = parsed;
        } else if (key == "cooldownMs" && parseFloat(value, parsed)) {
            parameters.cooldownMs = parsed;
        } else if (key == "loopPeriods" && parseFloat(value, parsed)) {
            parameters.loopPeriods = parsed;
        } else if (key == "mix" && parseFloat(value, parsed)) {
            parameters.mix = parsed;
        } else if (key == "liveUnder" && parseFloat(value, parsed)) {
            parameters.liveUnder = parsed;
        } else {
            return std::nullopt;
        }
    }

    return clampParameters(parameters);
}

}  // namespace downspout::orchid
