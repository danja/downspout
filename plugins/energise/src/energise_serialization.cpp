#include "energise_serialization.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string_view>

namespace downspout::energise {
namespace {

constexpr std::array<char, 64> kAlphabet = {{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
}};

[[nodiscard]] int base64Value(const char c) noexcept
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

void putU32(std::vector<std::uint8_t>& out, const std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}

void putI32(std::vector<std::uint8_t>& out, const std::int32_t v)
{
    putU32(out, static_cast<std::uint32_t>(v));
}

void putU8(std::vector<std::uint8_t>& out, const std::uint8_t v) { out.push_back(v); }

void putF32(std::vector<std::uint8_t>& out, const float v)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    putU32(out, bits);
}

void putF64(std::vector<std::uint8_t>& out, const double v)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
}

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t position = 0;

    [[nodiscard]] bool has(const std::size_t count) const noexcept
    {
        return position + count <= bytes.size();
    }

    [[nodiscard]] std::uint32_t u32() noexcept
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4 && has(1); ++i) {
            v |= static_cast<std::uint32_t>(bytes[position++]) << (8 * i);
        }
        return v;
    }

    [[nodiscard]] std::int32_t i32() noexcept { return static_cast<std::int32_t>(u32()); }

    [[nodiscard]] std::uint8_t u8() noexcept { return has(1) ? bytes[position++] : 0u; }

    [[nodiscard]] float f32() noexcept
    {
        const std::uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    [[nodiscard]] double f64() noexcept
    {
        std::uint64_t bits = 0;
        for (int i = 0; i < 8 && has(1); ++i) {
            bits |= static_cast<std::uint64_t>(bytes[position++]) << (8 * i);
        }
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

[[nodiscard]] std::vector<std::uint8_t> eventBytes(const DrumEvent& event)
{
    std::vector<std::uint8_t> out {};
    out.reserve(4 + 24 + 1 + 52 + 8 + 4);
    putU32(out, event.id);
    putF64(out, event.sourceTime);
    putF64(out, event.quantisedTime);
    putF64(out, event.sourceDuration);
    putU8(out, static_cast<std::uint8_t>(event.primary));
    for (const float score : event.scores)
        putF32(out, score);
    putF32(out, event.velocity);
    putF32(out, event.confidence);
    putU8(out, event.ghost ? 1u : 0u);
    putU8(out, event.muted ? 1u : 0u);
    putF32(out, event.pan);
    return out;
}

[[nodiscard]] bool readEvent(const std::vector<std::uint8_t>& bytes, DrumEvent& event)
{
    Reader reader {bytes, 0};
    event.id = reader.u32();
    event.sourceTime = reader.f64();
    event.quantisedTime = reader.f64();
    event.sourceDuration = reader.f64();
    event.primary = classFromIndex(reader.u8());
    for (float& score : event.scores)
        score = reader.f32();
    event.velocity = reader.f32();
    event.confidence = reader.f32();
    event.ghost = reader.u8() != 0u;
    event.muted = reader.u8() != 0u;
    event.pan = reader.f32();
    return reader.position == bytes.size() && std::isfinite(event.sourceTime) && std::isfinite(event.quantisedTime);
}

[[nodiscard]] std::vector<std::uint8_t> grooveBytes(const GrooveMap& groove)
{
    std::vector<std::uint8_t> out {};
    putF64(out, groove.bpm);
    putF64(out, groove.phaseSec);
    putI32(out, groove.gridDivisionsPerBeat);
    putF32(out, groove.swing);
    putU32(out, static_cast<std::uint32_t>(std::min<std::size_t>(groove.drift.size(), 64u)));
    for (std::size_t i = 0; i < groove.drift.size() && i < 64; ++i) {
        putF64(out, groove.drift[i].startSec);
        putF64(out, groove.drift[i].bpm);
    }
    putF32(out, groove.confidence);
    return out;
}

[[nodiscard]] bool readGroove(const std::vector<std::uint8_t>& bytes, GrooveMap& groove)
{
    Reader reader {bytes, 0};
    groove.bpm = std::clamp(reader.f64(), 1.0, 400.0);
    groove.phaseSec = reader.f64();
    groove.gridDivisionsPerBeat = std::clamp(reader.i32(), 1, 32);
    groove.swing = reader.f32();
    const std::uint32_t segments = std::min(reader.u32(), 64u);
    groove.drift.clear();
    for (std::uint32_t i = 0; i < segments; ++i) {
        if (!reader.has(16))
            return false;
        groove.drift.push_back({reader.f64(), reader.f64()});
    }
    groove.confidence = reader.f32();
    return reader.position == bytes.size() && std::isfinite(groove.bpm) && std::isfinite(groove.phaseSec);
}

[[nodiscard]] std::vector<std::uint8_t> settingsBytes(const RenderSettings& settings)
{
    std::vector<std::uint8_t> out {};
    const std::array<float, 24> values = {{
        settings.quantiseStrength, settings.ghostStrength, settings.swing, settings.humanise,
        settings.driftAllowance, settings.transientLevel, settings.bodyLevel, settings.residualLevel,
        settings.saturation, settings.variation, settings.outputTrim, 0.0f,
    }};
    for (std::size_t i = 0; i < 12; ++i)
        putF32(out, values[i]);
    for (const float offset : settings.classOffsetsMs)
        putF32(out, offset);
    for (const float gain : settings.classGains)
        putF32(out, gain);
    return out;
}

[[nodiscard]] bool readSettings(const std::vector<std::uint8_t>& bytes, RenderSettings& settings)
{
    Reader reader {bytes, 0};
    const std::array<float*, 11> fields = {{
        &settings.quantiseStrength, &settings.ghostStrength, &settings.swing, &settings.humanise,
        &settings.driftAllowance, &settings.transientLevel, &settings.bodyLevel, &settings.residualLevel,
        &settings.saturation, &settings.variation, &settings.outputTrim,
    }};
    for (float* field : fields)
        *field = reader.f32();
    (void)reader.f32();  // reserved
    for (float& offset : settings.classOffsetsMs)
        offset = reader.f32();
    for (float& gain : settings.classGains)
        gain = reader.f32();
    return reader.position == bytes.size();
}

[[nodiscard]] std::vector<std::string> floatChunks(const std::vector<float>& values)
{
    std::vector<std::string> chunks {};
    const std::size_t perChunk = kBase64ChunkBytes / sizeof(float);
    for (std::size_t offset = 0; offset < values.size(); offset += perChunk) {
        const std::size_t count = std::min(perChunk, values.size() - offset);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(values.data() + offset);
        chunks.push_back(base64Encode(bytes, count * sizeof(float)));
    }
    return chunks;
}

[[nodiscard]] bool readFloatChunks(const std::vector<std::string>& chunks, std::vector<float>& out)
{
    std::vector<std::uint8_t> bytes {};
    for (const std::string& chunk : chunks) {
        const std::size_t before = bytes.size();
        if (!base64Decode(chunk, bytes) || bytes.size() - before > kBase64ChunkBytes)
            return false;
    }
    if (bytes.size() % sizeof(float) != 0u)
        return false;
    out.clear();
    out.reserve(bytes.size() / sizeof(float));
    for (std::size_t i = 0; i < bytes.size(); i += sizeof(float)) {
        std::uint32_t bits = 0;
        for (int b = 0; b < 4; ++b)
            bits |= static_cast<std::uint32_t>(bytes[i + static_cast<std::size_t>(b)]) << (8 * b);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        if (!std::isfinite(value))
            return false;
        out.push_back(value);
    }
    return true;
}

[[nodiscard]] int parseInt(const std::string_view text, const int fallback)
{
    int value = fallback;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} ? value : fallback;
}

}  // namespace

