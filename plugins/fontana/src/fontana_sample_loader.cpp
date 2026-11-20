#include "fontana_sample_loader.hpp"

#include "fontana_soundfont.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../../../third_party/stb_vorbis/stb_vorbis.h"

namespace downspout::fontana {
namespace {

// A five minute ceiling keeps decoded previews bounded (at 384 kHz stereo
// this is still ~920 MB of float PCM, so it is a hard cap, not a target).
constexpr std::uint32_t kMinSampleRate = 8000;
constexpr std::uint32_t kMaxSampleRate = 384000;
constexpr double kMaxSeconds = 300.0;
constexpr std::size_t kMaxInterleavedFloats =
    static_cast<std::size_t>(kMaxSeconds) * kMaxSampleRate * 2u;
constexpr std::size_t kMaxFmtChunkBytes = 1024;
constexpr std::size_t kMaxSmplChunkBytes = 64;
constexpr int kOggChunkFrames = 4096;

#pragma pack(push, 1)
struct WavFormat {
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
};
#pragma pack(pop)

struct SmplInfo {
    bool hasUnityNote = false;
    bool hasLoop = false;
    int unityNote = 60;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
};

[[nodiscard]] std::uint16_t readU16(const unsigned char* data) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) |
                                      (static_cast<std::uint16_t>(data[1]) << 8));
}

[[nodiscard]] std::uint32_t readU32(const unsigned char* data) noexcept
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

[[nodiscard]] std::uint64_t readU64(const unsigned char* data) noexcept
{
    return static_cast<std::uint64_t>(readU32(data)) |
           (static_cast<std::uint64_t>(readU32(data + 4)) << 32);
}

[[nodiscard]] bool readBytes(std::ifstream& file, char* buffer, const std::streamsize count)
{
    file.read(buffer, count);
    return file.good();
}

[[nodiscard]] bool readU32FromFile(std::ifstream& file, std::uint32_t& value)
{
    std::array<unsigned char, 4> bytes {};
    if (!readBytes(file, reinterpret_cast<char*>(bytes.data()), 4))
        return false;
    value = readU32(bytes.data());
    return true;
}

[[nodiscard]] bool seekForward(std::ifstream& file, const std::uint64_t count)
{
    file.seekg(static_cast<std::streamoff>(count), std::ios::cur);
    return file.good();
}

[[nodiscard]] bool skipChunk(std::ifstream& file, const std::uint32_t chunkSize)
{
    return seekForward(file, static_cast<std::uint64_t>(chunkSize) + (chunkSize & 1u));
}

[[nodiscard]] bool parseFmtChunk(std::ifstream& file, std::uint32_t chunkSize, WavFormat& format)
{
    if (chunkSize < 16u)
        return false;

    const std::size_t bytesToRead = static_cast<std::size_t>(
        std::min<std::uint32_t>(chunkSize, static_cast<std::uint32_t>(kMaxFmtChunkBytes)));
    std::vector<unsigned char> bytes(bytesToRead);
    if (!readBytes(file, reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return false;
    const std::uint64_t remainder = static_cast<std::uint64_t>(chunkSize - bytesToRead) + (chunkSize & 1u);
    if (remainder > 0u && !seekForward(file, remainder))
        return false;

    format.audioFormat = readU16(bytes.data() + 0);
    format.channels = readU16(bytes.data() + 2);
    format.sampleRate = readU32(bytes.data() + 4);
    format.blockAlign = readU16(bytes.data() + 12);
    format.bitsPerSample = readU16(bytes.data() + 14);
    return true;
}

// Parse the first loop record of a WAV 'smpl' chunk. The header is 36 bytes;
// each 24-byte loop record starts with identifier, type, start and end.
[[nodiscard]] bool parseSmplChunk(const std::vector<unsigned char>& bytes, SmplInfo& info)
{
    if (bytes.size() < 36u)
        return false;

    const std::uint32_t unityNote = readU32(bytes.data() + 12);
    if (unityNote <= 127u) {
        info.hasUnityNote = true;
        info.unityNote = static_cast<int>(unityNote);
    }

    const std::uint32_t loopCount = readU32(bytes.data() + 28);
    if (loopCount > 0u && bytes.size() >= 36u + 24u) {
        const std::uint32_t loopType = readU32(bytes.data() + 36u + 4u);
        if (loopType != 2u) {  // 2 = backward, the engine only plays forward loops
            info.hasLoop = true;
            info.loopStart = readU32(bytes.data() + 36u + 8u);
            info.loopEnd = readU32(bytes.data() + 36u + 12u);
        }
    }
    return true;
}

[[nodiscard]] float sanitizeSample(const float value) noexcept
{
    if (!std::isfinite(value))
        return 0.0f;
    return std::clamp(value, -1.0f, 1.0f);
}

[[nodiscard]] float readPcmSample(const unsigned char* data, const std::uint16_t bitsPerSample) noexcept
{
    switch (bitsPerSample) {
    case 8:
        return (static_cast<float>(data[0]) - 128.0f) / 128.0f;
    case 16: {
        const auto value = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8));
        return static_cast<float>(value) / 32768.0f;
    }
    case 24: {
        std::int32_t value = static_cast<std::int32_t>(data[0]) |
                             (static_cast<std::int32_t>(data[1]) << 8) |
                             (static_cast<std::int32_t>(data[2]) << 16);
        if ((value & 0x00800000) != 0)
            value |= ~0x00ffffff;
        return static_cast<float>(value) / 8388608.0f;
    }
    case 32: {
        const auto value = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24));
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
    }
    default:
        return 0.0f;
    }
}

