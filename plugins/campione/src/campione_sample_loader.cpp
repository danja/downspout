#include "campione_sample_loader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace downspout::campione {
namespace {

struct WavFormat {
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
};

[[nodiscard]] std::uint16_t readU16(const std::array<unsigned char, 4>& b) {
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

[[nodiscard]] std::uint32_t readU32(const std::array<unsigned char, 4>& b) {
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

[[nodiscard]] bool readBytes(std::ifstream& f, char* buf, std::streamsize n) {
    f.read(buf, n);
    return f.good();
}

[[nodiscard]] bool readU32FromFile(std::ifstream& f, std::uint32_t& v) {
    std::array<unsigned char, 4> b {};
    if (!readBytes(f, reinterpret_cast<char*>(b.data()), 4)) return false;
    v = readU32(b);
    return true;
}

[[nodiscard]] bool parseFmtChunk(std::ifstream& f, std::uint32_t size, WavFormat& fmt) {
    if (size < 16u) return false;
    std::vector<unsigned char> b(size);
    if (!readBytes(f, reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()))) return false;

    const auto u16 = [&](std::size_t o) {
        std::array<unsigned char, 4> tmp {};
        tmp[0] = b[o]; tmp[1] = b[o + 1];
        return readU16(tmp);
    };
    const auto u32 = [&](std::size_t o) {
        std::array<unsigned char, 4> tmp {};
        tmp[0] = b[o]; tmp[1] = b[o + 1]; tmp[2] = b[o + 2]; tmp[3] = b[o + 3];
        return readU32(tmp);
    };

    fmt.audioFormat  = u16(0);
    fmt.channels     = u16(2);
    fmt.sampleRate   = u32(4);
    fmt.blockAlign   = u16(12);
    fmt.bitsPerSample = u16(14);
    return true;
}

[[nodiscard]] float readPcmSample(const unsigned char* d, std::uint16_t bits) {
    switch (bits) {
    case 8:  return (static_cast<float>(d[0]) - 128.0f) / 128.0f;
    case 16: {
        const auto v = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(d[0]) | (static_cast<std::uint16_t>(d[1]) << 8));
        return static_cast<float>(v) / 32768.0f;
    }
    case 24: {
        std::int32_t v = static_cast<std::int32_t>(d[0]) |
                         (static_cast<std::int32_t>(d[1]) << 8) |
                         (static_cast<std::int32_t>(d[2]) << 16);
        if ((v & 0x00800000) != 0) v |= ~0x00ffffff;
        return static_cast<float>(v) / 8388608.0f;
    }
    case 32: {
        const auto v = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(d[0]) | (static_cast<std::uint32_t>(d[1]) << 8) |
            (static_cast<std::uint32_t>(d[2]) << 16) | (static_cast<std::uint32_t>(d[3]) << 24));
        return static_cast<float>(static_cast<double>(v) / 2147483648.0);
    }
    default: return 0.0f;
    }
}

[[nodiscard]] float readFloatSample(const unsigned char* d) {
    std::uint32_t bits = static_cast<std::uint32_t>(d[0]) | (static_cast<std::uint32_t>(d[1]) << 8) |
                         (static_cast<std::uint32_t>(d[2]) << 16) | (static_cast<std::uint32_t>(d[3]) << 24);
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return std::clamp(v, -1.0f, 1.0f);
}

[[nodiscard]] bool isSupportedFormat(const WavFormat& f) {
    if (f.channels == 0u || f.channels > kMaxChannels || f.sampleRate == 0u || f.blockAlign == 0u)
        return false;
    if (f.audioFormat == 1u)
        return f.bitsPerSample == 8u || f.bitsPerSample == 16u ||
               f.bitsPerSample == 24u || f.bitsPerSample == 32u;
    return f.audioFormat == 3u && f.bitsPerSample == 32u;
}

}  // namespace

