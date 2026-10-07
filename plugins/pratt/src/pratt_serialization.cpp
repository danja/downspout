#include "pratt_params.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace downspout::pratt {

std::string serializeSettings(const Settings& s) {
    std::string out = "version=" + std::to_string(kStateVersion) + "\n";
    char buffer[64];
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(clampParameter(i, s.v[i])));
        out += kParameterSpecs[i].symbol;
        out += '=';
        out += buffer;
        out += '\n';
    }
    return out;
}

std::optional<Settings> deserializeSettings(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "version=" + std::to_string(kStateVersion)) return std::nullopt;

    Settings result;
    std::array<bool, kInputParameterCount> seen{};
    std::size_t count = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= line.size()) return std::nullopt;
        const std::string key = line.substr(0, eq);
        const std::string valueText = line.substr(eq + 1);

        std::size_t index = kInputParameterCount;
        for (std::size_t i = 0; i < kInputParameterCount; ++i)
            if (key == kParameterSpecs[i].symbol) index = i;
        if (index == kInputParameterCount || seen[index]) return std::nullopt;

        errno = 0;
        char* end = nullptr;
        const double value = std::strtod(valueText.c_str(), &end);
        if (errno != 0 || end == valueText.c_str() || *end != '\0' || !std::isfinite(value)) return std::nullopt;
        seen[index] = true;
        ++count;
        result.v[index] = static_cast<float>(value);
    }
    if (count != kInputParameterCount) return std::nullopt;
    return clampSettings(result);
}

}  // namespace downspout::pratt