[[nodiscard]] float readFloat32Sample(const unsigned char* data) noexcept
{
    const std::uint32_t bits = readU32(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return sanitizeSample(value);
}

[[nodiscard]] float readFloat64Sample(const unsigned char* data) noexcept
{
    const std::uint64_t bits = readU64(data);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value))
        return 0.0f;
    return sanitizeSample(static_cast<float>(std::clamp(value, -1.0, 1.0)));
}

[[nodiscard]] bool supportedWavFormat(const WavFormat& format) noexcept
{
    if (format.channels < 1u || format.channels > 2u)
        return false;
    if (format.sampleRate < kMinSampleRate || format.sampleRate > kMaxSampleRate)
        return false;
    if (format.audioFormat == 1u)
        return format.bitsPerSample == 8u || format.bitsPerSample == 16u ||
               format.bitsPerSample == 24u || format.bitsPerSample == 32u;
    if (format.audioFormat == 3u)
        return format.bitsPerSample == 32u || format.bitsPerSample == 64u;
    return false;
}

[[nodiscard]] std::string loweredExtension(const std::string& path)
{
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return {};
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos && dot < slash)
        return {};

    std::string extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

[[nodiscard]] SampleLoadResult failure(std::string message)
{
    SampleLoadResult result;
    result.error = std::move(message);
    return result;
}

}  // namespace

SampleLoadResult loadSampleSource(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return failure("Could not open WAV file");

    char riff[4] {};
    char wave[4] {};
    std::uint32_t riffSize = 0;
    if (!readBytes(file, riff, 4) || !readU32FromFile(file, riffSize) || !readBytes(file, wave, 4))
        return failure("File is too short to be a WAV");
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
        return failure("Not a RIFF/WAVE file");

    WavFormat format;
    SmplInfo smpl;
    bool foundFmt = false;
    bool foundData = false;
    std::vector<unsigned char> dataBytes;

    while (file) {
        char chunkId[4] {};
        file.read(chunkId, 4);
        if (file.gcount() == 0)
            break;  // clean end of file
        if (file.gcount() != 4)
            return failure("Truncated WAV chunk header");

        std::uint32_t chunkSize = 0;
        if (!readU32FromFile(file, chunkSize))
            return failure("Truncated WAV chunk size");

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (!parseFmtChunk(file, chunkSize, format))
                return failure("Invalid WAV fmt chunk");
            foundFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (foundData) {
                if (!skipChunk(file, chunkSize))
                    return failure("Invalid WAV data chunk");
                continue;
            }
            if (chunkSize == 0u)
                return failure("WAV data chunk is empty");
            // A coarse byte bound before allocating; the exact duration check
            // happens once the sample rate is known.
            if (chunkSize > kMaxInterleavedFloats * 4u)
                return failure("WAV data exceeds the 5 minute limit");
            dataBytes.resize(chunkSize);
            if (!readBytes(file, reinterpret_cast<char*>(dataBytes.data()),
                           static_cast<std::streamsize>(dataBytes.size())))
                return failure("Truncated WAV data chunk");
            if ((chunkSize & 1u) != 0u && !seekForward(file, 1))
                return failure("Invalid WAV data chunk padding");
            foundData = true;
        } else if (std::memcmp(chunkId, "smpl", 4) == 0) {
            const std::size_t bytesToRead = static_cast<std::size_t>(
                std::min<std::uint32_t>(chunkSize, static_cast<std::uint32_t>(kMaxSmplChunkBytes)));
            std::vector<unsigned char> bytes(bytesToRead);
            if (!bytes.empty() &&
                !readBytes(file, reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
                return failure("Invalid WAV smpl chunk");
            (void)parseSmplChunk(bytes, smpl);
            const std::uint64_t remainder = static_cast<std::uint64_t>(chunkSize - bytesToRead) + (chunkSize & 1u);
            if (remainder > 0u && !seekForward(file, remainder))
                return failure("Invalid WAV smpl chunk");
        } else if (!skipChunk(file, chunkSize)) {
            return failure("Invalid WAV chunk");
        }
    }

    if (!foundFmt || !foundData)
        return failure("WAV file is missing fmt or data chunk");
    if (!supportedWavFormat(format))
        return failure("Unsupported WAV format (need 8/16/24/32-bit PCM or 32/64-bit float, mono/stereo, 8-384 kHz)");

    const std::uint32_t bytesPerSample = format.bitsPerSample / 8u;
    const std::uint32_t expectedAlign = bytesPerSample * format.channels;
    if (format.blockAlign < expectedAlign)
        return failure("Invalid WAV frame layout");

    const std::size_t frameCount = dataBytes.size() / format.blockAlign;
    if (frameCount == 0u)
        return failure("WAV file has no complete frames");
    if (static_cast<double>(frameCount) / static_cast<double>(format.sampleRate) > kMaxSeconds)
        return failure("WAV file exceeds the 5 minute limit");

    SampleLoadResult result;
    SampleSource& source = result.source;
    source.path = path;
    source.channels = static_cast<int>(format.channels);
    source.sampleRate = format.sampleRate;
    source.data.resize(frameCount * format.channels);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const unsigned char* frameData = dataBytes.data() + frame * format.blockAlign;
        for (std::uint32_t channel = 0; channel < format.channels; ++channel) {
            const unsigned char* sampleData = frameData + channel * bytesPerSample;
            float value = 0.0f;
            if (format.audioFormat == 3u)
                value = format.bitsPerSample == 64u ? readFloat64Sample(sampleData) : readFloat32Sample(sampleData);
            else
                value = readPcmSample(sampleData, format.bitsPerSample);
            source.data[frame * format.channels + channel] = sanitizeSample(value);
        }
    }

    if (smpl.hasUnityNote)
        source.rootNote = smpl.unityNote;
    if (smpl.hasLoop) {
        const std::uint32_t lastFrame = static_cast<std::uint32_t>(frameCount - 1u);
        source.loopEnabled = true;
        source.loopStart = std::min(smpl.loopStart, lastFrame);
        source.loopEnd = std::min(smpl.loopEnd, lastFrame);
        if (source.loopEnd <= source.loopStart) {
            source.loopStart = 0;
            source.loopEnd = lastFrame;
        }
    }

    return result;
}

