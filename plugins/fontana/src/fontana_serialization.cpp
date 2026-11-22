#include "fontana_serialization.hpp"

#include "fontana_core_types.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace downspout::fontana {
namespace {

[[nodiscard]] bool parseInt(const std::string_view text, int& out)
{
    if (text.empty())
        return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc() && result.ptr == last;
}

[[nodiscard]] bool parseFloat(const std::string_view text, float& out)
{
    if (text.empty())
        return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc() && result.ptr == last;
}

[[nodiscard]] std::vector<std::string_view> splitLines(const std::string& text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (start < text.size())
                lines.emplace_back(text.data() + start, text.size() - start);
            break;
        }
        lines.emplace_back(text.data() + start, end - start);
        start = end + 1;
    }
    return lines;
}

[[nodiscard]] std::pair<std::string_view, std::string_view> splitAssignment(const std::string_view line)
{
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos)
        return {{}, {}};
    return {line.substr(0, equals), line.substr(equals + 1)};
}

}  // namespace

std::string serializeSliceSet(const SliceSet& slices)
{
    std::string out = "version=1\n";
    out += "path=" + slices.sourcePath + "\n";
    out += "rate=" + std::to_string(slices.sampleRate) + "\n";
    out += "frames=" + std::to_string(slices.frameCount) + "\n";
    out += "count=" + std::to_string(slices.slices.size()) + "\n";
    for (const Slice& slice : slices.slices) {
        out += std::to_string(slice.start) + ',' + std::to_string(slice.end) + ',' +
               std::to_string(slice.rootNote) + ',' + (slice.muted ? "1" : "0") + '\n';
    }
    return out;
}

std::optional<SliceSet> deserializeSliceSet(const std::string& text)
{
    const auto lines = splitLines(text);
    if (lines.empty() || lines[0] != "version=1")
        return std::nullopt;

    SliceSet result;
    int count = -1;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const auto [key, value] = splitAssignment(lines[i]);
        if (key == "path") {
            result.sourcePath.assign(value);
        } else if (key == "rate" || key == "frames" || key == "count") {
            int parsed = 0;
            if (!parseInt(value, parsed))
                return std::nullopt;
            if (key == "rate")
                result.sampleRate = static_cast<std::uint32_t>(std::max(0, parsed));
            else if (key == "frames")
                result.frameCount = static_cast<std::uint32_t>(std::max(0, parsed));
            else
                count = parsed;
        } else {
            // Slice rows have no key: start,end,root,muted.
            int start = 0, end = 0, root = 0, muted = 0;
            if (std::sscanf(std::string(lines[i]).c_str(), "%d,%d,%d,%d", &start, &end, &root, &muted) != 4)
                continue;
            if (result.slices.size() >= static_cast<std::size_t>(kMaxSlices))
                continue;
            result.slices.push_back(Slice {static_cast<std::uint32_t>(std::max(0, start)),
                                           static_cast<std::uint32_t>(std::max(0, end)),
                                           root, muted != 0});
        }
    }

    if (count < 0 || static_cast<int>(result.slices.size()) != count)
        return std::nullopt;
    return result;
}

std::string serializeSettings(const Parameters& parameters)
{
    return "version=1\n"
           "masterVolume=" + std::to_string(parameters.masterVolume) + "\n"
           "bank=" + std::to_string(parameters.bank) + "\n"
           "program=" + std::to_string(parameters.program) + "\n"
           "bendRange=" + std::to_string(parameters.pitchBendRange) + "\n"
           "transpose=" + std::to_string(parameters.transpose) + "\n"
           "sliceBaseNote=" + std::to_string(parameters.sliceBaseNote) + "\n"
           "pan=" + std::to_string(parameters.pan) + "\n";
}

std::optional<Parameters> deserializeSettings(const std::string& text)
{
    const auto lines = splitLines(text);
    if (lines.empty() || lines[0] != "version=1")
        return std::nullopt;

    Parameters result;
    bool sawAny = false;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const auto [key, value] = splitAssignment(lines[i]);
        float parsed = 0.0f;
        if (!parseFloat(value, parsed))
            continue;
        if (key == "masterVolume") {
            result.masterVolume = parsed;
            sawAny = true;
        } else if (key == "bank") {
            result.bank = parsed;
            sawAny = true;
        } else if (key == "program") {
            result.program = parsed;
            sawAny = true;
        } else if (key == "bendRange") {
            result.pitchBendRange = parsed;
            sawAny = true;
        } else if (key == "transpose") {
            result.transpose = parsed;
            sawAny = true;
        } else if (key == "sliceBaseNote") {
            result.sliceBaseNote = parsed;
            sawAny = true;
        } else if (key == "pan") {
            result.pan = parsed;
            sawAny = true;
        }
    }
    if (!sawAny)
        return std::nullopt;
    return clampParameters(result);
}

}  // namespace downspout::fontana
