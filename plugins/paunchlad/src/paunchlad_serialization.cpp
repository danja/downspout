#include "paunchlad_processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace downspout::paunchlad {
namespace {

// Bump when the meaning of a stored value changes, not merely when settings are
// added. Unknown keys and a future version number are rejected rather than
// guessed at, so a mismatched project fails loudly instead of loading a
// half-correct patch.
constexpr int kStateVersion = 1;
constexpr const char* kVersionPrefix = "version=";

struct Persisted {
    const char* symbol;
    std::uint32_t index;
};

// The pad cells and the panic trigger are momentary: restoring them would fire
// pads on load. The status outputs are derived. Everything else is a setting.
constexpr Persisted kPersisted[] = {
    {"dry", kParamDry},
    {"wet", kParamWet},
    {"feedback", kParamFeedback},
    {"tone", kParamTone},
    {"siren_level", kParamSirenLevel},
    {"spring", kParamSpring},
    {"output", kParamOutput},
    {"led_feedback", kParamLedFeedback},
    {"pad_map", kParamPadMap},
};

struct Parsed {
    std::array<float, std::size(kPersisted)> values {};
    std::array<bool, std::size(kPersisted)> present {};
};

[[nodiscard]] std::optional<Parsed> parseState(const std::string_view text)
{
    Parsed parsed;

    std::size_t start = 0;
    bool sawVersion = false;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;

        if (!line.empty())
        {
            const std::size_t separator = line.find('=');
            if (separator == std::string_view::npos)
                return std::nullopt;

            const std::string_view key = line.substr(0, separator);
            const std::string value(line.substr(separator + 1));

            if (line.rfind(kVersionPrefix, 0) == 0)
            {
                if (value != std::to_string(kStateVersion))
                    return std::nullopt;
                sawVersion = true;
            }
            else
            {
                bool known = false;
                for (std::size_t i = 0; i < std::size(kPersisted); ++i)
                {
                    if (key != kPersisted[i].symbol)
                        continue;
                    char* tail = nullptr;
                    const double number = std::strtod(value.c_str(), &tail);
                    if (tail == value.c_str() || !std::isfinite(number))
                        return std::nullopt;
                    parsed.values[i] = static_cast<float>(number);
                    parsed.present[i] = true;
                    known = true;
                    break;
                }
                if (!known)
                    return std::nullopt;
            }
        }

        if (newline == std::string_view::npos)
            break;
    }

    if (!sawVersion)
        return std::nullopt;
    return parsed;
}

}  // namespace

std::string Processor::serializeParameters() const
{
    std::string text = kVersionPrefix;
    text += std::to_string(kStateVersion);
    text += '\n';

    for (const Persisted& entry : kPersisted)
    {
        text += entry.symbol;
        text += '=';
        text += std::to_string(parameters_[entry.index]);
        text += '\n';
    }

    return text;
}

bool Processor::deserializeParameters(const std::string_view text)
{
    const auto parsed = parseState(text);
    if (!parsed)
        return false;

    // Route each value through setParameter so clamping, snapping and the LED
    // repaint a pad-map change needs all happen exactly as from the UI.
    for (std::size_t i = 0; i < std::size(kPersisted); ++i)
    {
        if (parsed->present[i])
            setParameter(kPersisted[i].index, parsed->values[i]);
    }

    return true;
}

}  // namespace downspout::paunchlad
