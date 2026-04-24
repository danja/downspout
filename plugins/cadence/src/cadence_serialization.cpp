#include "cadence_serialization.hpp"

#include <charconv>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::cadence {
namespace {

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end != nullptr && *end == '\0';
}

template <typename T>
bool parseInteger(std::string_view text, T& value)
{
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

std::vector<std::string_view> split(std::string_view text, const char delimiter)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;

    while (start <= text.size())
    {
        const std::size_t pos = text.find(delimiter, start);
        if (pos == std::string_view::npos)
        {
            parts.push_back(text.substr(start));
            break;
        }

        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }

    return parts;
}

}  // namespace

std::string serializeControls(const Controls& controls)
{
    return "version=1\n"
           "key=" + std::to_string(controls.key) + "\n"
           "scale=" + std::to_string(controls.scale) + "\n"
           "cycle_bars=" + std::to_string(controls.cycle_bars) + "\n"
           "granularity=" + std::to_string(controls.granularity) + "\n"
           "complexity=" + std::to_string(controls.complexity) + "\n"
           "movement=" + std::to_string(controls.movement) + "\n"
           "chord_size=" + std::to_string(controls.chord_size) + "\n"
           "note_length=" + std::to_string(controls.note_length) + "\n"
           "reg=" + std::to_string(controls.reg) + "\n"
           "spread=" + std::to_string(controls.spread) + "\n"
           "pass_input=" + std::to_string(controls.pass_input ? 1 : 0) + "\n"
           "output_channel=" + std::to_string(controls.output_channel) + "\n"
           "action_learn=" + std::to_string(controls.action_learn) + "\n"
           "vary=" + std::to_string(controls.vary) + "\n"
           "comp=" + std::to_string(controls.comp) + "\n";
}

std::optional<Controls> deserializeControls(const std::string& text)
{
    Controls controls = defaultControls();

    for (const std::string_view line : split(text, '\n'))
    {
        if (line.empty())
            continue;

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float floatValue = 0.0f;
        int intValue = 0;

        if (key == "version")
        {
            continue;
        }
        else if (key == "key" && parseInteger(value, intValue))
        {
            controls.key = intValue;
        }
        else if (key == "scale" && parseInteger(value, intValue))
        {
            controls.scale = intValue;
        }
        else if (key == "cycle_bars" && parseInteger(value, intValue))
        {
            controls.cycle_bars = intValue;
        }
        else if (key == "granularity" && parseInteger(value, intValue))
        {
            controls.granularity = intValue;
        }
        else if (key == "complexity" && parseFloat(value, floatValue))
        {
            controls.complexity = floatValue;
        }
        else if (key == "movement" && parseFloat(value, floatValue))
        {
            controls.movement = floatValue;
        }
        else if (key == "chord_size" && parseInteger(value, intValue))
        {
            controls.chord_size = intValue;
        }
        else if (key == "note_length" && parseFloat(value, floatValue))
        {
            controls.note_length = floatValue;
        }
        else if (key == "reg" && parseInteger(value, intValue))
        {
            controls.reg = intValue;
        }
        else if (key == "spread" && parseInteger(value, intValue))
        {
            controls.spread = intValue;
        }
        else if (key == "pass_input" && parseInteger(value, intValue))
        {
            controls.pass_input = intValue != 0;
        }
        else if (key == "output_channel" && parseInteger(value, intValue))
        {
            controls.output_channel = intValue;
        }
        else if (key == "action_learn" && parseInteger(value, intValue))
        {
            controls.action_learn = intValue;
        }
        else if (key == "vary" && parseFloat(value, floatValue))
        {
            controls.vary = floatValue;
        }
        else if (key == "comp" && parseFloat(value, floatValue))
        {
            controls.comp = floatValue;
        }
        else
        {
            return std::nullopt;
        }
    }

    return clampControls(controls);
}

