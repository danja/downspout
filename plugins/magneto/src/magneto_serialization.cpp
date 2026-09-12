#include "magneto_serialization.hpp"

#include "magneto_engine.hpp"
#include "magneto_params.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace downspout::magneto {
namespace {

// Address of each host-writable field, in ParamId order, so serialization and
// parsing both stay in step with kParameterSpecs.
using FieldPointer = float Parameters::*;

constexpr std::array<FieldPointer, kInputParameterCount> kFields = {{
    &Parameters::cylinders,
    &Parameters::displacement,
    &Parameters::compression,
    &Parameters::ignition,
    &Parameters::asymmetry,
    &Parameters::blockGain,
    &Parameters::intakeLen,
    &Parameters::intakeGain,
    &Parameters::turbulence,
    &Parameters::extractorLen,
    &Parameters::pipeLen,
    &Parameters::mufflerLen,
    &Parameters::mufflerAction,
    &Parameters::outletLen,
    &Parameters::outletGain,
    &Parameters::backfire,
    &Parameters::rpm,
    &Parameters::throttle,
    &Parameters::rpmSource,
    &Parameters::syncRatio,
    &Parameters::idleRpm,
    &Parameters::inertia,
    &Parameters::seed,
    &Parameters::midiCh,
    &Parameters::listen,
    &Parameters::width,
    &Parameters::level,
}};

[[nodiscard]] bool parseFloat(const std::string_view text, float& out)
{
    if (text.empty())
        return false;
    const std::string buffer(text);
    char* end = nullptr;
    const float value = std::strtof(buffer.c_str(), &end);
    if (end == nullptr || *end != '\0')
        return false;
    out = value;
    return true;
}

}  // namespace

std::string serializeParameters(const Parameters& params)
{
    std::string text = "version=" + std::to_string(kStateVersion) + "\n";
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
    {
        text += kParameterSpecs[i].symbol;
        text += '=';
        text += std::to_string(params.*kFields[i]);
        text += '\n';
    }
    return text;
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters params;

    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = (newline == std::string::npos) ? text.size() : newline;
        const std::string_view line(text.data() + start, end - start);
        start = end + 1;

        if (line.empty())
        {
            if (newline == std::string::npos)
                break;
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos)
            return std::nullopt;

        const std::string_view key = line.substr(0, separator);
        const std::string_view value = line.substr(separator + 1);

        if (key == "version")
            continue;

        bool matched = false;
        for (std::size_t i = 0; i < kInputParameterCount; ++i)
        {
            if (key != kParameterSpecs[i].symbol)
                continue;
            float parsed = 0.0f;
            if (!parseFloat(value, parsed))
                return std::nullopt;
            params.*kFields[i] = parsed;
            matched = true;
            break;
        }

        if (!matched)
            return std::nullopt;

        if (newline == std::string::npos)
            break;
    }

    return clampParameters(params);
}

}  // namespace downspout::magneto