std::string base64Encode(const std::uint8_t* bytes, const std::size_t size)
{
    std::string out {};
    out.reserve((size + 2u) / 3u * 4u);
    std::size_t i = 0;
    while (i + 3 <= size) {
        const std::uint32_t word = (static_cast<std::uint32_t>(bytes[i]) << 16)
                                 | (static_cast<std::uint32_t>(bytes[i + 1]) << 8)
                                 | static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(word >> 18) & 63u]);
        out.push_back(kAlphabet[(word >> 12) & 63u]);
        out.push_back(kAlphabet[(word >> 6) & 63u]);
        out.push_back(kAlphabet[word & 63u]);
        i += 3;
    }
    if (size - i == 1) {
        const std::uint32_t word = static_cast<std::uint32_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(word >> 18) & 63u]);
        out.push_back(kAlphabet[(word >> 12) & 63u]);
        out += "==";
    } else if (size - i == 2) {
        const std::uint32_t word = (static_cast<std::uint32_t>(bytes[i]) << 16)
                                 | (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(word >> 18) & 63u]);
        out.push_back(kAlphabet[(word >> 12) & 63u]);
        out.push_back(kAlphabet[(word >> 6) & 63u]);
        out.push_back('=');
    }
    return out;
}

bool base64Decode(const std::string& text, std::vector<std::uint8_t>& bytes)
{
    std::uint32_t word = 0;
    int bits = 0;
    for (const char c : text) {
        const int value = base64Value(c);
        if (value < 0)
            continue;
        word = (word << 6u) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>((word >> bits) & 0xFFu));
        }
    }
    return true;
}

