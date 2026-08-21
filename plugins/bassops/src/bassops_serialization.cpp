#include "bassops_serialization.hpp"

#include "bassops_engine.hpp"

#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::bassops {
namespace {

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
    char* parseEnd = nullptr;
    value = std::strtof(local.c_str(), &parseEnd);
    return parseEnd && *parseEnd == '\0';
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

std::string serializeParameters(const Parameters& p)
{
    return "version=1\n"
           "duckDepth=" + std::to_string(p.duckDepth) + "\n"
           "attackMs="  + std::to_string(p.attackMs)  + "\n"
           "releaseMs=" + std::to_string(p.releaseMs) + "\n"
           "cutoffHz="  + std::to_string(p.cutoffHz)  + "\n"
           "sideShape=" + std::to_string(p.sideShape) + "\n"
           "wet="       + std::to_string(p.wet)       + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) { continue; }
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) { return std::nullopt; }
        const std::string_view key   = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float fv = 0.0f;

        if (key == "version") {
            continue;
        } else if (key == "duckDepth" && parseFloat(value, fv)) {
            p.duckDepth = fv;
        } else if (key == "attackMs" && parseFloat(value, fv)) {
            p.attackMs = fv;
        } else if (key == "releaseMs" && parseFloat(value, fv)) {
            p.releaseMs = fv;
        } else if (key == "cutoffHz" && parseFloat(value, fv)) {
            p.cutoffHz = fv;
        } else if (key == "sideShape" && parseFloat(value, fv)) {
            p.sideShape = fv;
        } else if (key == "wet" && parseFloat(value, fv)) {
            p.wet = fv;
        } else {
            return std::nullopt;
        }
    }
    return clampParameters(p);
}

}  // namespace downspout::bassops
