#include "ground_serialization.hpp"

#include "ground_pattern.hpp"
#include "ground_variation.hpp"

#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace downspout::ground {
namespace {

template <typename T>
bool parseInteger(std::string_view text, T& value)
{
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end != nullptr && *end == '\0';
}

std::vector<std::string_view> split(std::string_view text, const char delimiter)
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

std::string serializeControls(const Controls& rawControls)
{
    const Controls controls = clampControls(rawControls);
    return "version=1\n"
           "root_note=" + std::to_string(controls.rootNote) + "\n"
           "scale=" + std::to_string(static_cast<int>(controls.scale)) + "\n"
           "style=" + std::to_string(static_cast<int>(controls.style)) + "\n"
           "channel=" + std::to_string(controls.channel) + "\n"
           "form_bars=" + std::to_string(controls.formBars) + "\n"
           "phrase_bars=" + std::to_string(controls.phraseBars) + "\n"
           "density=" + std::to_string(controls.density) + "\n"
           "motion=" + std::to_string(controls.motion) + "\n"
           "tension=" + std::to_string(controls.tension) + "\n"
           "cadence=" + std::to_string(controls.cadence) + "\n"
           "reg=" + std::to_string(controls.reg) + "\n"
           "register_arc=" + std::to_string(controls.registerArc) + "\n"
           "sequence=" + std::to_string(controls.sequence) + "\n"
           "vary=" + std::to_string(controls.vary) + "\n"
           "seed=" + std::to_string(controls.seed) + "\n"
           "action_new_form=" + std::to_string(controls.actionNewForm) + "\n"
           "action_new_phrase=" + std::to_string(controls.actionNewPhrase) + "\n"
           "action_mutate_cell=" + std::to_string(controls.actionMutateCell) + "\n";
}

std::string serializeFormState(const FormState& rawForm)
{
    const FormState form = sanitizeFormState(rawForm);
    std::string text;
    text.reserve(16384);
    text += "version=" + std::to_string(form.version) + "\n";
    text += "form_bars=" + std::to_string(form.formBars) + "\n";
    text += "phrase_bars=" + std::to_string(form.phraseBars) + "\n";
    text += "phrase_count=" + std::to_string(form.phraseCount) + "\n";
    text += "pattern_steps=" + std::to_string(form.patternSteps) + "\n";
    text += "steps_per_beat=" + std::to_string(form.stepsPerBeat) + "\n";
    text += "event_count=" + std::to_string(form.eventCount) + "\n";
    text += "generation_serial=" + std::to_string(form.generationSerial) + "\n";

    for (int i = 0; i < form.phraseCount; ++i) {
        const PhrasePlan& phrase = form.phrases[static_cast<std::size_t>(i)];
        const std::string prefix = "phrase" + std::to_string(i) + ".";
        text += prefix + "role=" + std::to_string(static_cast<int>(phrase.role)) + "\n";
        text += prefix + "start_bar=" + std::to_string(phrase.startBar) + "\n";
        text += prefix + "bars=" + std::to_string(phrase.bars) + "\n";
        text += prefix + "start_step=" + std::to_string(phrase.startStep) + "\n";
        text += prefix + "step_count=" + std::to_string(phrase.stepCount) + "\n";
        text += prefix + "event_start=" + std::to_string(phrase.eventStartIndex) + "\n";
        text += prefix + "event_count=" + std::to_string(phrase.eventCount) + "\n";
        text += prefix + "root_degree=" + std::to_string(phrase.rootDegree) + "\n";
        text += prefix + "register_offset=" + std::to_string(phrase.registerOffset) + "\n";
        text += prefix + "intensity=" + std::to_string(phrase.intensity) + "\n";
        text += prefix + "motion_bias=" + std::to_string(phrase.motionBias) + "\n";
    }

    for (int i = 0; i < form.eventCount; ++i) {
        const NoteEvent& event = form.events[static_cast<std::size_t>(i)];
        text += "event" + std::to_string(i) + "=" +
                std::to_string(event.startStep) + "," +
                std::to_string(event.durationSteps) + "," +
                std::to_string(event.note) + "," +
                std::to_string(event.velocity) + "\n";
    }

    return text;
}

std::string serializeVariationState(const VariationState& rawVariation)
{
    const VariationState variation = sanitizeVariationState(rawVariation);
    return "version=" + std::to_string(variation.version) + "\n"
           "completed_form_loops=" + std::to_string(variation.completedFormLoops) + "\n"
           "last_mutation_loop=" + std::to_string(variation.lastMutationLoop) + "\n";
}

std::optional<Controls> deserializeControls(const std::string& text)
{
    Controls controls;

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) {
            continue;
        }

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        int intValue = 0;
        unsigned int uintValue = 0;
        float floatValue = 0.0f;