std::string serializeState(const StateData& data)
{
    std::string out {};
    out += "version=" + std::to_string(kStateVersion) + "\n";
    out += "state=" + std::to_string(static_cast<int>(data.state)) + "\n";
    out += "sample_rate=" + std::to_string(data.capture.sampleRate) + "\n";
    out += "selected=" + std::to_string(data.selected) + "\n";
    out += "event_page=" + std::to_string(data.eventPage) + "\n";
    out += "ab_side=" + std::to_string(data.showingRender ? 1 : 0) + "\n";
    out += "render_rate=" + std::to_string(data.rendered.sampleRate) + "\n";

    for (const std::string& chunk : floatChunks(data.capture.left))
        out += "capture_l=" + chunk + "\n";
    for (const std::string& chunk : floatChunks(data.capture.right))
        out += "capture_r=" + chunk + "\n";
    for (const std::string& chunk : floatChunks(data.rendered.left))
        out += "render_l=" + chunk + "\n";
    for (const std::string& chunk : floatChunks(data.rendered.right))
        out += "render_r=" + chunk + "\n";

    out += "event_count=" + std::to_string(data.events.size()) + "\n";
    for (const DrumEvent& event : data.events)
        out += "event=" + base64Encode(eventBytes(event).data(), eventBytes(event).size()) + "\n";
    out += "groove=" + base64Encode(grooveBytes(data.groove).data(), grooveBytes(data.groove).size()) + "\n";
    out += "settings=" + base64Encode(settingsBytes(data.settings).data(), settingsBytes(data.settings).size()) + "\n";
    return out;
}

std::optional<StateData> deserializeState(const std::string& text)
{
    StateData data {};
    bool sawVersion = false;
    std::size_t declaredEvents = 0;
    std::vector<std::vector<std::uint8_t>> eventBlobs {};
    std::vector<std::string> captureL {}, captureR {}, renderL {}, renderR {};
    std::vector<std::uint8_t> grooveBytesIn {}, settingsBytesIn {};

    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('\n', begin);
        const std::string_view line(text.data() + begin, end == std::string::npos ? text.size() - begin : end - begin);
        const std::size_t equals = line.find('=');
        if (equals != std::string_view::npos) {
            const std::string_view key = line.substr(0, equals);
            const std::string_view value = line.substr(equals + 1);
            if (key == "version") {
                sawVersion = true;
                if (parseInt(value, -1) != kStateVersion)
                    return std::nullopt;
            } else if (key == "state") {
                const int state = parseInt(value, -1);
                if (state < 0 || state > 5)
                    return std::nullopt;
                data.state = static_cast<ProcessorState>(state);
            } else if (key == "sample_rate") {
                data.capture.sampleRate = parseInt(value, 0);
            } else if (key == "render_rate") {
                data.rendered.sampleRate = parseInt(value, 0);
            } else if (key == "selected") {
                data.selected = parseInt(value, -1);
            } else if (key == "event_page") {
                data.eventPage = parseInt(value, 0);
            } else if (key == "ab_side") {
                data.showingRender = parseInt(value, 0) != 0;
            } else if (key == "event_count") {
                declaredEvents = static_cast<std::size_t>(std::max(0, parseInt(value, 0)));
            } else if (key == "capture_l") {
                captureL.emplace_back(value);
            } else if (key == "capture_r") {
                captureR.emplace_back(value);
            } else if (key == "render_l") {
                renderL.emplace_back(value);
            } else if (key == "render_r") {
                renderR.emplace_back(value);
            } else if (key == "event") {
                std::vector<std::uint8_t> blob {};
                if (!base64Decode(std::string(value), blob))
                    return std::nullopt;
                eventBlobs.push_back(std::move(blob));
            } else if (key == "groove") {
                if (!base64Decode(std::string(value), grooveBytesIn))
                    return std::nullopt;
            } else if (key == "settings") {
                if (!base64Decode(std::string(value), settingsBytesIn))
                    return std::nullopt;
            }
            // Unknown keys are ignored so a newer host can open this project.
        }
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }

    if (!sawVersion || declaredEvents != eventBlobs.size() || declaredEvents > kMaxEvents)
        return std::nullopt;
    if (!readFloatChunks(captureL, data.capture.left) || !readFloatChunks(captureR, data.capture.right))
        return std::nullopt;
    if (data.capture.left.size() != data.capture.right.size() || data.capture.left.empty())
        return std::nullopt;
    if (!readFloatChunks(renderL, data.rendered.left) || !readFloatChunks(renderR, data.rendered.right))
        return std::nullopt;
    if (data.rendered.left.size() != data.rendered.right.size())
        return std::nullopt;
    if (data.rendered.sampleRate <= 0)
        data.rendered.sampleRate = data.capture.sampleRate;
    if (!grooveBytesIn.empty() && !readGroove(grooveBytesIn, data.groove))
        return std::nullopt;
    if (!settingsBytesIn.empty() && !readSettings(settingsBytesIn, data.settings))
        return std::nullopt;
    for (const auto& blob : eventBlobs) {
        DrumEvent event {};
        if (!readEvent(blob, event))
            return std::nullopt;
        data.events.push_back(event);
    }
    return data;
}

}  // namespace downspout::energise
