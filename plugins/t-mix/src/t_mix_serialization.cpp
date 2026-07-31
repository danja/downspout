#include "t_mix_serialization.hpp"

#include "t_mix_engine.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

namespace downspout::tmix {
namespace {

bool parseFloat(std::string_view text, float& value)
{
    const std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end != nullptr && *end == '\0';
}

bool parseChannelKey(std::string_view key,
                     std::uint32_t& channel,
                     std::string_view& field)
{
    if (!key.starts_with("channel"))
        return false;
    const std::size_t dot = key.find('.');
    if (dot == std::string_view::npos)
        return false;

    const std::string indexText(key.substr(7, dot - 7));
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(indexText.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed >= kInputChannelCount)
        return false;

    channel = static_cast<std::uint32_t>(parsed);
    field = key.substr(dot + 1);
    return true;
}

}  // namespace

std::string serializeParameters(const Parameters& rawParameters)
{
    const Parameters parameters = clampParameters(rawParameters);
    std::string text = "version=2\nmaster_db=" + std::to_string(parameters.masterDb) +
        "\nproducer_slew_ms=" + std::to_string(parameters.producerSlewMs) + "\n";
    for (std::uint32_t channel = 0; channel < kInputChannelCount; ++channel) {
        const std::string prefix = "channel" + std::to_string(channel) + ".";
        const ChannelParameters& strip = parameters.channels[channel];
        text += prefix + "level_db=" + std::to_string(strip.levelDb) + "\n";
        text += prefix + "pan=" + std::to_string(strip.pan) + "\n";
        text += prefix + "mute=" + std::to_string(strip.mute) + "\n";
        text += prefix + "solo=" + std::to_string(strip.solo) + "\n";
    }
    return text;
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
        const std::string_view value = line.substr(separator + 1);
        if (key == "version")
            continue;

        float parsed = 0.0f;
        if (!parseFloat(value, parsed))
            return std::nullopt;
        if (key == "master_db") {
            parameters.masterDb = parsed;
            continue;
        }
        if (key == "producer_slew_ms") {
            parameters.producerSlewMs = parsed;
            continue;
        }

        std::uint32_t channel = 0;
        std::string_view field;
        if (!parseChannelKey(key, channel, field))
            return std::nullopt;
        ChannelParameters& strip = parameters.channels[channel];
        if (field == "level_db")
            strip.levelDb = parsed;
        else if (field == "pan")
            strip.pan = parsed;
        else if (field == "mute")
            strip.mute = parsed;
        else if (field == "solo")
            strip.solo = parsed;
        else
            return std::nullopt;
    }
    return clampParameters(parameters);
}

}  // namespace downspout::tmix
