#include "lifeform_processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace downspout::lifeform {
namespace {

// Bump when the meaning of a stored value changes, not merely when settings are
// added. Unknown keys and a future version number are rejected rather than
// guessed at, so a mismatched project fails loudly instead of loading a
// half-correct patch.
constexpr int kStateVersion = 1;
constexpr const char* kVersionPrefix = "version=";
constexpr const char* kCellsKey = "cells";

struct Persisted {
    const char* symbol;
    std::uint32_t index;
};

// Settings only. The 64 cell parameters are stored as one bitmap instead; the
// randomize / clear / step / panic triggers are momentary and the status outputs
// are derived, so none of those are written. Seed is stored but applied without
// re-seeding, or it would overwrite the restored pattern.
constexpr Persisted kPersisted[] = {
    {"root", kParamRootNote},
    {"scale", kParamScale},
    {"gate", kParamGate},
    {"velocity", kParamVelocity},
    {"mutation", kParamMutation},
    {"density", kParamDensity},
    {"clock_mode", kParamClockMode},
    {"output_mode", kParamOutputMode},
    {"emit_mode", kParamEmitMode},
    {"base_channel", kParamBaseChannel},
    {"led_feedback", kParamLedFeedback},
    {"running", kParamRunning},
    {"seed", kParamSeed},
    {"pass_input", kParamPassInput},
};

struct Parsed {
    std::array<float, std::size(kPersisted)> values {};
    std::array<bool, std::size(kPersisted)> present {};
    std::array<bool, kCellCount> cells {};
    bool hasCells = false;
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
            else if (key == kCellsKey)
            {
                if (value.size() != kCellCount)
                    return std::nullopt;
                for (std::size_t i = 0; i < kCellCount; ++i)
                {
                    if (value[i] != '0' && value[i] != '1')
                        return std::nullopt;
                    parsed.cells[i] = value[i] == '1';
                }
                parsed.hasCells = true;
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

    text += kCellsKey;
    text += '=';
    for (std::size_t i = 0; i < kCellCount; ++i)
        text += cells_[i] ? '1' : '0';
    text += '\n';

    return text;
}

bool Processor::deserializeParameters(const std::string_view text)
{
    const auto parsed = parseState(text);
    if (!parsed)
        return false;

    for (std::size_t i = 0; i < std::size(kPersisted); ++i)
    {
        if (!parsed->present[i])
            continue;

        if (kPersisted[i].index == kParamSeed)
        {
            // Store the seed without calling seedPattern(), which would replace the
            // restored pattern with the preset.
            parameters_[kParamSeed] =
                static_cast<float>(std::clamp(static_cast<int>(std::lround(parsed->values[i])), 0, 7));
            continue;
        }
        setParameter(kPersisted[i].index, parsed->values[i]);
    }

    if (parsed->hasCells)
    {
        for (std::size_t i = 0; i < kCellCount; ++i)
        {
            cells_[i] = parsed->cells[i];
            born_[i] = parsed->cells[i];
            parameters_[kParamCellStart + i] = parsed->cells[i] ? 1.0f : 0.0f;
        }
    }

    lastCellLeds_.fill(255u);  // repaint the Launchpad with the restored pattern
    updateStatus();
    return true;
}

}  // namespace downspout::lifeform