std::string serializeProgressionState(const ProgressionState& state)
{
    std::string text;
    text.reserve(2048);
    text += "version=" + std::to_string(state.version) + "\n";
    text += "segment_count=" + std::to_string(clampi(state.segmentCount, 0, kMaxSegments)) + "\n";
    text += "ready=" + std::to_string(state.ready ? 1 : 0) + "\n";

    for (int i = 0; i < clampi(state.segmentCount, 0, kMaxSegments); ++i)
    {
        const ChordSlot& slot = state.slots[static_cast<std::size_t>(i)];
        const std::string prefix = "slot" + std::to_string(i) + ".";
        text += prefix + "valid=" + std::to_string(slot.valid ? 1 : 0) + "\n";
        text += prefix + "root_pc=" + std::to_string(slot.root_pc) + "\n";
        text += prefix + "quality=" + std::to_string(slot.quality) + "\n";
        text += prefix + "note_count=" + std::to_string(slot.note_count) + "\n";
        text += prefix + "velocity=" + std::to_string(slot.velocity) + "\n";
        for (int noteIndex = 0; noteIndex < kMaxChordNotes; ++noteIndex)
            text += prefix + "note" + std::to_string(noteIndex) + "=" + std::to_string(slot.notes[static_cast<std::size_t>(noteIndex)]) + "\n";
    }

    return text;
}

std::optional<ProgressionState> deserializeProgressionState(const std::string& text)
{
    ProgressionState state;

    for (const std::string_view line : split(text, '\n'))
    {
        if (line.empty())
            continue;

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        int intValue = 0;

        if (!parseInteger(value, intValue))
            return std::nullopt;

        if (key == "version")
        {
            state.version = intValue;
        }
        else if (key == "segment_count")
        {
            state.segmentCount = clampi(intValue, 0, kMaxSegments);
        }
        else if (key == "ready")
        {
            state.ready = intValue != 0;
        }
        else if (key.rfind("slot", 0) == 0)
        {
            const std::size_t dot = key.find('.');
            if (dot == std::string_view::npos)
                return std::nullopt;

            int slotIndex = 0;
            if (!parseInteger(key.substr(4, dot - 4), slotIndex))
                return std::nullopt;
            slotIndex = clampi(slotIndex, 0, kMaxSegments - 1);

            ChordSlot& slot = state.slots[static_cast<std::size_t>(slotIndex)];
            const std::string_view field = key.substr(dot + 1);

            if (field == "valid")
                slot.valid = intValue != 0;
            else if (field == "root_pc")
                slot.root_pc = static_cast<std::uint8_t>(clampi(intValue, 0, 11));
            else if (field == "quality")
                slot.quality = static_cast<std::uint8_t>(clampi(intValue, 0, QUALITY_MIN7));
            else if (field == "note_count")
                slot.note_count = static_cast<std::uint8_t>(clampi(intValue, 0, kMaxChordNotes));
            else if (field == "velocity")
                slot.velocity = static_cast<std::uint8_t>(clampi(intValue, 1, 127));
            else if (field.rfind("note", 0) == 0)
            {
                int noteIndex = 0;
                if (!parseInteger(field.substr(4), noteIndex))
                    return std::nullopt;
                if (noteIndex < 0 || noteIndex >= kMaxChordNotes)
                    return std::nullopt;
                slot.notes[static_cast<std::size_t>(noteIndex)] = static_cast<std::uint8_t>(clampi(intValue, 0, 127));
            }
            else
            {
                return std::nullopt;
            }
        }
        else
        {
            return std::nullopt;
        }
    }

    state.segmentCount = clampi(state.segmentCount, 0, kMaxSegments);
    state.ready = state.ready && state.segmentCount > 0;
    if (state.version != kProgressionStateVersion)
        state.version = kProgressionStateVersion;
    return state;
}

std::string serializeVariationState(const VariationState& state)
{
    return "version=" + std::to_string(state.version) + "\n"
           "completed_cycles=" + std::to_string(state.completed_cycles) + "\n"
           "last_mutation_cycle=" + std::to_string(state.last_mutation_cycle) + "\n"
           "mutation_serial=" + std::to_string(state.mutation_serial) + "\n";
}

std::optional<VariationState> deserializeVariationState(const std::string& text)
{
    VariationState state = defaultVariationState();

    for (const std::string_view line : split(text, '\n'))
    {
        if (line.empty())
            continue;

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        if (key == "version")
        {
            int version = 0;
            if (!parseInteger(value, version))
                return std::nullopt;
            state.version = version;
        }
        else if (key == "completed_cycles")
        {
            if (!parseInteger(value, state.completed_cycles))
                return std::nullopt;
        }
        else if (key == "last_mutation_cycle")
        {
            if (!parseInteger(value, state.last_mutation_cycle))
                return std::nullopt;
        }
        else if (key == "mutation_serial")
        {
            if (!parseInteger(value, state.mutation_serial))
                return std::nullopt;
        }
        else
        {
            return std::nullopt;
        }
    }

    if (state.version != kVariationStateVersion)
        state.version = kVariationStateVersion;
    return state;
}

}  // namespace downspout::cadence