ZoneLoadResult loadWavZone(const std::string& path)
{
    ZoneLoadResult result;

    std::ifstream file(path, std::ios::binary);
    if (!file) { result.error = "Could not open file"; return result; }

    char riff[4] {}, wave[4] {};
    std::uint32_t riffSize = 0;
    if (!readBytes(file, riff, 4) || !readU32FromFile(file, riffSize) || !readBytes(file, wave, 4)) {
        result.error = "File too short to be a WAV";
        return result;
    }
    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0) {
        result.error = "Not a RIFF/WAVE file";
        return result;
    }

    WavFormat fmt;
    bool foundFmt = false;
    std::vector<unsigned char> dataBytes;

    while (file) {
        char chunkId[4] {};
        std::uint32_t chunkSize = 0;
        if (!readBytes(file, chunkId, 4) || !readU32FromFile(file, chunkSize)) break;

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            if (!parseFmtChunk(file, chunkSize, fmt)) { result.error = "Invalid fmt chunk"; return result; }
            foundFmt = true;
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            dataBytes.resize(chunkSize);
            if (!readBytes(file, reinterpret_cast<char*>(dataBytes.data()), static_cast<std::streamsize>(dataBytes.size()))) {
                result.error = "Invalid data chunk";
                return result;
            }
        } else {
            file.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
        }
        if ((chunkSize & 1u) != 0u) file.seekg(1, std::ios::cur);
    }

    if (!foundFmt || dataBytes.empty()) { result.error = "Missing fmt or data chunk"; return result; }
    if (!isSupportedFormat(fmt))         { result.error = "Unsupported WAV format";    return result; }

    const std::uint32_t bytesPerSample = fmt.bitsPerSample / 8u;
    const std::uint32_t expectedAlign  = bytesPerSample * fmt.channels;
    if (bytesPerSample == 0u || fmt.blockAlign < expectedAlign) {
        result.error = "Invalid WAV frame layout";
        return result;
    }

    const std::uint32_t frameCount = static_cast<std::uint32_t>(dataBytes.size() / fmt.blockAlign);
    if (frameCount == 0) { result.error = "No complete frames"; return result; }

    result.zone.channelCount = static_cast<int>(fmt.channels);
    result.zone.sampleRate   = static_cast<double>(fmt.sampleRate);
    result.zone.data.resize(static_cast<std::size_t>(frameCount) * fmt.channels);

    for (std::uint32_t fr = 0; fr < frameCount; ++fr) {
        const unsigned char* framePtr = dataBytes.data() + static_cast<std::size_t>(fr) * fmt.blockAlign;
        for (std::uint32_t ch = 0; ch < fmt.channels; ++ch) {
            const unsigned char* sp = framePtr + static_cast<std::size_t>(ch) * bytesPerSample;
            const float v = fmt.audioFormat == 3u ? readFloatSample(sp) : readPcmSample(sp, fmt.bitsPerSample);
            result.zone.data[static_cast<std::size_t>(fr) * fmt.channels + ch] = std::clamp(v, -1.0f, 1.0f);
        }
    }

    return result;
}

std::string saveWavZone(const SampleZone& zone, const std::string& path)
{
    if (zone.data.empty() || zone.channelCount <= 0) return "No audio data";

    const auto frameCount = static_cast<std::uint32_t>(zone.data.size() / zone.channelCount);
    const std::uint32_t sampleRate  = static_cast<std::uint32_t>(zone.sampleRate);
    const std::uint16_t channels    = static_cast<std::uint16_t>(zone.channelCount);
    const std::uint16_t bitsPerSample = 16u;
    const std::uint16_t blockAlign  = static_cast<std::uint16_t>(channels * (bitsPerSample / 8u));
    const std::uint32_t byteRate    = sampleRate * blockAlign;
    const std::uint32_t dataSize    = frameCount * blockAlign;
    const std::uint32_t riffSize    = 36u + dataSize;

    std::ofstream f(path, std::ios::binary);
    if (!f) return "Could not open file for writing";

    const auto writeU16 = [&](std::uint16_t v) {
        const unsigned char b[2] = { static_cast<unsigned char>(v & 0xFF),
                                      static_cast<unsigned char>((v >> 8) & 0xFF) };
        f.write(reinterpret_cast<const char*>(b), 2);
    };
    const auto writeU32 = [&](std::uint32_t v) {
        const unsigned char b[4] = { static_cast<unsigned char>(v & 0xFF),
                                      static_cast<unsigned char>((v >> 8) & 0xFF),
                                      static_cast<unsigned char>((v >> 16) & 0xFF),
                                      static_cast<unsigned char>((v >> 24) & 0xFF) };
        f.write(reinterpret_cast<const char*>(b), 4);
    };

    f.write("RIFF", 4); writeU32(riffSize); f.write("WAVE", 4);
    f.write("fmt ", 4); writeU32(16u);
    writeU16(1u); writeU16(channels); writeU32(sampleRate);
    writeU32(byteRate); writeU16(blockAlign); writeU16(bitsPerSample);
    f.write("data", 4); writeU32(dataSize);

    for (std::uint32_t fr = 0; fr < frameCount; ++fr) {
        for (std::uint16_t ch = 0; ch < channels; ++ch) {
            const float fv = zone.data[static_cast<std::size_t>(fr) * zone.channelCount + ch];
            const float clamped = std::clamp(fv, -1.0f, 1.0f);
            const auto iv = static_cast<std::int16_t>(clamped * 32767.0f);
            const unsigned char b[2] = { static_cast<unsigned char>(iv & 0xFF),
                                          static_cast<unsigned char>((iv >> 8) & 0xFF) };
            f.write(reinterpret_cast<const char*>(b), 2);
        }
    }

    return f.good() ? "" : "Write error";
}

}  // namespace downspout::campione
