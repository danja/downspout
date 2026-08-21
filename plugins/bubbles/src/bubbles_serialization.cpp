#include "bubbles_serialization.hpp"

#include "bubbles_engine.hpp"

#include <charconv>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::bubbles {
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
    return "version=2\n"
           "mode="         + std::to_string(p.mode)        + "\n"
           "flow="         + std::to_string(p.flow)        + "\n"
           "turbulence="   + std::to_string(p.turbulence)  + "\n"
           "size="         + std::to_string(p.size)        + "\n"
           "density="      + std::to_string(p.density)     + "\n"
           "heat="         + std::to_string(p.heat)        + "\n"
           "depth="        + std::to_string(p.depth)       + "\n"
           "brightness="   + std::to_string(p.brightness)  + "\n"
           "resonance="    + std::to_string(p.resonance)   + "\n"
           "randomness="   + std::to_string(p.randomness)  + "\n"
           "space="        + std::to_string(p.space)       + "\n"
           "drive="        + std::to_string(p.drive)       + "\n"
           "output="       + std::to_string(p.output)      + "\n"
           "conductor_ch=" + std::to_string(p.conductorCh) + "\n"
           "lfo_target="   + std::to_string(p.lfoTarget)   + "\n"
           "lfo_shape="    + std::to_string(p.lfoShape)    + "\n"
           "lfo_rate="     + std::to_string(p.lfoRate)     + "\n"
           "lfo_depth="    + std::to_string(p.lfoDepth)    + "\n"
           "lfo_sync="     + std::to_string(p.lfoSync)     + "\n"
           "lfo_division=" + std::to_string(p.lfoDivision) + "\n";
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
        if (key == "version") continue;
        else if (key == "mode"       && parseFloat(value, v)) p.mode       = v;
        else if (key == "flow"       && parseFloat(value, v)) p.flow       = v;
        else if (key == "turbulence" && parseFloat(value, v)) p.turbulence = v;
        else if (key == "size"       && parseFloat(value, v)) p.size       = v;
        else if (key == "density"    && parseFloat(value, v)) p.density    = v;
        else if (key == "heat"       && parseFloat(value, v)) p.heat       = v;
        else if (key == "depth"      && parseFloat(value, v)) p.depth      = v;
        else if (key == "brightness" && parseFloat(value, v)) p.brightness = v;
        else if (key == "resonance"  && parseFloat(value, v)) p.resonance  = v;
        else if (key == "randomness" && parseFloat(value, v)) p.randomness = v;
        else if (key == "space"        && parseFloat(value, v)) p.space       = v;
        else if (key == "drive"        && parseFloat(value, v)) p.drive       = v;
        else if (key == "output"       && parseFloat(value, v)) p.output      = v;
        else if (key == "conductor_ch" && parseFloat(value, v)) p.conductorCh = v;
        else if (key == "lfo_target"   && parseFloat(value, v)) p.lfoTarget   = v;
        else if (key == "lfo_shape"    && parseFloat(value, v)) p.lfoShape    = v;
        else if (key == "lfo_rate"     && parseFloat(value, v)) p.lfoRate     = v;
        else if (key == "lfo_depth"    && parseFloat(value, v)) p.lfoDepth    = v;
        else if (key == "lfo_sync"     && parseFloat(value, v)) p.lfoSync     = v;
        else if (key == "lfo_division" && parseFloat(value, v)) p.lfoDivision = v;
        else return std::nullopt;
    }
    return clampParameters(p);
}

}  // namespace downspout::bubbles
