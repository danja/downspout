#include "loopdelay_serialization.hpp"

#include <cstdlib>
#include <string_view>

namespace downspout::loopdelay {
namespace {

bool parseFloat(const std::string_view text, float& value)
{
    const std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end != nullptr && *end == '\0';
}

} // namespace

std::string serializeParameters(const Parameters& raw)
{
    const Parameters parameters = clampParameters(raw);
    std::string result = "version=1\n";
    for (std::uint32_t index = 0; index <= kMidiEnabled; ++index)
        result += std::string(kParameterSpecs[index].symbol) + "="
            + std::to_string(parameters.values[index]) + "\n";
    return result;
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters parameters;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string_view line(text.data() + start,
            (newline == std::string::npos ? text.size() : newline) - start);
        start = newline == std::string::npos ? text.size() + 1 : newline + 1;
        if (line.empty())
            continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos)
            return std::nullopt;
        const std::string_view key = line.substr(0, separator);
        if (key == "version")
            continue;
        float value = 0.0f;
        if (!parseFloat(line.substr(separator + 1), value))
            return std::nullopt;
        bool found = false;
        for (std::uint32_t index = 0; index <= kMidiEnabled; ++index) {
            if (key == kParameterSpecs[index].symbol) {
                parameters.values[index] = value;
                found = true;
                break;
            }
        }
        if (!found)
            return std::nullopt;
    }
    return clampParameters(parameters);
}

} // namespace downspout::loopdelay
