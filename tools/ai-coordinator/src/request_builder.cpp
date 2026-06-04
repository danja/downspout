#include "ai_coordinator.hpp"

#include <sstream>

namespace downspout::ai_coordinator {
namespace {

void appendEscaped(std::ostringstream& out, const std::string& text)
{
    for (const char ch : text) {
        if (ch == '"' || ch == '\\')
            out << '\\';
        out << ch;
    }
}

void appendTuneStateObject(std::ostringstream& out, const TuneState& state)
{
    out << "{";
    out << "\"key\":" << state.key << ',';
    out << "\"scale\":\"";
    appendEscaped(out, state.scale);
    out << "\",";
    out << "\"genre\":\"";
    appendEscaped(out, state.genre);
    out << "\",";
    out << "\"tempo\":" << state.tempo << ',';
    out << "\"bars\":" << state.bars << ',';
    out << "\"beats_per_bar\":" << state.beatsPerBar << ',';
    out << "\"channel\":" << state.channel << ',';
    out << "\"register_low\":" << state.registerLow << ',';
    out << "\"register_high\":" << state.registerHigh << ',';
    out << "\"density\":" << state.density << ',';
    out << "\"risk\":" << state.risk << ',';
    out << "\"seed\":" << state.seed << ',';
    out << "\"midi_context\":" << (state.hasMidiContext ? "true" : "false") << ',';
    out << "\"guide_pitch_classes\":[";
    for (int i = 0; i < state.guidePitchClassCount; ++i) {
        if (i > 0)
            out << ',';
        out << state.guidePitchClasses[static_cast<std::size_t>(i)];
    }
    out << "]}";
}

}  // namespace

std::string buildSoloRequestJson(const TuneState& state)
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"protocol\": \"downspout.ai_solo.v1\",\n";
    out << "  \"task\": \"generate_midi_phrase\",\n";
    out << "  \"role\": \"Generate a short monophonic MIDI solo phrase for Downspout Sidecar.\",\n";
    out << "  \"instructions\": [\n";
    out << "    \"Return only one JSON object matching response_schema. No Markdown, no explanation, no code fence.\",\n";
    out << "    \"Use beat positions relative to the phrase start.\",\n";
    out << "    \"Keep events monophonic, sorted, and inside the phrase length.\",\n";
    out << "    \"Keep note values inside tune_state.register_low and tune_state.register_high.\",\n";
    out << "    \"Prefer guide pitch classes on strong beats when supplied.\",\n";
    out << "    \"Use a melodic contour with at least four distinct pitches when the register allows it.\",\n";
    out << "    \"Do not repeat the same MIDI note more than twice in a row.\",\n";
    out << "    \"Do not return a phrase consisting of one repeated note.\"\n";
    out << "  ],\n";
    out << "  \"quality_constraints\": {\n";
    out << "    \"minimum_distinct_notes_when_possible\": 4,\n";
    out << "    \"maximum_same_note_run\": 2,\n";
    out << "    \"must_have_melodic_contour\": true\n";
    out << "  },\n";
    out << "  \"tune_state\": ";
    appendTuneStateObject(out, state);
    out << ",\n";
    out << "  \"response_schema\": {\n";
    out << "    \"type\": \"object\",\n";
    out << "    \"required\": [\"version\", \"bars\", \"beats_per_bar\", \"events\"],\n";
    out << "    \"properties\": {\n";
    out << "      \"version\": {\"const\": 1},\n";
    out << "      \"bars\": {\"type\": \"integer\", \"minimum\": 1, \"maximum\": 8},\n";
    out << "      \"beats_per_bar\": {\"type\": \"integer\", \"minimum\": 1, \"maximum\": 12},\n";
    out << "      \"events\": {\n";
    out << "        \"type\": \"array\",\n";
    out << "        \"maxItems\": 256,\n";
    out << "        \"items\": {\n";
    out << "          \"type\": \"object\",\n";
    out << "          \"required\": [\"beat\", \"duration\", \"note\", \"velocity\"],\n";
    out << "          \"properties\": {\n";
    out << "            \"beat\": {\"type\": \"number\", \"minimum\": 0},\n";
    out << "            \"duration\": {\"type\": \"number\", \"exclusiveMinimum\": 0},\n";
    out << "            \"note\": {\"type\": \"integer\", \"minimum\": 0, \"maximum\": 127},\n";
    out << "            \"velocity\": {\"type\": \"integer\", \"minimum\": 1, \"maximum\": 127}\n";
    out << "          }\n";
    out << "        }\n";
    out << "      }\n";
    out << "    }\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

}  // namespace downspout::ai_coordinator
