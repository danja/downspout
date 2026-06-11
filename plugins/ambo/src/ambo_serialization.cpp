#include "ambo_serialization.hpp"

#include "ambo_engine.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace downspout::ambo {
namespace {

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
    char* parseEnd = nullptr;
    value = std::strtof(local.c_str(), &parseEnd);
    return parseEnd != nullptr && *parseEnd == '\0';
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

std::string serializeParameters(const Parameters& rawParameters)
{
    const Parameters parameters = clampParameters(rawParameters);
    return "version=1\n"
           "chain=" + std::to_string(parameters.chain) + "\n"
           "time=" + std::to_string(parameters.time) + "\n"
           "spectral=" + std::to_string(parameters.spectral) + "\n"
           "tape=" + std::to_string(parameters.tape) + "\n"
           "shimmer=" + std::to_string(parameters.shimmer) + "\n"
           "delay=" + std::to_string(parameters.delay) + "\n"
           "drive=" + std::to_string(parameters.drive) + "\n"
           "feedback=" + std::to_string(parameters.feedback) + "\n"
           "mix=" + std::to_string(parameters.mix) + "\n"
           "output=" + std::to_string(parameters.output) + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters parameters;

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty())
            continue;

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float floatValue = 0.0f;

        if (key == "version") {
            continue;
        } else if (key == "chain" && parseFloat(value, floatValue)) {
            parameters.chain = floatValue;
        } else if (key == "time" && parseFloat(value, floatValue)) {
            parameters.time = floatValue;
        } else if (key == "spectral" && parseFloat(value, floatValue)) {
            parameters.spectral = floatValue;
        } else if (key == "tape" && parseFloat(value, floatValue)) {
            parameters.tape = floatValue;
        } else if (key == "shimmer" && parseFloat(value, floatValue)) {
            parameters.shimmer = floatValue;
        } else if (key == "delay" && parseFloat(value, floatValue)) {
            parameters.delay = floatValue;
        } else if (key == "drive" && parseFloat(value, floatValue)) {
            parameters.drive = floatValue;
        } else if (key == "feedback" && parseFloat(value, floatValue)) {
            parameters.feedback = floatValue;
        } else if (key == "mix" && parseFloat(value, floatValue)) {
            parameters.mix = floatValue;
        } else if (key == "output" && parseFloat(value, floatValue)) {
            parameters.output = floatValue;
        } else {
            return std::nullopt;
        }
    }

    return clampParameters(parameters);
}

}  // namespace downspout::ambo
