#include "ground_ai_state.hpp"

#include <algorithm>
#include <sstream>

namespace downspout::ground {
namespace {

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

void appendQuoted(std::ostringstream& out, const char* value)
{
    out << '"';
    while (*value != '\0') {
        if (*value == '"' || *value == '\\')
            out << '\\';
        out << *value++;
    }
    out << '"';
}

}  // namespace

const char* scaleName(const ScaleId scale) noexcept
{
    switch (scale) {
    case ScaleId::minor: return "minor";
    case ScaleId::major: return "major";
    case ScaleId::dorian: return "dorian";
    case ScaleId::phrygian: return "phrygian";
    case ScaleId::pentMinor: return "pentatonic_minor";
    case ScaleId::blues: return "blues";
    case ScaleId::mixolydian: return "mixolydian";
    case ScaleId::harmonicMinor: return "harmonic_minor";
    case ScaleId::pentMajor: return "pentatonic_major";
    case ScaleId::locrian: return "locrian";
    case ScaleId::phrygianDominant: return "phrygian_dominant";
    case ScaleId::lydian: return "lydian";
    case ScaleId::melodicMinor: return "melodic_minor";
    case ScaleId::wholeTone: return "whole_tone";
    case ScaleId::altered: return "altered";
    case ScaleId::halfWholeDiminished: return "half_whole_diminished";
    case ScaleId::wholeHalfDiminished: return "whole_half_diminished";
    case ScaleId::bebopDominant: return "bebop_dominant";
    case ScaleId::bebopMajor: return "bebop_major";
    case ScaleId::bebopMinor: return "bebop_minor";
    case ScaleId::count: break;
    }
    return "minor";
}

const char* styleName(const StyleId style) noexcept
{
    switch (style) {
    case StyleId::grounded: return "grounded";
    case StyleId::ostinato: return "ostinato";
    case StyleId::march: return "march";
    case StyleId::pulse: return "pulse";
    case StyleId::drone: return "drone";
    case StyleId::climb: return "climb";
    case StyleId::dub: return "dub";
    case StyleId::jazz: return "jazz";
    case StyleId::rock: return "rock";
    case StyleId::count: break;
    }
    return "grounded";
}

const char* formShapeName(const FormShapeId shape) noexcept
{
    switch (shape) {
    case FormShapeId::free: return "free";
    case FormShapeId::blues12: return "12_bar_blues";
    case FormShapeId::blues12QuickChange: return "12_bar_blues_quick_change";
    case FormShapeId::blues12Minor: return "12_bar_minor_blues";
    case FormShapeId::blues12Jazz: return "12_bar_jazz_blues";
    case FormShapeId::classical16: return "16_bar_classical";
    case FormShapeId::fugue16: return "16_bar_fugue";
    case FormShapeId::jazzAaba32: return "32_bar_jazz_aaba";
    case FormShapeId::rhythmChanges32: return "32_bar_rhythm_changes";
    case FormShapeId::techno16: return "16_bar_techno";
    case FormShapeId::dub16: return "16_bar_dub";
    case FormShapeId::ambient8: return "8_bar_ambient";
    case FormShapeId::rondo32: return "32_bar_rondo";
    case FormShapeId::count: break;
    }
    return "free";
}

const char* phraseRoleName(const PhraseRoleId role) noexcept
{
    switch (role) {
    case PhraseRoleId::statement: return "statement";
    case PhraseRoleId::answer: return "answer";
    case PhraseRoleId::climb: return "climb";
    case PhraseRoleId::pedal: return "pedal";
    case PhraseRoleId::breakdown: return "breakdown";
    case PhraseRoleId::cadence: return "cadence";
    case PhraseRoleId::release: return "release";
    case PhraseRoleId::count: break;
    }
    return "statement";
}

std::string summarizeGroundAiState(const Controls& rawControls,
                                   const FormState& rawForm,
                                   const int rawCurrentPhraseIndex)
{
    const Controls controls = clampControls(rawControls);
    bool validForm = false;
    const FormState form = sanitizeFormState(rawForm, &validForm);
    const int phraseCount = validForm ? form.phraseCount : 0;
    const int currentPhraseIndex = phraseCount > 0
        ? clampi(rawCurrentPhraseIndex, 0, phraseCount - 1)
        : -1;
    const PhrasePlan* currentPhrase = currentPhraseIndex >= 0
        ? &form.phrases[static_cast<std::size_t>(currentPhraseIndex)]
        : nullptr;

    std::ostringstream out;
    out << "{";
    out << "\"plugin\":\"ground\",";
    out << "\"key\":" << ((controls.rootNote % 12 + 12) % 12) << ',';
    out << "\"root_note\":" << controls.rootNote << ',';
    out << "\"scale\":";
    appendQuoted(out, scaleName(controls.scale));
    out << ',';
    out << "\"style\":";
    appendQuoted(out, styleName(controls.style));
    out << ',';
    out << "\"form_shape\":";
    appendQuoted(out, formShapeName(controls.formShape));
    out << ',';
    out << "\"form_bars\":" << controls.formBars << ',';
    out << "\"phrase_bars\":" << controls.phraseBars << ',';
    out << "\"density\":" << controls.density << ',';
    out << "\"motion\":" << controls.motion << ',';
    out << "\"tension\":" << controls.tension << ',';
    out << "\"color\":" << controls.color << ',';
    out << "\"cadence\":" << controls.cadence << ',';
    out << "\"meter\":{\"numerator\":" << form.meter.numerator
        << ",\"denominator\":" << form.meter.denominator << "},";
    out << "\"form_ready\":" << (validForm ? "true" : "false") << ',';
    out << "\"phrase_count\":" << phraseCount << ',';
    out << "\"current_phrase_index\":" << currentPhraseIndex << ',';
    out << "\"current_role\":";
    appendQuoted(out, currentPhrase != nullptr ? phraseRoleName(currentPhrase->role) : "unknown");
    out << ',';
    out << "\"current_root_degree\":" << (currentPhrase != nullptr ? currentPhrase->rootDegree : 0) << ',';
    out << "\"current_start_bar\":" << (currentPhrase != nullptr ? currentPhrase->startBar : 0) << ',';
    out << "\"current_bars\":" << (currentPhrase != nullptr ? currentPhrase->bars : 0) << ',';
    out << "\"guide_events\":[";

    int written = 0;
    if (currentPhrase != nullptr) {
        const int eventLimit = std::min(currentPhrase->eventCount, 16);
        for (int i = 0; i < eventLimit; ++i) {
            const NoteEvent& event = form.events[static_cast<std::size_t>(currentPhrase->eventStartIndex + i)];
            if (written > 0)
                out << ',';
            const float startBeat = form.stepsPerBeat > 0
                ? static_cast<float>(event.startStep - currentPhrase->startStep) / static_cast<float>(form.stepsPerBeat)
                : 0.0f;
            const float durationBeats = form.stepsPerBeat > 0
                ? static_cast<float>(event.durationSteps) / static_cast<float>(form.stepsPerBeat)
                : 0.0f;
            out << "{\"beat\":" << startBeat
                << ",\"duration\":" << durationBeats
                << ",\"note\":" << event.note
                << ",\"velocity\":" << event.velocity
                << '}';
            ++written;
        }
    }

    out << "]}";
    return out.str();
}

}  // namespace downspout::ground
