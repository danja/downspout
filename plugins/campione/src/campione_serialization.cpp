#include "campione_serialization.hpp"

#include "campione_engine.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::campione {
namespace {

bool parseFloat(std::string_view text, float& value) {
    std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end && *end == '\0';
}

bool parseInt(std::string_view text, int& value) {
    const char* begin = text.data();
    auto result = std::from_chars(begin, begin + text.size(), value);
    return result.ec == std::errc();
}

bool parseUint32(std::string_view text, std::uint32_t& value) {
    const char* begin = text.data();
    auto result = std::from_chars(begin, begin + text.size(), value);
    return result.ec == std::errc();
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find('\n', start);
        if (pos == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

// Split text by "\n---\n" delimiter; returns header block (index 0), then zone blocks.
std::vector<std::string> splitZoneBlocks(const std::string& text) {
    std::vector<std::string> blocks;
    const std::string delim = "\n---\n";
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = text.find(delim, start);
        if (pos == std::string::npos) {
            blocks.push_back(text.substr(start));
            break;
        }
        blocks.push_back(text.substr(start, pos - start));
        start = pos + delim.size();
    }
    return blocks;
}

}  // namespace

std::string serializeParameters(const Parameters& p) {
    return "version=1\n"
           "master_volume="        + std::to_string(p.masterVolume)       + "\n"
           "midi_channel="         + std::to_string(p.midiChannel)        + "\n"
           "crossfade_duration_ms=" + std::to_string(p.crossfadeDurationMs) + "\n"
           "pitch_bend_range="     + std::to_string(p.pitchBendRange)     + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text) {
    Parameters p;
    for (const std::string_view line : splitLines(text)) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view val = line.substr(sep + 1);
        float fv = 0.0f;
        if (key == "version") continue;
        else if (key == "master_volume"         && parseFloat(val, fv)) p.masterVolume       = fv;
        else if (key == "midi_channel"          && parseFloat(val, fv)) p.midiChannel        = fv;
        else if (key == "crossfade_duration_ms" && parseFloat(val, fv)) p.crossfadeDurationMs = fv;
        else if (key == "pitch_bend_range"      && parseFloat(val, fv)) p.pitchBendRange     = fv;
        // ignore unknown keys for forward compatibility
    }
    return clampParameters(p);
}

std::string serializeZones(const std::vector<SampleZone>& zones) {
    std::string out = "version=1\nzone_count=" + std::to_string(zones.size()) + "\n";
    for (const SampleZone& z : zones) {
        out += "---\n";
        out += "root_note="       + std::to_string(z.rootNote)       + "\n";
        out += "range_low="       + std::to_string(z.rangeLow)       + "\n";
        out += "range_high="      + std::to_string(z.rangeHigh)      + "\n";
        out += "midi_channel="    + std::to_string(z.midiChannel)    + "\n";
        out += "loop_enabled="    + std::to_string(z.loopEnabled ? 1 : 0) + "\n";
        out += "loop_start="      + std::to_string(z.loopStart)      + "\n";
        out += "loop_end="        + std::to_string(z.loopEnd)        + "\n";
        out += "crossfade_frames=" + std::to_string(z.crossfadeFrames) + "\n";
        out += "source_path="     + z.sourcePath                     + "\n";
        out += "attack_ms="      + std::to_string(z.attackMs)      + "\n";
        out += "decay_ms="       + std::to_string(z.decayMs)        + "\n";
        out += "sustain_level="  + std::to_string(z.sustainLevel)   + "\n";
        out += "release_ms="     + std::to_string(z.releaseMs)      + "\n";
        out += "filter_enabled=" + std::to_string(z.filterEnabled ? 1 : 0) + "\n";
        out += "filter_type="    + std::to_string(z.filterType)     + "\n";
        out += "filter_cutoff="  + std::to_string(z.filterCutoffHz) + "\n";
        out += "filter_q="       + std::to_string(z.filterQ)        + "\n";
        out += "pan="            + std::to_string(z.pan)            + "\n";
    }
    return out;
}

std::optional<std::vector<SampleZone>> deserializeZones(const std::string& text) {
    if (text.empty()) return std::vector<SampleZone>{};

    const auto blocks = splitZoneBlocks(text);
    if (blocks.empty()) return std::vector<SampleZone>{};

    // Validate header
    bool sawVersion = false;
    for (const std::string_view line : splitLines(blocks[0])) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        if (key == "version") { sawVersion = true; continue; }
        if (key == "zone_count") continue;
        // ignore unknown header keys for forward compatibility
    }
    if (!sawVersion) return std::nullopt;

    std::vector<SampleZone> zones;
    zones.reserve(blocks.size() - 1);

    for (std::size_t bi = 1; bi < blocks.size(); ++bi) {
        SampleZone z;
        for (const std::string_view line : splitLines(blocks[bi])) {
            if (line.empty()) continue;
            const std::size_t sep = line.find('=');
            if (sep == std::string_view::npos) return std::nullopt;
            const std::string_view key = line.substr(0, sep);
            const std::string_view val = line.substr(sep + 1);
            int iv = 0; std::uint32_t uv = 0; float fv = 0.0f;
            if      (key == "root_note"        && parseInt(val, iv))    z.rootNote        = iv;
            else if (key == "range_low"        && parseInt(val, iv))    z.rangeLow        = iv;
            else if (key == "range_high"       && parseInt(val, iv))    z.rangeHigh       = iv;
            else if (key == "midi_channel"     && parseInt(val, iv))    z.midiChannel     = iv;
            else if (key == "loop_enabled"     && parseInt(val, iv))    z.loopEnabled     = iv != 0;
            else if (key == "loop_start"       && parseUint32(val, uv)) z.loopStart       = uv;
            else if (key == "loop_end"         && parseUint32(val, uv)) z.loopEnd         = uv;
            else if (key == "crossfade_frames" && parseUint32(val, uv)) z.crossfadeFrames = uv;
            else if (key == "source_path")                              z.sourcePath      = std::string(val);
            else if (key == "attack_ms"      && parseFloat(val, fv)) z.attackMs      = fv;
            else if (key == "decay_ms"       && parseFloat(val, fv)) z.decayMs       = fv;
            else if (key == "sustain_level"  && parseFloat(val, fv)) z.sustainLevel  = fv;
            else if (key == "release_ms"     && parseFloat(val, fv)) z.releaseMs     = fv;
            else if (key == "filter_enabled" && parseInt(val, iv))   z.filterEnabled = iv != 0;
            else if (key == "filter_type"    && parseInt(val, iv))   z.filterType    = iv;
            else if (key == "filter_cutoff"  && parseFloat(val, fv)) z.filterCutoffHz = fv;
            else if (key == "filter_q"       && parseFloat(val, fv)) z.filterQ       = fv;
            else if (key == "pan"            && parseFloat(val, fv)) z.pan            = std::clamp(fv, -1.0f, 1.0f);
            // ignore unknown keys for forward compatibility
        }
        zones.push_back(std::move(z));
    }

    return zones;
}

}  // namespace downspout::campione
