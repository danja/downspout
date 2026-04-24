#include "p_mix_serialization.hpp"

#include "p_mix_engine.hpp"

#include <charconv>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::pmix {
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

std::string serializeParameters(const Parameters& parameters) {
    return "version=2\n"
           "granularity=" + std::to_string(parameters.granularity) + "\n"
           "maintain=" + std::to_string(parameters.maintain) + "\n"
           "fade=" + std::to_string(parameters.fade) + "\n"
           "cut=" + std::to_string(parameters.cut) + "\n"
           "fadeDurMax=" + std::to_string(parameters.fadeDurMax) + "\n"
           "bias=" + std::to_string(parameters.bias) + "\n"
           "mute=" + std::to_string(parameters.mute) + "\n";
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
        } else if (key == "granularity" && parseFloat(value, floatValue)) {
            parameters.granularity = floatValue;
        } else if (key == "maintain" && parseFloat(value, floatValue)) {
            parameters.maintain = floatValue;
        } else if (key == "fade" && parseFloat(value, floatValue)) {
            parameters.fade = floatValue;
        } else if (key == "cut" && parseFloat(value, floatValue)) {
            parameters.cut = floatValue;
        } else if (key == "fadeDurMax" && parseFloat(value, floatValue)) {
            parameters.fadeDurMax = floatValue;
        } else if (key == "bias" && parseFloat(value, floatValue)) {
            parameters.bias = floatValue;
        } else if (key == "mute" && parseFloat(value, floatValue)) {
            parameters.mute = floatValue;
        } else {
            return std::nullopt;
        }
    }

    return clampParameters(parameters);
}

}  // namespace downspout::pmix
