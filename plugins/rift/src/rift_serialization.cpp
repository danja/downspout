#include "rift_serialization.hpp"

#include "rift_engine.hpp"

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
           "grid=" + std::to_string(parameters.grid) + "\n"
           "density=" + std::to_string(parameters.density) + "\n"
           "damage=" + std::to_string(parameters.damage) + "\n"
           "memoryBars=" + std::to_string(parameters.memoryBars) + "\n"
           "drift=" + std::to_string(parameters.drift) + "\n"
           "pitch=" + std::to_string(parameters.pitch) + "\n"
           "mix=" + std::to_string(parameters.mix) + "\n"
           "hold=" + std::to_string(parameters.hold) + "\n";
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
        } else if (key == "hold" && parseFloat(value, parsed)) {
            parameters.hold = parsed;
        } else {
            return std::nullopt;
        }
    }

    return clampParameters(parameters);
}

}  // namespace downspout::rift