SampleLoadResult loadOggSource(const std::string& path)
{
    int vorbisError = 0;
    stb_vorbis* decoder = stb_vorbis_open_filename(path.c_str(), &vorbisError, nullptr);
    if (decoder == nullptr)
        return failure("Could not open Ogg Vorbis file");

    const stb_vorbis_info info = stb_vorbis_get_info(decoder);
    const int channels = info.channels;
    const auto sampleRate = static_cast<std::uint32_t>(info.sample_rate);
    if (channels != 1 && channels != 2) {
        stb_vorbis_close(decoder);
        return failure("Only mono or stereo Ogg Vorbis files are supported");
    }
    if (sampleRate < kMinSampleRate || sampleRate > kMaxSampleRate) {
        stb_vorbis_close(decoder);
        return failure("Ogg Vorbis sample rate is out of range");
    }

    const auto knownFrames = stb_vorbis_stream_length_in_samples64(decoder);
    if (knownFrames > 0 &&
        static_cast<double>(knownFrames) / static_cast<double>(sampleRate) > kMaxSeconds) {
        stb_vorbis_close(decoder);
        return failure("Ogg Vorbis file exceeds the 5 minute limit");
    }

    const std::size_t maxFrames = static_cast<std::size_t>(kMaxSeconds) * sampleRate;
    std::vector<float> chunk(static_cast<std::size_t>(kOggChunkFrames) * static_cast<std::size_t>(channels));
    std::size_t totalFrames = 0;

    SampleLoadResult result;
    SampleSource& source = result.source;
    source.path = path;
    source.channels = channels;
    source.sampleRate = sampleRate;

    for (;;) {
        const int decoded = stb_vorbis_get_samples_float_interleaved(
            decoder, channels, chunk.data(), static_cast<int>(chunk.size()));
        if (decoded <= 0)
            break;
        const auto decodedFrames = static_cast<std::size_t>(decoded);
        if (totalFrames > maxFrames || decodedFrames > maxFrames - totalFrames) {
            stb_vorbis_close(decoder);
            return failure("Ogg Vorbis file exceeds the 5 minute limit");
        }
        const std::size_t values = decodedFrames * static_cast<std::size_t>(channels);
        for (std::size_t i = 0; i < values; ++i)
            chunk[i] = sanitizeSample(chunk[i]);
        source.data.insert(source.data.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(values));
        totalFrames += decodedFrames;
    }
    stb_vorbis_close(decoder);

    if (totalFrames == 0)
        return failure("Ogg Vorbis file contains no audio");
    return result;
}

