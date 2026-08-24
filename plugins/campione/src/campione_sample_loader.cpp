#include "campione_sample_loader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

struct SmplInfo {
    bool     hasLoop     = false;
    bool     hasUnityNote = false;
    int      unityNote   = 60;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd   = 0;
};

// Parse a WAV 'smpl' chunk. Returns false if the chunk is too short to be valid.
// Only the first loop point is used; loop type 2 (backward) is not honoured.
[[nodiscard]] bool parseSmplChunk(const std::vector<unsigned char>& b, SmplInfo& out)
{
    // smpl header is 36 bytes: manufacturer(4) product(4) samplePeriod(4)
    // midiUnityNote(4) midiPitchFraction(4) smpteFormat(4) smpteOffset(4)
    // numSampleLoops(4) samplerData(4)
    if (b.size() < 36u) return false;
    const auto u32b = [&](std::size_t o) -> std::uint32_t {
        return static_cast<std::uint32_t>(b[o])        |
               (static_cast<std::uint32_t>(b[o+1]) << 8) |
               (static_cast<std::uint32_t>(b[o+2]) << 16) |
               (static_cast<std::uint32_t>(b[o+3]) << 24);
    };
    const std::uint32_t unityNote      = u32b(12);
    const std::uint32_t numSampleLoops = u32b(28);

    if (unityNote <= 127u) {
        out.hasUnityNote = true;
        out.unityNote    = static_cast<int>(unityNote);
    }

    // Each loop record is 24 bytes starting at offset 36.
    if (numSampleLoops > 0u && b.size() >= 36u + 24u) {
        // loop type: 0=forward, 1=ping-pong, 2=backward — skip backward
        const std::uint32_t loopType = u32b(36 + 4);
        if (loopType != 2u) {
            out.hasLoop  = true;
            out.loopStart = u32b(36 + 8);
            out.loopEnd   = u32b(36 + 12);
        }
    }
    return true;
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
    SmplInfo smpl;

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
        } else if (std::strncmp(chunkId, "smpl", 4) == 0 && chunkSize >= 4u) {
            std::vector<unsigned char> smplBytes(chunkSize);
            if (readBytes(file, reinterpret_cast<char*>(smplBytes.data()), static_cast<std::streamsize>(chunkSize)))
                parseSmplChunk(smplBytes, smpl);
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

    // Apply embedded smpl chunk data: root note and loop points.
    if (smpl.hasUnityNote) {
        result.zone.rootNote       = smpl.unityNote;
        result.hasEmbeddedRootNote = true;
    }
    if (smpl.hasLoop) {
        const std::uint32_t totalF = frameCount;
        result.zone.loopEnabled = true;
        result.zone.loopStart   = std::min(smpl.loopStart, totalF > 0u ? totalF - 1u : 0u);
        result.zone.loopEnd     = std::min(smpl.loopEnd,   totalF > 0u ? totalF - 1u : 0u);
        // Ensure loopEnd > loopStart; fall back to full-file loop if the markers are degenerate.
        if (result.zone.loopEnd <= result.zone.loopStart) {
            result.zone.loopStart = 0;
            result.zone.loopEnd   = totalF > 0u ? totalF - 1u : 0u;
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

ZoneLoadResult importWavetableZone(const std::string& srcPath, const std::string& destDir)
{
    ZoneLoadResult result;

    std::ifstream file(srcPath, std::ios::binary);
    if (!file) { result.error = "Could not open file"; return result; }

    char riff[4]{}, wave[4]{};
    std::uint32_t riffSize = 0;
    if (!readBytes(file, riff, 4) || !readU32FromFile(file, riffSize) || !readBytes(file, wave, 4)) {
        result.error = "File too short";
        return result;
    }
    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0) {
        result.error = "Not a RIFF/WAVE file";
        return result;
    }

    WavFormat fmt;
    bool foundFmt = false;
    std::vector<unsigned char> dataBytes;
    std::uint32_t clmFrameLen = 0;

    while (file) {
        char chunkId[4]{};
        std::uint32_t chunkSize = 0;
        if (!readBytes(file, chunkId, 4) || !readU32FromFile(file, chunkSize)) break;

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            if (!parseFmtChunk(file, chunkSize, fmt)) { result.error = "Invalid fmt chunk"; return result; }
            foundFmt = true;
        } else if (std::strncmp(chunkId, "clm ", 4) == 0) {
            // Serum wavetable marker: "<!>2048 ..." where the number is samples per frame
            std::vector<char> clmData(chunkSize + 1, '\0');
            if (readBytes(file, clmData.data(), static_cast<std::streamsize>(chunkSize))) {
                if (chunkSize >= 4 && std::strncmp(clmData.data(), "<!>", 3) == 0)
                    clmFrameLen = static_cast<std::uint32_t>(std::strtoul(clmData.data() + 3, nullptr, 10));
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            dataBytes.resize(chunkSize);
            if (!readBytes(file, reinterpret_cast<char*>(dataBytes.data()),
                           static_cast<std::streamsize>(chunkSize))) {
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

    const std::uint32_t totalFrames = static_cast<std::uint32_t>(dataBytes.size() / fmt.blockAlign);
    if (totalFrames == 0) { result.error = "No complete frames"; return result; }

    // Use clm frame size if present and valid; otherwise treat the whole file as one cycle.
    const std::uint32_t frameLen = (clmFrameLen > 0 && clmFrameLen <= totalFrames)
                                       ? clmFrameLen : totalFrames;

    // Decode the first frame.
    result.zone.channelCount = static_cast<int>(fmt.channels);
    result.zone.sampleRate   = static_cast<double>(fmt.sampleRate);
    result.zone.data.resize(static_cast<std::size_t>(frameLen) * fmt.channels);

    for (std::uint32_t fr = 0; fr < frameLen; ++fr) {
        const unsigned char* fp = dataBytes.data() + static_cast<std::size_t>(fr) * fmt.blockAlign;
        for (std::uint32_t ch = 0; ch < fmt.channels; ++ch) {
            const unsigned char* sp = fp + static_cast<std::size_t>(ch) * bytesPerSample;
            const float v = fmt.audioFormat == 3u
                                ? readFloatSample(sp)
                                : readPcmSample(sp, fmt.bitsPerSample);
            result.zone.data[static_cast<std::size_t>(fr) * fmt.channels + ch] =
                std::clamp(v, -1.0f, 1.0f);
        }
    }

    // Root note: nearest MIDI note to the natural loop frequency (sr / frameLen).
    // At that note campione plays at rate≈1, so the loop repeats at the right pitch.
    const double naturalHz = static_cast<double>(fmt.sampleRate) / static_cast<double>(frameLen);
    const double midiFloat = 69.0 + 12.0 * std::log2(naturalHz / 440.0);
    result.zone.rootNote = std::clamp(static_cast<int>(std::lround(midiFloat)), 0, 127);

    // Enable full-frame loop with no crossfade (single-cycle waveforms don't need it).
    result.zone.loopEnabled   = true;
    result.zone.loopStart     = 0;
    result.zone.loopEnd       = frameLen - 1;
    result.zone.crossfadeFrames = 0;

    // Derive output filename from the source stem.
    std::string stem = srcPath;
    const auto slash = stem.rfind('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    const auto dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    const std::string outPath = destDir + "/" + stem + "_wt.wav";

    const std::string saveErr = saveWavZone(result.zone, outPath);
    if (!saveErr.empty()) { result.error = "Could not save converted file: " + saveErr; return result; }

    result.zone.sourcePath = outPath;
    return result;
}

}  // namespace downspout::campione
