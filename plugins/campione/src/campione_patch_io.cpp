#include "campione_patch_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace downspout::campione {

namespace {

// Escape backslash and double-quote for Turtle string literals.
static std::string escapeStr(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\') { out += "\\\\"; }
        else if (c == '"')  { out += "\\\""; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else                { out += c; }
    }
    return out;
}

static std::string unescapeStr(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            switch (s[i]) {
            case '\\': out += '\\'; break;
            case '"':  out += '"';  break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            default:   out += '\\'; out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

std::string savePatch(const PatchData& patch, const std::string& filePath)
{
    FILE* f = std::fopen(filePath.c_str(), "w");
    if (!f) return "cannot open for writing: " + filePath;

    std::fprintf(f, "@prefix dsp:  <http://purl.org/stuff/transmissions/plugins/downspout/> .\n");
    std::fprintf(f, "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n");
    std::fprintf(f, "@prefix xsd:  <http://www.w3.org/2001/XMLSchema#> .\n\n");

    const std::string label = patch.label.empty() ? "Campione Patch" : patch.label;

    std::fprintf(f, "<> a dsp:CampionePatch ;\n");
    std::fprintf(f, "    rdfs:label \"%s\" ;\n", escapeStr(label).c_str());
    std::fprintf(f, "    dsp:masterVolume \"%.6g\"^^xsd:float ;\n",   patch.parameters.masterVolume);
    std::fprintf(f, "    dsp:midiChannel \"%.6g\"^^xsd:float ;\n",    patch.parameters.midiChannel);
    std::fprintf(f, "    dsp:crossfadeMs \"%.6g\"^^xsd:float ;\n",    patch.parameters.crossfadeDurationMs);
    std::fprintf(f, "    dsp:pitchBendRange \"%.6g\"^^xsd:float",     patch.parameters.pitchBendRange);

    if (patch.zones.empty()) {
        std::fprintf(f, " .\n\n");
    } else {
        std::fprintf(f, " ;\n");
        for (std::size_t i = 0; i < patch.zones.size(); ++i) {
            const bool last = (i == patch.zones.size() - 1);
            std::fprintf(f, "    dsp:zone <#zone%zu>%s\n", i, last ? " ." : " ;");
        }
        std::fprintf(f, "\n");
    }

    for (std::size_t i = 0; i < patch.zones.size(); ++i) {
        const SampleZone& z = patch.zones[i];
        std::fprintf(f, "<#zone%zu> a dsp:SamplerZone ;\n", i);
        std::fprintf(f, "    dsp:order \"%zu\"^^xsd:integer ;\n",         i);
        std::fprintf(f, "    dsp:sourcePath \"%s\" ;\n",                  escapeStr(z.sourcePath).c_str());
        std::fprintf(f, "    dsp:rootNote \"%d\"^^xsd:integer ;\n",       z.rootNote);
        std::fprintf(f, "    dsp:rangeLow \"%d\"^^xsd:integer ;\n",       z.rangeLow);
        std::fprintf(f, "    dsp:rangeHigh \"%d\"^^xsd:integer ;\n",      z.rangeHigh);
        std::fprintf(f, "    dsp:midiChannel \"%d\"^^xsd:integer ;\n",    z.midiChannel);
        std::fprintf(f, "    dsp:loopEnabled \"%s\"^^xsd:boolean ;\n",    z.loopEnabled ? "true" : "false");
        std::fprintf(f, "    dsp:loopStart \"%u\"^^xsd:integer ;\n",      z.loopStart);
        std::fprintf(f, "    dsp:loopEnd \"%u\"^^xsd:integer ;\n",        z.loopEnd);
        std::fprintf(f, "    dsp:attackMs \"%.6g\"^^xsd:float ;\n",       z.attackMs);
        std::fprintf(f, "    dsp:decayMs \"%.6g\"^^xsd:float ;\n",        z.decayMs);
        std::fprintf(f, "    dsp:sustainLevel \"%.6g\"^^xsd:float ;\n",   z.sustainLevel);
        std::fprintf(f, "    dsp:releaseMs \"%.6g\"^^xsd:float ;\n",      z.releaseMs);
        std::fprintf(f, "    dsp:filterEnabled \"%s\"^^xsd:boolean ;\n",  z.filterEnabled ? "true" : "false");
        std::fprintf(f, "    dsp:filterType \"%d\"^^xsd:integer ;\n",     z.filterType);
        std::fprintf(f, "    dsp:filterCutoff \"%.6g\"^^xsd:float ;\n",   z.filterCutoffHz);
        std::fprintf(f, "    dsp:filterQ \"%.6g\"^^xsd:float ;\n",        z.filterQ);
        std::fprintf(f, "    dsp:pan \"%.6g\"^^xsd:float ;\n",             z.pan);
        std::fprintf(f, "    dsp:muted \"%d\"^^xsd:integer .\n\n",        z.muted ? 1 : 0);
    }

    std::fclose(f);
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────

std::pair<std::optional<PatchData>, std::string> loadPatch(const std::string& filePath)
{
    FILE* f = std::fopen(filePath.c_str(), "r");
    if (!f) return {{}, "cannot open: " + filePath};

    enum class Subject { None, Patch, Zone };
    Subject cur = Subject::None;
    int curZoneIdx = -1;

    PatchData patch;
    std::vector<SampleZone> zones;

    char lineBuf[4096];
    while (std::fgets(lineBuf, sizeof(lineBuf), f)) {
        std::string line = lineBuf;
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                 || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        // Detect subject declarations
        if (line.size() >= 3 && line[0] == '<' && line[1] == '>') {
            cur = Subject::Patch;
        } else if (line.size() >= 2 && line[0] == '<' && line[1] == '#') {
            const std::size_t end = line.find('>');
            if (end != std::string::npos) {
                const std::string frag = line.substr(2, end - 2);
                if (frag.size() > 4 && frag.substr(0, 4) == "zone") {
                    curZoneIdx = std::atoi(frag.c_str() + 4);
                    cur = Subject::Zone;
                    if (curZoneIdx >= static_cast<int>(zones.size()))
                        zones.resize(static_cast<std::size_t>(curZoneIdx + 1));
                }
            }
        }

        // Trim leading whitespace
        std::size_t pos = 0;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

        const bool isDsp  = (line.size() - pos >= 4 && line.substr(pos, 4) == "dsp:");
        const bool isRdfs = (line.size() - pos >= 5 && line.substr(pos, 5) == "rdfs:");
        if (!isDsp && !isRdfs) continue;

        // Extract predicate name
        const std::size_t prefixLen = isDsp ? 4u : 5u;
        std::size_t predStart = pos + prefixLen;
        std::size_t predEnd   = predStart;
        while (predEnd < line.size() && line[predEnd] != ' ' && line[predEnd] != '\t')
            ++predEnd;
        const std::string predicate = line.substr(predStart, predEnd - predStart);

        // Extract literal value between the first pair of double-quotes
        const std::size_t q1 = line.find('"', predEnd);
        if (q1 == std::string::npos) continue;
        const std::size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        const std::string value = unescapeStr(line.substr(q1 + 1, q2 - q1 - 1));

        if (cur == Subject::Patch) {
            if      (isRdfs && predicate == "label")    patch.label = value;
            else if (predicate == "masterVolume")        patch.parameters.masterVolume        = std::stof(value);
            else if (predicate == "midiChannel")         patch.parameters.midiChannel         = std::stof(value);
            else if (predicate == "crossfadeMs")         patch.parameters.crossfadeDurationMs = std::stof(value);
            else if (predicate == "pitchBendRange")      patch.parameters.pitchBendRange      = std::stof(value);
        } else if (cur == Subject::Zone && curZoneIdx >= 0) {
            SampleZone& z = zones[static_cast<std::size_t>(curZoneIdx)];
            if      (predicate == "sourcePath")   z.sourcePath     = value;
            else if (predicate == "rootNote")     z.rootNote       = std::stoi(value);
            else if (predicate == "rangeLow")     z.rangeLow       = std::stoi(value);
            else if (predicate == "rangeHigh")    z.rangeHigh      = std::stoi(value);
            else if (predicate == "midiChannel")  z.midiChannel    = std::stoi(value);
            else if (predicate == "loopEnabled")  z.loopEnabled    = (value == "true");
            else if (predicate == "loopStart")    z.loopStart      = static_cast<std::uint32_t>(std::stoul(value));
            else if (predicate == "loopEnd")      z.loopEnd        = static_cast<std::uint32_t>(std::stoul(value));
            else if (predicate == "attackMs")     z.attackMs       = std::stof(value);
            else if (predicate == "decayMs")      z.decayMs        = std::stof(value);
            else if (predicate == "sustainLevel") z.sustainLevel   = std::stof(value);
            else if (predicate == "releaseMs")    z.releaseMs      = std::stof(value);
            else if (predicate == "filterEnabled")z.filterEnabled  = (value == "true");
            else if (predicate == "filterType")   z.filterType     = std::stoi(value);
            else if (predicate == "filterCutoff") z.filterCutoffHz = std::stof(value);
            else if (predicate == "filterQ")      z.filterQ        = std::stof(value);
            else if (predicate == "pan")          z.pan            = std::clamp(std::stof(value), -1.0f, 1.0f);
            else if (predicate == "muted")        z.muted          = std::stoi(value) != 0;
        }
    }

    std::fclose(f);

    patch.zones = std::move(zones);
    return {std::move(patch), {}};
}

}  // namespace downspout::campione
