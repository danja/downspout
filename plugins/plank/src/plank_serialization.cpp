#include "plank_core.hpp"

#include <optional>

namespace downspout::plank {
namespace {

// Bump when the meaning of a symbol's value changes, not merely when parameters
// are added. Unknown symbols and a future version number are rejected rather
// than guessed at, so a mismatched project fails loudly instead of loading a
// half-correct patch.
constexpr int kStateVersion = 1;

constexpr const char* kVersionPrefix = "version=";

// Grid cells are momentary, not settings: restoring them would re-pluck every
// string on load. The panic trigger is excluded for the same reason, since
// restoring a value of 1 would fire it.
[[nodiscard]] bool isPersisted(const std::size_t index) noexcept
{
    return index < kCellParameterStart && index != static_cast<std::size_t>(ParamId::panic);
}

}  // namespace

std::string Processor::serializeParameters() const
{
    std::string text = kVersionPrefix;
    text += std::to_string(kStateVersion);
    text += '\n';

    for (std::size_t i = 0; i < kParameterCount; ++i)
    {
        if (!isPersisted(i))
            continue;
        text += kParameterSpecs[i].symbol;
        text += '=';
        text += std::to_string(parameters_[i]);
        text += '\n';
    }

    return text;
}

std::optional<std::array<float, kParameterCount>> Processor::parseState(const std::string_view text) const
{
    std::array<float, kParameterCount> parsed {};

    // Locate the version line first, and reject anything we do not understand.
    std::size_t start = 0;
    bool sawVersion = false;
    std::size_t bodyStart = text.size();

    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;

        if (!line.empty() && line.compare(0, std::char_traits<char>::length(kVersionPrefix), kVersionPrefix) == 0)
        {
            const std::string_view value = line.substr(std::char_traits<char>::length(kVersionPrefix));
            if (value != std::to_string(kStateVersion))
                return std::nullopt;
            sawVersion = true;
            bodyStart = start;
            break;
        }

        if (newline == std::string_view::npos)
            break;
    }

    if (!sawVersion)
        return std::nullopt;

    // Default everything first, so a partially-written state yields a complete
    // and playable patch rather than zeros.
    for (std::size_t i = 0; i < kParameterCount; ++i)
        parsed[i] = kParameterSpecs[i].defaultValue;

    start = bodyStart;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;

        if (line.empty())
        {
            if (newline == std::string_view::npos)
                break;
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos)
            return std::nullopt;

        const std::string_view symbol = line.substr(0, separator);
        const std::string_view value = line.substr(separator + 1);

        // Two different things can go wrong here, and they deserve different
        // answers. A symbol this plugin knows about but deliberately does not
        // persist -- a grid cell, the panic trigger -- is skipped, so a
        // hand-edited or older state cannot re-pluck strings. A symbol matching
        // no parameter at all means the state is from a different plugin or a
        // version we do not understand, and the whole load is refused rather
        // than half-applied.
        bool known = false;
        bool persisted = false;
        for (std::size_t i = 0; i < kParameterCount; ++i)
        {
            if (symbol != kParameterSpecs[i].symbol)
                continue;
            known = true;
            persisted = isPersisted(i);
            if (persisted)
                parsed[i] = static_cast<float>(std::atof(std::string(value).c_str()));
            break;
        }

        if (!known)
            return std::nullopt;
        static_cast<void>(persisted);

        if (newline == std::string_view::npos)
            break;
    }

    return parsed;
}

bool Processor::deserializeParameters(const std::string_view text)
{
    const auto parsed = parseState(text);
    if (!parsed)
        return false;

    // Route every value back through setParameter so clamping, integer snapping
    // and the tuning re-derivation all happen exactly as they would from the UI.
    for (std::size_t i = 0; i < kParameterCount; ++i)
    {
        if (!isPersisted(i))
            continue;
        setParameter(static_cast<std::uint32_t>(i), (*parsed)[i]);
    }

    return true;
}

}  // namespace downspout::plank
