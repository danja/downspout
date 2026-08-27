#include "skream_core.hpp"

#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::skream {
namespace {

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end && *end == '\0';
}

std::vector<std::string_view> split(std::string_view text, char delim)
{
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
           "input_gain="  + std::to_string(p.inputGain)  + "\n"
           "cutoff="      + std::to_string(p.cutoff)      + "\n"
           "scream="      + std::to_string(p.scream)      + "\n"
           "resonance="   + std::to_string(p.resonance)   + "\n"
           "mix="         + std::to_string(p.mix)         + "\n"
           "output_gain=" + std::to_string(p.outputGain)  + "\n"
           "track="       + std::to_string(p.track)       + "\n"
           "cc_cutoff="   + std::to_string(p.ccCutoff)    + "\n"
           "cc_scream="   + std::to_string(p.ccScream)    + "\n"
           "cc_channel="  + std::to_string(p.ccChannel)   + "\n";
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

        if      (key == "version")                              { continue; }
        else if (key == "input_gain"  && parseFloat(value, v)) { p.inputGain  = v; }
        else if (key == "cutoff"      && parseFloat(value, v)) { p.cutoff     = v; }
        else if (key == "scream"      && parseFloat(value, v)) { p.scream     = v; }
        else if (key == "resonance"   && parseFloat(value, v)) { p.resonance  = v; }
        else if (key == "mix"         && parseFloat(value, v)) { p.mix        = v; }
        else if (key == "output_gain" && parseFloat(value, v)) { p.outputGain = v; }
        else if (key == "track"       && parseFloat(value, v)) { p.track      = v; }
        else if (key == "cc_cutoff"   && parseFloat(value, v)) { p.ccCutoff   = v; }
        else if (key == "cc_scream"   && parseFloat(value, v)) { p.ccScream   = v; }
        else if (key == "cc_channel"  && parseFloat(value, v)) { p.ccChannel  = v; }
        else { continue; }  // unknown keys are ignored for forward compatibility
    }
    return clampParameters(p);
}

}  // namespace downspout::skream
