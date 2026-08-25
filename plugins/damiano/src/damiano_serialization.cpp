#include "damiano_core.hpp"

#include <charconv>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::damiano {
namespace {

bool parseFloat(std::string_view text, float& value) {
    std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end && *end == '\0';
}

std::vector<std::string_view> split(std::string_view text, char delim) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find(delim, start);
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
           "mode="        + std::to_string(p.mode)       + "\n"
           "drive="       + std::to_string(p.drive)      + "\n"
           "tone="        + std::to_string(p.tone)       + "\n"
           "fold_count="  + std::to_string(p.foldCount)  + "\n"
           "mix="         + std::to_string(p.mix)        + "\n"
           "output_gain=" + std::to_string(p.outputGain) + "\n"
           "cc_drive="    + std::to_string(p.ccDrive)    + "\n"
           "cc_channel="  + std::to_string(p.ccChannel)  + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;

        const std::string_view key   = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float v = 0.0f;

        if (key == "version") {
            continue;
        } else if (key == "mode"        && parseFloat(value, v)) { p.mode       = v; }
        else if   (key == "drive"       && parseFloat(value, v)) { p.drive      = v; }
        else if   (key == "tone"        && parseFloat(value, v)) { p.tone       = v; }
        else if   (key == "fold_count"  && parseFloat(value, v)) { p.foldCount  = v; }
        else if   (key == "mix"         && parseFloat(value, v)) { p.mix        = v; }
        else if   (key == "output_gain" && parseFloat(value, v)) { p.outputGain = v; }
        else if   (key == "cc_drive"    && parseFloat(value, v)) { p.ccDrive    = v; }
        else if   (key == "cc_channel"  && parseFloat(value, v)) { p.ccChannel  = v; }
        else { return std::nullopt; }
    }
    return clampParameters(p);
}

}  // namespace downspout::damiano
