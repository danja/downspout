#include "e_mix_serialization.hpp"

#include "e_mix_engine.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace downspout::emix {
namespace {

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

std::string serializeParameters(const Parameters& parameters) {
    return "version=1\n"
           "totalBars=" + std::to_string(parameters.totalBars) + "\n"
           "division=" + std::to_string(parameters.division) + "\n"
           "steps=" + std::to_string(parameters.steps) + "\n"
           "offset=" + std::to_string(parameters.offset) + "\n"
           "fadeBars=" + std::to_string(parameters.fadeBars) + "\n";
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
        float floatValue = 0.0f;

        if (key == "version") {
            continue;
        } else if (key == "totalBars" && parseFloat(value, floatValue)) {
            parameters.totalBars = floatValue;
        } else if (key == "division" && parseFloat(value, floatValue)) {
            parameters.division = floatValue;
        } else if (key == "steps" && parseFloat(value, floatValue)) {
            parameters.steps = floatValue;
        } else if (key == "offset" && parseFloat(value, floatValue)) {
            parameters.offset = floatValue;
        } else if (key == "fadeBars" && parseFloat(value, floatValue)) {
            parameters.fadeBars = floatValue;
        } else {
            return std::nullopt;
        }
    }

    return clampParameters(parameters);
}

}  // namespace downspout::emix