SampleLoadResult loadPreviewSource(const std::string& path)
{
    const std::string extension = loweredExtension(path);
    if (extension == ".wav" || extension == ".wave")
        return loadSampleSource(path);
    if (extension == ".ogg")
        return loadOggSource(path);

    if (extension == ".sf2" || extension == ".sf3") {
        const SoundFontLoadResult loaded = loadSoundFont(path);
        if (!loaded.error.empty())
            return failure("Could not load SoundFont: " + loaded.error);

        for (const Sample& sample : loaded.bank.samples) {
            if (!sample.valid || sample.data.empty() || sample.sampleRate == 0u)
                continue;

            const std::size_t sampleSize = sample.data.size();
            const std::size_t begin = std::min<std::size_t>(sample.start, sampleSize);
            std::size_t end = sampleSize;
            if (sample.end >= begin && sample.end < sampleSize)
                end = static_cast<std::size_t>(sample.end) + 1u;  // inclusive marker -> exclusive end
            if (end <= begin)
                continue;

            SampleLoadResult result;
            SampleSource& source = result.source;
            source.path = path;
            source.channels = 1;
            source.sampleRate = sample.sampleRate;
            source.data.assign(sample.data.begin() + static_cast<std::ptrdiff_t>(begin),
                               sample.data.begin() + static_cast<std::ptrdiff_t>(end));
            // The SoundFont's pitch-correction cents cannot be represented by
            // SampleSource's integer root note, so the original pitch wins.
            source.rootNote = std::clamp(sample.originalPitch, 0, 127);

            const std::uint32_t frames = source.frameCount();
            const std::size_t loopStart = sample.loopStart >= begin ? sample.loopStart - begin : 0u;
            const std::size_t loopEnd = sample.loopEnd >= begin ? sample.loopEnd - begin : 0u;
            if (frames > 0u && loopEnd > loopStart) {
                source.loopEnabled = true;
                source.loopStart = static_cast<std::uint32_t>(std::min<std::size_t>(loopStart, frames - 1u));
                source.loopEnd = static_cast<std::uint32_t>(std::min<std::size_t>(loopEnd, frames - 1u));
            }
            return result;
        }
        return failure("SoundFont has no usable decoded sample");
    }

    return failure("Unsupported preview file type (expected .wav, .ogg, .sf2 or .sf3)");
}

bool isAudioFile(const std::string& path)
{
    const std::string extension = loweredExtension(path);
    return extension == ".wav" || extension == ".wave" || extension == ".ogg";
}

bool isSoundFontPath(const std::string& path)
{
    const std::string extension = loweredExtension(path);
    return extension == ".sf2" || extension == ".sf3";
}

std::vector<Peak> computePeaks(const SampleSource& source, const std::size_t bins)
{
    std::vector<Peak> peaks(bins);
    if (bins == 0u || source.empty())
        return peaks;

    const std::size_t frames = source.frameCount();
    const std::size_t channels = static_cast<std::size_t>(source.channels);
    for (std::size_t bin = 0; bin < bins; ++bin) {
        const std::uint64_t first = static_cast<std::uint64_t>(bin) * frames / bins;
        const std::uint64_t last = static_cast<std::uint64_t>(bin + 1u) * frames / bins;
        float minimum = 0.0f;
        float maximum = 0.0f;
        for (std::uint64_t frame = first; frame < last; ++frame) {
            float average = 0.0f;
            for (std::size_t channel = 0; channel < channels; ++channel)
                average += source.data[static_cast<std::size_t>(frame) * channels + channel];
            average /= static_cast<float>(channels);
            minimum = std::min(minimum, average);
            maximum = std::max(maximum, average);
        }
        peaks[bin] = Peak { minimum, maximum };
    }
    return peaks;
}

std::string describeSource(const SampleSource& source)
{
    if (source.empty())
        return "no audio";

    std::ostringstream out;
    const double kiloHertz = static_cast<double>(source.sampleRate) / 1000.0;
    if (std::abs(kiloHertz - std::round(kiloHertz)) < 0.05)
        out << static_cast<int>(std::llround(kiloHertz)) << " kHz";
    else
        out << std::fixed << std::setprecision(1) << kiloHertz << " kHz";

    out << " \xc2\xb7 ";
    if (source.channels == 1)
        out << "mono";
    else if (source.channels == 2)
        out << "stereo";
    else
        out << source.channels << " ch";

    out << " \xc2\xb7 " << std::fixed << std::setprecision(2)
        << static_cast<double>(source.frameCount()) / static_cast<double>(source.sampleRate) << " s";
    return out.str();
}

}  // namespace downspout::fontana