        if (key == "version") {
            continue;
        } else if (key == "root_note" && parseInteger(value, intValue)) {
            controls.rootNote = intValue;
        } else if (key == "scale" && parseInteger(value, intValue)) {
            controls.scale = static_cast<ScaleId>(intValue);
        } else if (key == "style" && parseInteger(value, intValue)) {
            controls.style = static_cast<StyleId>(intValue);
        } else if (key == "channel" && parseInteger(value, intValue)) {
            controls.channel = intValue;
        } else if (key == "form_bars" && parseInteger(value, intValue)) {
            controls.formBars = intValue;
        } else if (key == "phrase_bars" && parseInteger(value, intValue)) {
            controls.phraseBars = intValue;
        } else if (key == "density" && parseFloat(value, floatValue)) {
            controls.density = floatValue;
        } else if (key == "motion" && parseFloat(value, floatValue)) {
            controls.motion = floatValue;
        } else if (key == "tension" && parseFloat(value, floatValue)) {
            controls.tension = floatValue;
        } else if (key == "cadence" && parseFloat(value, floatValue)) {
            controls.cadence = floatValue;
        } else if (key == "reg" && parseInteger(value, intValue)) {
            controls.reg = intValue;
        } else if (key == "register_arc" && parseFloat(value, floatValue)) {
            controls.registerArc = floatValue;
        } else if (key == "sequence" && parseFloat(value, floatValue)) {
            controls.sequence = floatValue;
        } else if (key == "vary" && parseFloat(value, floatValue)) {
            controls.vary = floatValue;
        } else if (key == "seed" && parseInteger(value, uintValue)) {
            controls.seed = uintValue;
        } else if (key == "action_new_form" && parseInteger(value, intValue)) {
            controls.actionNewForm = intValue;
        } else if (key == "action_new_phrase" && parseInteger(value, intValue)) {
            controls.actionNewPhrase = intValue;
        } else if (key == "action_mutate_cell" && parseInteger(value, intValue)) {
            controls.actionMutateCell = intValue;
        } else {
            return std::nullopt;
        }
    }

    return clampControls(controls);
}

std::optional<FormState> deserializeFormState(const std::string& text)
{
    FormState form;

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) {
            continue;
        }

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        int intValue = 0;
        float floatValue = 0.0f;

        if (key == "version") {
            if (!parseInteger(value, intValue)) {
                return std::nullopt;
            }
            form.version = intValue;
            continue;
        }

        if (key == "form_bars" && parseInteger(value, intValue)) {
            form.formBars = intValue;
            continue;
        }
        if (key == "phrase_bars" && parseInteger(value, intValue)) {
            form.phraseBars = intValue;
            continue;
        }
        if (key == "phrase_count" && parseInteger(value, intValue)) {
            form.phraseCount = intValue;
            continue;
        }
        if (key == "pattern_steps" && parseInteger(value, intValue)) {
            form.patternSteps = intValue;
            continue;
        }
        if (key == "steps_per_beat" && parseInteger(value, intValue)) {
            form.stepsPerBeat = intValue;
            continue;
        }
        if (key == "event_count" && parseInteger(value, intValue)) {
            form.eventCount = intValue;
            continue;
        }
        if (key == "generation_serial" && parseInteger(value, intValue)) {
            form.generationSerial = intValue;
            continue;
        }

        if (key.rfind("phrase", 0) == 0) {
            const std::size_t dot = key.find('.');
            if (dot == std::string_view::npos) {
                return std::nullopt;
            }

            int phraseIndex = 0;
            if (!parseInteger(key.substr(6, dot - 6), phraseIndex) ||
                phraseIndex < 0 || phraseIndex >= kMaxPhraseCount) {
                return std::nullopt;
            }

            PhrasePlan& phrase = form.phrases[static_cast<std::size_t>(phraseIndex)];
            const std::string_view field = key.substr(dot + 1);

            if (field == "role" && parseInteger(value, intValue)) {
                phrase.role = static_cast<PhraseRoleId>(intValue);
            } else if (field == "start_bar" && parseInteger(value, intValue)) {
                phrase.startBar = intValue;
            } else if (field == "bars" && parseInteger(value, intValue)) {
                phrase.bars = intValue;
            } else if (field == "start_step" && parseInteger(value, intValue)) {
                phrase.startStep = intValue;
            } else if (field == "step_count" && parseInteger(value, intValue)) {
                phrase.stepCount = intValue;
            } else if (field == "event_start" && parseInteger(value, intValue)) {
                phrase.eventStartIndex = intValue;
            } else if (field == "event_count" && parseInteger(value, intValue)) {
                phrase.eventCount = intValue;
            } else if (field == "root_degree" && parseInteger(value, intValue)) {
                phrase.rootDegree = intValue;
            } else if (field == "register_offset" && parseInteger(value, intValue)) {
                phrase.registerOffset = intValue;
            } else if (field == "intensity" && parseFloat(value, floatValue)) {
                phrase.intensity = floatValue;
            } else if (field == "motion_bias" && parseFloat(value, floatValue)) {
                phrase.motionBias = floatValue;
            } else {
                return std::nullopt;
            }
            continue;
        }

        if (key.rfind("event", 0) == 0) {
            int eventIndex = 0;
            if (!parseInteger(key.substr(5), eventIndex) ||
                eventIndex < 0 || eventIndex >= kMaxEvents) {
                return std::nullopt;
            }

            const auto fields = split(value, ',');
            if (fields.size() != 4) {
                return std::nullopt;
            }

            NoteEvent& event = form.events[static_cast<std::size_t>(eventIndex)];
            if (!parseInteger(fields[0], event.startStep) ||
                !parseInteger(fields[1], event.durationSteps) ||
                !parseInteger(fields[2], event.note) ||
                !parseInteger(fields[3], event.velocity)) {
                return std::nullopt;
            }
            continue;
        }

        return std::nullopt;
    }

    bool valid = false;
    FormState sanitized = sanitizeFormState(form, &valid);
    if (!valid) {
        return std::nullopt;
    }
    return sanitized;
}

std::optional<VariationState> deserializeVariationState(const std::string& text)
{
    VariationState variation;

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) {
            continue;
        }

        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);

        if (key == "version") {
            if (!parseInteger(value, variation.version)) {
                return std::nullopt;
            }
        } else if (key == "completed_form_loops") {
            if (!parseInteger(value, variation.completedFormLoops)) {
                return std::nullopt;
            }
        } else if (key == "last_mutation_loop") {
            if (!parseInteger(value, variation.lastMutationLoop)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }

    return sanitizeVariationState(variation);
}

}  // namespace downspout::ground
