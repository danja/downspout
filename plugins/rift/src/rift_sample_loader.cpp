#include "rift_sample_loader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace downspout::rift {
namespace {

struct WavFormat {
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
};

[[nodiscard]] std::uint16_t readU16(const std::array<unsigned char, 4>& bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

[[nodiscard]] std::uint32_t readU32(const std::array<unsigned char, 4>& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] bool readBytes(std::ifstream& file, char* data, const std::streamsize size) {
    file.read(data, size);
    return file.good();
}

[[nodiscard]] bool readChunkId(std::ifstream& file, char id[4]) {
    return readBytes(file, id, 4);
}

[[nodiscard]] bool readU32FromFile(std::ifstream& file, std::uint32_t& value) {
    std::array<unsigned char, 4> bytes {};
    if (!readBytes(file, reinterpret_cast<char*>(bytes.data()), 4)) {
        return false;
    }
    value = readU32(bytes);
    return true;
}

[[nodiscard]] bool parseFmtChunk(std::ifstream& file, const std::uint32_t chunkSize, WavFormat& format) {
    if (chunkSize < 16u) {
        return false;
    }

    std::vector<unsigned char> bytes(chunkSize);
    if (!readBytes(file, reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return false;
    }

    const auto u16At = [&](const std::size_t offset) {
        std::array<unsigned char, 4> local {};
        local[0] = bytes[offset];
        local[1] = bytes[offset + 1u];
        return readU16(local);
    };
    const auto u32At = [&](const std::size_t offset) {
        std::array<unsigned char, 4> local {};
        local[0] = bytes[offset];
        local[1] = bytes[offset + 1u];
        local[2] = bytes[offset + 2u];
        local[3] = bytes[offset + 3u];
        return readU32(local);
    };

    format.audioFormat = u16At(0);
    format.channels = u16At(2);
    format.sampleRate = u32At(4);
    format.blockAlign = u16At(12);
    format.bitsPerSample = u16At(14);
    return true;
}

[[nodiscard]] float readPcmSample(const unsigned char* data, const std::uint16_t bitsPerSample) {
    switch (bitsPerSample) {
    case 8:
        return (static_cast<float>(data[0]) - 128.0f) / 128.0f;
    case 16: {
        const std::int16_t value = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8));
        return static_cast<float>(value) / 32768.0f;
    }
    case 24: {
        std::int32_t value = static_cast<std::int32_t>(data[0]) |
                             (static_cast<std::int32_t>(data[1]) << 8) |
                             (static_cast<std::int32_t>(data[2]) << 16);
        if ((value & 0x00800000) != 0) {
            value |= ~0x00ffffff;
        }
        return static_cast<float>(value) / 8388608.0f;
    }
    case 32: {
        const std::int32_t value = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24));
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
    }
    default:
        return 0.0f;
    }
}

[[nodiscard]] float readFloatSample(const unsigned char* data) {
    std::uint32_t bits = static_cast<std::uint32_t>(data[0]) |
                         (static_cast<std::uint32_t>(data[1]) << 8) |
                         (static_cast<std::uint32_t>(data[2]) << 16) |
                         (static_cast<std::uint32_t>(data[3]) << 24);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return std::clamp(value, -1.0f, 1.0f);
}

[[nodiscard]] bool isSupportedFormat(const WavFormat& format) {
    if (format.channels == 0u || format.channels > kMaxChannels || format.sampleRate == 0u || format.blockAlign == 0u) {
        return false;
    }

    if (format.audioFormat == 1u) {
        return format.bitsPerSample == 8u ||
               format.bitsPerSample == 16u ||
               format.bitsPerSample == 24u ||
               format.bitsPerSample == 32u;
    }

    return format.audioFormat == 3u && format.bitsPerSample == 32u;
}

}  // namespace

SampleLoadResult loadWavSampleSource(const std::string& path, const double loopBeats) {
    SampleLoadResult result;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "Could not open sample file";
        return result;
    }

    char riff[4] {};
    char wave[4] {};
    std::uint32_t riffSize = 0;
    if (!readChunkId(file, riff) || !readU32FromFile(file, riffSize) || !readChunkId(file, wave)) {
        result.error = "File is too short to be a WAV";
        return result;
    }

    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0) {
        result.error = "Only RIFF/WAVE files are supported";
        return result;
    }

    WavFormat format;
    bool foundFmt = false;
    std::vector<unsigned char> dataBytes;

    while (file) {
        char chunkId[4] {};
        std::uint32_t chunkSize = 0;
        if (!readChunkId(file, chunkId) || !readU32FromFile(file, chunkSize)) {
            break;
        }

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            if (!parseFmtChunk(file, chunkSize, format)) {
                result.error = "Invalid WAV format chunk";
                return result;
            }
            foundFmt = true;
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            dataBytes.resize(chunkSize);
            if (!readBytes(file, reinterpret_cast<char*>(dataBytes.data()), static_cast<std::streamsize>(dataBytes.size()))) {
                result.error = "Invalid WAV data chunk";
                return result;
            }
        } else {
            file.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
            if (!file) {
                result.error = "Invalid WAV chunk";
                return result;
            }
        }

        if ((chunkSize & 1u) != 0u) {
            file.seekg(1, std::ios::cur);
        }
    }

    if (!foundFmt || dataBytes.empty()) {
        result.error = "WAV file is missing format or data";
        return result;
    }

    if (!isSupportedFormat(format)) {
        result.error = "Unsupported WAV format";
        return result;
    }

    const std::uint32_t bytesPerSample = format.bitsPerSample / 8u;
    const std::uint32_t expectedBlockAlign = bytesPerSample * format.channels;
    if (bytesPerSample == 0u || format.blockAlign < expectedBlockAlign) {
        result.error = "Invalid WAV frame layout";
        return result;
    }

    const std::uint32_t frameCount = static_cast<std::uint32_t>(dataBytes.size() / format.blockAlign);
    if (frameCount == 0u) {
        result.error = "WAV file contains no complete frames";
        return result;
    }

    result.source.channelCount = format.channels;
    result.source.sampleRate = static_cast<double>(format.sampleRate);
    result.source.loopBeats = loopBeats > 0.0 ? loopBeats : 4.0;
    result.source.interleaved.resize(static_cast<std::size_t>(frameCount) * format.channels);

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const unsigned char* frameData = dataBytes.data() + static_cast<std::size_t>(frame) * format.blockAlign;
        for (std::uint32_t channel = 0; channel < format.channels; ++channel) {
            const unsigned char* sampleData = frameData + static_cast<std::size_t>(channel) * bytesPerSample;
            const float value = format.audioFormat == 3u
                ? readFloatSample(sampleData)
                : readPcmSample(sampleData, format.bitsPerSample);
            result.source.interleaved[static_cast<std::size_t>(frame) * format.channels + channel] =
                std::clamp(value, -1.0f, 1.0f);
        }
    }

    return result;
}

}  // namespace downspout::rift
